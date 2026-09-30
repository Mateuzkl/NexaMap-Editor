// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_tools_lua.h"

#include "mcp_gui_call.h"
#include "mcp_registry.h"
#include "mcp_tool_utils.h"

#include "../lua_extension_editor.h"
#include "../lua_extension_registry.h"
#include "../editor.h"

#include <map>
#include <mutex>

namespace mcp {
	namespace {

		struct SavedPreview {
			std::string extensionId;
			std::string source;
			LuaExtension::Plan plan;
		};

		std::map<uint64_t, SavedPreview> previews;
		uint64_t nextPreviewId = 1;

		Json ExtensionJson(const LuaExtension::RegisteredExtension& extension) {
			return {
				{ "id", extension.id },
				{ "name", extension.metadata.name },
				{ "version", extension.metadata.version },
				{ "description", extension.metadata.description },
				{ "author", extension.metadata.author },
				{ "category", extension.metadata.category },
				{ "parameters", extension.metadata.parameters },
				{ "sourceFile", extension.path.filename().string() },
				{ "error", extension.error },
			};
		}

		const LuaExtension::RegisteredExtension& RequiredExtension(const Json& arguments) {
			if (!arguments.contains("id") || !arguments["id"].is_string()) {
				throw Error("id must name a registered extension");
			}
			const auto* extension = LuaExtension::Registry::Instance().Find(arguments["id"].get<std::string>());
			if (!extension) {
				throw Error("extension is not registered; call lua_extension_reload or lua_extension_list first");
			}
			return *extension;
		}

		SavedPreview& RequiredPreview(const Json& arguments) {
			if (!arguments.contains("previewId") || !arguments["previewId"].is_number_integer() || arguments["previewId"].get<int64_t>() <= 0) {
				throw Error("previewId is required");
			}
			const auto found = previews.find(arguments["previewId"].get<uint64_t>());
			if (found == previews.end()) {
				throw Error("preview expired; create a new preview");
			}
			return found->second;
		}

	} // namespace

	void RegisterLuaExtensionTools() {
		static std::once_flag once;
		std::call_once(once, [] {
			auto& registry = ToolRegistry::Instance();
			registry.add({
				"lua_extension_list",
				"List discovered, registered Lua extension files. No script is executed by discovery.",
				EmptyObjectSchema(),
				false,
				[](const Json&) {
					return StructuredResult(CallJsonOnGui([] {
						auto& extensions = LuaExtension::Registry::Instance();
						extensions.Reload();
						Json entries = Json::array();
						for (const auto& extension : extensions.Entries()) {
							entries.push_back(ExtensionJson(extension));
						}
						return Json { { "extensions", std::move(entries) } };
					}));
				},
			});
			registry.add({
				"lua_extension_info",
				"Inspect metadata and parameter schema of a registered Lua file inside the sandbox.",
				Json { { "type", "object" }, { "properties", { { "id", { { "type", "string" } } } } }, { "required", Json::array({ "id" }) }, { "additionalProperties", false } },
				false,
				[](const Json& arguments) {
					return StructuredResult(CallJsonOnGui([arguments] {
						const auto& extension = RequiredExtension(arguments);
						const auto* inspected = LuaExtension::Registry::Instance().Inspect(extension.id);
						return ExtensionJson(*inspected);
					}));
				},
			});
			registry.add({
				"lua_extension_reload",
				"Rescan registered Lua extension files without executing them; invalidates old previews.",
				EmptyObjectSchema(),
				false,
				[](const Json&) {
					return StructuredResult(CallJsonOnGui([] {
						LuaExtension::Registry::Instance().Reload();
						previews.clear();
						return Json { { "count", LuaExtension::Registry::Instance().Entries().size() } };
					}));
				},
			});
			registry.add({
				"lua_extension_preview",
				"Run a registered Lua extension only in preview mode. No live map writes occur.",
				Json { { "type", "object" }, { "properties", { { "id", { { "type", "string" } } }, { "parameters", { { "type", "object" } } } } }, { "required", Json::array({ "id" }) }, { "additionalProperties", false } },
				false,
				[](const Json& arguments) {
					return StructuredResult(OnGui([arguments](const EditorContext& context) {
						const auto& extension = RequiredExtension(arguments);
						std::string source, error;
						if (!LuaExtension::Registry::Instance().ReadSource(extension, source, error)) {
							throw Error(error);
						}
						auto luaContext = LuaExtension::CaptureContext(context.editor, error);
						if (!error.empty()) {
							throw Error(error);
						}
						const Json parameters = arguments.value("parameters", Json::object());
						auto plan = LuaExtension::Runtime::Preview(source, extension.path.filename().string(), luaContext, parameters);
						if (!plan.valid()) {
							throw Error(plan.error);
						}
						if (!LuaExtension::ValidatePlan(context.editor, plan, error)) {
							throw Error(error);
						}
						if (previews.size() >= 8) {
							previews.erase(previews.begin());
						}
						const uint64_t id = nextPreviewId++;
						const size_t count = plan.operations.size();
						const size_t tiles = luaContext.selection.size();
						previews.emplace(id, SavedPreview { extension.id, std::move(source), std::move(plan) });
						return Json { { "previewId", id }, { "extensionId", extension.id }, { "operationCount", count }, { "targetTileCount", tiles }, { "mapSessionId", context.mapSessionId }, { "workspaceGeneration", context.workspaceGeneration }, { "applied", false } };
					}));
				},
			});
			registry.add({
				"lua_extension_validate",
				"Revalidate a saved Lua preview against the active map, resources, reference source and exact target.",
				Json { { "type", "object" }, { "properties", { { "previewId", { { "type", "integer" }, { "minimum", 1 } } } } }, { "required", Json::array({ "previewId" }) }, { "additionalProperties", false } },
				false,
				[](const Json& arguments) {
					return StructuredResult(OnGui([arguments](const EditorContext& context) {
						std::string error;
						const bool valid = LuaExtension::ValidatePlan(context.editor, RequiredPreview(arguments).plan, error);
						return Json { { "valid", valid }, { "error", error } };
					}));
				},
			});
			registry.add({
				"lua_extension_run",
				"Apply a previously validated registered Lua preview as one undoable map edit. Arbitrary inline Lua is not accepted.",
				Json { { "type", "object" }, { "properties", { { "previewId", { { "type", "integer" }, { "minimum", 1 } } }, { "mapSessionId", { { "type", "integer" }, { "minimum", 1 } } }, { "workspaceGeneration", { { "type", "integer" }, { "minimum", 0 } } } } }, { "required", Json::array({ "previewId", "mapSessionId", "workspaceGeneration" }) }, { "additionalProperties", false } },
				true,
				[](const Json& arguments) {
					return StructuredResult(OnGui([arguments](const EditorContext& context) {
						VerifyWritableContext(context, arguments);
						SavedPreview& saved = RequiredPreview(arguments);
						const auto* extension = LuaExtension::Registry::Instance().Find(saved.extensionId);
						std::string source, error;
						if (!extension || !LuaExtension::Registry::Instance().ReadSource(*extension, source, error) || source != saved.source) {
							throw Error("extension source changed since preview; reload and preview again");
						}
						const size_t count = saved.plan.operations.size();
						if (!LuaExtension::ApplyPlan(context.editor, saved.plan, error)) {
							throw Error(error);
						}
						previews.clear();
						return Json { { "applied", true }, { "operationCount", count }, { "undoAvailable", context.editor.actionQueue->canUndo() } };
					}));
				},
			});
		});
	}

} // namespace mcp
