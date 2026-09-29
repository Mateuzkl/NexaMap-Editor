//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_REPLACE_TOOL_REPLACE_ENGINE_H_
#define RME_REPLACE_TOOL_REPLACE_ENGINE_H_

#include "../doodad_brush.h"
#include "../item.h"
#include "replace_rule.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class Editor;
class Item;
class Tile;

struct ReplaceExecutionOptions {
	bool dryRun = true;
	uint32_t randomSeed = 0;
	bool includeContainerContents = true;
	bool rebuildGroundBorders = false;
	DoodadBrush* doodadReplacementBrush = nullptr;
	int doodadVariation = 0;
	const CompositeTileList* doodadComposite = nullptr;
	PositionVector doodadAllowedPositions;
	bool enforceDoodadSelectionScope = false;
	bool allowDoodadOutsideScope = false;
	std::function<bool(const Tile&, const Item&)> matchFilter;
};

struct ReplaceExecutionResult {
	ReplacementValidationResult validation;
	uint32_t randomSeed = 0;
	size_t tilesScanned = 0;
	size_t itemsScanned = 0;
	size_t matchedItems = 0;
	size_t replacements = 0;
	size_t deletions = 0;
	size_t unchangedByProbability = 0;
	size_t changedTiles = 0;
	size_t doodadPlacements = 0;
	size_t doodadTilesChanged = 0;
	size_t doodadOutsideScopeTiles = 0;
	size_t bordersRebuilt = 0;
	bool committed = false;

	[[nodiscard]] size_t ChangedItems() const {
		return replacements + deletions;
	}
};

class ReplaceEngine {
public:
	[[nodiscard]] static ReplaceExecutionResult Run(Editor& editor, const std::vector<Tile*>& tiles, const std::vector<ReplacementRule>& rules, ReplaceExecutionOptions options = {});
};

// Rebuild an item through the factory so replacing an ID cannot leave a
// Container/Teleport/Door/Depot (or other specialized item) with the wrong
// runtime type. Container contents are retained when possible and otherwise
// returned to the caller for promotion into the owning item vector.
[[nodiscard]] bool ReplaceItemPreservingState(Item*& item, uint16_t targetServerId, ItemVector& promotedContents);

#endif
