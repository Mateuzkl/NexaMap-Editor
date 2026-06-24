//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include <wx/file.h>

#include <set>
#include <sstream>

#include "settings.h"
#include "gui.h" // Loadbar

#include "creatures.h"
#include "creature.h"
#include "map.h"
#include "tile.h"
#include "item.h"
#include "complexitem.h"
#include "town.h"
#include "wall_brush.h"

#include "iomap_otbm.h"

typedef uint8_t attribute_t;
typedef uint32_t flags_t;

static const uint32_t LEGACY_TILESTATE_ZONE_BRUSH = 0x0040;

// H4X
void reform(Map* map, Tile* tile, Item* item) {
	/*
	int aid = item->getActionID();
	int id = item->getID();
	int uid = item->getUniqueID();

	if(item->isDoor()) {
		item->eraseAttribute("aid");
		item->setAttribute("keyid", aid);
	}

	if((item->isDoor()) && tile && tile->getHouseID()) {
		Door* self = static_cast<Door*>(item);
		House* house = map->houses.getHouse(tile->getHouseID());
		self->setDoorID(house->getEmptyDoorID());
	}
	*/
}

// ============================================================================
// Item

Item* Item::Create_OTBM(const IOMap& maphandle, BinaryNode* stream, const ItemType** itemType) {
	if (itemType) {
		*itemType = nullptr;
	}

	uint16_t _id;
	if (!stream->getU16(_id)) {
		return nullptr;
	}

	const ItemType& iType = g_items[_id];
	if (itemType) {
		*itemType = &iType;
	}

	uint8_t _count = 0;

	if (maphandle.version.otbm == MAP_OTBM_1) {
		if (iType.stackable || iType.isSplash() || iType.isFluidContainer()) {
			stream->getU8(_count);
		}
	}
	return Item::Create(_id, _count);
}

bool Item::readItemAttribute_OTBM(const IOMap& maphandle, OTBM_ItemAttribute attr, BinaryNode* stream) {
	switch (attr) {
		case OTBM_ATTR_COUNT: {
			uint8_t subtype;
			if (!stream->getU8(subtype)) {
				return false;
			}
			setSubtype(subtype);
			break;
		}
		case OTBM_ATTR_ACTION_ID: {
			uint16_t aid;
			if (!stream->getU16(aid)) {
				return false;
			}
			setActionID(aid);
			break;
		}
		case OTBM_ATTR_UNIQUE_ID: {
			uint16_t uid;
			if (!stream->getU16(uid)) {
				return false;
			}
			setUniqueID(uid);
			break;
		}
		case OTBM_ATTR_CHARGES: {
			uint16_t charges;
			if (!stream->getU16(charges)) {
				return false;
			}
			setSubtype(charges);
			break;
		}
		case OTBM_ATTR_TEXT: {
			std::string text;
			if (!stream->getString(text)) {
				return false;
			}
			setText(text);
			break;
		}
		case OTBM_ATTR_DESC: {
			std::string text;
			if (!stream->getString(text)) {
				return false;
			}
			setDescription(text);
			break;
		}
		case OTBM_ATTR_RUNE_CHARGES: {
			uint8_t subtype;
			if (!stream->getU8(subtype)) {
				return false;
			}
			setSubtype(subtype);
			break;
		}
		case OTBM_ATTR_TIER: {
			uint8_t tier;
			if (!stream->getU8(tier)) {
				return false;
			}
			setTier(static_cast<uint16_t>(tier));
			break;
		}

		// The following *should* be handled in the derived classes
		// However, we still need to handle them here since otherwise things
		// will break horribly
		case OTBM_ATTR_DEPOT_ID:
			return stream->skip(2);
		case OTBM_ATTR_HOUSEDOORID:
			return stream->skip(1);
		case OTBM_ATTR_TELE_DEST:
			return stream->skip(5);
		case OTBM_ATTR_PODIUMOUTFIT:
			return stream->skip(15);
		default:
			return false;
	}
	return true;
}

bool Item::unserializeAttributes_OTBM(const IOMap& maphandle, BinaryNode* stream) {
	uint8_t attribute;
	while (stream->getU8(attribute)) {
		if (attribute == OTBM_ATTR_ATTRIBUTE_MAP) {
			if (!ItemAttributes::unserializeAttributeMap(maphandle, stream)) {
				return false;
			}
		} else if (!readItemAttribute_OTBM(maphandle, static_cast<OTBM_ItemAttribute>(attribute), stream)) {
			return false;
		}
	}
	return true;
}

bool Item::unserializeItemNode_OTBM(const IOMap& maphandle, BinaryNode* node) {
	return unserializeAttributes_OTBM(maphandle, node);
}

void Item::serializeItemAttributes_OTBM(const IOMap& maphandle, NodeFileWriteHandle& stream) const {
	if (maphandle.version.otbm >= MAP_OTBM_2) {
		const ItemType& iType = g_items[id];
		if (iType.stackable || iType.isSplash() || iType.isFluidContainer()) {
			stream.addU8(OTBM_ATTR_COUNT);
			stream.addU8(getSubtype());
		}
	}

	if (maphandle.version.otbm >= MAP_OTBM_4) {
		if (attributes && !attributes->empty()) {
			stream.addU8(OTBM_ATTR_ATTRIBUTE_MAP);
			serializeAttributeMap(maphandle, stream);
		}
	} else {
		if (g_items.MinorVersion >= CLIENT_VERSION_820 && isCharged()) {
			stream.addU8(OTBM_ATTR_CHARGES);
			stream.addU16(getSubtype());
		}

		uint16_t actionId = getActionID();
		if (actionId > 0) {
			stream.addU8(OTBM_ATTR_ACTION_ID);
			stream.addU16(actionId);
		}

		uint16_t uniqueId = getUniqueID();
		if (uniqueId > 0) {
			stream.addU8(OTBM_ATTR_UNIQUE_ID);
			stream.addU16(uniqueId);
		}

		const std::string& text = getText();
		if (!text.empty()) {
			stream.addU8(OTBM_ATTR_TEXT);
			stream.addString(text);
		}

		const std::string& description = getDescription();
		if (!description.empty()) {
			stream.addU8(OTBM_ATTR_DESC);
			stream.addString(description);
		}

		uint16_t tier = getTier();
		if (tier > 0) {
			stream.addU8(OTBM_ATTR_TIER);
			stream.addU8(static_cast<uint8_t>(tier));
		}
	}
}

void Item::serializeItemCompact_OTBM(const IOMap& maphandle, NodeFileWriteHandle& stream) const {
	stream.addU16(id);

	/* This is impossible
	const ItemType& iType = g_items[id];

	if(iType.stackable || iType.isSplash() || iType.isFluidContainer()){
		stream.addU8(getSubtype());
	}
	*/
}

bool Item::serializeItemNode_OTBM(const IOMap& maphandle, NodeFileWriteHandle& file) const {
	file.addNode(OTBM_ITEM);
	file.addU16(id);
	if (maphandle.version.otbm == MAP_OTBM_1) {
		const ItemType& iType = g_items[id];
		if (iType.stackable || iType.isSplash() || iType.isFluidContainer()) {
			file.addU8(getSubtype());
		}
	}
	serializeItemAttributes_OTBM(maphandle, file);
	file.endNode();
	return true;
}

// ============================================================================
// Teleport

bool Teleport::readItemAttribute_OTBM(const IOMap& maphandle, OTBM_ItemAttribute attribute, BinaryNode* stream) {
	if (OTBM_ATTR_TELE_DEST == attribute) {
		uint16_t x, y;
		uint8_t z;
		if (!stream->getU16(x) || !stream->getU16(y) || !stream->getU8(z)) {
			return false;
		}
		destination = Position(x, y, z);
		return true;
	} else {
		return Item::readItemAttribute_OTBM(maphandle, attribute, stream);
	}
}

