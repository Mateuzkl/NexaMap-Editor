//////////////////////////////////////////////////////////////////////
// This file is part of NexaMap Editor
//////////////////////////////////////////////////////////////////////
// NexaMap Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// NexaMap Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "unreachable_cleaner.h"

#include "map.h"
#include "tile.h"
#include "item.h"
#include "items.h"
#include "complexitem.h"
#include "creature.h"
#include "spawn.h"
#include "waypoints.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void logSummary(const char* tag, int64_t value) {
	std::printf("[unreachable_cleaner] %s: %lld\n", tag, static_cast<long long>(value));
}

// ---------------------------------------------------------------------------
// UnreachableCleaner
// ---------------------------------------------------------------------------

UnreachableCleaner::UnreachableCleaner(Map& map, const UnreachableCleanerSettings& settings) :
	map_(map),
	settings_(settings) {
}

// ---------------------------------------------------------------------------
// Walkable viewpoint classification
// ---------------------------------------------------------------------------

bool UnreachableCleaner::isWalkableViewpoint(const Tile* tile) const {
	if (!tile) {
		return false;
	}

	// Must have ground
	if (!tile->hasGround()) {
		return false;
	}

	// Tile must not be blocking overall
	if (tile->isBlocking()) {
		return false;
	}

	const Item* ground = tile->ground;
	const uint16_t groundId = ground->getID();
	const ItemType& groundType = g_items[groundId];

	// Ground must be a real ground tile
	if (!groundType.isGroundTile()) {
		return false;
	}

	// Ground must not be unpassable
	if (groundType.unpassable) {
		return false;
	}

	// Ground must not block pathfinder
	if (groundType.blockPathfinder) {
		return false;
	}

	// Ground must not be a wall, border (non-optional), or trash holder
	if (groundType.isWall || groundType.isTrashHolder()) {
		return false;
	}

	// Check for blocking loose items on the tile
	for (const Item* item : tile->items) {
		if (item->isBlocking() && !item->isMoveable()) {
			return false;
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// Tile protection checks
// ---------------------------------------------------------------------------

bool UnreachableCleaner::isTileProtected(const Tile* tile, UnreachableAnalysisResult& result) const {
	if (!tile) {
		return false;
	}

	// House
	if (settings_.preserveHouses && tile->isHouseTile()) {
		result.housesProtected++;
		return true;
	}
	if (settings_.preserveHouses && tile->isHouseExit()) {
		result.housesProtected++;
		return true;
	}

	// Spawn
	if (settings_.preserveSpawns && tile->spawn) {
		result.spawnsProtected++;
		return true;
	}

	// Creature
	if (settings_.preserveCreatures && tile->creature) {
		result.creaturesProtected++;
		return true;
	}

	// Items check: teleports, action/unique IDs
	if (tile->ground) {
		if (settings_.preserveActionUniqueID) {
			if (tile->ground->getActionID() > 0 || tile->ground->getUniqueID() > 0) {
				result.actionUidProtected++;
				return true;
			}
		}
	}

	for (const Item* item : tile->items) {
		if (settings_.preserveTeleports) {
			const ItemType& type = g_items[item->getID()];
			if (type.isTeleport()) {
				result.teleportsProtected++;
				return true;
			}
		}
		if (settings_.preserveActionUniqueID) {
			if (item->getActionID() > 0 || item->getUniqueID() > 0) {
				result.actionUidProtected++;
				return true;
			}
			// Check nested containers
			const Container* container = dynamic_cast<const Container*>(item);
			if (container) {
				const auto& contents = const_cast<Container*>(container)->getVector();
				for (const Item* nested : contents) {
					if (nested->getActionID() > 0 || nested->getUniqueID() > 0) {
						result.actionUidProtected++;
						return true;
					}
				}
			}
		}
	}

	// Waypoint
	if (settings_.preserveWaypoints) {
		const Position& pos = tile->getPosition();
		uint64_t hash = positionHash(pos.x, pos.y, pos.z);
		if (waypointPositions_.count(hash)) {
			result.waypointsProtected++;
			return true;
		}
	}

	return false;
}

// ---------------------------------------------------------------------------
// Visibility projection (Tibia isometric)
// ---------------------------------------------------------------------------

bool UnreachableCleaner::isTileVisibleFromViewpoint(const Position& target, const Position& viewpoint) const {
	return isTileVisible(target, viewpoint, settings_);
}

// ---------------------------------------------------------------------------
// Floor scope filter
// ---------------------------------------------------------------------------

bool UnreachableCleaner::isFloorInScope(int z) const {
	return isFloorInScope(z, settings_.floorScope);
}

// ---------------------------------------------------------------------------
// Waypoint index
// ---------------------------------------------------------------------------

void UnreachableCleaner::buildWaypointIndex() {
	waypointPositions_.clear();
	for (auto it = map_.waypoints.begin(); it != map_.waypoints.end(); ++it) {
		const Waypoint* wp = it->second;
		if (wp) {
			waypointPositions_.insert(positionHash(wp->pos.x, wp->pos.y, wp->pos.z));
		}
	}
}

// ---------------------------------------------------------------------------
// Pass 1: Build walkable index
// ---------------------------------------------------------------------------

void UnreachableCleaner::buildWalkableIndex(std::function<bool(int, const std::string&)> progressCallback) {
	walkableChunks_.clear();

	MapIterator it = map_.begin();
	const MapIterator end = map_.end();

	int64_t done = 0;
	const int64_t total = static_cast<int64_t>(map_.getTileCount());

	while (it != end) {
		Tile* tile = (*it)->get();
		if (tile && isFloorInScope(tile->getZ())) {
			if (isWalkableViewpoint(tile)) {
				const Position& pos = tile->getPosition();
				int cx = pos.x / CHUNK_SIZE;
				int cy = pos.y / CHUNK_SIZE;
				int cz = pos.z;
				walkableChunks_[chunkKey(cx, cy, cz)].push_back(pos);
			}
		}
		++it;
		++done;

		if (progressCallback && (done & 0xFFF) == 0) {
			int pct = total > 0 ? static_cast<int>(done * 40 / total) : 0; // 0-40% for indexing
			if (!progressCallback(pct, "Indexing walkable tiles...")) {
				return; // cancelled
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Pass 2: Query nearby walkable viewpoints
// ---------------------------------------------------------------------------

bool UnreachableCleaner::queryNearbyWalkable(const Position& target) const {
	// Calculate the chunk search range based on viewport + safety margin + floor offset
	int halfW = (settings_.viewportWidth / 2) + settings_.safetyMargin;
	int halfH = (settings_.viewportHeight / 2) + settings_.safetyMargin;

	// Additional range for floor projection (dz offset)
	int maxDz = 0;
	if (settings_.multiFloor) {
		if (target.z <= GROUND_LAYER) {
			maxDz = GROUND_LAYER; // Surface can project across all surface floors
		} else {
			maxDz = 2; // Underground ±2
		}
	}

	int searchRadius = std::max(halfW, halfH) + maxDz;
	int chunkRadius = (searchRadius / CHUNK_SIZE) + 1;

	int targetCx = target.x / CHUNK_SIZE;
	int targetCy = target.y / CHUNK_SIZE;

	// Determine floor range to search
	int minZ, maxZ;
	if (!settings_.multiFloor) {
		minZ = target.z;
		maxZ = target.z;
	} else if (target.z <= GROUND_LAYER) {
		minZ = 0;
		maxZ = GROUND_LAYER;
	} else {
		minZ = std::max(target.z - 2, GROUND_LAYER + 1);
		maxZ = std::min(target.z + 2, MAP_MAX_LAYER);
	}

	for (int cz = minZ; cz <= maxZ; ++cz) {
		for (int cy = targetCy - chunkRadius; cy <= targetCy + chunkRadius; ++cy) {
			for (int cx = targetCx - chunkRadius; cx <= targetCx + chunkRadius; ++cx) {
				if (cx < 0 || cy < 0) {
					continue;
				}
				auto found = walkableChunks_.find(chunkKey(cx, cy, cz));
				if (found == walkableChunks_.end()) {
					continue;
				}
				for (const Position& vp : found->second) {
					if (isTileVisibleFromViewpoint(target, vp)) {
						return true;
					}
				}
			}
		}
	}

	return false;
}

// ---------------------------------------------------------------------------
// Protected neighbor check
// ---------------------------------------------------------------------------

bool UnreachableCleaner::hasProtectedNeighbor(const Position& pos) const {
	int radius = settings_.safetyMargin;
	for (int dy = -radius; dy <= radius; ++dy) {
		for (int dx = -radius; dx <= radius; ++dx) {
			if (dx == 0 && dy == 0) {
				continue;
			}
			int nx = pos.x + dx;
			int ny = pos.y + dy;
			if (nx < 0 || ny < 0) {
				continue;
			}
			if (protectedPositions_.count(positionHash(nx, ny, pos.z))) {
				return true;
			}
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// Analyze
// ---------------------------------------------------------------------------

UnreachableAnalysisResult UnreachableCleaner::analyze(std::function<bool(int, const std::string&)> progressCallback) {
	auto startTime = std::chrono::high_resolution_clock::now();

	UnreachableAnalysisResult result;
	removalCandidates_.clear();
	protectedPositions_.clear();
	analyzed_ = false;

	// Build waypoint index for fast lookup
	buildWaypointIndex();

	// Pass 1: Index walkable viewpoints
	buildWalkableIndex(progressCallback);

	// Count walkable viewpoints
	for (const auto& pair : walkableChunks_) {
		result.walkableViewpoints += static_cast<int64_t>(pair.second.size());
	}

	logSummary("walkable", result.walkableViewpoints);

	// Pass 2: Find unreachable candidates
	// First pass: identify protected tiles and non-walkable candidates
	MapIterator it = map_.begin();
	const MapIterator end = map_.end();
	int64_t done = 0;
	const int64_t total = static_cast<int64_t>(map_.getTileCount());

	std::vector<std::pair<Position, bool>> candidatesWithProtection; // (pos, isProtected)

	while (it != end) {
		Tile* tile = (*it)->get();
		if (tile) {
			result.totalScanned++;
			const Position& pos = tile->getPosition();

			if (isFloorInScope(pos.z) && !isWalkableViewpoint(tile)) {
				// This tile is a candidate for removal — check protection first
				if (isTileProtected(tile, result)) {
					result.protectedSkipped++;
					protectedPositions_.insert(positionHash(pos.x, pos.y, pos.z));
					candidatesWithProtection.emplace_back(pos, true);
				} else {
					candidatesWithProtection.emplace_back(pos, false);
				}
			}
		}
		++it;
		++done;

		if (progressCallback && (done & 0xFFF) == 0) {
			int pct = total > 0 ? static_cast<int>(40 + done * 30 / total) : 40; // 40-70%
			if (!progressCallback(pct, "Analyzing unreachable areas...")) {
				return result; // cancelled
			}
		}
	}

	// Pass 3: Check visibility and neighbor protection for non-protected candidates
	int64_t candidatesDone = 0;
	const int64_t totalCandidates = static_cast<int64_t>(candidatesWithProtection.size());

	for (const auto& [pos, isProtected] : candidatesWithProtection) {
		if (!isProtected) {
			// Check if visible from any walkable viewpoint
			bool visible = queryNearbyWalkable(pos);

			if (!visible) {
				// Check neighbor protection
				if (hasProtectedNeighbor(pos)) {
					result.neighborProtected++;
					result.protectedSkipped++;
				} else {
					removalCandidates_.push_back(pos);
				}
			}
		}

		++candidatesDone;
		if (progressCallback && (candidatesDone & 0xFFF) == 0) {
			int pct = totalCandidates > 0 ? static_cast<int>(70 + candidatesDone * 25 / totalCandidates) : 70; // 70-95%
			if (!progressCallback(pct, "Checking protected gameplay tiles...")) {
				return result;
			}
		}
	}

	result.unreachableCandidates = static_cast<int64_t>(candidatesWithProtection.size());
	result.tilesToRemove = static_cast<int64_t>(removalCandidates_.size());

	auto endTime = std::chrono::high_resolution_clock::now();
	result.analysisTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

	analyzed_ = true;

	// Log summary
	logSummary("indexed", result.walkableViewpoints);
	logSummary("candidates", result.unreachableCandidates);
	logSummary("protected", result.protectedSkipped);
	std::printf("[unreachable_cleaner] protected breakdown: houses=%lld spawns=%lld creatures=%lld teleports=%lld aid/uid=%lld waypoints=%lld neighbors=%lld\n",
		static_cast<long long>(result.housesProtected),
		static_cast<long long>(result.spawnsProtected),
		static_cast<long long>(result.creaturesProtected),
		static_cast<long long>(result.teleportsProtected),
		static_cast<long long>(result.actionUidProtected),
		static_cast<long long>(result.waypointsProtected),
		static_cast<long long>(result.neighborProtected));
	logSummary("to_remove", result.tilesToRemove);
	std::printf("[unreachable_cleaner] analysis_ms: %.1f\n", result.analysisTimeMs);

	if (progressCallback) {
		progressCallback(95, "Analysis complete.");
	}

	return result;
}

// ---------------------------------------------------------------------------
// Execute removal
// ---------------------------------------------------------------------------

int64_t UnreachableCleaner::execute(std::function<bool(int, const std::string&)> progressCallback) {
	if (!analyzed_ || removalCandidates_.empty()) {
		return 0;
	}

	auto startTime = std::chrono::high_resolution_clock::now();

	int64_t removed = 0;
	const int64_t total = static_cast<int64_t>(removalCandidates_.size());

	for (int64_t i = 0; i < total; ++i) {
		const Position& pos = removalCandidates_[static_cast<size_t>(i)];

		// Verify tile still exists before removing (defensive)
		Tile* tile = map_.getTile(pos);
		if (tile) {
			// If tile has a spawn, remove from spawn registry
			if (tile->spawn) {
				map_.removeSpawn(tile);
			}
			map_.setTile(pos, nullptr, true);
			++removed;
		}

		if (progressCallback && (i & 0xFFF) == 0) {
			int pct = total > 0 ? static_cast<int>(i * 100 / total) : 0;
			if (!progressCallback(pct, "Removing unreachable tiles...")) {
				break; // Allow cancellation during removal
			}
		}
	}

	auto endTime = std::chrono::high_resolution_clock::now();
	double removalMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

	logSummary("removed", removed);
	std::printf("[unreachable_cleaner] removal_ms: %.1f\n", removalMs);

	// Clear state
	removalCandidates_.clear();
	analyzed_ = false;

	return removed;
}
