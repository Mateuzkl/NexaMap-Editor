//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_OTBM_ITEM_ATTRIBUTE_PARSER_H_
#define RME_OTBM_ITEM_ATTRIBUTE_PARSER_H_

#include <cstddef>
#include <cstdint>

namespace OTBMItemAttributeParser {
	enum Attribute : uint8_t {
		STORE = 1,
		ACTION_ID = 4,
		UNIQUE_ID = 5,
		TEXT = 6,
		DESCRIPTION = 7,
		TELEPORT_DESTINATION = 8,
		DEPOT_ID = 10,
		RUNE_CHARGES = 12,
		HOUSE_DOOR_ID = 14,
		COUNT = 15,
		DURATION = 16,
		DECAYING_STATE = 17,
		WRITTEN_DATE = 18,
		WRITTEN_BY = 19,
		SLEEPER_GUID = 20,
		SLEEP_START = 21,
		CHARGES = 22,
		CONTAINER_ITEMS = 23,
		NAME = 24,
		ARTICLE = 25,
		PLURAL_NAME = 26,
		WEIGHT = 27,
		ATTACK = 28,
		DEFENSE = 29,
		EXTRA_DEFENSE = 30,
		ARMOR = 31,
		HIT_CHANCE = 32,
		SHOOT_RANGE = 33,
		SPECIAL = 34,
		IMBUEMENT_SLOT = 35,
		OPEN_CONTAINER = 36,
		CUSTOM_ATTRIBUTES = 37,
		QUICK_LOOT_CONTAINER = 38,
		AMOUNT = 39,
		CANARY_TIER = 40,
		CUSTOM = 41,
		STORE_INBOX_CATEGORY = 42,
		OWNER = 43,
		OBTAIN_CONTAINER = 44,
		MANTRA = 45,

		LEGACY_PODIUM_OUTFIT = 40,
		LEGACY_TIER = 41,
		LEGACY_ATTRIBUTE_MAP = 128,
	};

	struct SpecialItemAttributeHints {
		bool hasTeleportDestination = false;
		bool hasHouseDoorId = false;
		bool hasDepotId = false;
		bool malformed = false;
		uint8_t malformedAttribute = 0;
	};

	template <typename Reader>
	bool skipString(Reader* stream) {
		uint16_t length = 0;
		return stream->getU16(length) && stream->skip(length);
	}

	template <typename Reader>
	bool skipLegacyAttributeMapValue(Reader* stream) {
		uint8_t type = 0;
		if (!stream->getU8(type)) {
			return false;
		}

		switch (type) {
			case 0: // none
				return true;
			case 1: { // string
				uint32_t length = 0;
				return stream->getU32(length) && stream->skip(length);
			}
			case 2: // integer
			case 3: // float
				return stream->skip(4);
			case 4: // boolean
				return stream->skip(1);
			case 5: // double
				return stream->skip(8);
			default:
				return false;
		}
	}

	template <typename Reader>
	bool skipLegacyAttributeMap(Reader* stream) {
		uint16_t count = 0;
		if (!stream->getU16(count)) {
			return false;
		}
		for (uint16_t index = 0; index < count; ++index) {
			if (!skipString(stream) || !skipLegacyAttributeMapValue(stream)) {
				return false;
			}
		}
		return true;
	}

	template <typename Reader>
	bool skipCanaryCustomValue(Reader* stream) {
		uint8_t type = 0;
		if (!stream->getU8(type)) {
			return false;
		}

		switch (type) {
			case 1: // string
				return skipString(stream);
			case 2: // int64
			case 3: // double
				return stream->skip(8);
			case 4: // bool
				return stream->skip(1);
			default:
				return false;
		}
	}

	template <typename Reader>
	bool skipCanaryCustomMap(Reader* stream) {
		uint64_t count = 0;
		if (!stream->getU64(count)) {
			return false;
		}

		// A persisted item cannot reasonably contain this many custom fields. The
		// bound also prevents a malformed count from turning inspection into a very
		// long loop before the reader reaches the end of its node.
		constexpr uint64_t MAX_CUSTOM_ATTRIBUTES = 1'000'000;
		if (count > MAX_CUSTOM_ATTRIBUTES) {
			return false;
		}

		for (uint64_t index = 0; index < count; ++index) {
			if (!skipString(stream) || !skipCanaryCustomValue(stream)) {
				return false;
			}
		}
		return true;
	}

