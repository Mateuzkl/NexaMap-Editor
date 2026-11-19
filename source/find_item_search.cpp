//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor / NexaMap
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "find_item_search.h"
#include "complexitem.h"
#include "graphics.h"
#include "item.h"
#include "items.h"
#include "map.h"
#include "tile.h"

#include <cctype>
#include <limits>
#include <queue>
#include <unordered_set>

ParsedItemId ParseUint32ItemId(std::string_view text) {
	ParsedItemId result;

	size_t start = 0;
	while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
		++start;
	}
	size_t end = text.size();
	while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
		--end;
	}

	if (start == end) {
		result.empty = true;
		result.error = "Enter an ID between 0 and 4294967295.";
		return result;
	}

	const std::string_view trimmed = text.substr(start, end - start);
	if (trimmed.front() == '-') {
		result.error = "ID cannot be negative (valid range: 0 to 4294967295).";
		return result;
	}

	uint64_t acc = 0;
	for (const char ch : trimmed) {
		if (ch < '0' || ch > '9') {
			result.error = "ID must contain only decimal digits (0-9).";
			return result;
		}
		acc = acc * 10u + static_cast<uint64_t>(ch - '0');
		if (acc > std::numeric_limits<uint32_t>::max()) {
			result.overflow = true;
			result.error = "ID exceeds 32-bit maximum (4294967295).";
			return result;
		}
	}

	result.valid = true;
	result.value = static_cast<uint32_t>(acc);
	return result;
}

bool ItemHasSpriteId(const ItemType& itemType, uint32_t spriteId) {
	if (!itemType.sprite || spriteId == 0) {
		return false;
	}
	return itemType.sprite->hasSpriteId(spriteId);
}

const char* ItemIdSearchKindLabel(ItemIdSearchKind kind) {
	switch (kind) {
		case ItemIdSearchKind::ServerId:
			return "Server ID";
		case ItemIdSearchKind::ClientId:
			return "Client ID";
		case ItemIdSearchKind::SpriteId:
			return "Sprite ID";
	}
	return "ID";
}

const char* ItemLocationKindLabel(ItemLocationKind location) {
	switch (location) {
		case ItemLocationKind::Ground:
			return "Ground";
		case ItemLocationKind::TileItem:
			return "Tile";
		case ItemLocationKind::ContainerItem:
			return "Container";
	}
	return "Tile";
}

std::string FormatSearchResultDescription(const MapItemSearchResult& match, ItemIdSearchKind kind, uint32_t searchedId) {
	std::string desc = "[ID " + std::to_string(match.serverId);
	if (match.clientId != 0 && match.clientId != match.serverId) {
		desc += " | Client " + std::to_string(match.clientId);
	}
	if (kind == ItemIdSearchKind::SpriteId) {
		desc += " | Sprite " + std::to_string(searchedId);
	}
	desc += "] ";
	if (!match.itemName.empty()) {
		desc += match.itemName;
	} else {
		desc += "Unknown Item";
	}
	desc += " [";
	desc += ItemLocationKindLabel(match.location);
	if (match.location == ItemLocationKind::ContainerItem && match.containerDepth > 1) {
		desc += " depth " + std::to_string(match.containerDepth);
	}
	desc += "]";
	return desc;
}

MapItemSearchSummary SearchItemsOnMap(
	Map& map,
	const ItemDatabase& itemDb,
	ItemIdSearchKind kind,
	uint32_t targetId,
	bool selectedOnly,
	uint32_t maxResults,
	const std::function<void(uint64_t done, uint64_t total)>& progress
) {
	MapItemSearchSummary summary;

	// Pre-compute matching Server IDs for ClientId / SpriteId searches so the map
	// scan stays O(1) per item even on very large maps and sparse 32-bit IDs.
	std::unordered_set<uint32_t> matchingServerIds;
	if (kind == ItemIdSearchKind::ClientId) {
		itemDb.forEachItemType([&](const ItemType& itemType) {
			if (itemType.id != 0 && itemType.clientID == targetId) {
				matchingServerIds.insert(itemType.id);
			}
		});
		if (matchingServerIds.empty()) {
			return summary;
		}
	} else if (kind == ItemIdSearchKind::SpriteId) {
		itemDb.forEachItemType([&](const ItemType& itemType) {
			if (itemType.id != 0 && ItemHasSpriteId(itemType, targetId)) {
				matchingServerIds.insert(itemType.id);
			}
		});
		if (matchingServerIds.empty()) {
			return summary;
		}
	}

	auto matchesItem = [&](const Item* item) -> bool {
		if (!item) {
			return false;
		}
		if (kind == ItemIdSearchKind::ServerId) {
			return item->getID() == targetId;
		}
		return matchingServerIds.find(item->getID()) != matchingServerIds.end();
	};

	const uint64_t totalTiles = map.getTileCount();
	uint64_t doneTiles = 0;

	MapIterator tileIter = map.begin();
	const MapIterator tileEnd = map.end();
	while (tileIter != tileEnd) {
		++doneTiles;
		if (progress && (doneTiles % 0x8000 == 0)) {
			progress(doneTiles, totalTiles);
		}

		TileLocation* location = *tileIter;
		++tileIter;
		if (!location) {
			continue;
		}
		Tile* tile = location->get();
		if (!tile) {
			continue;
		}
		if (selectedOnly && !tile->isSelected()) {
			continue;
		}

		bool tileHadMatch = false;
		auto recordMatch = [&](const Item* item, ItemLocationKind locKind, uint32_t containerDepth) {
			if (!matchesItem(item)) {
				return;
			}
			tileHadMatch = true;
			++summary.totalOccurrences;
			if (maxResults != 0 && summary.matches.size() >= static_cast<size_t>(maxResults)) {
				summary.limitReached = true;
				return;
			}

			const uint32_t serverId = item->getID();
			const ItemType& itemType = itemDb.getItemType(serverId);

			MapItemSearchResult entry;
			entry.position = tile->getPosition();
			entry.serverId = serverId;
			entry.clientId = itemType.id != 0 ? itemType.clientID : 0;
			entry.itemName = itemType.id != 0 ? itemType.name : std::string();
			entry.location = locKind;
			entry.containerDepth = containerDepth;
			summary.matches.push_back(std::move(entry));
		};

		std::queue<std::pair<const Container*, uint32_t>> containers;
		auto inspectContainerTree = [&](const Container* rootContainer) {
			if (!rootContainer) {
				return;
			}
			containers.push({ rootContainer, 1 });
			while (!containers.empty()) {
				const auto [current, depth] = containers.front();
				containers.pop();
				for (const Item* child : current->getVector()) {
					if (!child) {
						continue;
					}
					recordMatch(child, ItemLocationKind::ContainerItem, depth);
					if (const auto* nested = dynamic_cast<const Container*>(child)) {
						containers.push({ nested, depth + 1 });
					}
				}
			}
		};

		if (tile->ground) {
			recordMatch(tile->ground, ItemLocationKind::Ground, 0);
			if (const auto* groundContainer = dynamic_cast<const Container*>(tile->ground)) {
				inspectContainerTree(groundContainer);
			}
		}

		for (const Item* item : tile->items) {
			if (!item) {
				continue;
			}
			recordMatch(item, ItemLocationKind::TileItem, 0);
			if (const auto* container = dynamic_cast<const Container*>(item)) {
				inspectContainerTree(container);
			}
		}

		if (tileHadMatch) {
			++summary.matchedTiles;
		}
	}

	return summary;
}
