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
#include "profiling.h"

#include <wx/file.h>

#include <zlib.h>

#include <limits>
#include <new>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>

#include <wx/choicdlg.h>

#include "settings.h"
#include "gui.h" // Loadbar
#include "file_transaction.h"
#include "spawn_format.h"

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

namespace {
	struct SpecialItemAttributeHints {
		bool hasTeleportDestination = false;
		bool hasHouseDoorId = false;
		bool hasDepotId = false;
	};

	bool skipAttributeMapValue(BinaryNode* stream) {
		uint8_t type = ItemAttribute::NONE;
		if (!stream->getU8(type)) {
			return false;
		}
		switch (static_cast<ItemAttribute::Type>(type)) {
			case ItemAttribute::STRING: {
				std::string ignored;
				return stream->getLongString(ignored);
			}
			case ItemAttribute::INTEGER:
			case ItemAttribute::FLOAT:
				return stream->skip(4);
			case ItemAttribute::DOUBLE:
				return stream->skip(8);
			case ItemAttribute::BOOLEAN:
				return stream->skip(1);
			case ItemAttribute::NONE:
				return true;
			default:
				return false;
		}
	}

	bool skipAttributeMap(BinaryNode* stream) {
		uint16_t count = 0;
		if (!stream->getU16(count)) {
			return false;
		}
		for (uint16_t index = 0; index < count; ++index) {
			std::string key;
			if (!stream->getString(key) || !skipAttributeMapValue(stream)) {
				return false;
			}
		}
		return true;
	}

	SpecialItemAttributeHints inspectSpecialItemAttributes(BinaryNode* stream, const MapVersion& version, const ItemType& itemType) {
		SpecialItemAttributeHints hints;
		const size_t originalOffset = stream->tell();

		// OTBM 1 stores the subtype immediately after the ID instead of as an
		// attribute. Newer formats, including 8.60, start attributes here.
		if (version.otbm == MAP_OTBM_1 && (itemType.stackable || itemType.isSplash() || itemType.isFluidContainer())) {
			stream->skip(1);
		}

		uint8_t rawAttribute = 0;
		while (stream->getU8(rawAttribute)) {
			const auto attribute = static_cast<OTBM_ItemAttribute>(rawAttribute);
			bool valid = true;
			switch (attribute) {
				case OTBM_ATTR_TELE_DEST:
					hints.hasTeleportDestination = true;
					valid = stream->skip(5);
					break;
				case OTBM_ATTR_HOUSEDOORID:
					hints.hasHouseDoorId = true;
					valid = stream->skip(1);
					break;
				case OTBM_ATTR_DEPOT_ID:
					hints.hasDepotId = true;
					valid = stream->skip(2);
					break;
				case OTBM_ATTR_COUNT:
				case OTBM_ATTR_RUNE_CHARGES:
				case OTBM_ATTR_DECAYING_STATE:
				case OTBM_ATTR_TIER:
					valid = stream->skip(1);
					break;
				case OTBM_ATTR_ACTION_ID:
				case OTBM_ATTR_UNIQUE_ID:
				case OTBM_ATTR_CHARGES:
					valid = stream->skip(2);
					break;
				case OTBM_ATTR_DURATION:
				case OTBM_ATTR_WRITTENDATE:
				case OTBM_ATTR_SLEEPERGUID:
				case OTBM_ATTR_SLEEPSTART:
					valid = stream->skip(4);
					break;
				case OTBM_ATTR_TEXT:
				case OTBM_ATTR_DESC:
				case OTBM_ATTR_WRITTENBY: {
					std::string ignored;
					valid = stream->getString(ignored);
					break;
				}
				case OTBM_ATTR_PODIUMOUTFIT:
					valid = stream->skip(15);
					break;
				case OTBM_ATTR_ATTRIBUTE_MAP:
					valid = skipAttributeMap(stream);
					break;
				default:
					valid = false;
					break;
			}
			if (!valid) {
				break;
			}
		}

		stream->seek(originalOffset);
		return hints;
	}
}

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

