// SPDX-License-Identifier: GPL-3.0-or-later
// AI Style Reference — snapshot capture implementation.
// Reuses BorderLearningScanner, CollectQuickReplaceCandidates and
// Item brush relationships from the existing codebase.

#include "main.h"

#include "reference_style.h"

#include "basemap.h"
#include "border_learning.h"
#include "brush.h"
#include "carpet_brush.h"
#include "creature.h"
#include "doodad_brush.h"
#include "ground_brush.h"
#include "item.h"
#include "item_id_mapping.h"
#include "items.h"
#include "map.h"
#include "materials.h"
#include "raw_brush.h"
#include "selection.h"
#include "spawn.h"
#include "table_brush.h"
#include "tile.h"
#include "tileset.h"
#include "wall_brush.h"
#include "workspace_session.h"
#include "replace_tool/quick_replace_selection_model.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {

	// ─── Helpers ────────────────────────────────────────────────────────

	int DetectMajorityFloor(const Selection& selection) {
		std::unordered_map<int, size_t> floorCounts;
		for (const Tile* tile : selection) {
			if (tile) {
				++floorCounts[tile->getZ()];
			}
		}
		int bestFloor = 7;
		size_t bestCount = 0;
		for (const auto& [floor, count] : floorCounts) {
			if (count > bestCount || (count == bestCount && floor < bestFloor)) {
				bestCount = count;
				bestFloor = floor;
			}
		}
		return bestFloor;
	}

	std::string BrushKindFromItem(const Tile& tile, const Item& item) {
		if (tile.ground == &item) {
			GroundBrush* gb = item.getGroundBrush();
			if (gb) {
				return "ground";
			}
		}
		if (item.getWallBrush()) {
			return "wall";
		}
		if (item.getDoorBrush()) {
			return "door";
		}
		if (item.getCarpetBrush()) {
			return "carpet";
		}
		if (item.getTableBrush()) {
			return "table";
		}
		if (item.getDoodadBrush()) {
			return "doodad";
		}
		if (item.getRAWBrush()) {
			return "raw";
		}
		return "";
	}

	std::string BrushNameFromItem(const Tile& tile, const Item& item) {
		if (tile.ground == &item) {
			GroundBrush* gb = item.getGroundBrush();
			if (gb) {
				return gb->getName();
			}
		}
		if (WallBrush* wb = item.getWallBrush()) {
			return wb->getName();
		}
		if (DoorBrush* db = item.getDoorBrush()) {
			return db->getName();
		}
		if (CarpetBrush* cb = item.getCarpetBrush()) {
			return cb->getName();
		}
		if (TableBrush* tb = item.getTableBrush()) {
			return tb->getName();
		}
		if (Brush* doodad = item.getDoodadBrush()) {
			return doodad->getName();
		}
		if (RAWBrush* raw = item.getRAWBrush()) {
			return raw->getName();
		}
		return "";
	}

	double SafeRatio(size_t numerator, size_t denominator) {
		return denominator > 0 ? static_cast<double>(numerator) / static_cast<double>(denominator) : 0.0;
	}

	double RoundTo(double value, int decimals) {
		const double factor = std::pow(10.0, decimals);
		return std::round(value * factor) / factor;
	}

	bool IsPerimeterCell(int localX, int localY, int width, int height) {
		return localX == 0 || localY == 0 || localX == width - 1 || localY == height - 1;
	}

	char TopologyChar(const ReferenceTileCell& cell) {
		if (!cell.mapped) {
			return 'X';
		}
		if (cell.teleport) {
			return 'T';
		}
		if (cell.door) {
			return 'D';
		}
		if (cell.spawn) {
			return 'S';
		}
		if (cell.creature) {
			return 'C';
		}
		if (cell.wall || cell.blocking) {
			return 'W';
		}
		return '.';
	}

} // namespace

// ─── ReferenceStyleAnalyzer ─────────────────────────────────────────