void Teleport::serializeItemAttributes_OTBM(const IOMap& maphandle, NodeFileWriteHandle& stream) const {
	Item::serializeItemAttributes_OTBM(maphandle, stream);

	stream.addByte(OTBM_ATTR_TELE_DEST);
	stream.addU16(destination.x);
	stream.addU16(destination.y);
	stream.addU8(destination.z);
}

// ============================================================================
// Door

bool Door::readItemAttribute_OTBM(const IOMap& maphandle, OTBM_ItemAttribute attribute, BinaryNode* stream) {
	if (OTBM_ATTR_HOUSEDOORID == attribute) {
		uint8_t id = 0;
		if (!stream->getU8(id)) {
			return false;
		}
		doorId = id;
		return true;
	} else {
		return Item::readItemAttribute_OTBM(maphandle, attribute, stream);
	}
}

void Door::serializeItemAttributes_OTBM(const IOMap& maphandle, NodeFileWriteHandle& stream) const {
	Item::serializeItemAttributes_OTBM(maphandle, stream);
	if (doorId) {
		stream.addByte(OTBM_ATTR_HOUSEDOORID);
		stream.addU8(doorId);
	}
}

DoorType Door::getDoorType() const {
	WallBrush* wb = getWallBrush();
	if (!wb) {
		return WALL_UNDEFINED;
	}

	return wb->getDoorTypeFromID(id);
}

bool Door::isRealDoor() const {
	const DoorType& dt = getDoorType();
	// doors with no wallbrush will appear as WALL_UNDEFINED
	// this is for compatibility
	return dt == WALL_UNDEFINED || dt == WALL_DOOR_NORMAL || dt == WALL_DOOR_LOCKED || dt == WALL_DOOR_QUEST || dt == WALL_DOOR_MAGIC || dt == WALL_DOOR_NORMAL_ALT;
}

uint8_t Door::getDoorID() const {
	return isRealDoor() ? doorId : 0;
}

void Door::setDoorID(uint8_t id) {
	doorId = isRealDoor() ? id : 0;
}

// ============================================================================
// Depots

bool Depot::readItemAttribute_OTBM(const IOMap& maphandle, OTBM_ItemAttribute attribute, BinaryNode* stream) {
	if (OTBM_ATTR_DEPOT_ID == attribute) {
		uint16_t id = 0;
		if (!stream->getU16(id)) {
			return false;
		}
		depotId = id;
		return true;
	} else {
		return Item::readItemAttribute_OTBM(maphandle, attribute, stream);
	}
}

void Depot::serializeItemAttributes_OTBM(const IOMap& maphandle, NodeFileWriteHandle& stream) const {
	Item::serializeItemAttributes_OTBM(maphandle, stream);
	if (depotId) {
		stream.addByte(OTBM_ATTR_DEPOT_ID);
		stream.addU16(depotId);
	}
}

// ============================================================================
// Container

bool Container::unserializeItemNode_OTBM(const IOMap& maphandle, BinaryNode* node) {
	if (!Item::unserializeAttributes_OTBM(maphandle, node)) {
		return false;
	}

	BinaryNode* child = node->getChild();
	if (child) {
		do {
			uint8_t type;
			if (!child->getByte(type)) {
				return false;
			}

			if (type != OTBM_ITEM) {
				return false;
			}

			Item* item = Item::Create_OTBM(maphandle, child);
			if (!item) {
				return false;
			}

			if (!item->unserializeItemNode_OTBM(maphandle, child)) {
				delete item;
				return false;
			}

			contents.push_back(item);
		} while (child->advance());
	}
	return true;
}

bool Container::serializeItemNode_OTBM(const IOMap& maphandle, NodeFileWriteHandle& file) const {
	file.addNode(OTBM_ITEM);
	file.addU16(id);
	if (maphandle.version.otbm == MAP_OTBM_1) {
		// In the ludicrous event that an item is a container AND stackable, we have to do this. :p
		const ItemType& iType = g_items[id];
		if (iType.stackable || iType.isSplash() || iType.isFluidContainer()) {
			file.addU8(getSubtype());
		}
	}

	serializeItemAttributes_OTBM(maphandle, file);
	for (Item* item : contents) {
		item->serializeItemNode_OTBM(maphandle, file);
	}

	file.endNode();
	return true;
}

// ============================================================================
// Podium

bool Podium::readItemAttribute_OTBM(const IOMap& maphandle, OTBM_ItemAttribute attribute, BinaryNode* stream) {
	if (OTBM_ATTR_PODIUMOUTFIT == attribute) {
		uint8_t flags;
		uint8_t direction;

		uint16_t lookType;
		uint8_t lookHead;
		uint8_t lookBody;
		uint8_t lookLegs;
		uint8_t lookFeet;
		uint8_t lookAddon;

		uint16_t lookMount;
		uint8_t lookMountHead;
		uint8_t lookMountBody;
		uint8_t lookMountLegs;
		uint8_t lookMountFeet;

		if (
			// podium settings
			stream->getU8(flags) && stream->getU8(direction) &&

			// outfit
			stream->getU16(lookType) && stream->getU8(lookHead) && stream->getU8(lookBody) && stream->getU8(lookLegs) && stream->getU8(lookFeet) && stream->getU8(lookAddon) &&

			// mount
			stream->getU16(lookMount) && stream->getU8(lookMountHead) && stream->getU8(lookMountBody) && stream->getU8(lookMountLegs) && stream->getU8(lookMountFeet)
		) { //"if" condition ends here

			setShowOutfit((flags & PODIUM_SHOW_OUTFIT) != 0);
			setShowMount((flags & PODIUM_SHOW_MOUNT) != 0);
			setShowPlatform((flags & PODIUM_SHOW_PLATFORM) != 0);

			setDirection(static_cast<Direction>(direction));

			struct Outfit newOutfit = Outfit();
			newOutfit.lookType = static_cast<int>(lookType);
			newOutfit.lookHead = static_cast<int>(lookHead);
			newOutfit.lookBody = static_cast<int>(lookBody);
			newOutfit.lookLegs = static_cast<int>(lookLegs);
			newOutfit.lookFeet = static_cast<int>(lookFeet);
			newOutfit.lookAddon = static_cast<int>(lookAddon);
			newOutfit.lookMount = static_cast<int>(lookMount);
			newOutfit.lookMountHead = static_cast<int>(lookMountHead);
			newOutfit.lookMountBody = static_cast<int>(lookMountBody);
			newOutfit.lookMountLegs = static_cast<int>(lookMountLegs);
			newOutfit.lookMountFeet = static_cast<int>(lookMountFeet);
			setOutfit(newOutfit);
			return true;
		}
		return false;
	} else {
		return Item::readItemAttribute_OTBM(maphandle, attribute, stream);
	}
}

void Podium::serializeItemAttributes_OTBM(const IOMap& maphandle, NodeFileWriteHandle& stream) const {
	Item::serializeItemAttributes_OTBM(maphandle, stream);

	uint8_t flags = PODIUM_SHOW_OUTFIT * static_cast<uint8_t>(showOutfit) + PODIUM_SHOW_MOUNT * static_cast<uint8_t>(showMount) + PODIUM_SHOW_PLATFORM * static_cast<uint8_t>(showPlatform);

	stream.addByte(OTBM_ATTR_PODIUMOUTFIT);
	stream.addU8(flags);
	stream.addU8(direction);

	stream.addU16(outfit.lookType);
	stream.addU8(outfit.lookHead);
	stream.addU8(outfit.lookBody);
	stream.addU8(outfit.lookLegs);
	stream.addU8(outfit.lookFeet);
	stream.addU8(outfit.lookAddon);

	stream.addU16(outfit.lookMount);
	stream.addU8(outfit.lookMountHead);
	stream.addU8(outfit.lookMountBody);
	stream.addU8(outfit.lookMountLegs);
	stream.addU8(outfit.lookMountFeet);
}

