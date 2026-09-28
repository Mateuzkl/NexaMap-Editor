// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_tools_editor.h"

#include "mcp_registry.h"
#include "mcp_tool_utils.h"

#include "../editor.h"
#include "../gui.h"
#include "../map.h"
#include "../tile.h"

#include <mutex>
#include <optional>

namespace mcp {
	void RegisterEditorTools() {
		static std::once_flag once;
		std::call_once(once, [] {
			ToolRegistry& registry = ToolRegistry::Instance();
			registry.add({
				"editor_action",
				"Execute a bounded editor command. Undo and redo use NexaMap's native ActionQueue.",
				{
					{ "type", "object" },
					{ "properties", {
										{ "action", { { "type", "string" }, { "enum", Json::array({ "undo", "redo", "refresh" }) } } },
										{ "mapSessionId", { { "type", "integer" }, { "minimum", 1 } } },
										{ "workspaceGeneration", { { "type", "integer" }, { "minimum", 0 } } },
									} },
					{ "required", Json::array({ "action" }) },
					{ "additionalProperties", false },
				},
				true,
				[](const Json& arguments) {
					if (!arguments.contains("action") || !arguments["action"].is_string()) {
						throw Error("action must be a string");
					}
					const std::string action = arguments["action"].get<std::string>();
					return StructuredResult(OnGui([arguments, action](const EditorContext& context) {
						bool success = true;
						if (action == "undo") {
							VerifyWritableContext(context, arguments);
							success = context.editor.actionQueue->undo();
						} else if (action == "redo") {
							VerifyWritableContext(context, arguments);
							success = context.editor.actionQueue->redo();
						} else if (action == "refresh") {
							g_gui.RefreshView();
						} else {
							throw Error("unsupported editor action");
						}
						if (success) {
							g_gui.RefreshView();
						}
						return Json { { "action", action }, { "success", success }, { "canUndo", context.editor.actionQueue->canUndo() }, { "canRedo", context.editor.actionQueue->canRedo() }, { "changeGeneration", context.map.getChangeGeneration() } };
					}));
				},
			});

			registry.add({
				"selection_op",
				"Select an existing mapped region or run native borderize, randomize, delete and clear-selection operations.",
				{
					{ "type", "object" },
					{ "properties", {
										{ "operation", { { "type", "string" }, { "enum", Json::array({ "select-region", "clear", "borderize", "randomize", "delete" }) } } },
										{ "region", { { "type", "object" } } },
										{ "confirm", { { "type", "boolean" }, { "default", false } } },
										{ "mapSessionId", { { "type", "integer" }, { "minimum", 1 } } },
										{ "workspaceGeneration", { { "type", "integer" }, { "minimum", 0 } } },
									} },
					{ "required", Json::array({ "operation" }) },
					{ "additionalProperties", false },
				},
				true,
				[](const Json& arguments) {
					if (!arguments.contains("operation") || !arguments["operation"].is_string()) {
						throw Error("operation must be a string");
					}
					const std::string operation = arguments["operation"].get<std::string>();
					std::optional<Region> region;
					if (arguments.contains("region")) {
						region = ParseRegion(arguments["region"], 262144);
					}
					if (operation == "select-region" && !region) {
						throw Error("select-region requires region");
					}
					if (operation == "delete" && !arguments.value("confirm", false)) {
						throw Error("delete requires confirm=true");
					}
					return StructuredResult(OnGui([arguments, operation, region](const EditorContext& context) {
						if (operation == "select-region") {
							context.editor.selection.start();
							context.editor.selection.clear();
							for (int y = region->from.y; y <= region->to.y; ++y) {
								for (int x = region->from.x; x <= region->to.x; ++x) {
									Tile* tile = context.map.getTile(x, y, region->from.z);
									if (tile) {
										context.editor.selection.add(tile);
									}
								}
							}
							context.editor.selection.finish();
						} else if (operation == "clear") {
							context.editor.selection.start();
							context.editor.selection.clear();
							context.editor.selection.finish();
						} else if (operation == "borderize") {
							VerifyWritableContext(context, arguments);
							context.editor.borderizeSelection();
						} else if (operation == "randomize") {
							VerifyWritableContext(context, arguments);
							context.editor.randomizeSelection();
						} else if (operation == "delete") {
							VerifyWritableContext(context, arguments);
							context.editor.destroySelection();
						} else {
							throw Error("unsupported selection operation");
						}
						g_gui.RefreshView();
						return Json { { "operation", operation }, { "selectionTiles", context.editor.selection.size() }, { "changeGeneration", context.map.getChangeGeneration() } };
					}));
				},
			});
		});
	}
}
