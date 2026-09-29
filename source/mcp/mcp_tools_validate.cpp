// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_tools_validate.h"

#include "mcp_registry.h"
#include "mcp_tool_utils.h"

#include "../complexitem.h"
#include "../creature.h"
#include "../item.h"
#include "../map.h"
#include "../spawn.h"
#include "../tile.h"

#include <array>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace mcp {
	namespace {
		Json RegionWrapperSchema() {
			return {
				{ "type", "object" },
				{ "properties", {
									{ "region", { { "type", "object" } } },
									{ "limit", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 1000 }, { "default", 100 } } },
								} },
				{ "required", Json::array({ "region" }) },
				{ "additionalProperties", false },
			};
		}

		bool InRegion(const Position& position, const Region& region) {
			return position.z == region.from.z && position.x >= region.from.x && position.x <= region.to.x && position.y >= region.from.y && position.y <= region.to.y;
		}

		long long PositionKey(const Position& position) {
			return (static_cast<long long>(position.z) << 40) | (static_cast<long long>(position.y) << 20) | position.x;
		}

		bool Walkable(const Map& map, const Position& position) {
			const Tile* tile = map.getTile(position);
			return tile && tile->ground && !tile->isBlocking();
		}

		Json Issue(const std::string& code, const Position& position, const std::string& message) {
			return { { "code", code }, { "position", PositionJson(position) }, { "message", message } };
		}

		bool LooksLikeTransition(const Item& item) {
			const std::string name = LowerText(g_items.getItemType(item.getID()).name);
			return name.find("stair") != std::string::npos || name.find("ladder") != std::string::npos || name.find("hole") != std::string::npos || name.find("ramp") != std::string::npos || name.find("rope spot") != std::string::npos;
		}
	}

	void RegisterValidationTools() {
		static std::once_flag once;
		std::call_once(once, [] {
			ToolRegistry& registry = ToolRegistry::Instance();
			registry.add({
				"path_check",
				"Check actual walkable connectivity inside a bounded region using loaded item blocking flags.",
				{
					{ "type", "object" },
					{ "properties", {
										{ "region", { { "type", "object" } } },
										{ "start", PositionSchema() },
										{ "end", PositionSchema() },
										{ "mode", { { "type", "string" }, { "enum", Json::array({ "route", "flood-fill", "region-connectivity" }) }, { "default", "region-connectivity" } } },
									} },
					{ "required", Json::array({ "region" }) },
					{ "additionalProperties", false },
				},
				false,
				[](const Json& arguments) {
					if (!arguments.contains("region") || !arguments["region"].is_object()) {
						throw Error("region is required");
					}
					const Region region = ParseRegion(arguments["region"], 65536);
					const std::string mode = arguments.value("mode", std::string("region-connectivity"));
					std::optional<Position> start;
					std::optional<Position> end;
					if (arguments.contains("start")) {
						start = ParsePosition(arguments["start"], "start");
					}
					if (arguments.contains("end")) {
						end = ParsePosition(arguments["end"], "end");
					}
					if (mode == "route" && (!start || !end)) {
						throw Error("route mode requires start and end");
					}
					return StructuredResult(OnGui([region, mode, start, end](const EditorContext& context) {
						Position seed;
						bool foundSeed = false;
						if (start) {
							if (!InRegion(*start, region)) {
								throw Error("start is outside region");
							}
							seed = *start;
							foundSeed = Walkable(context.map, seed);
						} else {
							for (int y = region.from.y; y <= region.to.y && !foundSeed; ++y) {
								for (int x = region.from.x; x <= region.to.x; ++x) {
									Position candidate(x, y, region.from.z);
									if (Walkable(context.map, candidate)) {
										seed = candidate;
										foundSeed = true;
										break;
									}
								}
							}
						}
						if (!foundSeed) {
							return Json { { "connected", false }, { "walkableTiles", 0 }, { "reachableTiles", 0 }, { "message", "region has no walkable ground" } };
						}
						std::queue<Position> pending;
						std::unordered_set<long long> visited;
						pending.push(seed);
						visited.insert(PositionKey(seed));
						constexpr std::array<std::pair<int, int>, 4> offsets { std::pair { 0, -1 }, { -1, 0 }, { 1, 0 }, { 0, 1 } };
						while (!pending.empty()) {
							const Position current = pending.front();
							pending.pop();
							for (const auto& [dx, dy] : offsets) {
								const Position next(current.x + dx, current.y + dy, current.z);
								if (!InRegion(next, region) || !Walkable(context.map, next) || !visited.insert(PositionKey(next)).second) {
									continue;
								}
								pending.push(next);
							}
						}
						size_t walkable = 0;
						for (int y = region.from.y; y <= region.to.y; ++y) {
							for (int x = region.from.x; x <= region.to.x; ++x) {
								walkable += Walkable(context.map, Position(x, y, region.from.z)) ? 1 : 0;
							}
						}
						const bool routeReached = !end || visited.count(PositionKey(*end)) != 0;
						return Json { { "mode", mode }, { "connected", mode == "route" ? routeReached : visited.size() == walkable }, { "start", PositionJson(seed) }, { "endReached", routeReached }, { "walkableTiles", walkable }, { "reachableTiles", visited.size() }, { "unreachableTiles", walkable - visited.size() } };
					}));
				},
			});

			registry.add({
				"border_check",
				"Inspect adjacent ground transitions and report missing autoborders without modifying the map.",
				RegionWrapperSchema(),
				false,
				[](const Json& arguments) {
					const Region region = ParseRegion(arguments.at("region"));
					const size_t limit = std::min<size_t>(arguments.value("limit", 100), 1000);
					return StructuredResult(OnGui([region, limit](const EditorContext& context) {
						Json issues = Json::array();
						size_t total = 0;
						constexpr std::array<std::pair<int, int>, 2> offsets { std::pair { 1, 0 }, { 0, 1 } };
						for (int y = region.from.y; y <= region.to.y; ++y) {
							for (int x = region.from.x; x <= region.to.x; ++x) {
								const Tile* tile = context.map.getTile(x, y, region.from.z);
								if (!tile || !tile->ground) {
									continue;
								}
								for (const auto& [dx, dy] : offsets) {
									const Tile* neighbour = context.map.getTile(x + dx, y + dy, region.from.z);
									if (!neighbour || !neighbour->ground || tile->ground->getID() == neighbour->ground->getID()) {
										continue;
									}
									const GroundBrush* first = tile->getGroundBrush();
									const GroundBrush* second = neighbour->getGroundBrush();
									if (!first || !second || (!tile->hasBorders() && !neighbour->hasBorders())) {
										++total;
										if (issues.size() < limit) {
											issues.push_back(Issue("missing-ground-border", Position(x, y, region.from.z), "adjacent different grounds have no detected border item"));
										}
									}
								}
							}
						}
						return Json { { "valid", total == 0 }, { "issueCount", total }, { "issues", std::move(issues) }, { "truncated", total > limit } };
					}));
				},
			});

			registry.add({
				"map_validate",
				"Run bounded non-mutating map integrity checks for ground, duplicates, walls, IDs, teleports, spawns and creatures.",
				RegionWrapperSchema(),
				false,
				[](const Json& arguments) {
					const Region region = ParseRegion(arguments.at("region"), 65536);
					const size_t limit = std::min<size_t>(arguments.value("limit", 100), 1000);
					return StructuredResult(OnGui([region, limit](const EditorContext& context) {
						Json issues = Json::array();
						size_t total = 0;
						auto addIssue = [&](Json issue) { ++total; if (issues.size() < limit){ issues.push_back(std::move(issue));
} };
						for (int y = region.from.y; y <= region.to.y; ++y) {
							for (int x = region.from.x; x <= region.to.x; ++x) {
								const Position position(x, y, region.from.z);
								const Tile* tile = context.map.getTile(position);
								if (!tile) {
									continue;
								}
								if (!tile->ground && (!tile->items.empty() || tile->spawn || tile->creature)) {
									addIssue(Issue("missing-ground", position, "mapped content has no ground"));
								}
								std::unordered_map<uint16_t, size_t> ids;
								size_t walls = 0;
								for (const Item* item : tile->items) {
									if (!item || !item->typeExists()) {
										addIssue(Issue("invalid-item", position, "tile contains an item missing from active resources"));
										continue;
									}
									if (++ids[item->getID()] > 1 && IsRemovableDuplicatedItem(item)) {
										addIssue(Issue("duplicate-item", position, "tile contains a removable duplicate item ID"));
									}
									walls += item->isWall() ? 1 : 0;
									if (item->getUniqueID() != 0 && context.map.getUniqueIdCount(item->getUniqueID()) > 1) {
										addIssue(Issue("duplicate-unique-id", position, "unique ID occurs more than once on the map"));
									}
									if (const auto* teleport = dynamic_cast<const Teleport*>(item)) {
										if (!teleport->hasDestination() || !context.map.getTile(teleport->getDestination())) {
											addIssue(Issue("broken-teleport", position, "teleport destination is missing or unmapped"));
										}
									}
								}
								if (walls > 1) {
									addIssue(Issue("stacked-walls", position, "tile contains more than one wall item"));
								}
								if (tile->spawn && tile->spawn->getSize() <= 0) {
									addIssue(Issue("empty-spawn", position, "spawn radius is invalid"));
								}
								if (tile->creature && !tile->spawn) {
									addIssue(Issue("creature-outside-spawn", position, "creature tile has no spawn center"));
								}
							}
						}
						return Json { { "valid", total == 0 }, { "issueCount", total }, { "issues", std::move(issues) }, { "truncated", total > limit }, { "mapSessionId", context.mapSessionId }, { "changeGeneration", context.map.getChangeGeneration() } };
					}));
				},
			});

			registry.add({
				"teleport_graph",
				"List teleports in a bounded region and validate their destinations.",
				RegionWrapperSchema(),
				false,
				[](const Json& arguments) {
					const Region region = ParseRegion(arguments.at("region"), 65536);
					const size_t limit = std::min<size_t>(arguments.value("limit", 100), 1000);
					return StructuredResult(OnGui([region, limit](const EditorContext& context) {
						Json entries = Json::array();
						size_t total = 0;
						for (int y = region.from.y; y <= region.to.y; ++y) {
							for (int x = region.from.x; x <= region.to.x; ++x) {
								const Position source(x, y, region.from.z);
								const Tile* tile = context.map.getTile(source);
								if (!tile) {
									continue;
								}
								for (const Item* item : tile->items) {
									if (const auto* teleport = dynamic_cast<const Teleport*>(item)) {
										++total;
										if (entries.size() >= limit) {
											continue;
										}
										const Position destination = teleport->getDestination();
										const Tile* target = context.map.getTile(destination);
										entries.push_back({ { "source", PositionJson(source) }, { "destination", PositionJson(destination) }, { "destinationMapped", target != nullptr }, { "destinationBlocked", target ? target->isBlocking() : false }, { "valid", teleport->hasDestination() && target != nullptr } });
									}
								}
							}
						}
						return Json { { "teleports", std::move(entries) }, { "total", total }, { "truncated", total > limit } };
					}));
				},
			});

			registry.add({
				"floor_transitions",
				"Find stairs, ladders, holes, ramps and similar transition items in a bounded region.",
				RegionWrapperSchema(),
				false,
				[](const Json& arguments) {
					const Region region = ParseRegion(arguments.at("region"), 65536);
					const size_t limit = std::min<size_t>(arguments.value("limit", 100), 1000);
					return StructuredResult(OnGui([region, limit](const EditorContext& context) {
						Json entries = Json::array();
						size_t total = 0;
						for (int y = region.from.y; y <= region.to.y; ++y) {
							for (int x = region.from.x; x <= region.to.x; ++x) {
								const Position position(x, y, region.from.z);
								const Tile* tile = context.map.getTile(position);
								if (!tile) {
									continue;
								}
								auto inspect = [&](const Item* item) { if (item && LooksLikeTransition(*item)) { ++total; if (entries.size() < limit){ entries.push_back({ { "position", PositionJson(position) }, { "id", item->getID() }, { "name", g_items.getItemType(item->getID()).name } });
} } };
								inspect(tile->ground);
								for (const Item* item : tile->items) {
									inspect(item);
								}
							}
						}
						return Json { { "transitions", std::move(entries) }, { "total", total }, { "truncated", total > limit } };
					}));
				},
			});
		});
	}
}
