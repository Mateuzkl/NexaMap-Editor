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

#ifndef RME_UNREACHABLE_CLEANER_H_
#define RME_UNREACHABLE_CLEANER_H_

#include "position.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Map;
class Tile;

// ---------------------------------------------------------------------------
// Settings for the unreachable tile cleaner operation.
// ---------------------------------------------------------------------------
struct UnreachableCleanerSettings {
	int viewportWidth = 15;
	int viewportHeight = 11;
	int safetyMargin = 2;
	bool multiFloor = true;

	enum FloorScope { AllFloors,
		SurfaceOnly,
		UndergroundOnly };
	FloorScope floorScope = AllFloors;

	// NexaMap gameplay protection extensions
	bool preserveHouses = true;
	bool preserveSpawns = true;
	bool preserveCreatures = true;
	bool preserveTeleports = true;
	bool preserveActionUniqueID = true;
	bool preserveWaypoints = true;

	bool createBackup = true;
};

// ---------------------------------------------------------------------------
// Results produced by the analysis pass.
// ---------------------------------------------------------------------------
struct UnreachableAnalysisResult {
	int64_t totalScanned = 0;
	int64_t walkableViewpoints = 0;
	int64_t unreachableCandidates = 0;
	int64_t protectedSkipped = 0;
	int64_t tilesToRemove = 0;

	// Per-category protection counts
	int64_t housesProtected = 0;
	int64_t spawnsProtected = 0;
	int64_t creaturesProtected = 0;
	int64_t teleportsProtected = 0;
	int64_t actionUidProtected = 0;
	int64_t waypointsProtected = 0;
	int64_t neighborProtected = 0;

	double analysisTimeMs = 0;
};

// ---------------------------------------------------------------------------
// Two-pass unreachable tile cleaner.
//
// Pass 1: Indexes all walkable viewpoints into a spatial hash grid.
// Pass 2: For every non-walkable tile, queries nearby indexed viewpoints to
//          determine if the tile is visible from a valid player position using
//          Tibia's isometric floor projection rules.
//
// The algorithm is O(tiles + candidates) instead of the old
// O(tiles * viewport_area * floor_count) approach.
// ---------------------------------------------------------------------------
class UnreachableCleaner {
public:
	UnreachableCleaner(Map& map, const UnreachableCleanerSettings& settings);

	// Analyze without modifying the map. The progress callback receives a
	// percentage (0-100) and should return false to cancel.
	UnreachableAnalysisResult analyze(std::function<bool(int, const std::string&)> progressCallback = nullptr);

	// Remove the tiles identified by the last analyze() call.
	// Returns the number of tiles removed.
	int64_t execute(std::function<bool(int, const std::string&)> progressCallback = nullptr);

	const std::vector<Position>& getRemovalCandidates() const {
		return removalCandidates_;
	}

	// Static pure calculation helpers (accessible for testing)
	static bool isTileVisible(const Position& target, const Position& viewpoint, const UnreachableCleanerSettings& settings);
	static bool isFloorInScope(int z, UnreachableCleanerSettings::FloorScope scope);
	static uint64_t positionHash(int x, int y, int z);
	static uint64_t chunkKey(int cx, int cy, int cz);

private:
	bool isWalkableViewpoint(const Tile* tile) const;
	bool isTileProtected(const Tile* tile, UnreachableAnalysisResult& result) const;
	bool isTileVisibleFromViewpoint(const Position& target, const Position& viewpoint) const;
	bool isFloorInScope(int z) const;

	// Spatial hash helpers
	void buildWalkableIndex(std::function<bool(int, const std::string&)> progressCallback);
	bool queryNearbyWalkable(const Position& target) const;

	// Check whether any adjacent position is protected
	bool hasProtectedNeighbor(const Position& pos) const;

	// Build the set of waypoint positions for fast lookup
	void buildWaypointIndex();

	Map& map_;
	UnreachableCleanerSettings settings_;

	// Spatial hash: (chunk_x, chunk_y, chunk_z) -> list of walkable positions
	static constexpr int CHUNK_SIZE = 16;
	std::unordered_map<uint64_t, std::vector<Position>> walkableChunks_;

	// Positions that are protected (for neighbor safety checks)
	std::unordered_set<uint64_t> protectedPositions_;

	// Positions referenced by waypoints
	std::unordered_set<uint64_t> waypointPositions_;

	// Candidates for removal after analysis
	std::vector<Position> removalCandidates_;

	bool analyzed_ = false;
};

// ---------------------------------------------------------------------------
// Inline pure calculation helpers
// ---------------------------------------------------------------------------

inline uint64_t UnreachableCleaner::positionHash(int x, int y, int z) {
	return (static_cast<uint64_t>(static_cast<uint16_t>(x)) << 24) |
		(static_cast<uint64_t>(static_cast<uint16_t>(y)) << 8) |
		static_cast<uint64_t>(static_cast<uint8_t>(z));
}

inline uint64_t UnreachableCleaner::chunkKey(int cx, int cy, int cz) {
	return (static_cast<uint64_t>(static_cast<uint16_t>(cx)) << 24) |
		(static_cast<uint64_t>(static_cast<uint16_t>(cy)) << 8) |
		static_cast<uint64_t>(static_cast<uint8_t>(cz));
}

inline bool UnreachableCleaner::isFloorInScope(int z, UnreachableCleanerSettings::FloorScope scope) {
	switch (scope) {
		case UnreachableCleanerSettings::SurfaceOnly:
			return z <= GROUND_LAYER;
		case UnreachableCleanerSettings::UndergroundOnly:
			return z > GROUND_LAYER;
		case UnreachableCleanerSettings::AllFloors:
		default:
			return true;
	}
}

inline bool UnreachableCleaner::isTileVisible(const Position& target, const Position& viewpoint, const UnreachableCleanerSettings& settings) {
	if (!settings.multiFloor) {
		if (target.z != viewpoint.z) {
			return false;
		}
		int dx = std::abs(target.x - viewpoint.x);
		int dy = std::abs(target.y - viewpoint.y);
		int halfW = (settings.viewportWidth / 2) + settings.safetyMargin;
		int halfH = (settings.viewportHeight / 2) + settings.safetyMargin;
		return dx <= halfW && dy <= halfH;
	}

	int dz = target.z - viewpoint.z;

	if (viewpoint.z <= GROUND_LAYER) {
		if (target.z > GROUND_LAYER) {
			return false;
		}
	} else {
		if (target.z <= GROUND_LAYER) {
			return false;
		}
		if (std::abs(dz) > 2) {
			return false;
		}
	}

	int screenDx = std::abs((target.x + dz) - viewpoint.x);
	int screenDy = std::abs((target.y + dz) - viewpoint.y);

	int halfW = (settings.viewportWidth / 2) + settings.safetyMargin;
	int halfH = (settings.viewportHeight / 2) + settings.safetyMargin;

	return screenDx <= halfW && screenDy <= halfH;
}

#endif
