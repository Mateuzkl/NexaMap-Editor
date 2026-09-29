// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_item_identity.h"

#include "../item_id_mapping.h"
#include "../items.h"

namespace mcp {
	Json ItemIdentity(uint16_t activeId, ItemIdMode mode, const ItemType& type) {
		Json serverId = nullptr;
		Json clientId = nullptr;
		if (mode == ItemIdMode::ServerId) {
			serverId = activeId;
			if (type.clientID != 0) {
				clientId = type.clientID;
			} else {
				const ItemIdMapping::Result mapped = ItemIdMapping::serverToClient(activeId);
				if (mapped.found) {
					clientId = mapped.converted;
				}
			}
		} else if (mode == ItemIdMode::ClientId) {
			clientId = activeId;
			const ItemIdMapping::Result mapped = ItemIdMapping::clientToServer(activeId);
			if (mapped.found && !mapped.ambiguous) {
				serverId = mapped.converted;
			}
		}
		return {
			{ "id", activeId },
			{ "idMode", McpItemIdModeName(mode) },
			{ "serverId", std::move(serverId) },
			{ "clientId", std::move(clientId) },
			{ "name", type.name },
		};
	}

	Json ItemFlags(const ItemType& type, AssetMode mode) {
		return {
			{ "source", mode == AssetMode::Appearances ? "appearances" : (mode == AssetMode::ClassicDatSpr ? "dat-otb" : "unknown") },
			{ "ground", type.isGroundTile() },
			{ "container", type.isContainer() },
			{ "teleport", type.isTeleport() },
			{ "door", type.isDoor() || type.isBrushDoor },
			{ "blocking", type.unpassable },
			{ "walkable", !type.unpassable },
			{ "blockProjectile", type.blockMissiles },
			{ "blockPathfinder", type.blockPathfinder },
			{ "moveable", type.moveable },
			{ "pickupable", type.pickupable },
			{ "stackable", type.stackable },
			{ "readable", type.canReadText },
			{ "writable", type.canWriteText },
			{ "rotatable", type.rotable && type.rotateTo != 0 },
			{ "hangable", type.isHangable },
			{ "hookEast", type.hookEast },
			{ "hookSouth", type.hookSouth },
			{ "hasElevation", type.hasElevation },
			{ "floorChange", type.isFloorChange() },
			{ "border", type.isBorder },
			{ "wall", type.isWall },
			{ "table", type.isTable },
			{ "carpet", type.isCarpet },
		};
	}
}
