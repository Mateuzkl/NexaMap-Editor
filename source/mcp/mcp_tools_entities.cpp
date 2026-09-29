// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_tools_entities.h"

#include "mcp_registry.h"
#include "mcp_tool_utils.h"

#include "../action.h"
#include "../creature.h"
#include "../editor.h"
#include "../gui.h"
#include "../house.h"
#include "../map.h"
#include "../multiplayer_session.h"
#include "../tile.h"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>

namespace mcp {
	namespace {
		std::pair<size_t, size_t> Pagination(const Json& arguments, size_t maximum = 500) {
			const int64_t offset = arguments.value("offset", int64_t(0));
			const int64_t limit = arguments.value("limit", int64_t(100));
			if (offset < 0 || limit < 1 || static_cast<size_t>(limit) > maximum) {
				throw Error("invalid pagination bounds");
			}
			return { static_cast<size_t>(offset), static_cast<size_t>(limit) };
		}

		Json HouseJson(const House& house) {
			const HouseSnapshot snapshot = house.getSnapshot();
			return {
				{ "id", snapshot.id },
				{ "name", snapshot.name },
				{ "townId", snapshot.townid },
				{ "rent", snapshot.rent },
				{ "requiredReset", snapshot.requiredReset },
				{ "clientId", snapshot.clientid },
				{ "beds", snapshot.beds },
				{ "guildhall", snapshot.guildhall },
				{ "exit", PositionJson(snapshot.exit) },
				{ "tileCount", house.tileCount() },
				{ "sessionId", house.getSessionId() },
			};
		}

		Json PageResult(Json values, size_t offset, size_t limit, size_t total, const char* key) {
			Json result { { key, std::move(values) }, { "offset", offset }, { "limit", limit }, { "total", total } };
			if (offset + limit < total) {
				result["nextOffset"] = offset + limit;
			} else {
				result["nextOffset"] = nullptr;
			}
			return result;
		}

		void CommitEntityAction(const EditorContext& context, std::unique_ptr<Action> action) {
			std::unique_ptr<BatchAction> batch(context.editor.actionQueue->createBatch(ACTION_MCP));
			context.editor.actionQueue->resetTimer();
			if (!batch->addAndCommitAction(action.release())) {
				batch->rollback();
				throw Error("could not commit entity change");
			}
			context.editor.addBatch(batch.release(), 0);
			context.editor.actionQueue->resetTimer();
			g_gui.RefreshView();
		}

		HouseSnapshot HouseFromArguments(const Json& arguments, const HouseSnapshot* current = nullptr) {
			HouseSnapshot value = current ? *current : HouseSnapshot {};
			if (arguments.contains("id")) {
				value.id = arguments["id"].get<uint32_t>();
			}
			if (arguments.contains("name")) {
				value.name = arguments["name"].get<std::string>();
			}
			if (arguments.contains("townId")) {
				value.townid = arguments["townId"].get<uint32_t>();
			}
			if (arguments.contains("rent")) {
				value.rent = arguments["rent"].get<int>();
			}
			if (arguments.contains("requiredReset")) {
				value.requiredReset = arguments["requiredReset"].get<uint32_t>();
			}
			if (arguments.contains("clientId")) {
				value.clientid = arguments["clientId"].get<uint32_t>();
			}
			if (arguments.contains("beds")) {
				value.beds = arguments["beds"].get<int32_t>();
			}
			if (arguments.contains("guildhall")) {
				value.guildhall = arguments["guildhall"].get<bool>();
			}
			if (arguments.contains("exit")) {
				value.exit = ParsePosition(arguments["exit"], "exit");
			}
			return value;
		}

		Tile* WorkingTile(Map& map, std::map<Position, std::unique_ptr<Tile>>& changed, const Position& position) {
			auto [iterator, inserted] = changed.try_emplace(position);
			if (inserted) {
				TileLocation* location = map.createTileL(position);
				Tile* existing = location->get();
				iterator->second.reset(existing ? existing->deepCopy(map) : map.allocator(location));
			}
			return iterator->second.get();
		}