Item* Item::Create_OTBM(const IOMap& maphandle, BinaryNode* stream, const ItemType** itemType, bool inspectSpecialAttributes) {
	if (itemType) {
		*itemType = nullptr;
	}

	uint16_t storedId;
	uint16_t _id;
	if (!stream->getU16(storedId) || !maphandle.decodeStoredItemId(storedId, _id)) {
		return nullptr;
	}

	const ItemType& iType = g_items[_id];
	if (itemType) {
		*itemType = &iType;
	}

	const SpecialItemAttributeHints specialAttributes = inspectSpecialAttributes ? inspectSpecialItemAttributes(stream, maphandle.version, iType) : SpecialItemAttributeHints {};

	uint8_t _count = 0;

	if (maphandle.version.otbm == MAP_OTBM_1) {
		if (iType.stackable || iType.isSplash() || iType.isFluidContainer()) {
			stream->getU8(_count);
		}
	}
	// Converted IDs can point at a plain ItemType in the active items.otb. The
	// persisted OTBM attributes are authoritative: retain the dynamic type so a
	// reopen/save cycle cannot discard Destination, House Door ID, or the
	// Depot/Town association.
	if (specialAttributes.hasTeleportDestination) {
		return newd Teleport(_id);
	}
	if (specialAttributes.hasHouseDoorId) {
		return newd Door(_id);
	}
	if (specialAttributes.hasDepotId) {
		return newd Depot(_id);
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
			if (!hasSubtypeKind(SUBTYPE_STACK_COUNT) && !hasSubtypeKind(SUBTYPE_FLUID)) {
				addSubtypeKind(SUBTYPE_PERSISTED_COUNT_OR_FLUID);
			}
			addSubtypeAttribute(SUBTYPE_ATTR_COUNT);
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
			addSubtypeKind(SUBTYPE_CHARGES);
			addSubtypeAttribute(SUBTYPE_ATTR_CHARGES);
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
			addSubtypeKind(SUBTYPE_CHARGES);
			addSubtypeAttribute(SUBTYPE_ATTR_RUNE_CHARGES);
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
		if (shouldSerializeCountSubtype()) {
			stream.addU8(OTBM_ATTR_COUNT);
			stream.addU8(getSubtype());
		}
	}
	if (hasSubtypeAttribute(SUBTYPE_ATTR_CHARGES)) {
		stream.addU8(OTBM_ATTR_CHARGES);
		stream.addU16(getSubtype());
	}
	if (hasSubtypeAttribute(SUBTYPE_ATTR_RUNE_CHARGES)) {
		stream.addU8(OTBM_ATTR_RUNE_CHARGES);
		stream.addU8(getSubtype());
	}

	if (maphandle.version.otbm >= MAP_OTBM_4) {
		if (attributes && !attributes->empty()) {
			stream.addU8(OTBM_ATTR_ATTRIBUTE_MAP);
			serializeAttributeMap(maphandle, stream);
		}
	} else {
		if (subtypeAttributes == SUBTYPE_ATTR_NONE && g_items.MinorVersion >= CLIENT_VERSION_820 && hasSubtypeKind(SUBTYPE_CHARGES)) {
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

bool Item::serializeItemCompact_OTBM(const IOMap& maphandle, NodeFileWriteHandle& stream) const {
	uint16_t storedId;
	if (!maphandle.encodeStoredItemId(id, storedId) || !stream.addU16(storedId)) {
		return false;
	}

	/* This is impossible
	const ItemType& iType = g_items[id];

	if(iType.stackable || iType.isSplash() || iType.isFluidContainer()){
		stream.addU8(getSubtype());
	}
	*/
	return true;
}

bool Item::serializeItemNode_OTBM(const IOMap& maphandle, NodeFileWriteHandle& file) const {
	uint16_t storedId;
	if (!maphandle.encodeStoredItemId(id, storedId)) {
		return false;
	}
	file.addNode(OTBM_ITEM);
	file.addU16(storedId);
	if (maphandle.version.otbm == MAP_OTBM_1) {
		if (shouldSerializeCountSubtype()) {
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
	return doorId;
}

void Door::setDoorID(uint8_t id) {
	doorId = id;
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

			Item* item = Item::Create_OTBM(maphandle, child, nullptr, true);
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
	uint16_t storedId;
	if (!maphandle.encodeStoredItemId(id, storedId)) {
		return false;
	}
	file.addNode(OTBM_ITEM);
	file.addU16(storedId);
	if (maphandle.version.otbm == MAP_OTBM_1) {
		// In the ludicrous event that an item is a container AND stackable, we have to do this. :p
		if (shouldSerializeCountSubtype()) {
			file.addU8(getSubtype());
		}
	}

	serializeItemAttributes_OTBM(maphandle, file);
	for (Item* item : contents) {
		if (!item->serializeItemNode_OTBM(maphandle, file)) {
			return false;
		}
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

static bool checkOtbmMemoryBudget(const OTBMMemoryBudgetCheck& memoryBudgetCheck, const char* phase, uint64_t pendingBytes, std::string& readError) {
	return !memoryBudgetCheck || memoryBudgetCheck(phase, pendingBytes, readError);
}

static bool readOtbmBytes(const FileName& filename, std::vector<uint8_t>& output, std::string& readError, const OTBMMemoryBudgetCheck& memoryBudgetCheck = {}) {
	constexpr size_t MAX_OTBM_SIZE = 512ull * 1024 * 1024;

	FileReadHandle file(nstr(filename.GetFullPath()));
	if (!file.isOk()) {
		readError = "Couldn't open file for reading: " + file.getErrorMessage();
		return false;
	}

	const size_t fileSize = file.size();
	if (fileSize < 2) {
		readError = "Could not read OTBM file header.";
		return false;
	}
	if (fileSize > MAX_OTBM_SIZE) {
		readError = "OTBM input exceeds the 512 MiB safety limit.";
		return false;
	}
	if (!checkOtbmMemoryBudget(memoryBudgetCheck, "before reading the OTBM input", fileSize, readError)) {
		return false;
	}
	std::vector<uint8_t> bytes;
	try {
		bytes.resize(fileSize);
	} catch (const std::bad_alloc&) {
		readError = "Not enough memory to read the OTBM file.";
		return false;
	} catch (const std::length_error&) {
		readError = "The OTBM file is too large to fit in memory.";
		return false;
	}
	if (!file.getRAW(bytes.data(), bytes.size())) {
		readError = "Couldn't read file: " + file.getErrorMessage();
		return false;
	}
	if (bytes[0] != 0x1F || bytes[1] != 0x8B) {
		output = std::move(bytes);
		return true;
	}
	if (bytes.size() > std::numeric_limits<uInt>::max()) {
		readError = "Compressed OTBM input is too large for the native gzip decoder.";
		return false;
	}

	z_stream stream {};
	stream.next_in = bytes.data();
	stream.avail_in = static_cast<uInt>(bytes.size());
	const int initResult = inflateInit2(&stream, MAX_WBITS + 16);
	if (initResult != Z_OK) {
		readError = "Could not initialize the native gzip decoder.";
		return false;
	}

	constexpr size_t OUTPUT_CHUNK_SIZE = 1024 * 1024;
	std::vector<uint8_t> inflated;
	int inflateResult = Z_OK;
	do {
		if (inflated.size() >= MAX_OTBM_SIZE) {
			inflateEnd(&stream);
			readError = "Decompressed OTBM exceeds the 512 MiB safety limit.";
			return false;
		}
		const size_t offset = inflated.size();
		const size_t chunkSize = std::min(OUTPUT_CHUNK_SIZE, MAX_OTBM_SIZE - offset);
		if (!checkOtbmMemoryBudget(memoryBudgetCheck, "while decompressing the OTBM input", chunkSize, readError)) {
			inflateEnd(&stream);
			return false;
		}
		try {
			inflated.resize(offset + chunkSize);
		} catch (const std::bad_alloc&) {
			inflateEnd(&stream);
			readError = "Not enough memory to decompress the OTBM file.";
			return false;
		} catch (const std::length_error&) {
			inflateEnd(&stream);
			readError = "Decompressed OTBM is too large to fit in memory.";
			return false;
		}
		stream.next_out = inflated.data() + offset;
		stream.avail_out = static_cast<uInt>(chunkSize);
		inflateResult = inflate(&stream, Z_NO_FLUSH);
		const size_t produced = chunkSize - stream.avail_out;
		inflated.resize(offset + produced);
		if (inflateResult != Z_OK && inflateResult != Z_STREAM_END) {
			const std::string decoderMessage = stream.msg ? stream.msg : "invalid or truncated gzip stream";
			inflateEnd(&stream);
			readError = "Could not decompress gzip OTBM: " + decoderMessage + ".";
			return false;
		}
	} while (inflateResult != Z_STREAM_END);
	inflateEnd(&stream);

	output = std::move(inflated);
	return true;
}

bool IOMapOTBM::getVersionInfo(const FileName& filename, MapVersion& out_ver, uint32_t* itemMajorVersion, const OTBMMemoryBudgetCheck& memoryBudgetCheck) {
	std::vector<uint8_t> otbmBuffer;
	std::string readError;
	if (!readOtbmBytes(filename, otbmBuffer, readError, memoryBudgetCheck) || !hasValidOtbmPrefix(otbmBuffer.data(), otbmBuffer.size())) {
		return false;
	}
	if (!checkOtbmMemoryBudget(memoryBudgetCheck, "before parsing the OTBM header", otbmBuffer.size(), readError)) {
		return false;
	}

	MemoryNodeFileReadHandle f(otbmBuffer.data() + 4, otbmBuffer.size() - 4);
	return getVersionInfo(&f, out_ver, itemMajorVersion);
}

bool IOMapOTBM::getVersionInfo(NodeFileReadHandle* f, MapVersion& out_ver, uint32_t* itemMajorVersion) {
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
	if (!root->getU32(u32)) { // OTB major version
		return false;
	}
	if (itemMajorVersion) {
		*itemMajorVersion = u32;
	}

	if (!root->getU32(u32)) { // OTB minor version
		return false;
	}

	out_ver.client = ClientVersionID(u32);
	return true;
}

bool IOMapOTBM::checkMemoryBudget(const char* phase, uint64_t pendingBytes) {
	if (!memoryBudgetCheck) {
		return true;
	}
	std::string budgetError;
	if (memoryBudgetCheck(phase, pendingBytes, budgetError)) {
		return true;
	}
	error(wxstr(budgetError).wc_str());
	return false;
}

bool IOMapOTBM::loadMapData(Map& map, const FileName& filename) {
	if (!checkMemoryBudget("before loading the OTBM")) {
		return false;
	}

	// Parse from one in-memory buffer. Crystal may store gzip data under the
	// regular .otbm extension; decompression is read-only and never changes save.
	std::vector<uint8_t> otbmBuffer;
	std::string readError;
	if (!readOtbmBytes(filename, otbmBuffer, readError, memoryBudgetCheck)) {
		error(wxstr(readError).wc_str());
		return false;
	}
	const size_t otbmSize = otbmBuffer.size();
	if (!checkMemoryBudget("before parsing the OTBM", otbmSize)) {
		return false;
	}

	if (!hasValidOtbmPrefix(otbmBuffer.data(), otbmSize)) {
		error("File magic number not recognized.");
		return false;
	}

	MemoryNodeFileReadHandle f(otbmBuffer.data() + 4, otbmSize - 4);
	if (!loadMap(map, f)) {
		return false;
	}
	if (!checkMemoryBudget("after parsing the OTBM")) {
		return false;
	}
	map.mapVersion = version;
	return true;
}

bool IOMapOTBM::loadMap(Map& map, const FileName& filename) {
	if (!loadMapData(map, filename)) {
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

	const SpawnLoadStatus spawnStatus = loadSpawns(map, filename);
	if (spawnStatus == SpawnLoadStatus::Cancelled) {
		error("Map loading was cancelled while choosing spawn files.");
		return false;
	}
	if (spawnStatus == SpawnLoadStatus::Unavailable) {
		warning("Failed to load spawns.");
		map.spawnfile = nstr(filename.GetName()) + "-spawn.xml";
		map.spawnNpcFile.clear();
		map.spawnFormat = SpawnFormat::Tfs;
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
				if (!mapHeaderNode->getString(map.spawnNpcFile)) {
					warning("Invalid map NPC spawnfile tag");
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

bool IOMapOTBM::readTileArea(BinaryNode* mapNode, Map& map) {
	RME_PROFILE_SCOPE("IOMapOTBM::readTileArea");
	uint16_t base_x, base_y;
	uint8_t base_z;
	if (!mapNode->getU16(base_x) || !mapNode->getU16(base_y) || !mapNode->getU8(base_z)) {
		warning("Invalid map node, no base coordinate");
		return true;
	}

	Floor* cachedFloor = nullptr;
	int cachedFloorX = -1;
	int cachedFloorY = -1;
	int cachedFloorZ = -1;

	uint32_t tilesRead = 0;
	for (BinaryNode* tileNode = mapNode->getChild(); tileNode != nullptr; tileNode = tileNode->advance()) {
		if (++tilesRead % 2048 == 0 && !checkMemoryBudget("while parsing OTBM tiles")) {
			return false;
		}
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
					item = Item::Create_OTBM(*this, itemNode, &itemType, true);
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
	return true;
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
			if (!checkMemoryBudget("while parsing the OTBM map")) {
				return false;
			}
			if (!g_gui.SetLoadDone(std::min<int32_t>(99, static_cast<int32_t>(100.0 * f.tell() / f.size())))) {
				error("Map loading was cancelled.");
				return false;
			}
		}

		uint8_t node_type;
		if (!mapNode->getByte(node_type)) {
			warning("Invalid map node");
			continue;
		}
		if (node_type == OTBM_TILE_AREA) {
			if (!readTileArea(mapNode, map)) {
				return false;
			}
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

IOMapOTBM::SpawnLoadStatus IOMapOTBM::loadSpawns(Map& map, const FileName& dir) {
	const std::filesystem::path directory(nstr(dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME)));
	SpawnDetectionResult detection = SpawnFormatIO::Detect(directory, map.spawnfile, map.spawnNpcFile, nstr(dir.GetName()));
	if (detection.conflict) {
		static std::optional<SpawnFormat> sessionConflictChoice;
		if (!sessionConflictChoice.has_value()) {
			wxArrayString choices;
			choices.Add("TFS combined: " + wxstr(detection.primaryFile.string()));
			wxString canaryFiles = "Canary/Crystal split:";
			if (!detection.alternatePrimaryFile.empty()) {
				canaryFiles += "\n  monsters: " + wxstr(detection.alternatePrimaryFile.string());
			}
			if (!detection.alternateNpcFile.empty()) {
				canaryFiles += "\n  NPCs: " + wxstr(detection.alternateNpcFile.string());
			}
			choices.Add(canaryFiles);
			wxSingleChoiceDialog choiceDialog(
				g_gui.root,
				wxstr(detection.error) + "\n\nChoose the spawn set to load. This choice is remembered for this session.",
				"Multiple spawn formats found",
				choices
			);
			if (choiceDialog.ShowModal() != wxID_OK) {
				warnings.push_back("IOMapOTBM::loadSpawns: Spawn loading was cancelled because multiple formats were found.");
				return SpawnLoadStatus::Cancelled;
			}
			sessionConflictChoice = choiceDialog.GetSelection() == 0 ? detection.format : detection.alternateFormat;
		}
		if (*sessionConflictChoice == detection.alternateFormat) {
			detection.format = detection.alternateFormat;
			detection.primaryFile = detection.alternatePrimaryFile;
			detection.npcFile = detection.alternateNpcFile;
		}
		detection.conflict = false;
	}
	if (detection.format == SpawnFormat::Unknown) {
		warnings.push_back(wxstr("IOMapOTBM::loadSpawns: " + detection.error));
		return SpawnLoadStatus::Unavailable;
	}

	SpawnDocument document;
	std::string loadError;
	const SpawnLoadDefaults defaults {
		g_settings.getInteger(Config::DEFAULT_SPAWNTIME),
		static_cast<uint32_t>(std::max(1, g_settings.getInteger(Config::MONSTER_DEFAULT_WEIGHT))),
	};
	const bool loaded = SpawnFormatIO::Load(detection, document, loadError, defaults);
	if (!loaded) {
		warnings.push_back(wxstr("IOMapOTBM::loadSpawns: " + loadError));
		return SpawnLoadStatus::Unavailable;
	}

	map.spawnFormat = detection.format;
	map.spawnfile = detection.primaryFile.filename().string();
	map.spawnNpcFile = detection.format == SpawnFormat::CanaryCrystal && !detection.npcFile.empty()
		? detection.npcFile.filename().string()
		: std::string();
	for (const std::string& message : document.warnings) {
		warnings.push_back(wxstr(message));
	}

	std::vector<std::string> adapterWarnings;
	if (!SpawnMapAdapter::Apply(map, document, adapterWarnings)) {
		warnings.push_back("IOMapOTBM::loadSpawns: Failed to apply spawn data to the map.");
		return SpawnLoadStatus::Unavailable;
	}
	for (const std::string& message : adapterWarnings) {
		warnings.push_back(wxstr(message));
	}
	return SpawnLoadStatus::Loaded;
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

bool IOMapOTBM::saveMapData(Map& map, const FileName& identifier) {
	if (!checkMemoryBudget("before serializing the OTBM")) {
		return false;
	}
	const std::filesystem::path mapFile(nstr(identifier.GetFullPath()));
	DiskNodeFileWriteHandle file(
		mapFile.string(),
		(g_settings.getInteger(Config::SAVE_WITH_OTB_MAGIC_NUMBER) ? "OTBM" : std::string(4, '\0'))
	);
	if (!file.isOk()) {
		error("Can not open file %s for writing", mapFile.string().c_str());
		return false;
	}
	if (!saveMap(map, file) || !file.isOk()) {
		if (errorstr.empty()) {
			error("Could not write OTBM file %s", mapFile.string().c_str());
		}
		return false;
	}
	file.close();
	if (!checkMemoryBudget("after serializing the OTBM")) {
		return false;
	}

	MapVersion stagedVersion;
	if (!getVersionInfo(identifier, stagedVersion, nullptr, memoryBudgetCheck)) {
		error("Generated OTBM file failed validation: %s", mapFile.string().c_str());
		return false;
	}
	return true;
}

bool IOMapOTBM::saveMap(Map& map, const FileName& identifier) {
	const std::filesystem::path mapFile(nstr(identifier.GetFullPath()));
	const std::filesystem::path directory(nstr(identifier.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME)));
	const std::string mapName = nstr(identifier.GetName());
	std::error_code ec;
	if (!directory.empty()) {
		std::filesystem::create_directories(directory, ec);
	}
	if (ec) {
		error("Can not create map directory %s", directory.string().c_str());
		return false;
	}

	if (map.spawnFormat == SpawnFormat::CanaryCrystal) {
		if (map.spawnfile.empty()) {
			map.spawnfile = mapName + "-monster.xml";
		}
		if (map.spawnNpcFile.empty()) {
			map.spawnNpcFile = mapName + "-npc.xml";
		}
	} else {
		map.spawnFormat = SpawnFormat::Tfs;
		map.spawnNpcFile.clear();
		if (map.spawnfile.empty()) {
			map.spawnfile = mapName + "-spawn.xml";
		}
	}
	if (map.zonefile.empty()) {
		map.zonefile = mapName + "-zones.xml";
	}

	FileSaveTransaction transaction;
	const std::filesystem::path stagedMap = transaction.Stage(mapFile);
	if (!saveMapData(map, FileName(wxstr(stagedMap.string())))) {
		return false;
	}

	g_gui.SetLoadDone(99, "Saving spawns...");
	const SpawnDocument spawnDocument = SpawnMapAdapter::Capture(map);
	SpawnWriteResult spawnResult;
	const std::filesystem::path finalSpawn = directory / map.spawnfile;
	const std::filesystem::path stagedSpawn = transaction.Stage(finalSpawn);
	if (map.spawnFormat == SpawnFormat::CanaryCrystal) {
		const std::filesystem::path finalNpcSpawn = directory / map.spawnNpcFile;
		const std::filesystem::path stagedNpcSpawn = transaction.Stage(finalNpcSpawn);
		spawnResult = SpawnFormatIO::SaveCanaryCrystal(spawnDocument, stagedSpawn, stagedNpcSpawn);
	} else {
		spawnResult = SpawnFormatIO::SaveTfs(spawnDocument, stagedSpawn);
	}
	for (const std::string& message : spawnResult.warnings) {
		warnings.push_back(wxstr(message));
	}
	if (!spawnResult.success) {
		warnings.push_back(wxstr("IOMapOTBM::saveMap: " + spawnResult.error));
		return false;
	}

	auto stageXml = [&](pugi::xml_document& document, const std::filesystem::path& destination, const char* rootName) {
		const std::filesystem::path staged = transaction.Stage(destination);
		if (!document.save_file(staged.string().c_str(), "\t", pugi::format_default, pugi::encoding_utf8)) {
			warnings.push_back(wxstr("Could not stage " + destination.string() + "."));
			return false;
		}
		pugi::xml_document validation;
		const pugi::xml_parse_result parseResult = validation.load_file(staged.string().c_str());
		if (!parseResult || !validation.child(rootName)) {
			warnings.push_back(wxstr("Generated XML failed validation: " + destination.string() + "."));
			return false;
		}
		return true;
	};

	g_gui.SetLoadDone(99, "Saving houses...");
	if (!map.housefile.empty()) {
		pugi::xml_document houseDocument;
		if (!saveHouses(map, houseDocument) || !stageXml(houseDocument, directory / map.housefile, "houses")) {
			return false;
		}
	}

	g_gui.SetLoadDone(99, "Saving zones...");
	bool hasZones = !map.zones.zones.empty();
	for (MapIterator iterator = map.begin(); !hasZones && iterator != map.end(); ++iterator) {
		Tile* tile = (*iterator)->get();
		hasZones = tile && tile->hasZone();
	}
	const std::filesystem::path zoneFile = directory / map.zonefile;
	ec.clear();
	const bool zoneFileExists = std::filesystem::exists(zoneFile, ec);
	if (ec) {
		warnings.push_back(wxstr("Could not inspect zone file " + zoneFile.string() + ": " + ec.message()));
		return false;
	}
	if (hasZones || zoneFileExists) {
		pugi::xml_document zoneDocument;
		if (!saveZones(map, zoneDocument) || !stageXml(zoneDocument, zoneFile, "zones")) {
			return false;
		}
	}

	std::string commitError;
	if (!transaction.Commit(commitError)) {
		warnings.push_back(wxstr("IOMapOTBM::saveMap: " + commitError));
		return false;
	}
	return true;
}

bool IOMapOTBM::writeTiles(Map& map, NodeFileWriteHandle& f) {
	RME_PROFILE_SCOPE("IOMapOTBM::writeTiles");
	const IOMapOTBM& self = *this;
	uint32_t tiles_saved = 0;
	bool first = true;

	int local_x = -1, local_y = -1, local_z = -1;

	MapIterator map_iterator = map.begin();
	while (map_iterator != map.end()) {
		// Update progressbar
		++tiles_saved;
		if (tiles_saved % 2048 == 0 && !checkMemoryBudget("while serializing OTBM tiles")) {
			return false;
		}
		if (tiles_saved % 8192 == 0) {
			if (!g_gui.SetLoadDone(std::min<int32_t>(99, int(tiles_saved / double(map.getTileCount()) * 100.0)))) {
				error("Map saving was cancelled.");
				return false;
			}
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
					if (!ground->serializeItemNode_OTBM(self, f)) {
						return false;
					}
				}
			} else if (ground->isComplex() || ground->hasSubtype()) {
				if (!ground->serializeItemNode_OTBM(self, f)) {
					return false;
				}
			} else {
				f.addByte(OTBM_ATTR_ITEM);
				if (!ground->serializeItemCompact_OTBM(self, f)) {
					return false;
				}
			}
		}

		for (Item* item : save_tile->items) {
			if (!item->isMetaItem()) {
				if (!item->serializeItemNode_OTBM(self, f)) {
					return false;
				}
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
	return f.isOk();
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

		f.addU32(headerItemMajorVersion != 0 ? headerItemMajorVersion : g_items.MajorVersion);
		f.addU32(headerItemMinorVersion != 0 ? headerItemMinorVersion : g_items.MinorVersion);

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
			if (!map.spawnNpcFile.empty()) {
				tmpName.Assign(wxstr(map.spawnNpcFile));
				f.addU8(OTBM_ATTR_EXT_SPAWN_NPC_FILE);
				f.addString(nstr(tmpName.GetFullName()));
			}

			tmpName.Assign(wxstr(map.housefile));
			f.addU8(OTBM_ATTR_EXT_HOUSE_FILE);
			f.addString(nstr(tmpName.GetFullName()));

			// Start writing tiles
			if (!writeTiles(map, f)) {
				return false;
			}

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
	const std::filesystem::path directory(nstr(dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME)));
	const std::string mapName = nstr(dir.GetName());
	const SpawnDocument document = SpawnMapAdapter::Capture(map);
	SpawnWriteResult result;

	if (map.spawnFormat == SpawnFormat::CanaryCrystal) {
		if (map.spawnfile.empty()) {
			map.spawnfile = mapName + "-monster.xml";
		}
		if (map.spawnNpcFile.empty()) {
			map.spawnNpcFile = mapName + "-npc.xml";
		}
		result = SpawnFormatIO::SaveCanaryCrystal(document, directory / map.spawnfile, directory / map.spawnNpcFile);
	} else {
		map.spawnFormat = SpawnFormat::Tfs;
		map.spawnNpcFile.clear();
		if (map.spawnfile.empty()) {
			map.spawnfile = mapName + "-spawn.xml";
		}
		result = SpawnFormatIO::SaveTfs(document, directory / map.spawnfile);
	}

	for (const std::string& message : result.warnings) {
		warnings.push_back(wxstr(message));
	}
	if (!result.success) {
		warnings.push_back(wxstr("IOMapOTBM::saveSpawns: " + result.error));
		return false;
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
