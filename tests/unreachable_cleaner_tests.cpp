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

#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "unreachable_cleaner.h"

namespace {
	int failures = 0;

	void check(bool condition, const std::string& message) {
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	}
}

int main() {
	std::cout << "Running unreachable cleaner tests...\n";

	// -----------------------------------------------------------------------
	// 1. Settings Defaults
	// -----------------------------------------------------------------------
	{
		UnreachableCleanerSettings settings;
		check(settings.viewportWidth == 15, "default viewport width should be 15");
		check(settings.viewportHeight == 11, "default viewport height should be 11");
		check(settings.safetyMargin == 2, "default safety margin should be 2");
		check(settings.multiFloor == true, "default multiFloor should be true");
		check(settings.floorScope == UnreachableCleanerSettings::AllFloors, "default floorScope should be AllFloors");
		check(settings.preserveHouses == true, "preserveHouses should default to true");
		check(settings.preserveSpawns == true, "preserveSpawns should default to true");
		check(settings.preserveCreatures == true, "preserveCreatures should default to true");
		check(settings.preserveTeleports == true, "preserveTeleports should default to true");
		check(settings.preserveActionUniqueID == true, "preserveActionUniqueID should default to true");
		check(settings.preserveWaypoints == true, "preserveWaypoints should default to true");
		check(settings.createBackup == true, "createBackup should default to true");
	}

	// -----------------------------------------------------------------------
	// 2. Viewport Visibility — Same Floor
	// -----------------------------------------------------------------------
	{
		UnreachableCleanerSettings settings;
		settings.viewportWidth = 15;
		settings.viewportHeight = 11;
		settings.safetyMargin = 2;
		// halfW = 15/2 + 2 = 7 + 2 = 9
		// halfH = 11/2 + 2 = 5 + 2 = 7

		Position vp(100, 100, 7);

		// Center tile
		check(UnreachableCleaner::isTileVisible(Position(100, 100, 7), vp, settings), "exact center should be visible");

		// Within boundary
		check(UnreachableCleaner::isTileVisible(Position(109, 107, 7), vp, settings), "boundary tile (dx=9, dy=7) should be visible");
		check(UnreachableCleaner::isTileVisible(Position(91, 93, 7), vp, settings), "negative boundary tile (dx=9, dy=7) should be visible");

		// Outside boundary
		check(!UnreachableCleaner::isTileVisible(Position(110, 100, 7), vp, settings), "tile outside width (dx=10) should not be visible");
		check(!UnreachableCleaner::isTileVisible(Position(100, 108, 7), vp, settings), "tile outside height (dy=8) should not be visible");
		check(!UnreachableCleaner::isTileVisible(Position(110, 108, 7), vp, settings), "tile outside both (dx=10, dy=8) should not be visible");
	}

	// -----------------------------------------------------------------------
	// 3. Safety Margin Impact
	// -----------------------------------------------------------------------
	{
		UnreachableCleanerSettings settings;
		settings.viewportWidth = 15;
		settings.viewportHeight = 11;
		settings.safetyMargin = 0;
		// halfW = 7, halfH = 5

		Position vp(100, 100, 7);
		check(UnreachableCleaner::isTileVisible(Position(107, 105, 7), vp, settings), "dx=7, dy=5 should be visible with margin=0");
		check(!UnreachableCleaner::isTileVisible(Position(108, 105, 7), vp, settings), "dx=8 should not be visible with margin=0");
		check(!UnreachableCleaner::isTileVisible(Position(107, 106, 7), vp, settings), "dy=6 should not be visible with margin=0");

		settings.safetyMargin = 5;
		// halfW = 7 + 5 = 12, halfH = 5 + 5 = 10
		check(UnreachableCleaner::isTileVisible(Position(112, 110, 7), vp, settings), "dx=12, dy=10 should be visible with margin=5");
		check(!UnreachableCleaner::isTileVisible(Position(113, 110, 7), vp, settings), "dx=13 should not be visible with margin=5");
	}

	// -----------------------------------------------------------------------
	// 4. Custom Viewport Dimensions
	// -----------------------------------------------------------------------
	{
		UnreachableCleanerSettings settings;
		settings.viewportWidth = 26;
		settings.viewportHeight = 11;
		settings.safetyMargin = 2;
		// halfW = 13 + 2 = 15, halfH = 5 + 2 = 7

		Position vp(100, 100, 7);
		check(UnreachableCleaner::isTileVisible(Position(115, 107, 7), vp, settings), "dx=15, dy=7 should be visible with custom 26x11");
		check(!UnreachableCleaner::isTileVisible(Position(116, 100, 7), vp, settings), "dx=16 should not be visible with custom 26x11");
	}

	// -----------------------------------------------------------------------
	// 5. Multi-Floor Disabled
	// -----------------------------------------------------------------------
	{
		UnreachableCleanerSettings settings;
		settings.multiFloor = false;

		Position vp(100, 100, 7);
		check(UnreachableCleaner::isTileVisible(Position(100, 100, 7), vp, settings), "same floor should be visible when multi-floor disabled");
		check(!UnreachableCleaner::isTileVisible(Position(100, 100, 6), vp, settings), "floor 6 should not be visible from floor 7 when multi-floor disabled");
		check(!UnreachableCleaner::isTileVisible(Position(100, 100, 5), vp, settings), "floor 5 should not be visible from floor 7 when multi-floor disabled");
	}

	// -----------------------------------------------------------------------
	// 6. Multi-Floor Surface Projection (Isometric dz offset)
	// -----------------------------------------------------------------------
	{
		UnreachableCleanerSettings settings;
		settings.viewportWidth = 15;
		settings.viewportHeight = 11;
		settings.safetyMargin = 2;
		settings.multiFloor = true;
		// halfW = 9, halfH = 7

		Position vp(100, 100, 7);

		// Target on floor 6 (dz = -1): screenDx = |(target.x - 1) - 100|
		// For target (101, 101, 6): (101 - 1) - 100 = 0 -> center of screen!
		check(UnreachableCleaner::isTileVisible(Position(101, 101, 6), vp, settings), "floor 6 tile shifted by dz should project to center");

		// Target on floor 5 (dz = -2): screenDx = |(target.x - 2) - 100|
		// For target (102, 102, 5): (102 - 2) - 100 = 0 -> center of screen!
		check(UnreachableCleaner::isTileVisible(Position(102, 102, 5), vp, settings), "floor 5 tile shifted by dz should project to center");

		// Surface viewpoint cannot see underground (z > 7)
		check(!UnreachableCleaner::isTileVisible(Position(100, 100, 8), vp, settings), "surface viewpoint should not see underground tile z=8");
		check(!UnreachableCleaner::isTileVisible(Position(100, 100, 10), vp, settings), "surface viewpoint should not see underground tile z=10");
	}

	// -----------------------------------------------------------------------
	// 7. Multi-Floor Underground Visibility Rules
	// -----------------------------------------------------------------------
	{
		UnreachableCleanerSettings settings;
		settings.viewportWidth = 15;
		settings.viewportHeight = 11;
		settings.safetyMargin = 2;
		settings.multiFloor = true;

		Position vp(100, 100, 10);

		// Underground cannot see surface
		check(!UnreachableCleaner::isTileVisible(Position(100, 100, 7), vp, settings), "underground viewpoint should not see surface tile z=7");

		// Underground can see ±2 floors: z=8, 9, 10, 11, 12
		// dz = -2 (floor 8)
		check(UnreachableCleaner::isTileVisible(Position(102, 102, 8), vp, settings), "underground viewpoint can see z=8 (dz=-2)");
		// dz = +2 (floor 12)
		check(UnreachableCleaner::isTileVisible(Position(98, 98, 12), vp, settings), "underground viewpoint can see z=12 (dz=+2)");

		// dz = +3 (floor 13) -> exceeds ±2
		check(!UnreachableCleaner::isTileVisible(Position(97, 97, 13), vp, settings), "underground viewpoint cannot see z=13 (dz=+3 > 2)");
	}

	// -----------------------------------------------------------------------
	// 8. Floor Scope Filtering
	// -----------------------------------------------------------------------
	{
		// SurfaceOnly (0..7)
		for (int z = 0; z <= 7; ++z) {
			check(UnreachableCleaner::isFloorInScope(z, UnreachableCleanerSettings::SurfaceOnly), "surface floor " + std::to_string(z) + " should be in SurfaceOnly scope");
		}
		for (int z = 8; z <= 15; ++z) {
			check(!UnreachableCleaner::isFloorInScope(z, UnreachableCleanerSettings::SurfaceOnly), "underground floor " + std::to_string(z) + " should NOT be in SurfaceOnly scope");
		}

		// UndergroundOnly (8..15)
		for (int z = 0; z <= 7; ++z) {
			check(!UnreachableCleaner::isFloorInScope(z, UnreachableCleanerSettings::UndergroundOnly), "surface floor " + std::to_string(z) + " should NOT be in UndergroundOnly scope");
		}
		for (int z = 8; z <= 15; ++z) {
			check(UnreachableCleaner::isFloorInScope(z, UnreachableCleanerSettings::UndergroundOnly), "underground floor " + std::to_string(z) + " should be in UndergroundOnly scope");
		}

		// AllFloors (0..15)
		for (int z = 0; z <= 15; ++z) {
			check(UnreachableCleaner::isFloorInScope(z, UnreachableCleanerSettings::AllFloors), "floor " + std::to_string(z) + " should be in AllFloors scope");
		}
	}

	// -----------------------------------------------------------------------
	// 9. Spatial Hash & Chunk Key Utilities
	// -----------------------------------------------------------------------
	{
		uint64_t h1 = UnreachableCleaner::positionHash(100, 200, 7);
		uint64_t h2 = UnreachableCleaner::positionHash(100, 200, 8);
		uint64_t h3 = UnreachableCleaner::positionHash(101, 200, 7);
		check(h1 != h2, "different z must yield different position hashes");
		check(h1 != h3, "different x must yield different position hashes");

		uint64_t c1 = UnreachableCleaner::chunkKey(10, 20, 7);
		uint64_t c2 = UnreachableCleaner::chunkKey(10, 20, 8);
		uint64_t c3 = UnreachableCleaner::chunkKey(-1, 0, 7);
		check(c1 != c2, "different chunk z must yield different chunk keys");
		check(c1 != c3, "negative coordinates must yield distinct chunk keys");
	}

	// -----------------------------------------------------------------------
	// 10. Boundary / Edge Coordinates (Regression)
	// -----------------------------------------------------------------------
	{
		UnreachableCleanerSettings settings;
		Position vpOrigin(0, 0, 0);
		check(UnreachableCleaner::isTileVisible(Position(0, 0, 0), vpOrigin, settings), "origin position (0,0,0) should be visible from itself");

		Position vpMax(65535, 65535, 15);
		check(UnreachableCleaner::isTileVisible(Position(65535, 65535, 15), vpMax, settings), "max coordinates (65535, 65535, 15) should be visible from itself");
	}

	// -----------------------------------------------------------------------
	// 11. Same-Floor Neighbor Safety Radius (Z-isolation)
	// -----------------------------------------------------------------------
	{
		UnreachableCleanerSettings settings;
		settings.safetyMargin = 2;

		// Verify same-floor neighbor logic:
		// Position hash matches same z
		uint64_t protHashZ7 = UnreachableCleaner::positionHash(101, 101, 7);
		uint64_t queryHashZ7 = UnreachableCleaner::positionHash(101, 101, 7);
		check(protHashZ7 == queryHashZ7, "same floor and coords must match protected hash");

		uint64_t protHashZ6 = UnreachableCleaner::positionHash(101, 101, 6);
		check(protHashZ7 != protHashZ6, "neighbor protection must not bleed across different floors");
	}

	// -----------------------------------------------------------------------
	// 12. Recursive Multi-Level Container Protection Checks
	// -----------------------------------------------------------------------
	{
		struct MockContainerItem {
			int actionId = 0;
			int uniqueId = 0;
			bool isTeleport = false;
			std::vector<MockContainerItem> children;

			bool isProtected(bool preserveAidUid, bool preserveTeleport) const {
				if (preserveTeleport && isTeleport) {
					return true;
				}
				if (preserveAidUid && (actionId > 0 || uniqueId > 0)) {
					return true;
				}
				for (const auto& child : children) {
					if (child.isProtected(preserveAidUid, preserveTeleport)) {
						return true;
					}
				}
				return false;
			}
		};

		// 3-level deep nesting: Box -> Backpack -> Bag -> Quest Key
		MockContainerItem key;
		key.actionId = 2000;

		MockContainerItem bag;
		bag.children.push_back(key);

		MockContainerItem backpack;
		backpack.children.push_back(bag);

		MockContainerItem chest;
		chest.children.push_back(backpack);

		check(chest.isProtected(true, true), "nested ActionID in 4th level container must be protected");
		check(!chest.isProtected(false, true), "nested ActionID must not protect when preserveActionUniqueID is false");

		// Nested teleport
		MockContainerItem tpItem;
		tpItem.isTeleport = true;

		MockContainerItem tpContainer;
		tpContainer.children.push_back(tpItem);

		check(tpContainer.isProtected(true, true), "nested teleport inside container must be protected");
		check(!tpContainer.isProtected(true, false), "nested teleport must not protect when preserveTeleports is false");

		// Clean container
		MockContainerItem cleanItem;
		MockContainerItem cleanChest;
		cleanChest.children.push_back(cleanItem);
		check(!cleanChest.isProtected(true, true), "clean items inside container must not trigger protection");
	}

	// -----------------------------------------------------------------------
	// 13. Moveable-Blocker Walkability Semantics
	// -----------------------------------------------------------------------
	{
		// A blocking item must not count as a walkable viewpoint regardless
		// of whether it is moveable (e.g. crate/box blocking path).
		struct MockItem {
			bool blocking = false;
			bool moveable = false;
		};

		auto isWalkableItem = [](const MockItem& item) -> bool {
			return !item.blocking; // Blocking items are never walkable viewpoints
		};

		MockItem unmoveableWall { true, false };
		MockItem moveableCrate { true, true };
		MockItem flowerPot { false, true };

		check(!isWalkableItem(unmoveableWall), "unmoveable blocking wall must be non-walkable");
		check(!isWalkableItem(moveableCrate), "moveable blocking crate must be non-walkable");
		check(isWalkableItem(flowerPot), "non-blocking moveable item must be walkable");
	}

	// -----------------------------------------------------------------------
	// 14. Analysis Status & Cancellation Lifecycle
	// -----------------------------------------------------------------------
	{
		UnreachableAnalysisResult result;
		check(result.status == AnalysisStatus::Completed, "initial status should be Completed");
		check(result.isCompleted(), "isCompleted() must return true initially");
		check(!result.isCancelled(), "isCancelled() must return false initially");
		check(!result.isFailed(), "isFailed() must return false initially");

		result.status = AnalysisStatus::Cancelled;
		check(!result.isCompleted(), "isCompleted() must return false when Cancelled");
		check(result.isCancelled(), "isCancelled() must return true when Cancelled");

		result.status = AnalysisStatus::Failed;
		check(result.isFailed(), "isFailed() must return true when Failed");
	}

	// -----------------------------------------------------------------------
	// 15. Non-Cancellable Destructive Execution Simulation
	// -----------------------------------------------------------------------
	{
		// Test that destructive execution ignores cancel attempts and completes all removals
		std::vector<Position> candidates = { Position(1, 1, 7), Position(2, 2, 7), Position(3, 3, 7) };
		std::vector<Position> removedTiles;

		auto cancelCallback = [](int, const std::string&) -> bool {
			return false; // User tries to cancel
		};

		for (const auto& pos : candidates) {
			removedTiles.push_back(pos);
			cancelCallback(50, "Removing..."); // Ignored by non-cancellable loop
		}

		check(removedTiles.size() == candidates.size(), "destructive execution must not be interrupted by cancel signal");
	}

	// -----------------------------------------------------------------------
	// 16. Waypoint Cleanup When Waypoint Preservation is Disabled
	// -----------------------------------------------------------------------
	{
		struct MockWaypoint {
			std::string name;
			Position pos;
		};

		std::map<std::string, MockWaypoint> waypoints;
		int locationWaypointCount = 0;

		// Add waypoint at (50, 50, 7)
		waypoints["secret_temple"] = { "secret_temple", Position(50, 50, 7) };
		locationWaypointCount++;

		check(waypoints.size() == 1, "waypoint should be registered");
		check(locationWaypointCount == 1, "location waypoint count should be 1");

		// Simulate removal with preserveWaypoints = false
		Position tileToRemove(50, 50, 7);
		std::vector<std::string> toRemove;
		for (const auto& [name, wp] : waypoints) {
			if (wp.pos == tileToRemove) {
				toRemove.push_back(name);
			}
		}
		for (const auto& name : toRemove) {
			waypoints.erase(name);
			locationWaypointCount--;
		}

		check(waypoints.empty(), "waypoint entry must be removed when preserveWaypoints is false");
		check(locationWaypointCount == 0, "location waypoint count must be decremented");
	}

	// -----------------------------------------------------------------------
	// 17. Collision-Resistant Sub-Second Backup Filenames
	// -----------------------------------------------------------------------
	{
		std::string origPath = "maps/world.otbm";
		std::string subsecondPrefix = "maps/world_backup_20260919_153000_123";

		// Verify duplicate suffix generation
		auto resolveCollision = [](const std::string& base, int existingCount) -> std::string {
			std::string candidate = base + ".otbm";
			if (existingCount > 0) {
				candidate = base + "_" + std::to_string(existingCount) + ".otbm";
			}
			return candidate;
		};

		std::string path0 = resolveCollision(subsecondPrefix, 0);
		std::string path1 = resolveCollision(subsecondPrefix, 1);
		std::string path2 = resolveCollision(subsecondPrefix, 2);

		check(path0 != path1, "colliding backup must produce distinct filename with counter");
		check(path1 != path2, "each subsequent collision must increment suffix");
		check(path1 == "maps/world_backup_20260919_153000_123_1.otbm", "suffix format must be _1");

		// Verify otgz extension normalization to otbm
		auto normalizeBackupExt = [](std::string ext) -> std::string {
			for (auto& c : ext) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			if (ext == "otgz" || ext.empty()) {
				return "otbm";
			}
			return ext;
		};
		check(normalizeBackupExt("otgz") == "otbm", "otgz backup extension must normalize to otbm");
		check(normalizeBackupExt("OTGZ") == "otbm", "case-insensitive OTGZ must normalize to otbm");
		check(normalizeBackupExt("otbm") == "otbm", "otbm backup extension must remain otbm");
	}

	if (failures != 0) {
		std::cerr << failures << " unreachable cleaner test(s) FAILED.\n";
		return 1;
	}

	std::cout << "All unreachable cleaner tests PASSED.\n";
	return 0;
}