/*
	OTBM_ROOTV1
	|
	|--- OTBM_MAP_DATA
	|	|
	|	|--- OTBM_TILE_AREA
	|	|	|--- OTBM_TILE
	|	|	|--- OTBM_TILE_SQUARE (not implemented)
	|	|	|--- OTBM_TILE_REF (not implemented)
	|	|	|--- OTBM_HOUSETILE
	|	|
	|	|--- OTBM_SPAWNS (not implemented)
	|	|	|--- OTBM_SPAWN_AREA (not implemented)
	|	|	|--- OTBM_MONSTER (not implemented)
	|	|
	|	|--- OTBM_TOWNS
	|		|--- OTBM_TOWN
	|
	|--- OTBM_ITEM_DEF (not implemented)
*/

static bool isWildcardOtbmIdentifier(const uint8_t* data) {
	return data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 0;
}

static bool hasValidOtbmPrefix(const uint8_t* data, size_t size) {
	if (size < 5) {
		return false;
	}
	if (!isWildcardOtbmIdentifier(data) && memcmp(data, "OTBM", 4) != 0) {
		return false;
	}
	return data[4] == NODE_START;
}

bool IOMapOTBM::getVersionInfo(const FileName& filename, MapVersion& out_ver) {

	// Validate the OTBM prefix before parsing
	FileReadHandle otbmProbe(nstr(filename.GetFullPath()));
	if (!otbmProbe.isOk()) {
		return false;
	}

	uint8_t otbmPrefix[5] = { 0 };
	if (otbmProbe.size() < sizeof(otbmPrefix)) {
		return false;
	}
	if (!otbmProbe.getRAW(otbmPrefix, sizeof(otbmPrefix))) {
		return false;
	}
	if (!hasValidOtbmPrefix(otbmPrefix, sizeof(otbmPrefix))) {
		return false;
	}

	// Just open a disk-based read handle
	DiskNodeFileReadHandle f(nstr(filename.GetFullPath()), StringVector(1, "OTBM"));
	if (!f.isOk()) {
		return false;
	}
	return getVersionInfo(&f, out_ver);
}

bool IOMapOTBM::getVersionInfo(NodeFileReadHandle* f, MapVersion& out_ver) {
	BinaryNode* root = f->getRootNode();
	if (!root) {
		return false;
	}

	root->skip(1); // Skip the type byte

	uint16_t u16;
	uint32_t u32;

	if (!root->getU32(u32)) { // Version
		return false;
	}
	out_ver.otbm = (MapVersionID)u32;

	root->getU16(u16); // map size X
	root->getU16(u16); // map size Y
	root->getU32(u32); // OTB major version

	if (!root->getU32(u32)) { // OTB minor version
		return false;
	}

	out_ver.client = ClientVersionID(u32);
	return true;
}

bool IOMapOTBM::loadMap(Map& map, const FileName& filename) {

	// Read the whole OTBM into memory and parse from there. Parsing directly off
	// a disk handle (DiskNodeFileReadHandle) interleaves chunked disk reads with
	// parsing, which can stall the load mid-way on large maps.
	FileReadHandle otbmFile(nstr(filename.GetFullPath()));
	if (!otbmFile.isOk()) {
		error(("Couldn't open file for reading\nThe error reported was: " + wxstr(otbmFile.getErrorMessage())).wc_str());
		return false;
	}

	const size_t otbmSize = otbmFile.size();
	if (otbmSize < 5) {
		error("Could not read OTBM file header.");
		return false;
	}

	std::vector<uint8_t> otbmBuffer(otbmSize);
	if (!otbmFile.getRAW(otbmBuffer.data(), otbmBuffer.size())) {
		error(("Couldn't read file\nThe error reported was: " + wxstr(otbmFile.getErrorMessage())).wc_str());
		return false;
	}

	const uint8_t* buf = otbmBuffer.data();
	const bool isWildcard = buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] == 0;
	const bool isOtbm = buf[0] == 'O' && buf[1] == 'T' && buf[2] == 'B' && buf[3] == 'M';
	if (!isWildcard && !isOtbm) {
		error("File magic number not recognized.");
		return false;
	}
	if (buf[4] != NODE_START) {
		error("Could not read root node.");
		return false;
	}

	MemoryNodeFileReadHandle f(otbmBuffer.data() + 4, otbmSize - 4);
	if (!loadMap(map, f)) {
		return false;
	}

	// Read auxilliary files
	if (!loadHouses(map, filename)) {
		warning("Failed to load houses.");
		map.housefile = nstr(filename.GetName()) + "-house.xml";
	}

	if (map.zonefile.empty()) {
		map.zonefile = nstr(filename.GetName()) + "-zones.xml";
	}
	if (!loadZones(map, filename)) {
		warning("Failed to load zones.");
	}

	if (!loadSpawns(map, filename)) {
		warning("Failed to load spawns.");
		map.spawnfile = nstr(filename.GetName()) + "-spawn.xml";
	}

	if (!loadWaypoints(map, filename)) {
		// just assume the map did not have this file before
		// warning("Failed to load waypoints.");
		map.waypointfile = nstr(filename.GetName()) + "-waypoint.xml";
	}
	return true;
}

void IOMapOTBM::readMapHeaderAttributes(BinaryNode* mapHeaderNode, Map& map) {
	uint8_t attribute;
	while (mapHeaderNode->getU8(attribute)) {
		switch (attribute) {
			case OTBM_ATTR_DESCRIPTION: {
				if (!mapHeaderNode->getString(map.description)) {
					warning("Invalid map description tag");
				}
				// std::cout << "Map description: " << mapDescription << std::endl;
				break;
			}
			case OTBM_ATTR_EXT_SPAWN_FILE: {
				if (!mapHeaderNode->getString(map.spawnfile)) {
					warning("Invalid map spawnfile tag");
				}
				break;
			}
			case OTBM_ATTR_EXT_HOUSE_FILE: {
				if (!mapHeaderNode->getString(map.housefile)) {
					warning("Invalid map housefile tag");
				}
				break;
			}
			case OTBM_ATTR_EXT_ZONE_FILE: {
				if (!mapHeaderNode->getString(map.zonefile)) {
					warning("Invalid map zonefile tag");
				}
				break;
			}
			case OTBM_ATTR_EXT_SPAWN_NPC_FILE: {
				// compatibility: skip RME NPC spawn file tag
				std::string stringToSkip;
				if (!mapHeaderNode->getString(stringToSkip)) {
					warning("Invalid map housefile tag");
				}
				break;
			}
			default: {
				warning("Unknown header node.");
				break;
			}
		}
	}
}

void IOMapOTBM::readTowns(BinaryNode* mapNode, Map& map) {
	for (BinaryNode* townNode = mapNode->getChild(); townNode != nullptr; townNode = townNode->advance()) {
		Town* town = nullptr;
		uint8_t town_type;
		if (!townNode->getByte(town_type)) {
			warning("Invalid town type (1)");
			continue;
		}
		if (town_type != OTBM_TOWN) {
			warning("Invalid town type (2)");
			continue;
		}
		uint32_t town_id;
		if (!townNode->getU32(town_id)) {
			warning("Invalid town id");
			continue;
		}

		town = map.towns.getTown(town_id);
		if (town) {
			warning("Duplicate town id %d, discarding duplicate", town_id);
			continue;
		} else {
			town = newd Town(town_id);
			if (!map.towns.addTown(town)) {
				delete town;
				continue;
			}
		}
		std::string town_name;
		if (!townNode->getString(town_name)) {
			warning("Invalid town name");
			continue;
		}
		town->setName(town_name);
		Position pos;
		uint16_t x;
		uint16_t y;
		uint8_t z;
		if (!townNode->getU16(x) || !townNode->getU16(y) || !townNode->getU8(z)) {
			warning("Invalid town temple position");
			continue;
		}
		pos.x = x;
		pos.y = y;
		pos.z = z;
		town->setTemplePosition(pos);
		map.getOrCreateTile(pos)->getLocation()->increaseTownCount();
	}
}

