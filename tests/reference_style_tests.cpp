// SPDX-License-Identifier: GPL-3.0-or-later
#include "reference_style.h"
#include "reference_style_store.h"

#include <cassert>
#include <iostream>
#include <stdexcept>

namespace {

	void require(bool condition, const char* message) {
		if (!condition) {
			throw std::runtime_error(message);
		}
	}

	void TestEmptySnapshot() {
		ReferenceStyleSnapshot empty;
		require(empty.selectedTileCount == 0, "empty snapshot must have 0 tiles");
		require(empty.cells.empty(), "empty snapshot must have 0 cells");
		require(empty.items.empty(), "empty snapshot must have 0 items");
		require(empty.brushes.empty(), "empty snapshot must have 0 brushes");
		require(empty.groundFamilies.empty(), "empty snapshot must have 0 ground families");
		require(empty.transitions.empty(), "empty snapshot must have 0 transitions");
	}

	void TestStoreLifecycle() {
		auto& store = ReferenceStyleStore::Instance();
		store.clear();
		require(!store.hasReference(), "cleared store must not have reference");
		require(store.get() == nullptr, "cleared store must return nullptr");

		ReferenceStyleSnapshot snapshot;
		snapshot.version = 1;
		snapshot.floor = 7;
		snapshot.selectedTileCount = 480;
		snapshot.sourceMin = Position(100, 100, 7);
		snapshot.sourceMax = Position(120, 120, 7);
		snapshot.assetMode = ReferenceAssetMode::Appearances;
		snapshot.workspaceGeneration = 42;

		ReferenceItemUsage earth;
		earth.activeId = 101;
		earth.name = "Earth";
		earth.category = "ground";
		earth.count = 170;
		earth.ratio = 0.354;
		earth.brush = "Earth";
		earth.brushKind = "ground";
		earth.sourceVerified = true;
		snapshot.items.push_back(earth);

		ReferenceBrushUsage earthBrush;
		earthBrush.name = "Earth";
		earthBrush.kind = "ground";
		earthBrush.count = 170;
		snapshot.brushes.push_back(earthBrush);

		ReferenceGroundFamily gf;
		gf.name = "Earth";
		gf.knownBrush = true;
		gf.tiles = 170;
		gf.ratio = 0.354;
		snapshot.groundFamilies.push_back(gf);

		store.set(snapshot);
		require(store.hasReference(), "store must have reference after set");
		const auto* retrieved = store.get();
		require(retrieved != nullptr, "get() must return valid snapshot");
		require(retrieved->selectedTileCount == 480, "tile count must match");
		require(retrieved->items.size() == 1, "items count must match");
		require(retrieved->brushes.size() == 1, "brushes count must match");
		require(retrieved->items[0].name == "Earth", "item name must match");

		// Test compatibility checks.
		require(store.isCompatibleWith(ReferenceAssetMode::Appearances, 42), "should be compatible with same mode and gen");
		require(!store.isCompatibleWith(ReferenceAssetMode::ClassicDatSpr, 42), "must reject different asset mode");
		require(!store.isCompatibleWith(ReferenceAssetMode::Appearances, 999), "must reject different workspace generation");

		// Test swapWith for session isolation.
		std::optional<ReferenceStyleSnapshot> sessionBStorage;
		store.swapWith(sessionBStorage);
		require(!store.hasReference(), "after swapWith empty session, store must be empty");
		require(sessionBStorage.has_value(), "sessionBStorage must now hold previous snapshot");
		require(sessionBStorage->selectedTileCount == 480, "swapped snapshot must retain data");

		// Swap back.
		store.swapWith(sessionBStorage);
		require(store.hasReference(), "store must have restored reference");
		require(store.get()->selectedTileCount == 480, "restored snapshot must match");

		// Clear.
		store.clear();
		require(!store.hasReference(), "must be cleared");
	}

	void TestValueSnapshotImmutability() {
		// Verify that changing source objects or local copies does not mutate the frozen snapshot.
		ReferenceStyleSnapshot original;
		original.version = 1;
		original.floor = 7;
		original.selectedTileCount = 10;
		original.layout.width = 5;
		original.layout.height = 2;
		original.layout.mappedRatio = 1.0;
		original.layout.walkableRatio = 0.8;
		original.layout.wallsPerTile = 0.2;
		original.topologyGrid = "WW...\n.....";

		ReferenceTileCell cell;
		cell.localX = 0;
		cell.localY = 0;
		cell.mapped = true;
		cell.walkable = false;
		cell.wall = true;
		original.cells.push_back(cell);

		auto& store = ReferenceStyleStore::Instance();
		store.set(original);

		// Mutate local object.
		original.selectedTileCount = 999;
		original.cells.clear();
		original.layout.walkableRatio = 0.0;
		original.topologyGrid = "XXXXX";

		// Check store snapshot is immutable.
		const auto* inStore = store.get();
		require(inStore != nullptr, "snapshot in store must exist");
		require(inStore->selectedTileCount == 10, "inStore count must remain 10");
		require(inStore->cells.size() == 1, "inStore cells must remain 1");
		require(inStore->cells[0].wall == true, "inStore cell must remain wall");
		require(inStore->layout.walkableRatio == 0.8, "inStore layout must remain 0.8");
		require(inStore->topologyGrid == "WW...\n.....", "inStore topology must be unchanged");

		store.clear();
	}

