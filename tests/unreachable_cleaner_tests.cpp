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
		check(UnreachableCleaner::isTileVisible(Position(100, 100, 7), vp, settings),
			"exact center should be visible");

		// Within boundary
		check(UnreachableCleaner::isTileVisible(Position(109, 107, 7), vp, settings),
			"boundary tile (dx=9, dy=7) should be visible");
		check(UnreachableCleaner::isTileVisible(Position(91, 93, 7), vp, settings),
			"negative boundary tile (dx=9, dy=7) should be visible");

		// Outside boundary
		check(!UnreachableCleaner::isTileVisible(Position(110, 100, 7), vp, settings),
			"tile outside width (dx=10) should not be visible");
		check(!UnreachableCleaner::isTileVisible(Position(100, 108, 7), vp, settings),
			"tile outside height (dy=8) should not be visible");
		check(!UnreachableCleaner::isTileVisible(Position(110, 108, 7), vp, settings),
			"tile outside both (dx=10, dy=8) should not be visible");
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
		check(UnreachableCleaner::isTileVisible(Position(107, 105, 7), vp, settings),
			"dx=7, dy=5 should be visible with margin=0");
		check(!UnreachableCleaner::isTileVisible(Position(108, 105, 7), vp, settings),
			"dx=8 should not be visible with margin=0");
		check(!UnreachableCleaner::isTileVisible(Position(107, 106, 7), vp, settings),
			"dy=6 should not be visible with margin=0");

		settings.safetyMargin = 5;
		// halfW = 7 + 5 = 12, halfH = 5 + 5 = 10
		check(UnreachableCleaner::isTileVisible(Position(112, 110, 7), vp, settings),
			"dx=12, dy=10 should be visible with margin=5");
		check(!UnreachableCleaner::isTileVisible(Position(113, 110, 7), vp, settings),
			"dx=13 should not be visible with margin=5");
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
		check(UnreachableCleaner::isTileVisible(Position(115, 107, 7), vp, settings),
			"dx=15, dy=7 should be visible with custom 26x11");
		check(!UnreachableCleaner::isTileVisible(Position(116, 100, 7), vp, settings),
			"dx=16 should not be visible with custom 26x11");
	}

	// -----------------------------------------------------------------------
	// 5. Multi-Floor Disabled
	// -----------------------------------------------------------------------
	{
		UnreachableCleanerSettings settings;
		settings.multiFloor = false;

		Position vp(100, 100, 7);
		check(UnreachableCleaner::isTileVisible(Position(100, 100, 7), vp, settings),
			"same floor should be visible when multi-floor disabled");
		check(!UnreachableCleaner::isTileVisible(Position(100, 100, 6), vp, settings),
			"floor 6 should not be visible from floor 7 when multi-floor disabled");
		check(!UnreachableCleaner::isTileVisible(Position(100, 100, 5), vp, settings),
			"floor 5 should not be visible from floor 7 when multi-floor disabled");
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
		check(UnreachableCleaner::isTileVisible(Position(101, 101, 6), vp, settings),
			"floor 6 tile shifted by dz should project to center");

		// Target on floor 5 (dz = -2): screenDx = |(target.x - 2) - 100|
		// For target (102, 102, 5): (102 - 2) - 100 = 0 -> center of screen!
		check(UnreachableCleaner::isTileVisible(Position(102, 102, 5), vp, settings),
			"floor 5 tile shifted by dz should project to center");

		// Surface viewpoint cannot see underground (z > 7)
		check(!UnreachableCleaner::isTileVisible(Position(100, 100, 8), vp, settings),
			"surface viewpoint should not see underground tile z=8");
		check(!UnreachableCleaner::isTileVisible(Position(100, 100, 10), vp, settings),
			"surface viewpoint should not see underground tile z=10");
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
		check(!UnreachableCleaner::isTileVisible(Position(100, 100, 7), vp, settings),
			"underground viewpoint should not see surface tile z=7");

		// Underground can see ±2 floors: z=8, 9, 10, 11, 12
		// dz = -2 (floor 8)
		check(UnreachableCleaner::isTileVisible(Position(102, 102, 8), vp, settings),
			"underground viewpoint can see z=8 (dz=-2)");
		// dz = +2 (floor 12)
		check(UnreachableCleaner::isTileVisible(Position(98, 98, 12), vp, settings),
			"underground viewpoint can see z=12 (dz=+2)");

		// dz = +3 (floor 13) -> exceeds ±2
		check(!UnreachableCleaner::isTileVisible(Position(97, 97, 13), vp, settings),
			"underground viewpoint cannot see z=13 (dz=+3 > 2)");
	}

	// -----------------------------------------------------------------------
	// 8. Floor Scope Filtering
	// -----------------------------------------------------------------------
	{
		// SurfaceOnly (0..7)
		for (int z = 0; z <= 7; ++z) {
			check(UnreachableCleaner::isFloorInScope(z, UnreachableCleanerSettings::SurfaceOnly),
				"surface floor " + std::to_string(z) + " should be in SurfaceOnly scope");
		}
		for (int z = 8; z <= 15; ++z) {
			check(!UnreachableCleaner::isFloorInScope(z, UnreachableCleanerSettings::SurfaceOnly),
				"underground floor " + std::to_string(z) + " should NOT be in SurfaceOnly scope");
		}

		// UndergroundOnly (8..15)
		for (int z = 0; z <= 7; ++z) {
			check(!UnreachableCleaner::isFloorInScope(z, UnreachableCleanerSettings::UndergroundOnly),
				"surface floor " + std::to_string(z) + " should NOT be in UndergroundOnly scope");
		}
		for (int z = 8; z <= 15; ++z) {
			check(UnreachableCleaner::isFloorInScope(z, UnreachableCleanerSettings::UndergroundOnly),
				"underground floor " + std::to_string(z) + " should be in UndergroundOnly scope");
		}

		// AllFloors (0..15)
		for (int z = 0; z <= 15; ++z) {
			check(UnreachableCleaner::isFloorInScope(z, UnreachableCleanerSettings::AllFloors),
				"floor " + std::to_string(z) + " should be in AllFloors scope");
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
		check(UnreachableCleaner::isTileVisible(Position(0, 0, 0), vpOrigin, settings),
			"origin position (0,0,0) should be visible from itself");

		Position vpMax(65535, 65535, 15);
		check(UnreachableCleaner::isTileVisible(Position(65535, 65535, 15), vpMax, settings),
			"max coordinates (65535, 65535, 15) should be visible from itself");
	}

	if (failures != 0) {
		std::cerr << failures << " unreachable cleaner test(s) FAILED.\n";
		return 1;
	}

	std::cout << "All unreachable cleaner tests PASSED.\n";
	return 0;
}