void IOMapOTBM::readWaypoints(BinaryNode* mapNode, Map& map) {
	for (BinaryNode* waypointNode = mapNode->getChild(); waypointNode != nullptr; waypointNode = waypointNode->advance()) {
		uint8_t waypoint_type;
		if (!waypointNode->getByte(waypoint_type)) {
			warning("Invalid waypoint type (1)");
			continue;
		}
		if (waypoint_type != OTBM_WAYPOINT) {
			warning("Invalid waypoint type (2)");
			continue;
		}

		Waypoint wp;

		if (!waypointNode->getString(wp.name)) {
			warning("Invalid waypoint name");
			continue;
		}
		uint16_t x;
		uint16_t y;
		uint8_t z;
		if (!waypointNode->getU16(x) || !waypointNode->getU16(y) || !waypointNode->getU8(z)) {
			warning("Invalid waypoint position");
			continue;
		}
		wp.pos.x = x;
		wp.pos.y = y;
		wp.pos.z = z;

		map.waypoints.addWaypoint(newd Waypoint(wp));
	}
}

void IOMapOTBM::readTileArea(BinaryNode* mapNode, Map& map) {
	uint16_t base_x, base_y;
	uint8_t base_z;
	if (!mapNode->getU16(base_x) || !mapNode->getU16(base_y) || !mapNode->getU8(base_z)) {
		warning("Invalid map node, no base coordinate");
		return;
	}

	Floor* cachedFloor = nullptr;
	int cachedFloorX = -1;
	int cachedFloorY = -1;
	int cachedFloorZ = -1;

	for (BinaryNode* tileNode = mapNode->getChild(); tileNode != nullptr; tileNode = tileNode->advance()) {
		Tile* tile = nullptr;
		uint8_t tile_type;
		if (!tileNode->getByte(tile_type)) {
			warning("Invalid tile type");
			continue;
		}
		if (tile_type == OTBM_TILE || tile_type == OTBM_HOUSETILE) {
			// printf("Start\n");
			uint8_t x_offset, y_offset;
			if (!tileNode->getU8(x_offset) || !tileNode->getU8(y_offset)) {
				warning("Could not read position of tile");
				continue;
			}
			const Position pos(base_x + x_offset, base_y + y_offset, base_z);

			const int floorX = pos.x & ~3;
			const int floorY = pos.y & ~3;
			if (!cachedFloor || cachedFloorX != floorX || cachedFloorY != floorY || cachedFloorZ != pos.z) {
				cachedFloor = map.createLeaf(pos.x, pos.y)->createFloor(pos.x, pos.y, pos.z);
				cachedFloorX = floorX;
				cachedFloorY = floorY;
				cachedFloorZ = pos.z;
			}
			TileLocation* tileLocation = &cachedFloor->locs[(pos.x & 3) * 4 + (pos.y & 3)];

			if (tileLocation->get()) {
				warning("Duplicate tile at %d:%d:%d, discarding duplicate", pos.x, pos.y, pos.z);
				continue;
			}

			tile = map.allocator(tileLocation);
			House* house = nullptr;
			if (tile_type == OTBM_HOUSETILE) {
				uint32_t house_id;
				if (!tileNode->getU32(house_id)) {
					warning("House tile without house data, discarding tile");
					continue;
				}
				if (house_id) {
					house = map.houses.getHouse(house_id);
					if (!house) {
						house = newd House(map);
						house->setID(house_id);
						map.houses.addHouse(house);
					}
				} else {
					warning("Invalid house id from tile %d:%d:%d", pos.x, pos.y, pos.z);
				}
			}

			// printf("So far so good\n");

			bool needsFullTileUpdate = false;
			uint8_t attribute;
			while (tileNode->getU8(attribute)) {
				switch (attribute) {
					case OTBM_ATTR_TILE_FLAGS: {
						uint32_t flags = 0;
						if (!tileNode->getU32(flags)) {
							warning("Invalid tile flags of tile on %d:%d:%d", pos.x, pos.y, pos.z);
						}
						tile->setMapFlags(flags & ~LEGACY_TILESTATE_ZONE_BRUSH);
						if (flags & LEGACY_TILESTATE_ZONE_BRUSH) {
							uint16_t zoneId = 0;
							do {
								if (!tileNode->getU16(zoneId)) {
									warning("Invalid zone id of tile on %d:%d:%d", pos.x, pos.y, pos.z);
								}

								if (zoneId != 0) {
									tile->addZone(zoneId);
									std::string zoneName = "Zone " + i2s(zoneId);
									if (!map.zones.hasZone(zoneName) && !map.zones.hasZone(zoneId)) {
										map.zones.addZone(zoneName, zoneId);
									}
								}
							} while (zoneId != 0);
						}
						break;
					}
					case OTBM_ATTR_ITEM: {
						const ItemType* itemType = nullptr;
						Item* item = Item::Create_OTBM(*this, tileNode, &itemType);
						if (item == nullptr) {
							warning("Invalid item at tile %d:%d:%d", pos.x, pos.y, pos.z);
						}
						if (item && itemType) {
							if (((itemType->isGroundTile() || itemType->ground_equivalent != 0) && tile->ground) || (itemType->alwaysOnBottom && !tile->items.empty())) {
								needsFullTileUpdate = true;
							}
							tile->addLoadedItem(item, *itemType);
						}
						break;
					}
					default: {
						warning("Unknown tile attribute at %d:%d:%d", pos.x, pos.y, pos.z);
						break;
					}
				}
			}

			// printf("Didn't die in loop\n");

			for (BinaryNode* itemNode = tileNode->getChild(); itemNode != nullptr; itemNode = itemNode->advance()) {
				Item* item = nullptr;
				uint8_t node_type;
				if (!itemNode->getByte(node_type)) {
					warning("Unknown item type %d:%d:%d", pos.x, pos.y, pos.z);
					continue;
				}
				if (node_type == OTBM_ITEM) {
					const ItemType* itemType = nullptr;
					item = Item::Create_OTBM(*this, itemNode, &itemType);
					if (item) {
						if (!item->unserializeItemNode_OTBM(*this, itemNode)) {
							warning("Couldn't unserialize item attributes at %d:%d:%d", pos.x, pos.y, pos.z);
						}
						// reform(&map, tile, item);
						if (itemType) {
							if (((itemType->isGroundTile() || itemType->ground_equivalent != 0) && tile->ground) || (itemType->alwaysOnBottom && !tile->items.empty())) {
								needsFullTileUpdate = true;
							}
							tile->addLoadedItem(item, *itemType);
						}
					}
				} else if (node_type == OTBM_TILE_ZONE) {
					uint16_t zone_count;
					if (!itemNode->getU16(zone_count)) {
						warning("Invalid zone count at %d:%d:%d", pos.x, pos.y, pos.z);
						continue;
					}
					for (uint16_t i = 0; i < zone_count; ++i) {
						uint16_t zone_id;
						if (!itemNode->getU16(zone_id)) {
							warning("Invalid zone id at %d:%d:%d", pos.x, pos.y, pos.z);
							continue;
						}
						tile->addZone(zone_id);
					}
				} else {
					warning("Unknown type of tile child node");
				}
			}

			if (needsFullTileUpdate) {
				tile->update();
			} else {
				tile->finalizeLoadedState();
			}
			if (house) {
				house->addTile(tile);
			}

			map.setTile(pos.x, pos.y, pos.z, tile);
		} else {
			warning("Unknown type of tile node");
		}
	}
}