ReferenceStyleSnapshot ReferenceStyleAnalyzer::Capture(
	const Selection& selection,
	const BaseMap& map,
	const ReferenceStyleCaptureOptions& options
) {
	if (selection.size() == 0) {
		return {};
	}

	// Determine floor.
	const int captureFloor = options.floor >= 0 ? options.floor : DetectMajorityFloor(selection);

	// Collect on-floor tiles (copies pointer list only — we will extract values immediately).
	std::vector<Tile*> floorTiles;
	size_t ignoredOtherFloor = 0;
	floorTiles.reserve(selection.size());
	for (const Tile* tileConst : selection) {
		Tile* tile = const_cast<Tile*>(tileConst);
		if (!tile) {
			continue;
		}
		if (tile->getZ() == captureFloor) {
			floorTiles.push_back(tile);
		} else {
			++ignoredOtherFloor;
		}
	}

	if (floorTiles.empty() || floorTiles.size() > options.maxTiles) {
		return {};
	}

	// Sort by position for deterministic output.
	std::sort(floorTiles.begin(), floorTiles.end(), [](const Tile* a, const Tile* b) {
		return a->getPosition() < b->getPosition();
	});

	// Compute bounds.
	Position minPos = floorTiles.front()->getPosition();
	Position maxPos = floorTiles.front()->getPosition();
	for (const Tile* tile : floorTiles) {
		const Position& p = tile->getPosition();
		if (p.x < minPos.x) {
			minPos.x = p.x;
		}
		if (p.y < minPos.y) {
			minPos.y = p.y;
		}
		if (p.x > maxPos.x) {
			maxPos.x = p.x;
		}
		if (p.y > maxPos.y) {
			maxPos.y = p.y;
		}
	}

	ReferenceStyleSnapshot snapshot;
	snapshot.version = 1;
	snapshot.floor = captureFloor;
	snapshot.sourceMin = minPos;
	snapshot.sourceMax = maxPos;
	snapshot.sourceMask.reserve(floorTiles.size());
	for (const Tile* tile : floorTiles) {
		snapshot.sourceMask.push_back(tile->getPosition());
	}
	snapshot.selectedTileCount = floorTiles.size();
	snapshot.ignoredOtherFloorTiles = ignoredOtherFloor;

	// Populate session/workspace metadata.
	snapshot.workspaceGeneration = g_workspace.getGeneration();
	snapshot.itemIdMode = g_workspace.getEffectiveItemIdMode();
	if (g_workspace.getClient().mode == WorkspaceClientMode::Classic) {
		snapshot.assetMode = ReferenceAssetMode::ClassicDatSpr;
	} else if (g_workspace.getClient().mode == WorkspaceClientMode::Appearances) {
		snapshot.assetMode = ReferenceAssetMode::Appearances;
	} else {
		snapshot.assetMode = ReferenceAssetMode::Unknown;
	}

	const Map* fullMap = dynamic_cast<const Map*>(&map);
	if (fullMap) {
		snapshot.sourceMapSessionId = fullMap->getSessionId();
	}

	const int gridWidth = maxPos.x - minPos.x + 1;
	const int gridHeight = maxPos.y - minPos.y + 1;

	// Build selected position set for topology.
	std::set<Position> selectedPositions;
	for (const Tile* tile : floorTiles) {
		selectedPositions.insert(tile->getPosition());
	}

	// ── 1. Per-cell topology ──────────────────────────────────────
	snapshot.cells.reserve(floorTiles.size());
	size_t mappedCount = 0;
	size_t walkableCount = 0;
	size_t blockingCount = 0;

	// Counters for items by category.
	size_t totalVisibleItems = 0;
	size_t borderItemCount = 0;
	size_t wallItemCount = 0;
	size_t doodadItemCount = 0;
	size_t decorativeItemCount = 0;
	size_t groundCount = 0;

	// Perimeter / interior wall counts.
	size_t perimeterTiles = 0;
	size_t interiorTiles = 0;
	size_t perimeterWalls = 0;
	size_t interiorWalls = 0;

	// Gameplay summary counters.
	ReferenceGameplaySummary& gameplay = snapshot.gameplay;

	for (const Tile* tile : floorTiles) {
		const Position& pos = tile->getPosition();
		ReferenceTileCell cell;
		cell.localX = static_cast<int16_t>(pos.x - minPos.x);
		cell.localY = static_cast<int16_t>(pos.y - minPos.y);
		cell.localZ = 0;

		const bool hasGround = tile->ground != nullptr && tile->ground->getID() != 0;
		const bool hasItems = !tile->items.empty();
		cell.mapped = hasGround || hasItems;
		if (cell.mapped) {
			++mappedCount;
		}

		if (hasGround) {
			++groundCount;
		}

		// Scan items for type flags.
		bool tileHasWall = false;
		bool tileHasDoor = false;
		bool tileHasTeleport = false;
		bool tileBlocking = (hasGround && tile->ground->isBlocking());

		for (const Item* item : tile->items) {
			if (!item || item->isMetaItem()) {
				continue;
			}
			++totalVisibleItems;

			const QuickReplaceCategory cat = ClassifyPlacedItem(*tile, *item);
			switch (cat) {
				case QuickReplaceCategory::Border:
					++borderItemCount;
					break;
				case QuickReplaceCategory::Wall:
					++wallItemCount;
					tileHasWall = true;
					break;
				case QuickReplaceCategory::Door:
					tileHasDoor = true;
					break;
				case QuickReplaceCategory::Doodad:
					++doodadItemCount;
					break;
				case QuickReplaceCategory::Item:
					++decorativeItemCount;
					break;
				default:
					break;
			}

			if (item->isBlocking()) {
				tileBlocking = true;
			}

			const ItemType& type = g_items[item->getID()];
			if (type.isTeleport()) {
				tileHasTeleport = true;
				++gameplay.teleports;
			}
			if (type.isDoor() || type.isBrushDoor) {
				++gameplay.doors;
			}
			if (type.isContainer()) {
				++gameplay.containers;
			}
		}

		// Spawn/creature.
		if (tile->spawn) {
			cell.spawn = true;
			++gameplay.spawns;
		}
		if (tile->creature) {
			cell.creature = true;
			++gameplay.creatures;
		}

		cell.wall = tileHasWall;
		cell.door = tileHasDoor;
		cell.teleport = tileHasTeleport;
		cell.blocking = tileBlocking;
		cell.walkable = !tileBlocking;

		if (cell.walkable) {
			++walkableCount;
		}
		if (cell.blocking) {
			++blockingCount;
		}

		// Perimeter tracking.
		const bool isPerimeter = IsPerimeterCell(cell.localX, cell.localY, gridWidth, gridHeight);
		if (isPerimeter) {
			++perimeterTiles;
			if (tileHasWall) {
				++perimeterWalls;
			}
		} else {
			++interiorTiles;
			if (tileHasWall) {
				++interiorWalls;
			}
		}

		snapshot.cells.push_back(cell);
	}

	snapshot.mappedTileCount = mappedCount;
	totalVisibleItems += groundCount; // ground is also a visible item

	// ── 2. Item usage profile (reuse CollectQuickReplaceCandidates) ─
	{
		const auto candidates = CollectQuickReplaceCandidates(floorTiles);
		snapshot.items.reserve(candidates.size());

		for (const auto& candidate : candidates) {
			ReferenceItemUsage usage;
			usage.activeId = candidate.mapItemId;
			usage.name = candidate.name;
			usage.category = QuickReplaceCategoryName(candidate.category);
			usage.count = candidate.count;
			usage.ratio = RoundTo(SafeRatio(candidate.count, totalVisibleItems), 4);

			const ItemType& type = g_items.getItemType(candidate.mapItemId);
			if (snapshot.itemIdMode == ItemIdMode::ServerId) {
				usage.serverId = candidate.mapItemId;
				if (type.clientID != 0) {
					usage.clientId = type.clientID;
				} else {
					const auto mapped = ItemIdMapping::serverToClient(candidate.mapItemId);
					if (mapped.found) {
						usage.clientId = mapped.converted;
					}
				}
			} else if (snapshot.itemIdMode == ItemIdMode::ClientId) {
				usage.clientId = candidate.mapItemId;
				const auto mapped = ItemIdMapping::clientToServer(candidate.mapItemId);
				if (mapped.found && !mapped.ambiguous) {
					usage.serverId = mapped.converted;
				}
			} else {
				usage.serverId = candidate.mapItemId;
				usage.clientId = type.clientID;
			}

			// Look up brush relationship on a representative tile.
			for (const Tile* tile : floorTiles) {
				if (tile->ground && tile->ground->getID() == candidate.mapItemId) {
					usage.brush = BrushNameFromItem(*tile, *tile->ground);
					usage.brushKind = BrushKindFromItem(*tile, *tile->ground);
					break;
				}
				bool found = false;
				for (const Item* item : tile->items) {
					if (item && item->getID() == candidate.mapItemId) {
						usage.brush = BrushNameFromItem(*tile, *item);
						usage.brushKind = BrushKindFromItem(*tile, *item);
						found = true;
						break;
					}
				}
				if (found) {
					break;
				}
			}

			usage.sourceVerified = true;
			snapshot.items.push_back(std::move(usage));
		}
	}

	// ── 3. Brush aggregation ──────────────────────────────────────
	{
		struct BrushAccum {
			std::string kind;
			size_t count = 0;
			uint16_t lookId = 0;
			bool needBorders = false;
			bool hasVariations = false;
			std::unordered_set<std::string> tilesets;
		};
		std::unordered_map<std::string, BrushAccum> brushMap;

		for (const Tile* tile : floorTiles) {
			const auto addBrush = [&](const Tile& t, const Item& item) {
				const std::string brushName = BrushNameFromItem(t, item);
				if (brushName.empty()) {
					return;
				}
				auto& accum = brushMap[brushName];
				if (accum.kind.empty()) {
					accum.kind = BrushKindFromItem(t, item);
					Brush* brush = item.getBrush();
					if (!brush) {
						brush = item.getDoodadBrush();
					}
					if (!brush) {
						brush = item.getRAWBrush();
					}
					if (brush) {
						accum.lookId = brush->getLookID();
						if (GroundBrush* gb = dynamic_cast<GroundBrush*>(brush)) {
							accum.needBorders = gb->hasOuterBorder() || gb->hasInnerBorder() || gb->hasOptionalBorder();
							accum.hasVariations = true;
						}
						// Collect tilesets that contain this brush.
						for (const auto& [tsName, ts] : g_materials.tilesets) {
							if (ts && ts->containsBrush(brush)) {
								accum.tilesets.insert(tsName);
							}
						}
					}
				}
				++accum.count;
			};

			if (tile->ground) {
				addBrush(*tile, *tile->ground);
			}
			for (const Item* item : tile->items) {
				if (item && !item->isMetaItem()) {
					addBrush(*tile, *item);
				}
			}
		}

		snapshot.brushes.reserve(brushMap.size());
		for (auto& [name, accum] : brushMap) {
			ReferenceBrushUsage bu;
			bu.name = name;
			bu.kind = accum.kind;
			bu.count = accum.count;
			bu.ratio = RoundTo(SafeRatio(accum.count, totalVisibleItems), 4);
			bu.lookId = accum.lookId;
			bu.needBorders = accum.needBorders;
			bu.hasVariations = accum.hasVariations;
			for (const auto& ts : accum.tilesets) {
				bu.tilesets.push_back(ts);
			}
			snapshot.brushes.push_back(std::move(bu));
		}
		// Sort by count descending.
		std::sort(snapshot.brushes.begin(), snapshot.brushes.end(), [](const ReferenceBrushUsage& a, const ReferenceBrushUsage& b) {
			return a.count > b.count;
		});
	}

	// ── 4. Ground family profile (reuse BorderLearningScanner) ────
	{
		BorderLearningSnapshot borderSnapshot = BorderLearningScanner::capture(selection, map, captureFloor);

		snapshot.groundFamilies.reserve(borderSnapshot.groundFamilies.size());
		for (size_t fi = 0; fi < borderSnapshot.groundFamilies.size(); ++fi) {
			const auto& family = borderSnapshot.groundFamilies[fi];
			ReferenceGroundFamily gf;
			gf.name = family.name;
			gf.knownBrush = family.knownBrush;
			gf.brushId = family.brushId;
			gf.representativeItemId = family.representativeItemId;
			gf.itemIds = family.itemIds;

			// Count tiles with this family.
			size_t tileCount = 0;
			for (const auto& blTile : borderSnapshot.tiles) {
				if (blTile.groundFamily == static_cast<BorderGroundFamilyIndex>(fi)) {
					++tileCount;
				}
			}
			gf.tiles = tileCount;
			gf.ratio = RoundTo(SafeRatio(tileCount, borderSnapshot.selectedTileCount), 4);

			if (gf.knownBrush) {
				gf.brush = gf.name;
			}

			snapshot.groundFamilies.push_back(std::move(gf));
		}
		// Sort by tile count descending.
		std::sort(snapshot.groundFamilies.begin(), snapshot.groundFamilies.end(), [](const ReferenceGroundFamily& a, const ReferenceGroundFamily& b) {
			return a.tiles > b.tiles;
		});

		// Detect transitions.
		const auto blTransitions = BorderLearningAnalyzer::detectTransitions(borderSnapshot);
		snapshot.transitions.reserve(blTransitions.size());
		for (const auto& tr : blTransitions) {
			ReferenceGroundTransition gt;
			if (tr.familyA < borderSnapshot.groundFamilies.size()) {
				gt.groundA = borderSnapshot.groundFamilies[tr.familyA].name;
			}
			if (tr.familyB < borderSnapshot.groundFamilies.size()) {
				gt.groundB = borderSnapshot.groundFamilies[tr.familyB].name;
			}
			gt.contacts = tr.contacts;

			// Collect observed border items at boundary tiles.
			for (const auto& blTile : borderSnapshot.tiles) {
				bool atBoundary = false;
				for (size_t ni = 0; ni < 8; ++ni) {
					const BorderGroundFamilyIndex nf = blTile.neighbourFamilies[ni];
					if ((blTile.groundFamily == tr.familyA && nf == tr.familyB) || (blTile.groundFamily == tr.familyB && nf == tr.familyA)) {
						atBoundary = true;
						break;
					}
				}
				if (!atBoundary) {
					continue;
				}
				for (const auto& bli : blTile.items) {
					if (bli.knownBorder) {
						if (std::find(gt.borderItemIds.begin(), gt.borderItemIds.end(), bli.itemId) == gt.borderItemIds.end()) {
							gt.borderItemIds.push_back(bli.itemId);
						}
						if (bli.knownAlignment != BORDER_NONE) {
							std::string alignStr = std::to_string(static_cast<int>(bli.knownAlignment));
							if (std::find(gt.borderAlignments.begin(), gt.borderAlignments.end(), alignStr) == gt.borderAlignments.end()) {
								gt.borderAlignments.push_back(alignStr);
							}
						}
					}
				}
			}

			snapshot.transitions.push_back(std::move(gt));
		}
	}

	// ── 5. Layout metrics ─────────────────────────────────────────
	{
		ReferenceLayoutMetrics& lm = snapshot.layout;
		lm.width = static_cast<size_t>(gridWidth);
		lm.height = static_cast<size_t>(gridHeight);
		lm.selectedTileCount = floorTiles.size();
		lm.mappedRatio = RoundTo(SafeRatio(mappedCount, floorTiles.size()), 4);
		lm.walkableRatio = RoundTo(SafeRatio(walkableCount, floorTiles.size()), 4);
		lm.blockingRatio = RoundTo(SafeRatio(blockingCount, floorTiles.size()), 4);

		lm.groundCoverage = RoundTo(SafeRatio(groundCount, floorTiles.size()), 4);
		lm.borderItemsPerTile = RoundTo(SafeRatio(borderItemCount, floorTiles.size()), 4);
		lm.wallsPerTile = RoundTo(SafeRatio(wallItemCount, floorTiles.size()), 4);
		lm.doodadsPerTile = RoundTo(SafeRatio(doodadItemCount, floorTiles.size()), 4);
		lm.decorativeItemsPerTile = RoundTo(SafeRatio(decorativeItemCount, floorTiles.size()), 4);
		lm.totalVisibleItemsPerTile = RoundTo(SafeRatio(totalVisibleItems, floorTiles.size()), 4);

		lm.perimeterWallRatio = RoundTo(SafeRatio(perimeterWalls, perimeterTiles), 4);
		lm.interiorWallRatio = RoundTo(SafeRatio(interiorWalls, interiorTiles), 4);

		// Unique counts.
		std::unordered_set<std::string> uniqueWallBrushes;
		std::unordered_set<std::string> uniqueDoodadBrushes;
		for (const auto& bu : snapshot.brushes) {
			if (bu.kind == "wall") {
				uniqueWallBrushes.insert(bu.name);
			}
			if (bu.kind == "doodad") {
				uniqueDoodadBrushes.insert(bu.name);
			}
		}
		lm.uniqueGroundCount = snapshot.groundFamilies.size();
		lm.uniqueWallBrushCount = uniqueWallBrushes.size();
		lm.uniqueDoodadBrushCount = uniqueDoodadBrushes.size();
		lm.uniqueVisibleItemCount = snapshot.items.size();
	}

	// ── 6. ASCII topology grid ────────────────────────────────────
	{
		std::vector<std::vector<char>> grid(gridHeight, std::vector<char>(gridWidth, 'X'));

		for (const auto& cell : snapshot.cells) {
			if (cell.localX >= 0 && cell.localX < gridWidth && cell.localY >= 0 && cell.localY < gridHeight) {
				grid[cell.localY][cell.localX] = TopologyChar(cell);
			}
		}

		std::ostringstream oss;
		for (int y = 0; y < gridHeight; ++y) {
			for (int x = 0; x < gridWidth; ++x) {
				oss << grid[y][x];
			}
			if (y + 1 < gridHeight) {
				oss << '\n';
			}
		}
		snapshot.topologyGrid = oss.str();
	}

	return snapshot;
}

