// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_tools_orientation.h"

#include "mcp_context.h"
#include "mcp_gui_call.h"
#include "mcp_item_identity.h"
#include "mcp_registry.h"

#include "../brush.h"
#include "../carpet_brush.h"
#include "../editor.h"
#include "../ground_brush.h"
#include "../gui.h"
#include "../items.h"
#include "../map.h"
#include "../multiplayer_session.h"
#include "../table_brush.h"
#include "../tile.h"
#include "../wall_brush.h"
#include "../workspace_session.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <mutex>
#include <unordered_set>

namespace mcp {
	namespace {
		Json EmptySchema() {
			return { { "type", "object" }, { "properties", Json::object() }, { "additionalProperties", false } };
		}

		Json IdSchema() {
			return {
				{ "type", "object" },
				{ "properties", { { "id", { { "type", "integer" }, { "minimum", 1 }, { "maximum", std::numeric_limits<uint16_t>::max() } } } } },
				{ "required", Json::array({ "id" }) },
				{ "additionalProperties", false },
			};
		}

		uint16_t RequiredId(const Json& arguments) {
			if (!arguments.contains("id") || !arguments["id"].is_number_unsigned()) {
				throw Error("id must be an unsigned integer");
			}
			const uint64_t value = arguments["id"].get<uint64_t>();
			if (value == 0 || value > std::numeric_limits<uint16_t>::max()) {
				throw Error("id must be between 1 and 65535");
			}
			return static_cast<uint16_t>(value);
		}

		std::string LowerToolText(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
			return value;
		}

		std::string FileBasename(const std::filesystem::path& path) {
			return path.empty() ? std::string() : path.filename().string();
		}

		std::string BrushKind(const Brush& brush) {
			if (dynamic_cast<const GroundBrush*>(&brush)) {
				return "ground";
			}
			if (dynamic_cast<const WallBrush*>(&brush)) {
				return "wall";
			}
			if (dynamic_cast<const CarpetBrush*>(&brush)) {
				return "carpet";
			}
			if (dynamic_cast<const TableBrush*>(&brush)) {
				return "table";
			}
			if (brush.isDoodad()) {
				return "doodad";
			}
			if (brush.isRaw()) {
				return "raw";
			}
			return "other";
		}

		Json OnGui(std::function<Json(const EditorContext&)> function) {
			return CallJsonOnGui([function = std::move(function)] {
				const EditorContext context = ResolveEditorContext();
				return function(context);
			});
		}
	}