bool IOMapOTBM::loadMap(Map& map, NodeFileReadHandle& f) {
	BinaryNode* root = f.getRootNode();
	if (!root) {
		error("Could not read root node.");
		return false;
	}
	root->skip(1); // Skip the type byte

	uint8_t u8;
	uint16_t u16;
	uint32_t u32;

	if (!root->getU32(u32)) {
		return false;
	}

	version.otbm = (MapVersionID)u32;

	if (version.otbm > MAP_OTBM_4) {
		// Failed to read version
		if (g_gui.PopupDialog("Map error", "The loaded map appears to be a OTBM format that is not supported by the editor."
										   "Do you still want to attempt to load the map?",
							  wxYES | wxNO)
			== wxID_YES) {
			warning("Unsupported or damaged map version");
		} else {
			error("Unsupported OTBM version, could not load map");
			return false;
		}
	}

	if (!root->getU16(u16)) {
		return false;
	}

	map.width = u16;
	if (!root->getU16(u16)) {
		return false;
	}

	map.height = u16;

	if (!root->getU32(u32) || u32 > (unsigned long)g_items.MajorVersion) { // OTB major version
		if (g_gui.PopupDialog("Map error", "The loaded map appears to be a items.otb format that deviates from the "
										   "items.otb loaded by the editor. Do you still want to attempt to load the map?",
							  wxYES | wxNO)
			== wxID_YES) {
			warning("Unsupported or damaged map version");
		} else {
			error("Outdated items.otb, could not load map");
			return false;
		}
	}

	if (!root->getU32(u32) || u32 > (unsigned long)g_items.MinorVersion) { // OTB minor version
		warning("This editor needs an updated items.otb version");
	}
	version.client = (ClientVersionID)u32;

	BinaryNode* mapHeaderNode = root->getChild();
	if (mapHeaderNode == nullptr || !mapHeaderNode->getByte(u8) || u8 != OTBM_MAP_DATA) {
		error("Could not get root child node. Cannot recover from fatal error!");
		return false;
	}

	readMapHeaderAttributes(mapHeaderNode, map);

	int nodes_loaded = 0;

	for (BinaryNode* mapNode = mapHeaderNode->getChild(); mapNode != nullptr; mapNode = mapNode->advance()) {
		++nodes_loaded;
		if (nodes_loaded % 15 == 0) {
			g_gui.SetLoadDone(static_cast<int32_t>(100.0 * f.tell() / f.size()));
		}

		uint8_t node_type;
		if (!mapNode->getByte(node_type)) {
			warning("Invalid map node");
			continue;
		}
		if (node_type == OTBM_TILE_AREA) {
			readTileArea(mapNode, map);
		} else if (node_type == OTBM_TOWNS) {
			readTowns(mapNode, map);
		} else if (node_type == OTBM_WAYPOINTS) {
			readWaypoints(mapNode, map);
		}
	}

	if (!f.isOk()) {
		warning(wxstr(f.getErrorMessage()).wc_str());
	}
	return true;
}

bool IOMapOTBM::loadSpawns(Map& map, const FileName& dir) {
	std::string fn = (const char*)(dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME).mb_str(wxConvUTF8));
	fn += map.spawnfile;

	FileName filename(wxstr(fn));
	if (!filename.FileExists()) {
		warnings.push_back("IOMapOTBM::loadSpawns: File not found.");
		return false;
	}

	// has to be declared again as encoding-specific characters break loading there
	std::string encoded_path = (const char*)(dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME).mb_str(wxConvWhateverWorks));
	encoded_path += map.spawnfile;
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(encoded_path.c_str());
	if (!result) {
		warnings.push_back("IOMapOTBM::loadSpawns: File loading error.");
		return false;
	}
	return loadSpawns(map, doc);
}

bool IOMapOTBM::loadSpawns(Map& map, pugi::xml_document& doc) {
	pugi::xml_node node = doc.child("spawns");
	if (!node) {
		warnings.push_back("IOMapOTBM::loadSpawns: Invalid rootheader.");
		return false;
	}

	for (pugi::xml_node spawnNode = node.first_child(); spawnNode; spawnNode = spawnNode.next_sibling()) {
		if (as_lower_str(spawnNode.name()) != "spawn") {
			continue;
		}

		Position spawnPosition;
		spawnPosition.x = spawnNode.attribute("centerx").as_int();
		spawnPosition.y = spawnNode.attribute("centery").as_int();
		spawnPosition.z = spawnNode.attribute("centerz").as_int();

		if (spawnPosition.x == 0 || spawnPosition.y == 0) {
			warning("Bad position data on one spawn, discarding...");
			continue;
		}

		int32_t radius = spawnNode.attribute("radius").as_int();
		if (radius < 1) {
			warning("Couldn't read radius of spawn.. discarding spawn...");
			continue;
		}

		Tile* tile = map.getTile(spawnPosition);
		if (tile && tile->spawn) {
			warning("Duplicate spawn on position %d:%d:%d\n", tile->getX(), tile->getY(), tile->getZ());
			continue;
		}

		auto* spawn = newd Spawn(radius);
		if (!tile) {
			tile = map.allocator(map.createTileL(spawnPosition));
			map.setTile(spawnPosition, tile);
		}

		tile->spawn = spawn;
		map.addSpawn(tile);

		for (pugi::xml_node creatureNode = spawnNode.first_child(); creatureNode; creatureNode = creatureNode.next_sibling()) {
			const std::string& creatureNodeName = as_lower_str(creatureNode.name());
			if (creatureNodeName != "monster" && creatureNodeName != "npc") {
				continue;
			}

			bool isNpc = creatureNodeName == "npc";
			const std::string& name = creatureNode.attribute("name").as_string();
			if (name.empty()) {
				wxString err;
				err << "Bad creature position data, discarding creature at spawn " << spawnPosition.x << ":" << spawnPosition.y << ":" << spawnPosition.z << " due missing name.";
				warnings.Add(err);
				break;
			}

			int32_t spawntime = creatureNode.attribute("spawntime").as_int();
			if (spawntime == 0) {
				spawntime = g_settings.getInteger(Config::DEFAULT_SPAWNTIME);
			}

			uint8_t weight = static_cast<uint8_t>(creatureNode.attribute("weight").as_uint());
			if (weight == 0) {
				weight = static_cast<uint8_t>(g_settings.getInteger(Config::MONSTER_DEFAULT_WEIGHT));
			}

			Direction direction = NORTH;
			int dir = creatureNode.attribute("direction").as_int(-1);
			if (dir >= DIRECTION_FIRST && dir <= DIRECTION_LAST) {
				direction = (Direction)dir;
			}

			Position creaturePosition(spawnPosition);

			pugi::xml_attribute xAttribute = creatureNode.attribute("x");
			pugi::xml_attribute yAttribute = creatureNode.attribute("y");
			if (!xAttribute || !yAttribute) {
				wxString err;
				err << "Bad creature position data, discarding creature \"" << name << "\" at spawn " << creaturePosition.x << ":" << creaturePosition.y << ":" << creaturePosition.z << " due to invalid position.";
				warnings.Add(err);
				break;
			}

			creaturePosition.x += xAttribute.as_int();
			creaturePosition.y += yAttribute.as_int();

			radius = std::max<int32_t>(radius, std::abs(creaturePosition.x - spawnPosition.x));
			radius = std::max<int32_t>(radius, std::abs(creaturePosition.y - spawnPosition.y));
			radius = std::min<int32_t>(radius, g_settings.getInteger(Config::MAX_SPAWN_RADIUS));

			Tile* creatureTile;
			if (creaturePosition == spawnPosition) {
				creatureTile = tile;
			} else {
				creatureTile = map.getTile(creaturePosition);
			}

			if (!creatureTile) {
				wxString err;
				err << "Discarding creature \"" << name << "\" at " << creaturePosition.x << ":" << creaturePosition.y << ":" << creaturePosition.z << " due to invalid position.";
				warnings.Add(err);
				break;
			}

			if (creatureTile->creature) {
				wxString err;
				err << "Duplicate creature \"" << name << "\" at " << creaturePosition.x << ":" << creaturePosition.y << ":" << creaturePosition.z << " was discarded.";
				warnings.Add(err);
				break;
			}

			CreatureType* type = g_creatures[name];
			if (!type) {
				type = g_creatures.addMissingCreatureType(name, isNpc);
			}

			auto* creature = newd Creature(type);
			creature->setDirection(direction);
			creature->setSpawnTime(spawntime);
			creature->setWeight(weight);
			creatureTile->creature = creature;

			if (creatureTile->getLocation()->getSpawnCount() == 0) {
				// No spawn, create a newd one
				ASSERT(creatureTile->spawn == nullptr);
				auto* spawn = newd Spawn(5);
				creatureTile->spawn = spawn;
				map.addSpawn(creatureTile);
			}
		}
	}
	return true;
}

