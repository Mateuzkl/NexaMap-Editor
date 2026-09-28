// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_tools_map.h"

#include "mcp_item_identity.h"
#include "mcp_registry.h"
#include "mcp_tool_utils.h"

#include "../complexitem.h"
#include "../creature.h"
#include "../editor.h"
#include "../item.h"
#include "../map.h"
#include "../spawn.h"
#include "../tile.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <sstream>

namespace mcp {
	namespace {
		Json ItemJson(const Item& item, const EditorContext& context) {
			const ItemType& type = g_items.getItemType(item.getID());
			Json value = ItemIdentity(item.getID(), context.itemIdMode, type);
			value["actionId"] = item.getActionID();
			value["uniqueId"] = item.getUniqueID();
			value["subtype"] = item.getSubtype();
			value["flags"] = ItemFlags(type, context.assetMode);
			if (!item.getText().empty()) {
				value["text"] = item.getText();
			}
			if (!item.getDescription().empty()) {
				value["description"] = item.getDescription();
			}
			if (const auto* teleport = dynamic_cast<const Teleport*>(&item)) {
				value["teleportDestination"] = PositionJson(teleport->getDestination());
			}
			if (const auto* door = dynamic_cast<const Door*>(&item)) {
				value["doorId"] = door->getDoorID();
			}
			if (const auto* depot = dynamic_cast<const Depot*>(&item)) {
				value["depotId"] = depot->getDepotID();
			}
			if (const auto* container = dynamic_cast<const Container*>(&item)) {
				value["containerItemCount"] = container->getItemCount();
			}
			return value;
		}

		Json TileJson(const Position& position, const Tile* tile, const EditorContext& context) {
			Json value {
				{ "position", PositionJson(position) },
				{ "mapped", tile != nullptr },
			};
			if (!tile) {
				return value;
			}
			value["ground"] = tile->ground ? ItemJson(*tile->ground, context) : Json(nullptr);
			Json items = Json::array();
			for (const Item* item : tile->items) {
				if (item) {
					items.push_back(ItemJson(*item, context));
				}
			}
			value["items"] = std::move(items);
			value["blocking"] = tile->isBlocking();
			value["mapFlags"] = tile->getMapFlags();
			value["houseId"] = tile->getHouseID() == 0 ? Json(nullptr) : Json(tile->getHouseID());
			value["zones"] = tile->zones;
			value["spawn"] = tile->spawn ? Json { { "radius", tile->spawn->getSize() } } : Json(nullptr);
			value["creature"] = tile->creature ? Json { { "name", tile->creature->getName() }, { "spawnTime", tile->creature->getSpawnTime() }, { "npc", tile->creature->isNpc() } } : Json(nullptr);
			value["houseExit"] = tile->isHouseExit();
			return value;
		}

