// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_tools_search.h"

#include "mcp_item_identity.h"
#include "mcp_registry.h"
#include "mcp_tool_utils.h"

#include "../complexitem.h"
#include "../creature.h"
#include "../item.h"
#include "../map.h"
#include "../tile.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace mcp {
	namespace {
		bool ItemMatches(const Item& item, const std::string& type, const Json& arguments) {
			if (type == "item-id") {
				return arguments.contains("id") && item.getID() == arguments["id"].get<uint16_t>();
			}
			if (type == "item-name") {
				return arguments.contains("query") && LowerText(g_items.getItemType(item.getID()).name).find(LowerText(arguments["query"].get<std::string>())) != std::string::npos;
			}
			if (type == "action-id") {
				return arguments.contains("id") && item.getActionID() == arguments["id"].get<uint16_t>();
			}
			if (type == "unique-id") {
				return arguments.contains("id") && item.getUniqueID() == arguments["id"].get<uint16_t>();
			}
			if (type == "teleport") {
				return dynamic_cast<const Teleport*>(&item) != nullptr;
			}
			if (type == "container") {
				return dynamic_cast<const Container*>(&item) != nullptr;
			}
			if (type == "writeable") {
				return item.canWriteText() || !item.getText().empty();
			}
			return false;
		}
	}

	void RegisterSearchTools() {
		static std::once_flag once;
		std::call_once(once, [] {
			ToolRegistry::Instance().add({
				"map_find",
				"Search a bounded region for items, metadata, creatures, teleports, duplicates or stacked walls with pagination.",
				{
					{ "type", "object" },
					{ "properties", {
										{ "region", { { "type", "object" } } },
										{ "type", { { "type", "string" }, { "enum", Json::array({ "item-id", "item-name", "action-id", "unique-id", "teleport", "container", "writeable", "monster", "npc", "house", "zone", "spawn", "waypoint", "duplicate-item", "wall-on-wall" }) } } },
										{ "id", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 65535 } } },
										{ "query", { { "type", "string" }, { "maxLength", 128 } } },
										{ "limit", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 1000 }, { "default", 100 } } },
										{ "offset", { { "type", "integer" }, { "minimum", 0 }, { "default", 0 } } },
									} },
					{ "required", Json::array({ "region", "type" }) },
					{ "additionalProperties", false },
				},
				false,
				[](const Json& arguments) {
					if (!arguments.contains("region") || !arguments["region"].is_object() || !arguments.contains("type") || !arguments["type"].is_string()) {
						throw Error("region and type are required");
					}
					const Region region = ParseRegion(arguments["region"], 262144);
					const std::string type = arguments["type"].get<std::string>();
					if ((type == "item-id" || type == "action-id" || type == "unique-id") && (!arguments.contains("id") || !arguments["id"].is_number_unsigned())) {
						throw Error("this search type requires id");
					}
					if (type == "item-name" && (!arguments.contains("query") || !arguments["query"].is_string())) {
						throw Error("item-name search requires query");
					}
					const size_t limit = std::min<size_t>(arguments.value("limit", 100), 1000);
					const size_t offset = arguments.value("offset", 0);
					return StructuredResult(OnGui([arguments, region, type, limit, offset](const EditorContext& context) {
						Json matches = Json::array();
						size_t total = 0;
						auto append = [&](const Position& position, Json detail) { if (total++ >= offset && matches.size() < limit){ matches.push_back({ { "position", PositionJson(position) }, { "detail", std::move(detail) } });
} };
						for (int y = region.from.y; y <= region.to.y; ++y) {
							for (int x = region.from.x; x <= region.to.x; ++x) {
								const Position position(x, y, region.from.z);
								const Tile* tile = context.map.getTile(position);
								if (!tile) {
									continue;
								}
								if ((type == "monster" || type == "npc") && tile->creature && (tile->creature->isNpc() == (type == "npc"))) {
									append(position, { { "creature", tile->creature->getName() } });
								}
								if (type == "house" && tile->getHouseID() != 0 && (!arguments.contains("id") || tile->getHouseID() == arguments["id"].get<uint32_t>())) {
									append(position, { { "houseId", tile->getHouseID() } });
									continue;
								}
								if (type == "zone") {
									for (unsigned int zoneId : tile->zones) {
										if (!arguments.contains("id") || zoneId == arguments["id"].get<unsigned int>()) {
											append(position, { { "zoneId", zoneId }, { "name", context.map.zones.getZoneName(zoneId) } });
										}
									}
									continue;
								}
								if (type == "spawn" && tile->spawn) {
									append(position, { { "radius", tile->spawn->getSize() } });
									continue;
								}
								if (type == "waypoint") {
									if (Waypoint* waypoint = context.map.waypoints.getWaypoint(tile->getLocation())) {
										append(position, { { "name", waypoint->name } });
									}
									continue;
								}
								if (type == "duplicate-item") {
									std::unordered_map<uint16_t, size_t> counts;
									for (const Item* item : tile->items) {
										if (item && ++counts[item->getID()] == 2) {
											append(position, { { "id", item->getID() } });
										}
									}
									continue;
								}
								if (type == "wall-on-wall") {
									size_t walls = 0;
									for (const Item* item : tile->items) {
										walls += item && item->isWall() ? 1 : 0;
									}
									if (walls > 1) {
										append(position, { { "wallCount", walls } });
									}
									continue;
								}
								auto inspect = [&](const Item* item) { if (item && ItemMatches(*item, type, arguments)) { Json identity = ItemIdentity(item->getID(), context.itemIdMode, g_items.getItemType(item->getID())); identity["actionId"] = item->getActionID(); identity["uniqueId"] = item->getUniqueID(); append(position, std::move(identity)); } };
								inspect(tile->ground);
								for (const Item* item : tile->items) {
									inspect(item);
								}
							}
						}
						return Json { { "matches", std::move(matches) }, { "total", total }, { "offset", offset }, { "nextOffset", offset + limit < total ? Json(offset + limit) : Json(nullptr) }, { "mapSessionId", context.mapSessionId } };
					}));
				},
			});

			const Tool mapFind = *ToolRegistry::Instance().find("map_find");
			ToolRegistry::Instance().add({ "map_search", "Alias of map_find for broad map and entity searches with pagination.", mapFind.inputSchema, false, mapFind.handler });
		});
	}
}
