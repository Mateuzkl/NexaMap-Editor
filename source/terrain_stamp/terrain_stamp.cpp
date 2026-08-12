//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "../main.h"

#include "terrain_stamp.h"

#include "../basemap.h"
#include "../complexitem.h"
#include "../copybuffer.h"
#include "../editor.h"
#include "../gui.h"
#include "../item.h"
#include "../tile.h"

#include <algorithm>

namespace {
	TerrainStampItem CaptureItem(const Item* item) {
		TerrainStampItem entry;
		entry.id = item->getID();
		entry.count = static_cast<uint16_t>(item->getSubtype());
		if (entry.count == 0) {
			entry.count = 1;
		}
		entry.actionId = item->getActionID();
		entry.uniqueId = item->getUniqueID();
		entry.text = item->getText();

		if (const auto* teleport = dynamic_cast<const Teleport*>(item); teleport && teleport->hasDestination()) {
			entry.teleportDestination = teleport->getDestination();
		}
		if (const auto* door = dynamic_cast<const Door*>(item)) {
			entry.doorId = door->getDoorID();
		}
		return entry;
	}

	Item* CreateItemFromStamp(const TerrainStampItem& entry) {
		Item* item = Item::Create(entry.id, entry.count);
		if (!item) {
			return nullptr;
		}
		if (entry.actionId != 0) {
			item->setActionID(entry.actionId);
		}
		if (entry.uniqueId != 0) {
			item->setUniqueID(entry.uniqueId);
		}
		if (!entry.text.empty()) {
			item->setText(entry.text);
		}
		if (entry.teleportDestination) {
			if (auto* teleport = dynamic_cast<Teleport*>(item)) {
				teleport->setDestination(*entry.teleportDestination);
			}
		}
		if (entry.doorId) {
			if (auto* door = dynamic_cast<Door*>(item)) {
				door->setDoorID(*entry.doorId);
			}
		}
		return item;
	}
}

TerrainStamp TerrainStamp::FromSelection(Editor& editor, int floor) {
	TerrainStamp stamp;
	stamp.clientVersion = g_gui.GetCurrentVersionID();

	if (editor.selection.size() == 0) {
		return stamp;
	}

	Position anchor(0xFFFF, 0xFFFF, floor);
	for (Tile* tile : editor.selection) {
		if (!tile) {
			continue;
		}
		if (tile->getX() < anchor.x) {
			anchor.x = tile->getX();
		}
		if (tile->getY() < anchor.y) {
			anchor.y = tile->getY();
		}
	}
	if (anchor.x == 0xFFFF || anchor.y == 0xFFFF) {
		return stamp;
	}

	for (Tile* tile : editor.selection) {
		if (!tile) {
			continue;
		}

		ItemVector selected = tile->getSelectedItems();
		if (selected.empty()) {
			continue;
		}

		TerrainStampTile stampTile;
		stampTile.dx = tile->getX() - anchor.x;
		stampTile.dy = tile->getY() - anchor.y;
		stampTile.dz = tile->getZ() - floor;
		stampTile.items.reserve(selected.size());
		for (Item* item : selected) {
			if (!item) {
				continue;
			}
			stampTile.items.push_back(CaptureItem(item));
		}
		if (!stampTile.items.empty()) {
			stamp.tiles.push_back(std::move(stampTile));
		}
	}

	return stamp;
}

bool TerrainStamp::LoadIntoCopyBuffer(CopyBuffer& buffer) const {
	if (empty()) {
		return false;
	}

	int minDz = tiles.front().dz;
	for (const TerrainStampTile& stampTile : tiles) {
		minDz = std::min(minDz, stampTile.dz);
	}

	auto* map = newd BaseMap();
	for (const TerrainStampTile& stampTile : tiles) {
		const Position position(stampTile.dx, stampTile.dy, stampTile.dz - minDz);
		if (position.x < 0 || position.y < 0 || position.z < 0 || position.z > MAP_MAX_LAYER) {
			continue;
		}

		std::vector<Item*> createdItems;
		createdItems.reserve(stampTile.items.size());
		for (const TerrainStampItem& stampItem : stampTile.items) {
			Item* item = CreateItemFromStamp(stampItem);
			if (item) {
				createdItems.push_back(item);
			}
		}
		if (createdItems.empty()) {
			continue;
		}

		TileLocation* location = map->createTileL(position);
		Tile* tile = map->allocator(location);
		for (Item* item : createdItems) {
			tile->addItem(item);
		}
		map->setTile(tile);
	}

	if (map->size() == 0) {
		delete map;
		return false;
	}

	// paste uses buffer_pos - copyPos + cursor; copyPos.z = -minDz restores relative floors.
	buffer.replace(map, Position(0, 0, -minDz));
	return true;
}
