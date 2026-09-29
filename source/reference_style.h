// SPDX-License-Identifier: GPL-3.0-or-later
// AI Style Reference — immutable value-based style snapshot.
// Captured from a user-selected map area so that an external AI client
// can reproduce the visual language in a different target region.

#ifndef NEXAMAP_REFERENCE_STYLE_H_
#define NEXAMAP_REFERENCE_STYLE_H_

#include "border_learning.h"
#include "position.h"
#include "session_id.h"
#include "server_workspace.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Forward declarations — no pointers stored.
class BaseMap;
class Selection;

// ─── Sub-structures (all value types) ───────────────────────────────

enum class ReferenceAssetMode : uint8_t {
	ClassicDatSpr = 0,
	Appearances,
	Unknown,
};

/// A single cell in the normalized reference grid.
struct ReferenceTileCell {
	int16_t localX = 0;
	int16_t localY = 0;
	int16_t localZ = 0;
	bool mapped = false; ///< Has ground or any visible item.
	bool walkable = true;
	bool blocking = false;
	bool wall = false;
	bool door = false;
	bool teleport = false;
	bool spawn = false;
	bool creature = false;
};

/// Observed usage of a single item ID in the reference.
struct ReferenceItemUsage {
	uint16_t activeId = 0; ///< The ID as it appears in the map (server or client).
	uint16_t serverId = 0;
	uint16_t clientId = 0;
	std::string name;
	std::string category; ///< ground, border, wall, door, carpet, table, doodad, item
	size_t count = 0;
	double ratio = 0.0; ///< count / total visible items
	std::string brush; ///< Owning brush name (empty if unmapped).
	std::string brushKind; ///< ground, wall, door, carpet, table, doodad, raw
	bool sourceVerified = true; ///< This ID was directly observed in active resources.
};

/// Aggregated brush usage.
struct ReferenceBrushUsage {
	std::string name;
	std::string kind; ///< ground, wall, door, carpet, table, doodad, raw
	size_t count = 0;
	double ratio = 0.0;
	uint16_t lookId = 0;
	bool needBorders = false;
	bool hasVariations = false;
	std::vector<std::string> tilesets;
};

/// Ground family summary (reused from BorderLearning data).
struct ReferenceGroundFamily {
	std::string name;
	std::string brush;
	bool knownBrush = false;
	uint32_t brushId = 0;
	uint16_t representativeItemId = 0;
	std::vector<uint16_t> itemIds;
	size_t tiles = 0;
	double ratio = 0.0;
};

/// Transition between ground families.
struct ReferenceGroundTransition {
	std::string groundA;
	std::string groundB;
	size_t contacts = 0;
	std::vector<uint16_t> borderItemIds;
	std::vector<std::string> borderAlignments;
};

/// Layout metrics — deterministic spatial statistics.
struct ReferenceLayoutMetrics {
	size_t width = 0;
	size_t height = 0;
	size_t selectedTileCount = 0;
	double mappedRatio = 0.0;
	double walkableRatio = 0.0;
	double blockingRatio = 0.0;

	double groundCoverage = 0.0;
	double borderItemsPerTile = 0.0;
	double wallsPerTile = 0.0;
	double doodadsPerTile = 0.0;
	double decorativeItemsPerTile = 0.0;
	double totalVisibleItemsPerTile = 0.0;

	double perimeterWallRatio = 0.0;
	double interiorWallRatio = 0.0;

	size_t uniqueGroundCount = 0;
	size_t uniqueWallBrushCount = 0;
	size_t uniqueDoodadBrushCount = 0;
	size_t uniqueVisibleItemCount = 0;
};

/// Protected gameplay summary — metadata only, never cloned.
struct ReferenceGameplaySummary {
	size_t teleports = 0;
	size_t doors = 0;
	size_t houses = 0;
	size_t spawns = 0;
	size_t creatures = 0;
	size_t waypoints = 0;
	size_t zones = 0;
	size_t containers = 0;
};

// ─── Main snapshot ──────────────────────────────────────────────────