bool IOMapOTBM::loadHouses(Map& map, const FileName& dir) {
	std::string fn = (const char*)(dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME).mb_str(wxConvUTF8));
	fn += map.housefile;

	FileName filename(wxstr(fn));
	if (!filename.FileExists()) {
		warnings.push_back("IOMapOTBM::loadHouses: File not found.");
		return false;
	}

	// has to be declared again as encoding-specific characters break loading there
	std::string encoded_path = (const char*)(dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME).mb_str(wxConvWhateverWorks));
	encoded_path += map.housefile;
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(encoded_path.c_str());
	if (!result) {
		warnings.push_back("IOMapOTBM::loadHouses: File loading error.");
		return false;
	}
	return loadHouses(map, doc);
}

bool IOMapOTBM::loadHouses(Map& map, pugi::xml_document& doc) {
	pugi::xml_node node = doc.child("houses");
	if (!node) {
		warnings.push_back("IOMapOTBM::loadHouses: Invalid rootheader.");
		return false;
	}

	pugi::xml_attribute attribute;
	for (pugi::xml_node houseNode = node.first_child(); houseNode; houseNode = houseNode.next_sibling()) {
		if (as_lower_str(houseNode.name()) != "house") {
			continue;
		}

		House* house = nullptr;
		if ((attribute = houseNode.attribute("houseid"))) {
			house = map.houses.getHouse(attribute.as_uint());
			if (!house) {
				break;
			}
		}

		if ((attribute = houseNode.attribute("name"))) {
			house->name = attribute.as_string();
		} else {
			house->name = "House #" + std::to_string(house->getID());
		}

		Position exitPosition(
			houseNode.attribute("entryx").as_int(),
			houseNode.attribute("entryy").as_int(),
			houseNode.attribute("entryz").as_int()
		);
		if (exitPosition.x != 0 && exitPosition.y != 0 && exitPosition.z != 0) {
			house->setExit(exitPosition);
		}

		if ((attribute = houseNode.attribute("rent"))) {
			house->rent = attribute.as_int();
		}

		if ((attribute = houseNode.attribute("guildhall"))) {
			house->guildhall = attribute.as_bool();
		}

		if ((attribute = houseNode.attribute("townid"))) {
			house->townid = attribute.as_uint();
		} else {
			warning("House %d has no town! House was removed.", house->getID());
			map.houses.removeHouse(house);
		}
	}
	return true;
}

bool IOMapOTBM::loadWaypoints(Map& map, const FileName& dir) {
	std::string fn = (const char*)(dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME).mb_str(wxConvUTF8));
	fn += map.waypointfile;
	FileName filename(wxstr(fn));
	if (!filename.FileExists()) {
		return false;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(fn.c_str());
	if (!result) {
		return false;
	}
	return loadWaypoints(map, doc);
};
bool IOMapOTBM::loadWaypoints(Map& map, pugi::xml_document& doc) {
	return true;
};

bool IOMapOTBM::loadZones(Map& map, const FileName& dir) {
	if (map.zonefile.empty()) {
		return true;
	}

	std::string fn = (const char*)(dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME).mb_str(wxConvUTF8));
	fn += map.zonefile;
	FileName filename(wxstr(fn));
	if (!filename.FileExists()) {
		return true;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(fn.c_str());
	if (!result) {
		return false;
	}
	return loadZones(map, doc);
}

bool IOMapOTBM::loadZones(Map& map, pugi::xml_document& doc) {
	pugi::xml_node node = doc.child("zones");
	if (!node) {
		warnings.push_back("IOMapOTBM::loadZones: Invalid rootheader.");
		return false;
	}

	pugi::xml_attribute attribute;
	for (pugi::xml_node zoneNode = node.first_child(); zoneNode; zoneNode = zoneNode.next_sibling()) {
		if (as_lower_str(zoneNode.name()) != "zone") {
			continue;
		}

		std::string name = zoneNode.attribute("name").as_string();
		unsigned int id = zoneNode.attribute("zoneid").as_uint(zoneNode.attribute("id").as_uint());
		if (id == 0) {
			continue;
		}
		map.zones.addZone(name, id);

		for (pugi::xml_node positionNode = zoneNode.child("position"); positionNode; positionNode = positionNode.next_sibling("position")) {
			Position position(
				positionNode.attribute("x").as_int(),
				positionNode.attribute("y").as_int(),
				positionNode.attribute("z").as_int()
			);

			Tile* tile = map.getTile(position);
			if (tile) {
				tile->addZone(id);
			}
		}
	}
	return true;
}

bool IOMapOTBM::saveMap(Map& map, const FileName& identifier) {

	DiskNodeFileWriteHandle f(
		nstr(identifier.GetFullPath()),
		(g_settings.getInteger(Config::SAVE_WITH_OTB_MAGIC_NUMBER) ? "OTBM" : std::string(4, '\0'))
	);

	if (!f.isOk()) {
		error("Can not open file %s for writing", (const char*)identifier.GetFullPath().mb_str(wxConvUTF8));
		return false;
	}

	if (!saveMap(map, f)) {
		return false;
	}

	g_gui.SetLoadDone(99, "Saving spawns...");
	saveSpawns(map, identifier);

	g_gui.SetLoadDone(99, "Saving houses...");
	saveHouses(map, identifier);

	g_gui.SetLoadDone(99, "Saving zones...");
	saveZones(map, identifier);

	return true;
}

void IOMapOTBM::writeTiles(Map& map, NodeFileWriteHandle& f) {
	const IOMapOTBM& self = *this;
	uint32_t tiles_saved = 0;
	bool first = true;

	int local_x = -1, local_y = -1, local_z = -1;

	MapIterator map_iterator = map.begin();
	while (map_iterator != map.end()) {
		// Update progressbar
		++tiles_saved;
		if (tiles_saved % 8192 == 0) {
			g_gui.SetLoadDone(int(tiles_saved / double(map.getTileCount()) * 100.0));
		}

		// Get tile
		Tile* save_tile = (*map_iterator)->get();

		// Is it an empty tile that we can skip? (Leftovers...)
		if (!save_tile || (save_tile->size() == 0 && !save_tile->hasZone())) {
			++map_iterator;
			continue;
		}

		const Position& pos = save_tile->getPosition();

		// Decide if newd node should be created
		if (pos.x < local_x || pos.x >= local_x + 256 || pos.y < local_y || pos.y >= local_y + 256 || pos.z != local_z) {
			// End last node
			if (!first) {
				f.endNode();
			}
			first = false;

			// Start newd node
			f.addNode(OTBM_TILE_AREA);
			f.addU16(local_x = pos.x & 0xFF00);
			f.addU16(local_y = pos.y & 0xFF00);
			f.addU8(local_z = pos.z);
		}
		f.addNode(save_tile->isHouseTile() ? OTBM_HOUSETILE : OTBM_TILE);

		f.addU8(save_tile->getX() & 0xFF);
		f.addU8(save_tile->getY() & 0xFF);

		if (save_tile->isHouseTile()) {
			f.addU32(save_tile->getHouseID());
		}

		if (save_tile->getMapFlags()) {
			f.addByte(OTBM_ATTR_TILE_FLAGS);
			f.addU32(save_tile->getMapFlags());
		}

		if (save_tile->ground) {
			Item* ground = save_tile->ground;
			if (ground->isMetaItem()) {
				// Do nothing, we don't save metaitems...
			} else if (ground->hasBorderEquivalent()) {
				bool found = false;
				for (Item* item : save_tile->items) {
					if (item->getGroundEquivalent() == ground->getID()) {
						// Do nothing
						// Found equivalent
						found = true;
						break;
					}
				}

				if (!found) {
					ground->serializeItemNode_OTBM(self, f);
				}
			} else if (ground->isComplex()) {
				ground->serializeItemNode_OTBM(self, f);
			} else {
				f.addByte(OTBM_ATTR_ITEM);
				ground->serializeItemCompact_OTBM(self, f);
			}
		}

		for (Item* item : save_tile->items) {
			if (!item->isMetaItem()) {
				item->serializeItemNode_OTBM(self, f);
			}
		}

		if (save_tile->hasZone()) {
			f.addNode(OTBM_TILE_ZONE);
			f.addU16(save_tile->zones.size());
			for (const auto& zoneId : save_tile->zones) {
				f.addU16(zoneId);
			}
			f.endNode();
		}

		f.endNode();
		++map_iterator;
	}

	// Only close the last node if one has actually been created
	if (!first) {
		f.endNode();
	}
}