		Json RegionSchema() {
			return {
				{ "type", "object" },
				{ "properties", {
									{ "from", PositionSchema() },
									{ "to", PositionSchema() },
									{ "origin", PositionSchema() },
									{ "width", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 512 } } },
									{ "height", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 512 } } },
									{ "mode", { { "type", "string" }, { "enum", Json::array({ "summary", "ascii", "tiles" }) }, { "default", "summary" } } },
									{ "limit", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 1000 }, { "default", 250 } } },
									{ "offset", { { "type", "integer" }, { "minimum", 0 }, { "default", 0 } } },
								} },
				{ "additionalProperties", false },
			};
		}
	}

	void RegisterMapTools() {
		static std::once_flag once;
		std::call_once(once, [] {
			ToolRegistry& registry = ToolRegistry::Instance();
			registry.add({
				"tile_get",
				"Inspect one map tile, including item identities, metadata, entities and blocking state.",
				{ { "type", "object" }, { "properties", { { "position", PositionSchema() } } }, { "required", Json::array({ "position" }) }, { "additionalProperties", false } },
				false,
				[](const Json& arguments) {
					if (!arguments.contains("position")) {
						throw Error("position is required");
					}
					const Position position = ParsePosition(arguments["position"]);
					return StructuredResult(OnGui([position](const EditorContext& context) { return TileJson(position, context.map.getTile(position), context); }));
				},
			});

			registry.add({
				"map_read_region",
				"Read a bounded single-floor region as a summary, compact ASCII topology, or paginated tile details.",
				RegionSchema(),
				false,
				[](const Json& arguments) {
					const Region region = ParseRegion(arguments);
					const std::string mode = arguments.value("mode", std::string("summary"));
					const size_t limit = std::min<size_t>(arguments.value("limit", 250), 1000);
					const size_t offset = arguments.value("offset", 0);
					if (mode != "summary" && mode != "ascii" && mode != "tiles") {
						throw Error("mode must be summary, ascii or tiles");
					}
					return StructuredResult(OnGui([region, mode, limit, offset](const EditorContext& context) {
						Json result {
							{ "from", PositionJson(region.from) },
							{ "to", PositionJson(region.to) },
							{ "width", region.width() },
							{ "height", region.height() },
							{ "mode", mode },
							{ "mapSessionId", context.mapSessionId },
							{ "changeGeneration", context.map.getChangeGeneration() },
						};
						if (mode == "tiles") {
							Json tiles = Json::array();
							size_t index = 0;
							for (int y = region.from.y; y <= region.to.y; ++y) {
								for (int x = region.from.x; x <= region.to.x; ++x, ++index) {
									if (index >= offset && tiles.size() < limit) {
										const Position position(x, y, region.from.z);
										tiles.push_back(TileJson(position, context.map.getTile(position), context));
									}
								}
							}
							result["tiles"] = std::move(tiles);
							result["offset"] = offset;
							result["nextOffset"] = offset + limit < region.area() ? Json(offset + limit) : Json(nullptr);
							return result;
						}

						std::map<uint16_t, size_t> grounds;
						std::map<uint16_t, size_t> items;
						size_t mapped = 0;
						size_t blocking = 0;
						size_t houses = 0;
						size_t zones = 0;
						size_t spawns = 0;
						size_t creatures = 0;
						size_t teleports = 0;
						std::ostringstream ascii;
						for (int y = region.from.y; y <= region.to.y; ++y) {
							for (int x = region.from.x; x <= region.to.x; ++x) {
								const Tile* tile = context.map.getTile(x, y, region.from.z);
								char marker = ' ';
								if (tile) {
									++mapped;
									marker = tile->isBlocking() ? '#' : '.';
									if (tile->ground) {
										++grounds[tile->ground->getID()];
									}
									for (const Item* item : tile->items) {
										if (!item) {
											continue;
										}
										++items[item->getID()];
										if (dynamic_cast<const Teleport*>(item)) {
											++teleports;
											marker = 'T';
										}
									}
									blocking += tile->isBlocking() ? 1 : 0;
									houses += tile->isHouseTile() ? 1 : 0;
									zones += tile->hasZone() ? 1 : 0;
									if (tile->spawn) {
										++spawns;
										marker = 'S';
									}
									if (tile->creature) {
										++creatures;
										marker = 'C';
									}
								}
								if (mode == "ascii") {
									ascii << marker;
								}
							}
							if (mode == "ascii" && y != region.to.y) {
								ascii << '\n';
							}
						}
						if (mode == "ascii") {
							result["ascii"] = ascii.str();
							result["legend"] = { { " ", "unmapped" }, { ".", "walkable" }, { "#", "blocking" }, { "T", "teleport" }, { "S", "spawn" }, { "C", "creature" } };
							return result;
						}
						Json groundDistribution = Json::array();
						for (const auto& [id, count] : grounds) {
							groundDistribution.push_back({ { "id", id }, { "count", count } });
						}
						Json itemDistribution = Json::array();
						for (const auto& [id, count] : items) {
							itemDistribution.push_back({ { "id", id }, { "count", count } });
						}
						result["tileCount"] = region.area();
						result["mappedTiles"] = mapped;
						result["unmappedTiles"] = region.area() - mapped;
						result["emptyUnmappedRatio"] = region.area() == 0 ? 0.0 : static_cast<double>(region.area() - mapped) / static_cast<double>(region.area());
						result["blockingTiles"] = blocking;
						result["blockingRatio"] = mapped == 0 ? 0.0 : static_cast<double>(blocking) / static_cast<double>(mapped);
						result["groundDistribution"] = std::move(groundDistribution);
						result["itemDistribution"] = std::move(itemDistribution);
						result["houseTiles"] = houses;
						result["zoneTiles"] = zones;
						result["spawns"] = spawns;
						result["creatures"] = creatures;
						result["teleports"] = teleports;
						return result;
					}));
				},
			});

			registry.add({
				"map_statistics",
				"Return the map_read_region summary statistics for a bounded explicit region.",
				{ { "type", "object" }, { "properties", { { "region", { { "type", "object" } } } } }, { "required", Json::array({ "region" }) }, { "additionalProperties", false } },
				false,
				[](const Json& arguments) {
					if (!arguments.contains("region") || !arguments["region"].is_object()) {
						throw Error("region is required");
					}
					Json request = arguments["region"];
					request["mode"] = "summary";
					const auto tool = ToolRegistry::Instance().find("map_read_region");
					if (!tool) {
						throw Error("map_read_region is unavailable");
					}
					return tool->handler(request);
				},
			});
		});
	}
}