	void RegisterOrientationTools() {
		static std::once_flag once;
		std::call_once(once, [] {
			ToolRegistry& registry = ToolRegistry::Instance();
			registry.add({
				"workspace_info",
				"Describe the active map workspace, resource backend and effective item ID mode.",
				EmptySchema(),
				false,
				[](const Json&) {
					return StructuredResult(OnGui([](const EditorContext& context) {
						const ServerWorkspace& server = g_workspace.getServer();
						const WorkspaceClientSelection& client = g_workspace.getClient();
						return Json {
							{ "serverType", ServerTypeNameForMcp(server.serverType) },
							{ "assetMode", AssetModeName(context.assetMode) },
							{ "itemIdMode", McpItemIdModeName(context.itemIdMode) },
							{ "clientVersion", client.versionName.ToStdString() },
							{ "mapName", context.map.getName() },
							{ "mapFile", FileBasename(std::filesystem::path(context.map.getFilename())) },
							{ "hasItemsOtb", server.itemsOtbFingerprint.exists },
							{ "hasItemsXml", server.itemsXmlFingerprint.exists },
							{ "hasAppearances", server.appearancesFingerprint.exists || context.assetMode == AssetMode::Appearances },
							{ "resourceGeneration", context.workspaceGeneration },
						};
					}));
				},
			});

			registry.add({
				"client_info",
				"Report the active classic DAT/SPR or appearances client and ID semantics.",
				EmptySchema(),
				false,
				[](const Json&) {
					return StructuredResult(OnGui([](const EditorContext& context) {
						const WorkspaceClientSelection& client = g_workspace.getClient();
						return Json {
							{ "assetMode", AssetModeName(context.assetMode) },
							{ "itemIdMode", McpItemIdModeName(context.itemIdMode) },
							{ "version", client.versionName.ToStdString() },
							{ "valid", client.valid },
							{ "serverType", ServerTypeNameForMcp(g_workspace.getServer().serverType) },
						};
					}));
				},
			});

			registry.add({
				"map_info",
				"Return dimensions, revision and basic metadata for the currently open map.",
				EmptySchema(),
				false,
				[](const Json&) {
					return StructuredResult(OnGui([](const EditorContext& context) {
						return Json {
							{ "name", context.map.getName() },
							{ "file", FileBasename(std::filesystem::path(context.map.getFilename())) },
							{ "width", context.map.getWidth() },
							{ "height", context.map.getHeight() },
							{ "tileCount", context.map.getTileCount() },
							{ "changed", context.map.hasChanged() },
							{ "changeGeneration", context.map.getChangeGeneration() },
							{ "sessionId", context.mapSessionId },
							{ "assetMode", AssetModeName(context.assetMode) },
							{ "itemIdMode", McpItemIdModeName(context.itemIdMode) },
						};
					}));
				},
			});

			registry.add({
				"editor_state",
				"Return active editor, selection, brush and multiplayer edit state.",
				EmptySchema(),
				false,
				[](const Json&) {
					return StructuredResult(OnGui([](const EditorContext& context) {
						MultiplayerSession* multiplayer = context.editor.multiplayer.get();
						Brush* brush = g_gui.GetCurrentBrush();
						return Json {
							{ "mapOpen", true },
							{ "selectionTiles", context.editor.selection.size() },
							{ "floor", g_gui.GetCurrentFloor() },
							{ "zoom", g_gui.GetCurrentZoom() },
							{ "brush", brush ? Json(brush->getName()) : Json(nullptr) },
							{ "multiplayer", multiplayer && multiplayer->active() },
							{ "canEdit", !multiplayer || !multiplayer->active() || multiplayer->canEdit() },
						};
					}));
				},
			});

			registry.add({
				"selection_get",
				"Return the exact current tile selection and its bounds. Generation requires a non-empty selection.",
				EmptySchema(),
				false,
				[](const Json&) {
					return StructuredResult(OnGui([](const EditorContext& context) {
						constexpr size_t MaximumPositions = 10000;
						Json positions = Json::array();
						size_t count = 0;
						for (const Tile* tile : context.editor.selection) {
							if (count++ < MaximumPositions) {
								const Position position = tile->getPosition();
								positions.push_back({ { "x", position.x }, { "y", position.y }, { "z", position.z } });
							}
						}
						Json result {
							{ "empty", context.editor.selection.size() == 0 },
							{ "count", context.editor.selection.size() },
							{ "positions", std::move(positions) },
							{ "truncated", context.editor.selection.size() > MaximumPositions },
						};
						if (context.editor.selection.size() == 0) {
							result["message"] = "Select an area in NexaMap before generating.";
						} else {
							const Position minimum = context.editor.selection.minPosition();
							const Position maximum = context.editor.selection.maxPosition();
							result["bounds"] = { { "min", { { "x", minimum.x }, { "y", minimum.y }, { "z", minimum.z } } }, { "max", { { "x", maximum.x }, { "y", maximum.y }, { "z", maximum.z } } } };
						}
						return result;
					}));
				},
			});

			registry.add({
				"item_info",
				"Inspect an item using the active workspace ID mode and return both server/client identities when known.",
				IdSchema(),
				false,
				[](const Json& arguments) {
					const uint16_t id = RequiredId(arguments);
					return StructuredResult(OnGui([id](const EditorContext& context) {
						if (!g_items.typeExists(id)) {
							throw Error("item ID is not loaded in the active resource session");
						}
						const ItemType& type = g_items.getItemType(id);
						Json result = ItemIdentity(id, context.itemIdMode, type);
						result["assetMode"] = AssetModeName(context.assetMode);
						result["flags"] = ItemFlags(type, context.assetMode);
						return result;
					}));
				},
			});

			registry.add({
				"item_flags",
				"Return normalized flags derived from the active classic or appearances item database.",
				IdSchema(),
				false,
				[](const Json& arguments) {
					const uint16_t id = RequiredId(arguments);
					return StructuredResult(OnGui([id](const EditorContext& context) {
						if (!g_items.typeExists(id)) {
							throw Error("item ID is not loaded in the active resource session");
						}
						return ItemFlags(g_items.getItemType(id), context.assetMode);
					}));
				},
			});

			registry.add({
				"item_search",
				"Search loaded items by name or active-mode ID with bounded pagination.",
				{
					{ "type", "object" },
					{ "properties", {
										{ "query", { { "type", "string" }, { "maxLength", 128 } } },
										{ "limit", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 100 }, { "default", 25 } } },
										{ "offset", { { "type", "integer" }, { "minimum", 0 }, { "default", 0 } } },
									} },
					{ "required", Json::array({ "query" }) },
					{ "additionalProperties", false },
				},
				false,
				[](const Json& arguments) {
					if (!arguments.contains("query") || !arguments["query"].is_string()) {
						throw Error("query must be a string");
					}
					const std::string query = LowerToolText(arguments["query"].get<std::string>());
					const size_t limit = std::min<size_t>(arguments.value("limit", 25), 100);
					const size_t offset = arguments.value("offset", 0);
					return StructuredResult(OnGui([query, limit, offset](const EditorContext& context) {
						Json matches = Json::array();
						size_t total = 0;
						for (size_t id = 1; id <= g_items.getMaxID(); ++id) {
							if (!g_items.typeExists(static_cast<int>(id))) {
								continue;
							}
							const ItemType& type = g_items.getItemType(static_cast<int>(id));
							const std::string name = LowerToolText(type.name);
							if (!query.empty() && name.find(query) == std::string::npos && std::to_string(id).find(query) == std::string::npos) {
								continue;
							}
							if (total >= offset && matches.size() < limit) {
								matches.push_back(ItemIdentity(static_cast<uint16_t>(id), context.itemIdMode, type));
							}
							++total;
						}
						return Json { { "items", std::move(matches) }, { "total", total }, { "offset", offset }, { "nextOffset", offset + limit < total ? Json(offset + limit) : Json(nullptr) }, { "assetMode", AssetModeName(context.assetMode) }, { "itemIdMode", McpItemIdModeName(context.itemIdMode) } };
					}));
				},
			});

			registry.add({
				"brush_search",
				"Search the real brushes loaded in the active resource session by semantic name and optional kind.",
				{
					{ "type", "object" },
					{ "properties", { { "query", { { "type", "string" }, { "maxLength", 128 } } }, { "kind", { { "type", "string" } } }, { "limit", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 100 }, { "default", 25 } } } } },
					{ "required", Json::array({ "query" }) },
					{ "additionalProperties", false },
				},
				false,
				[](const Json& arguments) {
					if (!arguments.contains("query") || !arguments["query"].is_string()) {
						throw Error("query must be a string");
					}
					const std::string query = LowerToolText(arguments["query"].get<std::string>());
					const std::string kind = LowerToolText(arguments.value("kind", std::string()));
					const size_t limit = std::min<size_t>(arguments.value("limit", 25), 100);
					return StructuredResult(OnGui([query, kind, limit](const EditorContext& context) {
						Json brushes = Json::array();
						std::unordered_set<const Brush*> visited;
						for (const auto& [name, brush] : g_brushes.getMap()) {
							if (!brush || !visited.insert(brush).second) {
								continue;
							}
							const std::string brushName = brush->getName().empty() ? name : brush->getName();
							const std::string brushKind = BrushKind(*brush);
							if ((!query.empty() && LowerToolText(brushName).find(query) == std::string::npos) || (!kind.empty() && brushKind != kind)) {
								continue;
							}
							brushes.push_back({ { "name", brushName }, { "kind", brushKind }, { "lookId", brush->getLookID() }, { "previewable", brush->getLookID() > 0 } });
							if (brushes.size() >= limit) {
								break;
							}
						}
						return Json { { "brushes", std::move(brushes) }, { "assetMode", AssetModeName(context.assetMode) }, { "itemIdMode", McpItemIdModeName(context.itemIdMode) } };
					}));
				},
			});

			registry.add({
				"generation_guide",
				"Return the safe semantic workflow for AI generation in the selected area.",
				EmptySchema(),
				false,
				[](const Json&) {
					return StructuredResult({
						{ "workflow", Json::array({ "workspace_info", "map_info", "editor_state", "selection_get", "brush_search", "generator_plan", "generator_preview", "generator_apply", "border_check", "path_check", "map_validate", "map_render_region" }) },
						{ "rules", Json::array({ "Use semantic brushes and loaded resources; never guess raw IDs.", "Preview must not mutate the map.", "Apply only after explicit user approval and as one undoable operation.", "Stay inside the exact selected area unless expansion is explicitly allowed.", "Preserve teleports, houses, spawns, creatures, waypoints, action IDs and unique IDs by default." }) },
						{ "status", "Foundation tools are available; typed generator plan/preview/apply tools are introduced in the generator phase." },
					});
				},
			});
		});
	}
}