/// Immutable value snapshot of the visual style of a selected map area.
/// Contains ONLY values: IDs, strings, counts, positions, enums.
/// Safe to retain after selection change, map edit, undo, session switch.
struct ReferenceStyleSnapshot {
	uint32_t version = 1;

	/// Source identification — used for compatibility checks.
	SessionId sourceMapSessionId = InvalidSessionId;
	uint64_t workspaceGeneration = 0;

	ItemIdMode itemIdMode = ItemIdMode::Unknown;
	ReferenceAssetMode assetMode = ReferenceAssetMode::Unknown;

	int floor = 0;

	Position sourceMin;
	Position sourceMax;

	/// Exact source tiles captured as visual reference. This is retained
	/// independently of the normalized cells so the source can be protected
	/// from generation writes after the editor selection has changed.
	std::vector<Position> sourceMask;

	size_t selectedTileCount = 0;
	size_t mappedTileCount = 0;
	size_t ignoredOtherFloorTiles = 0;

	/// Per-cell topology (normalized local coordinates).
	std::vector<ReferenceTileCell> cells;

	/// Item distribution profile.
	std::vector<ReferenceItemUsage> items;

	/// Brush aggregation.
	std::vector<ReferenceBrushUsage> brushes;

	/// Ground family profile (from border learning).
	std::vector<ReferenceGroundFamily> groundFamilies;

	/// Transitions between ground families.
	std::vector<ReferenceGroundTransition> transitions;

	/// Deterministic layout/spatial metrics.
	ReferenceLayoutMetrics layout;

	/// Protected gameplay summary (read-only evidence).
	ReferenceGameplaySummary gameplay;

	/// ASCII topology grid (short, human/AI readable).
	std::string topologyGrid;
};

/// Explicit writable destination for a reference-driven generation request.
/// It deliberately has no relationship to the current editor selection after
/// capture; callers must capture it again whenever the intended target changes.
struct TargetAreaSnapshot {
	SessionId mapSessionId = InvalidSessionId;
	uint64_t workspaceGeneration = 0;
	int floor = 0;
	Position targetMin;
	Position targetMax;
	std::vector<Position> positions;
	bool allowReferenceSourceOverwrite = false;
};

// ─── Capture options ────────────────────────────────────────────────

struct ReferenceStyleCaptureOptions {
	/// Which floor to capture. -1 means auto-detect from selection majority.
	int floor = -1;

	/// Maximum tiles to accept (hard cap).
	size_t maxTiles = 65536;
};

/// Captures the exact currently selected positions as a generation target.
/// This is intentionally separate from ReferenceStyleAnalyzer::Capture.
[[nodiscard]] TargetAreaSnapshot CaptureTargetAreaSnapshot(
	const Selection& selection,
	int floor,
	SessionId mapSessionId,
	uint64_t workspaceGeneration,
	size_t maxTiles = 65536
);

[[nodiscard]] bool ReferenceSourceOverlapsTarget(
	const ReferenceStyleSnapshot& reference,
	const TargetAreaSnapshot& target
);

/// Returns a user-facing rejection reason, or std::nullopt when every write
/// position is inside the current target and the reference source is protected.
[[nodiscard]] std::optional<std::string> ValidateReferenceTargetWrite(
	const ReferenceStyleSnapshot* reference,
	const TargetAreaSnapshot* target,
	SessionId activeMapSessionId,
	uint64_t activeWorkspaceGeneration,
	const std::vector<Position>& writePositions,
	bool allowReferenceSourceOverwrite = false
);

// ─── Analyzer (stateless) ───────────────────────────────────────────

/// Captures an immutable value-based style snapshot from the current
/// selection + map state. Must be called on the GUI thread while
/// selection and map pointers are valid.
class ReferenceStyleAnalyzer final {
public:
	[[nodiscard]] static ReferenceStyleSnapshot Capture(
		const Selection& selection,
		const BaseMap& map,
		const ReferenceStyleCaptureOptions& options
	);
};

#endif // NEXAMAP_REFERENCE_STYLE_H_
