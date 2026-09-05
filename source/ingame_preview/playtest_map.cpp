// SPDX-License-Identifier: GPL-3.0-or-later
#include "../main.h"
#include "playtest_map.h"
#include "../map.h"
#include "../complexitem.h"
#include "../brush.h"

namespace Playtest {
	TileInfo MapWorld::read(const Position& position) const {
		TileInfo result;
		if (!position.isValid()) {
			return result;
		}
		const Tile* tile = map.getTile(position);
		if (!tile) {
			return result;
		}
		result.exists = true;
		result.ground = tile->hasGround();
		result.blocked = tile->creature != nullptr;
		result.groundSpeed = tile->getGroundSpeed() ? tile->getGroundSpeed() : 100;
		int doors = 0;
		auto inspect = [&](const Item* item) {
			if (!item) {
				return;
			}
			const ItemType& type = g_items[item->getID()];
			if (!type.id) {
				result.blocked = true;
				return;
			}
			unsigned flags = (type.floorChangeDown ? Down : 0) | (type.floorChangeNorth ? North : 0) | (type.floorChangeEast ? East : 0) | (type.floorChangeSouth ? South : 0) | (type.floorChangeWest ? West : 0);
			result.floorFlags |= flags;
			if (type.isFloorChange() && flags == 0) {
				result.use = UseKind::UnknownFloorChange;
			}
			const std::string name = as_lower_str(type.name);
			if (name == "rope spot" || name == "rope hole") {
				result.use = UseKind::Rope;
			} else if (name == "ladder") {
				result.use = UseKind::Ladder;
			}
			if (const auto* teleport = dynamic_cast<const Teleport*>(item)) {
				result.hasTeleport = true;
				if (teleport->hasDestination()) {
					result.destination = teleport->getDestination();
				}
			}
			if (type.isDoor() || type.isBrushDoor) {
				++doors;
				result.doorId = type.id;
				result.doorOpen = type.isOpen;
				// Reuse the editor's definition-based pairing on an isolated item.
				// No map instance or UID/AID/house/container properties are copied.
				if (type.isBrushDoor && item->getWallBrush() && static_cast<unsigned>(item->getWallAlignment()) < 17) {
					std::unique_ptr<Item> probe(Item::Create(type.id));
					if (!probe) {
						return;
					}
					DoorBrush::switchDoor(probe.get());
					const auto& paired = g_items[probe->getID()];
					if (paired.id && paired.id != type.id && (paired.isDoor() || paired.isBrushDoor) && paired.isOpen != type.isOpen) {
						result.doorAlternate = paired.id;
					}
				} else if (type.rotateTo) {
					const auto& paired = g_items[type.rotateTo];
					if (paired.id && (paired.isDoor() || paired.isBrushDoor) && paired.isOpen != type.isOpen) {
						result.doorAlternate = paired.id;
					}
				}
			} else if (type.unpassable && flags == 0) {
				result.blocked = true;
			}
		};
		inspect(tile->ground);
		for (const auto* item : tile->items) {
			inspect(item);
		}
		if (doors > 1) {
			result.blocked = true;
			result.doorAlternate = 0;
		}
		return result;
	}
}
