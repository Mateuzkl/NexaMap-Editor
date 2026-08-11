//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "../main.h"
#include "replace_scope.h"

#include "../map.h"
#include "../selection.h"
#include "../tile.h"

#include <algorithm>
#include <set>

bool ReplaceViewportBounds::IsValid() const {
	return valid && minX <= maxX && minY <= maxY && floor >= 0 && floor <= MAP_MAX_LAYER;
}

bool ReplaceViewportBounds::Contains(const Tile& tile) const {
	const Position position = tile.getPosition();
	return IsValid() && position.z == floor && position.x >= minX && position.x <= maxX && position.y >= minY && position.y <= maxY;
}

ReplaceViewportBounds CalculateReplaceViewportBounds(int scrollX, int scrollY, int screenWidth, int screenHeight, double zoom, int floor) {
	if (screenWidth <= 0 || screenHeight <= 0 || zoom <= 0.0 || floor < 0 || floor > MAP_MAX_LAYER) {
		return {};
	}

	const int renderedTileSize = std::max(1, static_cast<int>(TileSize / zoom));
	int minX = scrollX / TileSize;
	int minY = scrollY / TileSize;
	if (floor > GROUND_LAYER) {
		minX -= 2;
		minY -= 2;
	}

	return {
		true,
		minX,
		minY,
		minX + screenWidth / renderedTileSize + 2,
		minY + screenHeight / renderedTileSize + 2,
		floor,
	};
}

std::vector<Tile*> CollectReplaceScopeTiles(Map& map, Selection& selection, ReplaceScope scope, const ReplaceViewportBounds& viewport) {
	std::vector<Tile*> tiles;
	if (scope == ReplaceScope::Selection) {
		tiles.reserve(selection.size());
		for (Tile* tile : selection.getTiles()) {
			if (tile) {
				tiles.push_back(tile);
			}
		}
		return tiles;
	}

	tiles.reserve(scope == ReplaceScope::AllMap ? static_cast<size_t>(map.getTileCount()) : 256);
	for (MapIterator iterator = map.begin(); iterator != map.end(); ++iterator) {
		Tile* tile = iterator->get();
		if (tile && (scope == ReplaceScope::AllMap || viewport.Contains(*tile))) {
			tiles.push_back(tile);
		}
	}
	return tiles;
}

std::vector<ServerItemId> CollectVisibleServerItemIds(const std::vector<Tile*>& tiles) {
	std::set<uint16_t> uniqueIds;
	for (const Tile* tile : tiles) {
		if (!tile) {
			continue;
		}
		if (tile->ground && !tile->ground->isMetaItem()) {
			uniqueIds.insert(tile->ground->getID());
		}
		for (const Item* item : tile->items) {
			if (item && !item->isMetaItem()) {
				uniqueIds.insert(item->getID());
			}
		}
	}

	std::vector<ServerItemId> result;
	result.reserve(uniqueIds.size());
	for (uint16_t id : uniqueIds) {
		if (id != 0) {
			result.emplace_back(id);
		}
	}
	return result;
}
