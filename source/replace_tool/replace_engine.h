//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_REPLACE_TOOL_REPLACE_ENGINE_H_
#define RME_REPLACE_TOOL_REPLACE_ENGINE_H_

#include "replace_rule.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class Editor;
class Tile;

struct ReplaceExecutionOptions {
	bool dryRun = true;
	uint32_t randomSeed = 0;
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
	bool committed = false;

	[[nodiscard]] size_t ChangedItems() const {
		return replacements + deletions;
	}
};

class ReplaceEngine {
public:
	[[nodiscard]] static ReplaceExecutionResult Run(Editor& editor, const std::vector<Tile*>& tiles, const std::vector<ReplacementRule>& rules, ReplaceExecutionOptions options = {});
};

#endif