void IOMapOTBM::writeTowns(Map& map, NodeFileWriteHandle& f) {
	f.addNode(OTBM_TOWNS);
	for (const auto& townEntry : map.towns) {
		Town* town = townEntry.second;
		const Position& townPosition = town->getTemplePosition();
		f.addNode(OTBM_TOWN);
		f.addU32(town->getID());
		f.addString(town->getName());
		f.addU16(townPosition.x);
		f.addU16(townPosition.y);
		f.addU8(townPosition.z);
		f.endNode();
	}
	f.endNode();
}

void IOMapOTBM::writeWaypoints(Map& map, NodeFileWriteHandle& f, bool& waypointsWarning) {
	bool supportWaypoints = version.otbm >= MAP_OTBM_3;
	if (supportWaypoints || map.waypoints.waypoints.size() > 0) {
		if (!supportWaypoints) {
			waypointsWarning = true;
		}

		f.addNode(OTBM_WAYPOINTS);
		for (const auto& waypointEntry : map.waypoints) {
			Waypoint* waypoint = waypointEntry.second;
			f.addNode(OTBM_WAYPOINT);
			f.addString(waypoint->name);
			f.addU16(waypoint->pos.x);
			f.addU16(waypoint->pos.y);
			f.addU8(waypoint->pos.z);
			f.endNode();
		}
		f.endNode();
	}
}

bool IOMapOTBM::saveMap(Map& map, NodeFileWriteHandle& f) {
	/* STOP!
	 * Before you even think about modifying this, please reconsider.
	 * while adding stuff to the binary format may be "cool", you'll
	 * inevitably make it incompatible with any future releases of
	 * the map editor, meaning you cannot reuse your map. Before you
	 * try to modify this, PLEASE consider using an external file
	 * like spawns.xml or houses.xml, as that will be MUCH easier
	 * to port to newer versions of the editor than a custom binary
	 * format.
	 */

	bool waypointsWarning = false;


	FileName tmpName;
	MapVersion mapVersion = map.getVersion();

	f.addNode(0);
	{
		f.addU32(mapVersion.otbm); // Version

		f.addU16(map.width);
		f.addU16(map.height);

		f.addU32(g_items.MajorVersion);
		f.addU32(g_items.MinorVersion);

		f.addNode(OTBM_MAP_DATA);
		{
			f.addByte(OTBM_ATTR_DESCRIPTION);
			// Neither SimOne's nor OpenTibia cares for additional description tags
			f.addString("Saved with " + __RME_APPLICATION_NAME__ + " " + __RME_VERSION__);

			f.addU8(OTBM_ATTR_DESCRIPTION);
			f.addString(map.description);

			tmpName.Assign(wxstr(map.spawnfile));
			f.addU8(OTBM_ATTR_EXT_SPAWN_FILE);
			f.addString(nstr(tmpName.GetFullName()));

			tmpName.Assign(wxstr(map.housefile));
			f.addU8(OTBM_ATTR_EXT_HOUSE_FILE);
			f.addString(nstr(tmpName.GetFullName()));

			// Start writing tiles
			writeTiles(map, f);

			writeTowns(map, f);

			writeWaypoints(map, f, waypointsWarning);
		}
		f.endNode();
	}
	f.endNode();

	if (waypointsWarning) {
		g_gui.PopupDialog(g_gui.root, "Warning", "Waypoints were saved, but they are not supported in OTBM 2!\nIf your map fails to load, consider removing all waypoints and saving again.\n\nThis warning can be disabled in file->preferences.", wxOK);
	}
	return true;
}

static bool readFileContent(const wxString& filepath, std::string& content) {
	if (!wxFileExists(filepath)) {
		return false;
	}

	wxFile file(filepath, wxFile::read);
	if (!file.IsOpened()) {
		return false;
	}

	const wxFileOffset fileSize = file.Length();
	if (fileSize < 0) {
		return false;
	}

	content.resize(static_cast<size_t>(fileSize));
	if (content.empty()) {
		return true;
	}

	const auto bytesRead = file.Read(content.data(), content.size());
	return static_cast<size_t>(bytesRead) == content.size();
}

static bool readNormalizedLineEndingChar(const std::string& content, size_t& offset, char& out) {
	if (offset >= content.size()) {
		return false;
	}

	out = content[offset++];
	if (out == '\r') {
		if (offset < content.size() && content[offset] == '\n') {
			++offset;
		}
		out = '\n';
	}
	return true;
}

static bool contentMatchesIgnoringLineEndings(const std::string& left, const std::string& right) {
	size_t leftOffset = 0;
	size_t rightOffset = 0;
	char leftChar = '\0';
	char rightChar = '\0';

	while (true) {
		const bool hasLeft = readNormalizedLineEndingChar(left, leftOffset, leftChar);
		const bool hasRight = readNormalizedLineEndingChar(right, rightOffset, rightChar);
		if (hasLeft != hasRight) {
			return false;
		}
		if (!hasLeft) {
			return true;
		}
		if (leftChar != rightChar) {
			return false;
		}
	}
}

static bool fileMatchesXmlContent(const wxString& filepath, const std::string& content) {
	std::string existingContent;
	if (!readFileContent(filepath, existingContent)) {
		return false;
	}

	if (existingContent == content) {
		return true;
	}

	return contentMatchesIgnoringLineEndings(existingContent, content);
}

static bool writeContentToFile(const wxString& filepath, const std::string& content) {
	wxFile file(filepath, wxFile::write);
	if (!file.IsOpened()) {
		return false;
	}

	if (!content.empty()) {
		const auto bytesWritten = file.Write(content.data(), content.size());
		if (static_cast<size_t>(bytesWritten) != content.size()) {
			return false;
		}
	}
	return file.Close();
}

static bool saveXmlFileIfChanged(const pugi::xml_document& doc, const wxString& filepath) {
	std::ostringstream stream;
	doc.save(stream, "\t", pugi::format_default, pugi::encoding_utf8);
	const std::string content = stream.str();

	if (fileMatchesXmlContent(filepath, content)) {
		return true;
	}

	const wxString backupPath = filepath + "~";
	if (!wxFileExists(filepath) && fileMatchesXmlContent(backupPath, content)) {
		return wxRenameFile(backupPath, filepath, false);
	}

	return writeContentToFile(filepath, content);
}