		Json CreatureJson(const Tile& tile) {
			if (!tile.creature) {
				return nullptr;
			}
			Json value {
				{ "position", PositionJson(tile.getPosition()) },
				{ "name", tile.creature->getName() },
				{ "npc", tile.creature->isNpc() },
				{ "spawnTime", tile.creature->getSpawnTime() },
				{ "weight", tile.creature->getWeight() },
				{ "direction", static_cast<int>(tile.creature->getDirection()) },
			};
			if (tile.creature->hasSpawnSource()) {
				value["spawnSource"] = PositionJson(tile.creature->getSpawnSource());
			}
			return value;
		}

		Json SpawnJson(const Map& map, const Position& center) {
			const Tile* tile = map.getTile(center);
			if (!tile || !tile->spawn) {
				return nullptr;
			}
			const int radius = tile->spawn->getSize();
			Json creatures = Json::array();
			for (int y = center.y - radius; y <= center.y + radius; ++y) {
				for (int x = center.x - radius; x <= center.x + radius; ++x) {
					const Tile* candidate = map.getTile(x, y, center.z);
					if (candidate && candidate->creature) {
						creatures.push_back(CreatureJson(*candidate));
					}
				}
			}
			return { { "position", PositionJson(center) }, { "radius", radius }, { "creatures", std::move(creatures) } };
		}

		Json MutationResult(const EditorContext& context, const std::string& operation) {
			return { { "operation", operation }, { "changed", true }, { "mapSessionId", context.mapSessionId }, { "changeGeneration", context.map.getChangeGeneration() }, { "undoAvailable", context.editor.actionQueue->canUndo() } };
		}
	}

