//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_REPLACE_TOOL_REPLACE_SCOPE_H_
#define RME_REPLACE_TOOL_REPLACE_SCOPE_H_

#include "replace_rule.h"

#include <vector>

class Map;
class Selection;
class Tile;

struct ReplaceViewportBounds {
	bool valid = false;
	int minX = 0;
	int minY = 0;
	int maxX = -1;
	int maxY = -1;
	int floor = 0;

	[[nodiscard]] bool IsValid() const;
	[[nodiscard]] bool Contains(const Tile& tile) const;
};

[[nodiscard]] ReplaceViewportBounds CalculateReplaceViewportBounds(int scrollX, int scrollY, int screenWidth, int screenHeight, double zoom, int floor);
[[nodiscard]] std::vector<Tile*> CollectReplaceScopeTiles(Map& map, Selection& selection, ReplaceScope scope, const ReplaceViewportBounds& viewport);
[[nodiscard]] std::vector<ServerItemId> CollectVisibleServerItemIds(const std::vector<Tile*>& tiles);

#endif