template <typename FillFn>
static bool saveSidecarXml(const FileName& dir, const std::string& filename, FillFn fill) {
	wxString filepath = dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME);
	filepath += wxString(filename.c_str(), wxConvUTF8);

	pugi::xml_document doc;
	if (fill(doc)) {
		return saveXmlFileIfChanged(doc, filepath);
	}
	return false;
}

bool IOMapOTBM::prependXmlDeclaration(pugi::xml_document& doc) {
	pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
	if (!decl) {
		return false;
	}
	decl.append_attribute("version") = "1.0";
	return true;
}

bool IOMapOTBM::saveSpawns(Map& map, const FileName& dir) {
	return saveSidecarXml(dir, map.spawnfile, [&](pugi::xml_document& doc) {
		return saveSpawns(map, doc);
	});
}

bool IOMapOTBM::saveSpawns(Map& map, pugi::xml_document& doc) {
	if (!prependXmlDeclaration(doc)) {
		return false;
	}

	CreatureList creatureList;

	pugi::xml_node spawnNodes = doc.append_child("spawns");
	for (const auto& spawnPosition : map.spawns) {
		Tile* tile = map.getTile(spawnPosition);
		if (tile == nullptr) {
			continue;
		}

		Spawn* spawn = tile->spawn;
		ASSERT(spawn);

		pugi::xml_node spawnNode = spawnNodes.append_child("spawn");

		spawnNode.append_attribute("centerx") = spawnPosition.x;
		spawnNode.append_attribute("centery") = spawnPosition.y;
		spawnNode.append_attribute("centerz") = spawnPosition.z;

		int32_t radius = spawn->getSize();
		spawnNode.append_attribute("radius") = radius;

		for (int32_t y = -radius; y <= radius; ++y) {
			for (int32_t x = -radius; x <= radius; ++x) {
				Tile* creature_tile = map.getTile(spawnPosition + Position(x, y, 0));
				if (creature_tile) {
					Creature* creature = creature_tile->creature;
					if (creature && !creature->isSaved()) {
						pugi::xml_node creatureNode = spawnNode.append_child(creature->isNpc() ? "npc" : "monster");

						creatureNode.append_attribute("name") = creature->getName().c_str();
						creatureNode.append_attribute("x") = x;
						creatureNode.append_attribute("y") = y;
						creatureNode.append_attribute("z") = spawnPosition.z;
						creatureNode.append_attribute("spawntime") = creature->getSpawnTime();
						// Weight is not serialized to XML (internal only, stored in OTBM)
						// Architecture note: Unlike RME which uses Tile::monsters (vector), this project uses Tile::creature (single pointer)
						if (creature->getDirection() != NORTH) {
							creatureNode.append_attribute("direction") = creature->getDirection();
						}

						// Mark as saved
						creature->save();
						creatureList.push_back(creature);
					}
				}
			}
		}
	}

	for (Creature* creature : creatureList) {
		creature->reset();
	}
	return true;
}

bool IOMapOTBM::saveHouses(Map& map, const FileName& dir) {
	return saveSidecarXml(dir, map.housefile, [&](pugi::xml_document& doc) {
		return saveHouses(map, doc);
	});
}

bool IOMapOTBM::saveHouses(Map& map, pugi::xml_document& doc) {
	if (!prependXmlDeclaration(doc)) {
		return false;
	}

	pugi::xml_node houseNodes = doc.append_child("houses");
	for (const auto& houseEntry : map.houses) {
		const House* house = houseEntry.second;
		pugi::xml_node houseNode = houseNodes.append_child("house");

		houseNode.append_attribute("name") = house->name.c_str();
		houseNode.append_attribute("houseid") = house->getID();

		const Position& exitPosition = house->getExit();
		houseNode.append_attribute("entryx") = exitPosition.x;
		houseNode.append_attribute("entryy") = exitPosition.y;
		houseNode.append_attribute("entryz") = exitPosition.z;

		houseNode.append_attribute("rent") = house->rent;
		if (house->guildhall) {
			houseNode.append_attribute("guildhall") = true;
		}

		houseNode.append_attribute("townid") = house->townid;
		houseNode.append_attribute("size") = static_cast<int32_t>(house->size());
	}
	return true;
}

bool IOMapOTBM::saveWaypoints(Map& map, const FileName& dir) {
	return saveSidecarXml(dir, map.waypointfile, [&](pugi::xml_document& doc) {
		return saveWaypoints(map, doc);
	});
}

bool IOMapOTBM::saveWaypoints(Map& map, pugi::xml_document& doc) {
	if (!prependXmlDeclaration(doc)) {
		return false;
	}

	pugi::xml_node houseNodes = doc.append_child("waypoints");
	for (const auto& houseEntry : map.houses) {
		const House* house = houseEntry.second;
		pugi::xml_node houseNode = houseNodes.append_child("waypoint");

		houseNode.append_attribute("name") = house->name.c_str();
		houseNode.append_attribute("id") = house->getID();
		houseNode.append_attribute("icon") = house->getID();

		const Position& exitPosition = house->getExit();
		houseNode.append_attribute("x") = exitPosition.x;
		houseNode.append_attribute("y") = exitPosition.y;
		houseNode.append_attribute("z") = exitPosition.z;

		houseNode.append_attribute("townid") = house->townid;
	}
	return true;
}

bool IOMapOTBM::saveZones(Map& map, const FileName& dir) {
	if (map.zonefile.empty()) {
		map.zonefile = nstr(dir.GetName()) + "-zones.xml";
	}

	wxString filepath = dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME);
	filepath += wxString(map.zonefile.c_str(), wxConvUTF8);

	bool hasZones = !map.zones.zones.empty();
	for (MapIterator miter = map.begin(); !hasZones && miter != map.end(); ++miter) {
		Tile* tile = (*miter)->get();
		hasZones = tile && tile->hasZone();
	}

	if (!hasZones && !wxFileExists(filepath)) {
		return true;
	}

	return saveSidecarXml(dir, map.zonefile, [&](pugi::xml_document& doc) {
		return saveZones(map, doc);
	});
}

bool IOMapOTBM::saveZones(Map& map, pugi::xml_document& doc) {
	if (!prependXmlDeclaration(doc)) {
		return false;
	}

	pugi::xml_node zoneNodes = doc.append_child("zones");
	std::set<unsigned int> zoneIds;
	for (const auto& zone : map.zones.zones) {
		if (zone.second != 0) {
			zoneIds.insert(zone.second);
		}
	}

	// Single map pass: collect, per zone id, the tile positions in iterator order.
	// (Previously this re-scanned the whole map once per zone -> O(tiles * zones).)
	std::map<unsigned int, std::vector<Position>> zonePositions;
	for (MapIterator miter = map.begin(); miter != map.end(); ++miter) {
		Tile* tile = (*miter)->get();
		if (!tile) {
			continue;
		}

		for (const auto& zoneId : tile->zones) {
			if (zoneId != 0) {
				zoneIds.insert(zoneId);
				zonePositions[zoneId].push_back(tile->getPosition());
			}
		}
	}

	for (const auto& zoneId : zoneIds) {
		std::string zoneName = "Zone " + i2s(zoneId);
		for (const auto& zone : map.zones.zones) {
			if (zone.second == zoneId) {
				zoneName = zone.first;
				break;
			}
		}

		pugi::xml_node zoneNode = zoneNodes.append_child("zone");
		zoneNode.append_attribute("name") = zoneName.c_str();
		zoneNode.append_attribute("id") = zoneId;
		zoneNode.append_attribute("zoneid") = zoneId;

		auto positionsIt = zonePositions.find(zoneId);
		if (positionsIt != zonePositions.end()) {
			for (const Position& position : positionsIt->second) {
				pugi::xml_node positionNode = zoneNode.append_child("position");
				positionNode.append_attribute("x") = position.x;
				positionNode.append_attribute("y") = position.y;
				positionNode.append_attribute("z") = position.z;
			}
		}
	}
	return true;
}