TargetAreaSnapshot CaptureTargetAreaSnapshot(
	const Selection& selection,
	int floor,
	SessionId mapSessionId,
	uint64_t workspaceGeneration,
	size_t maxTiles
) {
	TargetAreaSnapshot snapshot;
	snapshot.mapSessionId = mapSessionId;
	snapshot.workspaceGeneration = workspaceGeneration;
	snapshot.floor = floor;

	for (const Tile* tile : selection) {
		if (tile && tile->getZ() == floor) {
			snapshot.positions.push_back(tile->getPosition());
		}
	}

	if (snapshot.positions.empty() || snapshot.positions.size() > maxTiles) {
		snapshot.positions.clear();
		return snapshot;
	}

	std::sort(snapshot.positions.begin(), snapshot.positions.end());
	snapshot.positions.erase(std::unique(snapshot.positions.begin(), snapshot.positions.end()), snapshot.positions.end());
	if (snapshot.positions.empty()) {
		return snapshot;
	}

	snapshot.targetMin = snapshot.positions.front();
	snapshot.targetMax = snapshot.positions.front();
	for (const Position& position : snapshot.positions) {
		snapshot.targetMin.x = std::min(snapshot.targetMin.x, position.x);
		snapshot.targetMin.y = std::min(snapshot.targetMin.y, position.y);
		snapshot.targetMax.x = std::max(snapshot.targetMax.x, position.x);
		snapshot.targetMax.y = std::max(snapshot.targetMax.y, position.y);
	}

	return snapshot;
}