	void RegisterEntityTools() {
		static std::once_flag once;
		std::call_once(once, [] {
			ToolRegistry& registry = ToolRegistry::Instance();
			const Json paginationProperties = {
				{ "offset", { { "type", "integer" }, { "minimum", 0 }, { "default", 0 } } },
				{ "limit", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 500 }, { "default", 100 } } },
			};

			registry.add({
				"house_list",
				"List houses from the active map with bounded pagination.",
				{ { "type", "object" }, { "properties", paginationProperties }, { "additionalProperties", false } },
				false,
				[](const Json& arguments) { return StructuredResult(OnGui([arguments](const EditorContext& context) {
												auto [offset, limit] = Pagination(arguments);
												Json houses = Json::array();
												size_t index = 0;
												for (const auto& [id, house] : context.map.houses) {
													if (index++ >= offset && houses.size() < limit && house) {
														houses.push_back(HouseJson(*house));
													}
												}
												return PageResult(std::move(houses), offset, limit, context.map.houses.count(), "houses");
											})); },
			});

			registry.add({
				"house_get",
				"Inspect one house and its tile positions.",
				{ { "type", "object" }, { "properties", { { "id", { { "type", "integer" }, { "minimum", 1 } } }, { "offset", paginationProperties["offset"] }, { "limit", paginationProperties["limit"] } } }, { "required", Json::array({ "id" }) }, { "additionalProperties", false } },
				false,
				[](const Json& arguments) { return StructuredResult(OnGui([arguments](const EditorContext& context) {
												if (!arguments.contains("id") || !arguments["id"].is_number_unsigned()) {
													throw Error("id must be an unsigned integer");
												}
												const House* house = context.map.houses.getHouse(arguments["id"].get<uint32_t>());
												if (!house) {
													throw Error("house not found");
												}
												auto [offset, limit] = Pagination(arguments);
												Json result = HouseJson(*house);
												Json tiles = Json::array();
												const auto& positions = house->getTilePositions();
												size_t index = 0;
												for (const Position& position : positions) {
													if (index++ < offset) {
														continue;
													}
													if (tiles.size() >= limit) {
														break;
													}
													tiles.push_back(PositionJson(position));
												}
												result["tiles"] = std::move(tiles);
												result["offset"] = offset;
												result["limit"] = limit;
												result["nextOffset"] = offset + limit < positions.size() ? Json(offset + limit) : Json(nullptr);
												return result;
											})); },
			});

			registry.add({
				"house_manage",
				"Create, update or delete an empty house through NexaMap's undo queue.",
				{ { "type", "object" }, { "properties", {
															{ "operation", { { "type", "string" }, { "enum", Json::array({ "create", "update", "delete" }) } } },
															{ "id", { { "type", "integer" }, { "minimum", 1 } } },
															{ "name", { { "type", "string" }, { "maxLength", 255 } } },
															{ "townId", { { "type", "integer" }, { "minimum", 0 } } },
															{ "rent", { { "type", "integer" }, { "minimum", 0 } } },
															{ "requiredReset", { { "type", "integer" }, { "minimum", 0 } } },
															{ "clientId", { { "type", "integer" }, { "minimum", 0 } } },
															{ "beds", { { "type", "integer" }, { "minimum", 0 } } },
															{ "guildhall", { { "type", "boolean" } } },
															{ "exit", PositionSchema() },
															{ "confirm", { { "type", "boolean" }, { "default", false } } },
															{ "mapSessionId", { { "type", "integer" }, { "minimum", 1 } } },
															{ "workspaceGeneration", { { "type", "integer" }, { "minimum", 0 } } },
														} },
				  { "required", Json::array({ "operation" }) },
				  { "additionalProperties", false } },
				true,
				[](const Json& arguments) { const std::string operation = arguments.value("operation", ""); return StructuredResult(OnGui([arguments, operation](const EditorContext& context) {
					VerifyWritableContext(context, arguments); std::unique_ptr<Action> action(context.editor.actionQueue->createAction(ACTION_MCP));
					if (operation == "create") {
						HouseSnapshot snapshot = HouseFromArguments(arguments); if (snapshot.id == 0){ snapshot.id = context.map.houses.getEmptyID();
}
						if (context.map.houses.getHouse(snapshot.id)){ throw Error("house id already exists");
} action->addChange(Change::CreateHouse(snapshot));
					} else {
						if (!arguments.contains("id") || !arguments["id"].is_number_unsigned()){ throw Error("update/delete requires id");
} House* house = context.map.houses.getHouse(arguments["id"].get<uint32_t>()); if (!house){ throw Error("house not found");
}
						if (operation == "update") { const HouseSnapshot before = house->getSnapshot(); const HouseSnapshot after = HouseFromArguments(arguments, &before); action->addChange(Change::UpdateHouse(before, after, house->getSessionId())); }
						else if (operation == "delete") { if (!arguments.value("confirm", false)){ throw Error("delete requires confirm=true");
} if (house->tileCount() != 0){ throw Error("house still owns tiles; clear or reassign those tiles first");
} action->addChange(Change::CreateHouse(house->getSnapshot(), false, house->getSessionId())); }
						else{ throw Error("unsupported house operation");
}
					}
					CommitEntityAction(context, std::move(action)); return MutationResult(context, operation);
				})); },
			});

			registry.add({
				"town_list",
				"List towns and temple positions from the active map.",
				{ { "type", "object" }, { "properties", paginationProperties }, { "additionalProperties", false } },
				false,
				[](const Json& arguments) { return StructuredResult(OnGui([arguments](const EditorContext& context) { auto [offset, limit] = Pagination(arguments); Json towns = Json::array(); size_t index = 0; for (const auto& [id, town] : context.map.towns){ if (index++ >= offset && towns.size() < limit && town){ towns.push_back({ { "id", id }, { "name", town->getName() }, { "templePosition", PositionJson(town->getTemplePosition()) } });
}} return PageResult(std::move(towns), offset, limit, context.map.towns.count(), "towns"); })); },
			});

			registry.add({
				"town_manage",
				"Create, update or delete a town as one undoable action.",
				{ { "type", "object" }, { "properties", { { "operation", { { "type", "string" }, { "enum", Json::array({ "create", "update", "delete" }) } } }, { "id", { { "type", "integer" }, { "minimum", 1 } } }, { "name", { { "type", "string" }, { "maxLength", 255 } } }, { "templePosition", PositionSchema() }, { "confirm", { { "type", "boolean" }, { "default", false } } }, { "mapSessionId", { { "type", "integer" }, { "minimum", 1 } } }, { "workspaceGeneration", { { "type", "integer" }, { "minimum", 0 } } } } }, { "required", Json::array({ "operation" }) }, { "additionalProperties", false } },
				true,
				[](const Json& arguments) { const std::string operation = arguments.value("operation", ""); return StructuredResult(OnGui([arguments, operation](const EditorContext& context) { VerifyWritableContext(context, arguments); uint32_t id = arguments.value("id", uint32_t(0)); if (operation == "create" && id == 0){ id = context.map.towns.getEmptyID();
} Town* existing = id ? context.map.towns.getTown(id) : nullptr; std::unique_ptr<Action> action(context.editor.actionQueue->createAction(ACTION_MCP)); if (operation == "create") { if (existing){ throw Error("town id already exists");
} if (!arguments.contains("name") || !arguments.contains("templePosition")){ throw Error("create requires name and templePosition");
} action->addChange(Change::CreateTown(id, arguments["name"].get<std::string>(), ParsePosition(arguments["templePosition"], "templePosition"), true)); } else { if (!existing){ throw Error("town not found");
} if (operation == "delete" && !arguments.value("confirm", false)){ throw Error("delete requires confirm=true");
} if (operation == "update"){ action->addChange(Change::UpdateTown(id, existing->getName(), existing->getTemplePosition(), arguments.value("name", existing->getName()), arguments.contains("templePosition") ? ParsePosition(arguments["templePosition"], "templePosition") : existing->getTemplePosition()));
} else if (operation == "delete"){ action->addChange(Change::CreateTown(id, existing->getName(), existing->getTemplePosition(), false));
} else { throw Error("unsupported town operation");
} } CommitEntityAction(context, std::move(action)); return MutationResult(context, operation); })); },
			});

			registry.add({
				"waypoint_list",
				"List map waypoints with bounded pagination.",
				{ { "type", "object" }, { "properties", paginationProperties }, { "additionalProperties", false } },
				false,
				[](const Json& arguments) { return StructuredResult(OnGui([arguments](const EditorContext& context) { auto [offset, limit] = Pagination(arguments); Json values = Json::array(); size_t index = 0; for (const auto& [key, waypoint] : context.map.waypoints){ if (index++ >= offset && values.size() < limit && waypoint){ values.push_back({ { "name", waypoint->name }, { "position", PositionJson(waypoint->pos) } });
}} return PageResult(std::move(values), offset, limit, context.map.waypoints.size(), "waypoints"); })); },
			});

			registry.add({
				"waypoint_manage",
				"Create, update or delete a waypoint as one undoable action.",
				{ { "type", "object" }, { "properties", { { "operation", { { "type", "string" }, { "enum", Json::array({ "create", "update", "delete" }) } } }, { "name", { { "type", "string" }, { "minLength", 1 }, { "maxLength", 128 } } }, { "newName", { { "type", "string" }, { "minLength", 1 }, { "maxLength", 128 } } }, { "position", PositionSchema() }, { "confirm", { { "type", "boolean" }, { "default", false } } }, { "mapSessionId", { { "type", "integer" }, { "minimum", 1 } } }, { "workspaceGeneration", { { "type", "integer" }, { "minimum", 0 } } } } }, { "required", Json::array({ "operation", "name" }) }, { "additionalProperties", false } },
				true,
				[](const Json& arguments) { const std::string operation = arguments.value("operation", ""); const std::string name = arguments.value("name", ""); return StructuredResult(OnGui([arguments, operation, name](const EditorContext& context) { VerifyWritableContext(context, arguments); Waypoint* existing = context.map.waypoints.getWaypoint(name); std::unique_ptr<Action> action(context.editor.actionQueue->createAction(ACTION_MCP)); if (operation == "create") { if (existing){ throw Error("waypoint already exists");
} if (!arguments.contains("position")){ throw Error("create requires position");
} const Position position = ParsePosition(arguments["position"]); if (!context.map.getTile(position)){ throw Error("waypoint position must already be mapped");
} action->addChange(Change::CreateWaypoint(name, position, true)); } else { if (!existing){ throw Error("waypoint not found");
} if (operation == "delete" && !arguments.value("confirm", false)){ throw Error("delete requires confirm=true");
} if (operation == "update") { const std::string newName = arguments.value("newName", existing->name); const Position position = arguments.contains("position") ? ParsePosition(arguments["position"]) : existing->pos; if (!context.map.getTile(position)){ throw Error("waypoint position must already be mapped");
} Waypoint* collision = context.map.waypoints.getWaypoint(newName); if (collision && collision != existing){ throw Error("new waypoint name already exists");
} action->addChange(Change::UpdateWaypoint(existing->name, existing->pos, newName, position)); } else if (operation == "delete"){ action->addChange(Change::CreateWaypoint(existing->name, existing->pos, false, existing->category, context.map.waypoints.waypointOrderIndex(existing->name))); } else { throw Error("unsupported waypoint operation");
} } CommitEntityAction(context, std::move(action)); return MutationResult(context, operation); })); },
			});

			registry.add({
				"zone_list",
				"List zone names and IDs from the active map.",
				{ { "type", "object" }, { "properties", paginationProperties }, { "additionalProperties", false } },
				false,
				[](const Json& arguments) { return StructuredResult(OnGui([arguments](const EditorContext& context) { auto [offset, limit] = Pagination(arguments); Json values = Json::array(); size_t index = 0; for (const auto& [name, id] : context.map.zones){ if (index++ >= offset && values.size() < limit){ values.push_back({ { "id", id }, { "name", name } });
}} return PageResult(std::move(values), offset, limit, context.map.zones.size(), "zones"); })); },
			});

			registry.add({
				"zone_manage",
				"Create, rename or delete a zone through the native zone registry and undo queue.",
				{ { "type", "object" }, { "properties", { { "operation", { { "type", "string" }, { "enum", Json::array({ "create", "rename", "delete" }) } } }, { "name", { { "type", "string" }, { "minLength", 1 }, { "maxLength", 128 } } }, { "newName", { { "type", "string" }, { "minLength", 1 }, { "maxLength", 128 } } }, { "id", { { "type", "integer" }, { "minimum", 1 } } }, { "confirm", { { "type", "boolean" }, { "default", false } } }, { "mapSessionId", { { "type", "integer" }, { "minimum", 1 } } }, { "workspaceGeneration", { { "type", "integer" }, { "minimum", 0 } } } } }, { "required", Json::array({ "operation", "name" }) }, { "additionalProperties", false } },
				true,
				[](const Json& arguments) { const std::string operation = arguments.value("operation", ""); const std::string name = arguments.value("name", ""); return StructuredResult(OnGui([arguments, operation, name](const EditorContext& context) { VerifyWritableContext(context, arguments); std::unique_ptr<Action> action(context.editor.actionQueue->createAction(ACTION_MCP)); if (operation == "create") { const unsigned int id = arguments.value("id", context.map.zones.getEmptyID()); if (!Zones::isValidName(name) || context.map.zones.hasZone(name) || context.map.zones.hasZone(id)){ throw Error("invalid or duplicate zone name/id");
} action->addChange(Change::CreateZone(name, id, true)); } else if (operation == "rename") { if (!context.map.zones.hasZone(name) || !arguments.contains("newName")){ throw Error("zone not found or newName missing");
} const std::string newName = arguments["newName"].get<std::string>(); if (!Zones::isValidName(newName) || context.map.zones.hasZone(newName)){ throw Error("new zone name is invalid or already exists");
} action->addChange(Change::RenameZone(name, newName)); } else if (operation == "delete") { if (!arguments.value("confirm", false)){ throw Error("delete requires confirm=true");
} const unsigned int id = context.map.zones.getZoneID(name); if (!id){ throw Error("zone not found");
} action->addChange(Change::CreateZone(name, id, false)); } else{ throw Error("unsupported zone operation");
} CommitEntityAction(context, std::move(action)); return MutationResult(context, operation); })); },
			});

			registry.add({
				"spawn_list",
				"List spawn centers, radii and creatures with bounded pagination.",
				{ { "type", "object" }, { "properties", paginationProperties }, { "additionalProperties", false } },
				false,
				[](const Json& arguments) { return StructuredResult(OnGui([arguments](const EditorContext& context) { auto [offset, limit] = Pagination(arguments); Json values = Json::array(); size_t index = 0; for (const Position& position : context.map.spawns){ if (index++ >= offset && values.size() < limit){ values.push_back(SpawnJson(context.map, position));
}} return PageResult(std::move(values), offset, limit, std::distance(context.map.spawns.begin(), context.map.spawns.end()), "spawns"); })); },
			});

			registry.add({
				"spawn_manage",
				"Create/delete/update a spawn or add/remove one creature using one undoable tile batch.",
				{ { "type", "object" }, { "properties", { { "operation", { { "type", "string" }, { "enum", Json::array({ "create", "delete", "update-radius", "add-monster", "add-npc", "remove-creature" }) } } }, { "position", PositionSchema() }, { "creaturePosition", PositionSchema() }, { "radius", { { "type", "integer" }, { "minimum", 0 }, { "maximum", 255 } } }, { "name", { { "type", "string" }, { "minLength", 1 }, { "maxLength", 128 } } }, { "spawnTime", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 86400 } } }, { "weight", { { "type", "integer" }, { "minimum", 0 } } }, { "direction", { { "type", "integer" }, { "minimum", 0 }, { "maximum", 3 } } }, { "confirm", { { "type", "boolean" }, { "default", false } } }, { "removeCreatures", { { "type", "boolean" }, { "default", false } } }, { "mapSessionId", { { "type", "integer" }, { "minimum", 1 } } }, { "workspaceGeneration", { { "type", "integer" }, { "minimum", 0 } } } } }, { "required", Json::array({ "operation", "position" }) }, { "additionalProperties", false } },
				true,
				[](const Json& arguments) {
					const std::string operation = arguments.value("operation", "");
					const Position center = ParsePosition(arguments["position"]);
					return StructuredResult(OnGui([arguments, operation, center](const EditorContext& context) {
						VerifyWritableContext(context, arguments);
						std::map<Position, std::unique_ptr<Tile>> changed;
						Tile* centerTile = WorkingTile(context.map, changed, center);
						if (operation == "create") {
							if (centerTile->spawn) {
								throw Error("spawn already exists at position");
							}
							centerTile->spawn = newd Spawn(arguments.value("radius", 3));
						} else if (operation == "delete") {
							if (!arguments.value("confirm", false)) {
								throw Error("delete requires confirm=true");
							}
							if (!centerTile->spawn) {
								throw Error("spawn not found");
							}
							const int radius = centerTile->spawn->getSize();
							for (int y = center.y - radius; y <= center.y + radius; ++y) {
								for (int x = center.x - radius; x <= center.x + radius; ++x) {
									const Position position(x, y, center.z);
									const Tile* source = context.map.getTile(position);
									if (!source || !source->creature || !source->creature->hasSpawnSource() || source->creature->getSpawnSource() != center) {
										continue;
									}
									if (!arguments.value("removeCreatures", false)) {
										throw Error("spawn still owns creatures; pass removeCreatures=true to remove them in the same undo step");
									}
									Tile* creatureTile = WorkingTile(context.map, changed, position);
									delete creatureTile->creature;
									creatureTile->creature = nullptr;
								}
							}
							delete centerTile->spawn;
							centerTile->spawn = nullptr;
						} else if (operation == "update-radius") {
							if (!centerTile->spawn || !arguments.contains("radius")) {
								throw Error("spawn not found or radius missing");
							}
							centerTile->spawn->setSize(arguments["radius"].get<int>());
						} else if (operation == "add-monster" || operation == "add-npc") {
							if (!centerTile->spawn) {
								throw Error("spawn not found");
							}
							if (!arguments.contains("name")) {
								throw Error("creature name is required");
							}
							const std::string name = arguments["name"].get<std::string>();
							const CreatureType* type = g_creatures[name];
							if (!type || type->missing) {
								throw Error("creature type is not loaded");
							}
							const bool npc = operation == "add-npc";
							if (type->isNpc != npc) {
								throw Error(npc ? "requested type is not an NPC" : "requested type is not a monster");
							}
							const Position creaturePosition = arguments.contains("creaturePosition") ? ParsePosition(arguments["creaturePosition"]) : center;
							if (creaturePosition.z != center.z || std::abs(creaturePosition.x - center.x) > centerTile->spawn->getSize() || std::abs(creaturePosition.y - center.y) > centerTile->spawn->getSize()) {
								throw Error("creaturePosition is outside the spawn radius");
							}
							Tile* creatureTile = WorkingTile(context.map, changed, creaturePosition);
							if (creatureTile->creature) {
								throw Error("creature tile is already occupied");
							}
							creatureTile->creature = newd Creature(type);
							creatureTile->creature->setSpawnSource(center);
							creatureTile->creature->setSpawnTime(arguments.value("spawnTime", 60));
							if (arguments.contains("weight")) {
								creatureTile->creature->setWeight(arguments["weight"].get<int>());
							}
							if (arguments.contains("direction")) {
								creatureTile->creature->setDirection(static_cast<Direction>(arguments["direction"].get<int>()));
							}
						} else if (operation == "remove-creature") {
							const Position creaturePosition = arguments.contains("creaturePosition") ? ParsePosition(arguments["creaturePosition"]) : center;
							Tile* creatureTile = WorkingTile(context.map, changed, creaturePosition);
							if (!creatureTile->creature) {
								throw Error("creature not found");
							}
							if (creatureTile->creature->hasSpawnSource() && creatureTile->creature->getSpawnSource() != center) {
								throw Error("creature belongs to a different spawn center");
							}
							delete creatureTile->creature;
							creatureTile->creature = nullptr;
						} else {
							throw Error("unsupported spawn operation");
						}
						const size_t changedCount = changed.size();
						std::unique_ptr<BatchAction> batch(context.editor.actionQueue->createBatch(ACTION_MCP));
						std::unique_ptr<Action> action(context.editor.actionQueue->createAction(batch.get()));
						for (auto& [position, tile] : changed) {
							tile->update();
							action->addChange(newd Change(tile.release()));
						}
						if (!batch->addAndCommitAction(action.release())) {
							batch->rollback();
							throw Error("could not commit spawn changes");
						}
						context.editor.addBatch(batch.release(), 0);
						context.editor.actionQueue->resetTimer();
						g_gui.RefreshView();
						Json result = MutationResult(context, operation);
						result["changedTiles"] = changedCount;
						return result;
					}));
				},
			});

			auto addCreatureList = [&registry, paginationProperties](const char* toolName, bool npc) {
				registry.add({ toolName, npc ? "List loaded NPC types." : "List loaded monster types.", { { "type", "object" }, { "properties", paginationProperties }, { "additionalProperties", false } }, false, [npc](const Json& arguments) { return StructuredResult(OnGui([arguments, npc](const EditorContext&) { auto [offset, limit] = Pagination(arguments); Json values = Json::array(); size_t matching = 0; for (const auto& [key, type] : g_creatures) { if (!type || type->isNpc != npc){ continue;
} if (matching++ < offset || values.size() >= limit){ continue;
} values.push_back({ { "name", type->name }, { "missing", type->missing }, { "standard", type->standard }, { "hasBrush", type->brush != nullptr } }); } return PageResult(std::move(values), offset, limit, matching, npc ? "npcs" : "monsters"); })); } });
			};
			addCreatureList("monster_types_list", false);
			addCreatureList("npc_types_list", true);
		});
	}
}