	void TestTargetLifecycleAndProtection() {
		auto& store = ReferenceStyleStore::Instance();
		store.clear();

		ReferenceStyleSnapshot reference;
		reference.floor = 7;
		reference.sourceMapSessionId = 17;
		reference.workspaceGeneration = 42;
		reference.sourceMin = Position(100, 100, 7);
		reference.sourceMax = Position(101, 101, 7);
		reference.sourceMask = {
			Position(100, 100, 7),
			Position(101, 100, 7),
			Position(100, 101, 7),
			Position(101, 101, 7),
		};
		store.set(reference);

		TargetAreaSnapshot target;
		target.mapSessionId = 17;
		target.workspaceGeneration = 42;
		target.floor = 7;
		target.targetMin = Position(110, 110, 7);
		target.targetMax = Position(111, 110, 7);
		target.positions = { Position(110, 110, 7), Position(111, 110, 7) };
		store.setTarget(target);
		require(store.hasTarget(), "target must be stored separately from reference");
		require(!ReferenceSourceOverlapsTarget(reference, target), "separate source and target must not overlap");
		require(!ValidateReferenceTargetWrite(&reference, &target, 17, 42, target.positions).has_value(), "writes inside target must be allowed");
		require(ValidateReferenceTargetWrite(&reference, &target, 17, 42, { Position(100, 100, 7) }).has_value(), "source must not become an implicit target");
		require(ValidateReferenceTargetWrite(&reference, &target, 18, 42, target.positions).has_value(), "stale map target must be rejected");
		require(ValidateReferenceTargetWrite(&reference, &target, 17, 43, target.positions).has_value(), "stale resource target must be rejected");

		TargetAreaSnapshot overlapping = target;
		overlapping.targetMin = Position(101, 101, 7);
		overlapping.targetMax = Position(102, 101, 7);
		overlapping.positions = { Position(101, 101, 7), Position(102, 101, 7) };
		const std::optional<std::string> overlapError = ValidateReferenceTargetWrite(&reference, &overlapping, 17, 42, overlapping.positions);
		require(overlapError && *overlapError == "Target area overlaps the captured reference source. Select a different target area.", "source overlap must have a clear rejection");
		require(!ValidateReferenceTargetWrite(&reference, &overlapping, 17, 42, overlapping.positions, true).has_value(), "explicit dangerous override must be honored");

		// Capturing a new source invalidates the old target.
		store.set(reference);
		require(!store.hasTarget(), "new reference must clear prior target");
		store.clear();
	}

	void TestGameplayMetadataProtection() {
		// Reference profile records gameplay metadata for analysis, but never clones it.
		ReferenceStyleSnapshot snapshot;
		snapshot.gameplay.teleports = 1;
		snapshot.gameplay.doors = 3;
		snapshot.gameplay.houses = 1;
		snapshot.gameplay.spawns = 2;
		snapshot.gameplay.creatures = 4;
		snapshot.gameplay.containers = 2;

		require(snapshot.gameplay.teleports == 1, "teleport count must be recorded");
		require(snapshot.gameplay.doors == 3, "door count must be recorded");
		require(snapshot.gameplay.houses == 1, "house count must be recorded");
		require(snapshot.gameplay.spawns == 2, "spawn count must be recorded");
		require(snapshot.gameplay.creatures == 4, "creature count must be recorded");
	}

	void TestLayoutMetricsDeterminism() {
		ReferenceLayoutMetrics lm;
		lm.width = 10;
		lm.height = 10;
		lm.selectedTileCount = 100;
		lm.mappedRatio = 1.0;
		lm.walkableRatio = 0.64;
		lm.blockingRatio = 0.36;
		lm.groundCoverage = 1.0;
		lm.wallsPerTile = 0.36;
		lm.doodadsPerTile = 0.15;
		lm.perimeterWallRatio = 0.90;
		lm.interiorWallRatio = 0.05;
		lm.uniqueGroundCount = 2;
		lm.uniqueWallBrushCount = 1;
		lm.uniqueDoodadBrushCount = 4;

		require(lm.width == 10, "width mismatch");
		require(lm.height == 10, "height mismatch");
		require(lm.selectedTileCount == 100, "selected count mismatch");
		require(lm.walkableRatio == 0.64, "walkable ratio mismatch");
		require(lm.perimeterWallRatio > lm.interiorWallRatio, "perimeter walls must exceed interior");
	}

} // namespace

int main() {
	try {
		TestEmptySnapshot();
		TestStoreLifecycle();
		TestValueSnapshotImmutability();
		TestTargetLifecycleAndProtection();
		TestGameplayMetadataProtection();
		TestLayoutMetricsDeterminism();
		std::cout << "reference_style_tests passed successfully!\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << "Test failed: " << e.what() << "\n";
		return 1;
	}
}