	template <typename Reader>
	bool skipLegacyAttribute(Reader* stream, uint8_t attribute, size_t persistedCountSize, SpecialItemAttributeHints& hints) {
		switch (attribute) {
			case TELEPORT_DESTINATION:
				hints.hasTeleportDestination = true;
				return stream->skip(5);
			case HOUSE_DOOR_ID:
				hints.hasHouseDoorId = true;
				return stream->skip(1);
			case DEPOT_ID:
				hints.hasDepotId = true;
				return stream->skip(2);
			case COUNT:
				return stream->skip(persistedCountSize);
			case RUNE_CHARGES:
			case DECAYING_STATE:
			case LEGACY_TIER:
				return stream->skip(1);
			case ACTION_ID:
			case UNIQUE_ID:
			case CHARGES:
				return stream->skip(2);
			case DURATION:
			case WRITTEN_DATE:
			case SLEEPER_GUID:
			case SLEEP_START:
				return stream->skip(4);
			case TEXT:
			case DESCRIPTION:
			case WRITTEN_BY:
				return skipString(stream);
			case LEGACY_PODIUM_OUTFIT:
				return stream->skip(15);
			case LEGACY_ATTRIBUTE_MAP:
				return skipLegacyAttributeMap(stream);
			default:
				return false;
		}
	}

	template <typename Reader>
	bool skipCanaryAttribute(Reader* stream, uint8_t attribute, size_t persistedCountSize, SpecialItemAttributeHints& hints) {
		switch (attribute) {
			case STORE:
				return stream->skip(8);
			case TELEPORT_DESTINATION:
				hints.hasTeleportDestination = true;
				return stream->skip(5);
			case HOUSE_DOOR_ID:
				hints.hasHouseDoorId = true;
				return stream->skip(1);
			case DEPOT_ID:
				hints.hasDepotId = true;
				return stream->skip(2);
			case COUNT:
				return stream->skip(persistedCountSize);
			case RUNE_CHARGES:
			case DECAYING_STATE:
			case HIT_CHANCE:
			case SHOOT_RANGE:
			case OPEN_CONTAINER:
			case CANARY_TIER:
				return stream->skip(1);
			case ACTION_ID:
			case UNIQUE_ID:
			case CHARGES:
			case AMOUNT:
				return stream->skip(2);
			case DURATION:
			case SLEEPER_GUID:
			case SLEEP_START:
			case WEIGHT:
			case ATTACK:
			case DEFENSE:
			case EXTRA_DEFENSE:
			case ARMOR:
			case IMBUEMENT_SLOT:
			case QUICK_LOOT_CONTAINER:
			case OWNER:
			case OBTAIN_CONTAINER:
			case MANTRA:
				return stream->skip(4);
			case WRITTEN_DATE:
				return stream->skip(8);
			case TEXT:
			case DESCRIPTION:
			case WRITTEN_BY:
			case NAME:
			case ARTICLE:
			case PLURAL_NAME:
			case SPECIAL:
			case STORE_INBOX_CATEGORY:
				return skipString(stream);
			case CUSTOM_ATTRIBUTES:
			case CUSTOM:
				return skipCanaryCustomMap(stream);
			case LEGACY_ATTRIBUTE_MAP:
				// NexaMap OTBM 6 files may contain its legacy editor attribute map.
				// It remains unambiguous because Canary/Crystal does not use ID 128.
				return skipLegacyAttributeMap(stream);
			case CONTAINER_ITEMS:
				// This marker belongs to the server's separate house-item stream. It
				// is not a valid inline attribute in an OTBM item node.
				return false;
			default:
				return false;
		}
	}

	template <typename Reader>
	SpecialItemAttributeHints inspect(Reader* stream, bool canaryCrystal, bool hasOtbm1Subtype, size_t persistedCountSize) {
		SpecialItemAttributeHints hints;
		const size_t originalOffset = stream->tell();

		if (hasOtbm1Subtype && !stream->skip(persistedCountSize)) {
			stream->seek(originalOffset);
			return hints;
		}

		uint8_t attribute = 0;
		while (stream->getU8(attribute)) {
			const bool valid = canaryCrystal
				? skipCanaryAttribute(stream, attribute, persistedCountSize, hints)
				: skipLegacyAttribute(stream, attribute, persistedCountSize, hints);
			if (!valid) {
				hints.malformed = true;
				hints.malformedAttribute = attribute;
				break;
			}
		}

		stream->seek(originalOffset);
		return hints;
	}
}

#endif
