// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_tools_edit.h"

#include "mcp_registry.h"
#include "mcp_tool_utils.h"

#include "../action.h"
#include "../brush.h"
#include "../complexitem.h"
#include "../editor.h"
#include "../gui.h"
#include "../item.h"
#include "../map.h"
#include "../multiplayer_session.h"
#include "../tile.h"
#include "../workspace_session.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace mcp {
	namespace {
		void VerifyWritableContext(const EditorContext& context, const Json& arguments) {
			if (context.editor.multiplayer && context.editor.multiplayer->active() && !context.editor.multiplayer->canEdit()) {
				throw Error("the active multiplayer role cannot edit this map");
			}
			if (arguments.contains("mapSessionId")) {
				if (!arguments["mapSessionId"].is_number_unsigned() || arguments["mapSessionId"].get<SessionId>() != context.mapSessionId) {
					throw Error("stale map session; inspect map_info again before writing");
				}
			}
			if (arguments.contains("workspaceGeneration")) {
				if (!arguments["workspaceGeneration"].is_number_unsigned() || arguments["workspaceGeneration"].get<uint64_t>() != context.workspaceGeneration) {
					throw Error("stale resource workspace; inspect workspace_info again before writing");
				}
			}
		}

		std::vector<Position> ParsePositions(const Json& arguments) {
			std::vector<Position> positions;
			if (arguments.contains("positions")) {
				if (!arguments["positions"].is_array() || arguments["positions"].empty() || arguments["positions"].size() > 16384) {
					throw Error("positions must be an array containing 1 to 16384 positions");
				}
				positions.reserve(arguments["positions"].size());
				for (const Json& value : arguments["positions"]) {
					positions.push_back(ParsePosition(value));
				}
			} else {
				const Region region = ParseRegion(arguments, 16384);
				positions.reserve(region.area());
				for (int y = region.from.y; y <= region.to.y; ++y) {
					for (int x = region.from.x; x <= region.to.x; ++x) {
						positions.emplace_back(x, y, region.from.z);
					}
				}
			}
			std::sort(positions.begin(), positions.end());
			positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
			return positions;
		}

		void EnforceSelectionBoundary(const EditorContext& context, const std::vector<Position>& positions, bool allowExpansion) {
			if (allowExpansion || context.editor.selection.size() == 0) {
				return;
			}
			std::unordered_set<const Tile*> selected;
			for (const Tile* tile : context.editor.selection) {
				selected.insert(tile);
			}
			for (const Position& position : positions) {
				const Tile* tile = context.map.getTile(position);
				if (!tile || selected.find(tile) == selected.end()) {
					throw Error("write would leave the current selection; pass allowExpansion=true only when intended");
				}
			}
		}

		bool IsProtected(const Item& item) {
			if (item.getActionID() != 0 || item.getUniqueID() != 0 || dynamic_cast<const Teleport*>(&item)) {
				return true;
			}
			const auto* container = dynamic_cast<const Container*>(&item);
			return container && container->getItemCount() != 0;
		}

		uint16_t RequiredItemId(const Json& value) {
			if (!value.is_number_unsigned()) {
				throw Error("item id must be an unsigned integer");
			}
			const uint64_t id = value.get<uint64_t>();
			if (id == 0 || id > 65535 || !g_items.typeExists(static_cast<int>(id))) {
				throw Error("item id is not loaded in the active resource session");
			}
			return static_cast<uint16_t>(id);
		}

		Item* CreateItemFromSpec(const Json& specification, Map& map, size_t depth = 0) {
			if (!specification.is_object() || !specification.contains("id")) {
				throw Error("each added item must contain id");
			}
			if (depth > 8) {
				throw Error("container nesting exceeds 8 levels");
			}
			const uint16_t id = RequiredItemId(specification["id"]);
			std::unique_ptr<Item> item(Item::Create(id, specification.value("subtype", 0xFFFF)));
			if (!item) {
				throw Error("could not create requested item");
			}
			if (specification.contains("actionId")) {
				item->setActionID(specification["actionId"].get<uint16_t>());
			}
			if (specification.contains("uniqueId")) {
				const uint16_t uniqueId = specification["uniqueId"].get<uint16_t>();
				if (uniqueId != 0 && map.hasUniqueId(uniqueId)) {
					throw Error("uniqueId already exists on this map");
				}
				item->setUniqueID(uniqueId);
			}
			if (specification.contains("text")) {
				item->setText(specification["text"].get<std::string>());
			}
			if (specification.contains("description")) {
				item->setDescription(specification["description"].get<std::string>());
			}
			if (specification.contains("teleportDestination")) {
				auto* teleport = dynamic_cast<Teleport*>(item.get());
				if (!teleport) {
					throw Error("teleportDestination is only valid for teleport items");
				}
				teleport->setDestination(ParsePosition(specification["teleportDestination"], "teleportDestination"));
			}
			if (specification.contains("doorId")) {
				auto* door = dynamic_cast<Door*>(item.get());
				if (!door || !specification["doorId"].is_number_unsigned() || specification["doorId"].get<uint64_t>() > 255) {
					throw Error("doorId is only valid for doors and must fit uint8");
				}
				door->setDoorID(specification["doorId"].get<uint8_t>());
			}
			if (specification.contains("depotId")) {
				auto* depot = dynamic_cast<Depot*>(item.get());
				if (!depot || !specification["depotId"].is_number_unsigned() || specification["depotId"].get<uint64_t>() > 65535) {
					throw Error("depotId is only valid for depot items and must fit uint16");
				}
				depot->setDepotID(specification["depotId"].get<uint16_t>());
			}
			if (specification.contains("contents")) {
				auto* container = dynamic_cast<Container*>(item.get());
				if (!container || !specification["contents"].is_array() || specification["contents"].size() > 128) {
					throw Error("contents is only valid for containers and must contain at most 128 items");
				}
				if (specification["contents"].size() > container->getVolume()) {
					throw Error("container contents exceed its loaded volume");
				}
				for (const Json& child : specification["contents"]) {
					container->getVector().push_back(CreateItemFromSpec(child, map, depth + 1));
				}
			}
			return item.release();
		}
	}

	void RegisterEditTools() {
		static std::once_flag once;
		std::call_once(once, [] {
			ToolRegistry& registry = ToolRegistry::Instance();
			registry.add({
				"brush_apply",
				"Apply a real loaded NexaMap brush directly to explicit positions or a rectangle, preserving autoborders and one undo step.",
				{
					{ "type", "object" },
					{ "properties", {
										{ "brush", { { "type", "string" }, { "minLength", 1 }, { "maxLength", 128 } } },
										{ "positions", { { "type", "array" }, { "maxItems", 16384 }, { "items", PositionSchema() } } },
										{ "from", PositionSchema() },
										{ "to", PositionSchema() },
										{ "origin", PositionSchema() },
										{ "width", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 512 } } },
										{ "height", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 512 } } },
										{ "erase", { { "type", "boolean" }, { "default", false } } },
										{ "alt", { { "type", "boolean" }, { "default", false } } },
										{ "autoborder", { { "type", "boolean" }, { "default", true } } },
										{ "allowExpansion", { { "type", "boolean" }, { "default", false } } },
										{ "mapSessionId", { { "type", "integer" }, { "minimum", 1 } } },
										{ "workspaceGeneration", { { "type", "integer" }, { "minimum", 0 } } },
									} },
					{ "required", Json::array({ "brush" }) },
					{ "additionalProperties", false },
				},
				true,
				[](const Json& arguments) {
					if (!arguments.contains("brush") || !arguments["brush"].is_string()) {
						throw Error("brush must be a string");
					}
					const std::string brushName = arguments["brush"].get<std::string>();
					const std::vector<Position> positions = ParsePositions(arguments);
					const bool erase = arguments.value("erase", false);
					const bool alt = arguments.value("alt", false);
					const bool autoborder = arguments.value("autoborder", true);
					const bool allowExpansion = arguments.value("allowExpansion", false);
					return StructuredResult(OnGui([=](const EditorContext& context) {
						VerifyWritableContext(context, arguments);
						EnforceSelectionBoundary(context, positions, allowExpansion);
						Brush* brush = FindBrush(brushName);
						for (const Position& position : positions) {
							if (!brush->canDraw(&context.map, position) && !erase) {
								throw Error("brush cannot draw at one or more requested positions");
							}
						}
						Brush* previous = g_gui.GetCurrentBrush();
						context.editor.actionQueue->resetTimer();
						g_gui.SelectBrushInternal(brush);
						if (brush->isDoodad()) {
							for (const Position& position : positions) {
								if (erase) {
									context.editor.undraw(position, alt);
								} else {
									context.editor.draw(position, alt);
								}
							}
						} else {
							PositionVector drawPositions(positions.begin(), positions.end());
							PositionVector borderPositions;
							if (autoborder) {
								std::unordered_set<long long> requested;
								for (const Position& position : positions) {
									requested.insert((static_cast<long long>(position.z) << 40) | (static_cast<long long>(position.y) << 20) | position.x);
								}
								for (const Position& position : positions) {
									for (int dy = -1; dy <= 1; ++dy) {
										for (int dx = -1; dx <= 1; ++dx) {
											const Position border(position.x + dx, position.y + dy, position.z);
											if (!border.isValid()) {
												continue;
											}
											if (!allowExpansion && requested.find((static_cast<long long>(border.z) << 40) | (static_cast<long long>(border.y) << 20) | border.x) == requested.end()) {
												continue;
											}
											borderPositions.push_back(border);
										}
									}
								}
							}
							if (erase) {
								context.editor.undraw(drawPositions, borderPositions, alt);
							} else {
								context.editor.draw(drawPositions, borderPositions, alt);
							}
						}
						g_gui.SelectBrushInternal(previous);
						context.editor.actionQueue->resetTimer();
						g_gui.RefreshView();
						return Json { { "applied", true }, { "brush", brushName }, { "kind", BrushKind(*brush) }, { "positions", positions.size() }, { "mapSessionId", context.mapSessionId }, { "changeGeneration", context.map.getChangeGeneration() }, { "undoAvailable", context.editor.actionQueue->canUndo() } };
					}));
				},
			});

			registry.add({
				"tile_edit",
				"Apply a validated batch of direct tile edits as one undoable MCP action. Semantic terrain should normally use brush_apply.",
				{
					{ "type", "object" },
					{ "properties", {
										{ "tiles", { { "type", "array" }, { "minItems", 1 }, { "maxItems", 4096 }, { "items", { { "type", "object" } } } } },
										{ "preserveProtected", { { "type", "boolean" }, { "default", true } } },
										{ "allowExpansion", { { "type", "boolean" }, { "default", false } } },
										{ "mapSessionId", { { "type", "integer" }, { "minimum", 1 } } },
										{ "workspaceGeneration", { { "type", "integer" }, { "minimum", 0 } } },
									} },
					{ "required", Json::array({ "tiles" }) },
					{ "additionalProperties", false },
				},
				true,
				[](const Json& arguments) {
					if (!arguments.contains("tiles") || !arguments["tiles"].is_array() || arguments["tiles"].empty() || arguments["tiles"].size() > 4096) {
						throw Error("tiles must contain 1 to 4096 edits");
					}
					const bool preserveProtected = arguments.value("preserveProtected", true);
					const bool allowExpansion = arguments.value("allowExpansion", false);
					return StructuredResult(OnGui([arguments, preserveProtected, allowExpansion](const EditorContext& context) {
						VerifyWritableContext(context, arguments);
						std::vector<Position> positions;
						positions.reserve(arguments["tiles"].size());
						for (const Json& edit : arguments["tiles"]) {
							if (!edit.is_object() || !edit.contains("position")) {
								throw Error("each tile edit requires position");
							}
							positions.push_back(ParsePosition(edit["position"]));
						}
						EnforceSelectionBoundary(context, positions, allowExpansion);
						std::unique_ptr<BatchAction> batch(context.editor.actionQueue->createBatch(ACTION_MCP));
						std::unique_ptr<Action> action(context.editor.actionQueue->createAction(batch.get()));
						for (const Json& edit : arguments["tiles"]) {
							const Position position = ParsePosition(edit["position"]);
							TileLocation* location = context.map.createTileL(position);
							Tile* existing = location->get();
							std::unique_ptr<Tile> tile(existing ? existing->deepCopy(context.map) : context.map.allocator(location));
							if (edit.contains("ground")) {
								if (edit["ground"].is_null()) {
									delete tile->ground;
									tile->ground = nullptr;
								} else {
									if (!edit["ground"].is_object() || !edit["ground"].contains("id")) {
										throw Error("ground must be null or contain id");
									}
									const uint16_t id = RequiredItemId(edit["ground"]["id"]);
									if (!g_items.getItemType(id).isGroundTile()) {
										throw Error("ground id must identify a ground item");
									}
									tile->addItem(CreateItemFromSpec(edit["ground"], context.map));
								}
							}
							if (edit.value("clearItems", false)) {
								auto iterator = tile->items.begin();
								while (iterator != tile->items.end()) {
									if (preserveProtected && IsProtected(**iterator)) {
										++iterator;
										continue;
									}
									delete *iterator;
									iterator = tile->items.erase(iterator);
								}
							}
							if (edit.contains("removeItems")) {
								if (!edit["removeItems"].is_array()) {
									throw Error("removeItems must be an array of IDs");
								}
								std::unordered_set<uint16_t> removeIds;
								for (const Json& id : edit["removeItems"]) {
									removeIds.insert(RequiredItemId(id));
								}
								auto iterator = tile->items.begin();
								while (iterator != tile->items.end()) {
									if (removeIds.count((*iterator)->getID()) == 0 || (preserveProtected && IsProtected(**iterator))) {
										++iterator;
										continue;
									}
									delete *iterator;
									iterator = tile->items.erase(iterator);
								}
							}
							if (edit.contains("addItems")) {
								if (!edit["addItems"].is_array() || edit["addItems"].size() > 128) {
									throw Error("addItems must be an array of at most 128 item specifications");
								}
								for (const Json& specification : edit["addItems"]) {
									tile->addItem(CreateItemFromSpec(specification, context.map));
								}
							}
							if (edit.contains("mapFlags")) {
								if (!edit["mapFlags"].is_number_unsigned() || edit["mapFlags"].get<uint64_t>() > 65535) {
									throw Error("mapFlags must fit uint16");
								}
								tile->unsetMapFlags(0xFFFF);
								tile->setMapFlags(edit["mapFlags"].get<uint16_t>());
							}
							if (edit.contains("houseId")) {
								if (edit["houseId"].is_null()) {
									tile->setHouseID(0);
								} else {
									if (!edit["houseId"].is_number_unsigned()) {
										throw Error("houseId must be null or an unsigned integer");
									}
									const uint32_t houseId = edit["houseId"].get<uint32_t>();
									if (!context.map.houses.getHouse(houseId)) {
										throw Error("houseId does not exist in this map");
									}
									tile->setHouseID(houseId);
								}
							}
							if (edit.contains("zoneIds")) {
								if (!edit["zoneIds"].is_array() || edit["zoneIds"].size() > 256) {
									throw Error("zoneIds must be an array of at most 256 IDs");
								}
								tile->zones.clear();
								for (const Json& value : edit["zoneIds"]) {
									if (!value.is_number_unsigned()) {
										throw Error("zoneIds must contain unsigned integers");
									}
									const unsigned int zoneId = value.get<unsigned int>();
									if (!context.map.zones.hasZone(zoneId)) {
										throw Error("zoneIds contains an unknown zone");
									}
									tile->zones.insert(zoneId);
								}
							}
							tile->update();
							action->addChange(newd Change(tile.release()));
						}
						batch->addAndCommitAction(action.release());
						context.editor.actionQueue->resetTimer();
						context.editor.addBatch(batch.release(), 0);
						context.editor.actionQueue->resetTimer();
						g_gui.RefreshView();
						return Json { { "edited", true }, { "tiles", positions.size() }, { "mapSessionId", context.mapSessionId }, { "changeGeneration", context.map.getChangeGeneration() }, { "undoAvailable", context.editor.actionQueue->canUndo() } };
					}));
				},
			});
		});
	}
}
