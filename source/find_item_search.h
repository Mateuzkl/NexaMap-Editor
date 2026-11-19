//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor / NexaMap
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_FIND_ITEM_SEARCH_H_
#define NEXAMAP_FIND_ITEM_SEARCH_H_

#include "position.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class ItemDatabase;
class ItemType;
class Map;

enum class ItemIdSearchKind {
	ServerId = 0,
	ClientId = 1,
	SpriteId = 2,
};

enum class ItemLocationKind {
	Ground = 0,
	TileItem = 1,
	ContainerItem = 2,
};

struct ParsedItemId {
	bool valid = false;
	bool empty = false;
	bool overflow = false;
	uint32_t value = 0;
	std::string error;
};

struct MapItemSearchResult {
	Position position;
	uint32_t serverId = 0;
	uint32_t clientId = 0;
	std::string itemName;
	ItemLocationKind location = ItemLocationKind::TileItem;
	uint32_t containerDepth = 0;
};

struct MapItemSearchSummary {
	std::vector<MapItemSearchResult> matches;
	uint64_t totalOccurrences = 0;
	uint64_t matchedTiles = 0;
	bool limitReached = false;
};

ParsedItemId ParseUint32ItemId(std::string_view text);
bool ItemHasSpriteId(const ItemType& itemType, uint32_t spriteId);
const char* ItemIdSearchKindLabel(ItemIdSearchKind kind);
const char* ItemLocationKindLabel(ItemLocationKind location);
std::string FormatSearchResultDescription(const MapItemSearchResult& match, ItemIdSearchKind kind, uint32_t searchedId);

MapItemSearchSummary SearchItemsOnMap(
	Map& map,
	const ItemDatabase& itemDb,
	ItemIdSearchKind kind,
	uint32_t targetId,
	bool selectedOnly,
	uint32_t maxResults,
	const std::function<void(uint64_t done, uint64_t total)>& progress = {}
);

#endif // NEXAMAP_FIND_ITEM_SEARCH_H_
