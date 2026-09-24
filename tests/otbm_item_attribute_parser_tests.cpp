#include "otbm_item_attribute_parser.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
	class ByteReader {
	public:
		explicit ByteReader(std::vector<uint8_t> bytes) : data(std::move(bytes)) { }

		bool getU8(uint8_t& value) {
			return read(value);
		}
		bool getU16(uint16_t& value) {
			return read(value);
		}
		bool getU32(uint32_t& value) {
			return read(value);
		}
		bool getU64(uint64_t& value) {
			return read(value);
		}
		bool skip(size_t count) {
			if (offset + count > data.size()) {
				offset = data.size();
				return false;
			}
			offset += count;
			return true;
		}
		size_t tell() const {
			return offset;
		}
		bool seek(size_t target) {
			if (target > data.size()) {
				return false;
			}
			offset = target;
			return true;
		}

	private:
		template <typename T>
		bool read(T& value) {
			if (offset + sizeof(T) > data.size()) {
				offset = data.size();
				return false;
			}
			std::memcpy(&value, data.data() + offset, sizeof(T));
			offset += sizeof(T);
			return true;
		}

		std::vector<uint8_t> data;
		size_t offset = 0;
	};

	int failures = 0;
	int checks = 0;

	void check(bool condition, const std::string& message) {
		++checks;
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	}

	template <typename T>
	void append(std::vector<uint8_t>& bytes, T value) {
		const auto* first = reinterpret_cast<const uint8_t*>(&value);
		bytes.insert(bytes.end(), first, first + sizeof(T));
	}

	void appendString(std::vector<uint8_t>& bytes, const std::string& value) {
		append<uint16_t>(bytes, static_cast<uint16_t>(value.size()));
		bytes.insert(bytes.end(), value.begin(), value.end());
	}

	void appendFixedAttribute(std::vector<uint8_t>& bytes, uint8_t attribute, size_t payloadSize) {
		append<uint8_t>(bytes, attribute);
		bytes.insert(bytes.end(), payloadSize, 0x5A);
	}

	void appendStringAttribute(std::vector<uint8_t>& bytes, uint8_t attribute, const std::string& value) {
		append<uint8_t>(bytes, attribute);
		appendString(bytes, value);
	}

	void appendCanaryCustomMap(std::vector<uint8_t>& bytes, uint8_t attribute) {
		append<uint8_t>(bytes, attribute);
		append<uint64_t>(bytes, 4);

		appendString(bytes, "string");
		append<uint8_t>(bytes, 1);
		appendString(bytes, "value");

		appendString(bytes, "integer");
		append<uint8_t>(bytes, 2);
		append<uint64_t>(bytes, 42);

		appendString(bytes, "double");
		append<uint8_t>(bytes, 3);
		append<uint64_t>(bytes, 0);

		appendString(bytes, "boolean");
		append<uint8_t>(bytes, 4);
		append<uint8_t>(bytes, 1);
	}

	void testCanaryModernAttributesStayAligned() {
		using namespace OTBMItemAttributeParser;
		std::vector<uint8_t> bytes;
		appendFixedAttribute(bytes, STORE, 8);
		appendFixedAttribute(bytes, WRITTEN_DATE, 8);
		appendStringAttribute(bytes, NAME, "teleporter");
		appendStringAttribute(bytes, ARTICLE, "a");
		appendStringAttribute(bytes, PLURAL_NAME, "teleporters");
		appendFixedAttribute(bytes, WEIGHT, 4);
		appendFixedAttribute(bytes, ATTACK, 4);
		appendFixedAttribute(bytes, DEFENSE, 4);
		appendFixedAttribute(bytes, EXTRA_DEFENSE, 4);
		appendFixedAttribute(bytes, ARMOR, 4);
		appendFixedAttribute(bytes, HIT_CHANCE, 1);
		appendFixedAttribute(bytes, SHOOT_RANGE, 1);
		appendStringAttribute(bytes, SPECIAL, "metadata");
		appendFixedAttribute(bytes, IMBUEMENT_SLOT, 4);
		appendFixedAttribute(bytes, OPEN_CONTAINER, 1);
		appendCanaryCustomMap(bytes, CUSTOM_ATTRIBUTES);
		appendFixedAttribute(bytes, QUICK_LOOT_CONTAINER, 4);
		appendFixedAttribute(bytes, AMOUNT, 2);
		appendFixedAttribute(bytes, CANARY_TIER, 1);
		appendCanaryCustomMap(bytes, CUSTOM);
		appendStringAttribute(bytes, STORE_INBOX_CATEGORY, "store");
		appendFixedAttribute(bytes, OWNER, 4);
		appendFixedAttribute(bytes, OBTAIN_CONTAINER, 4);
		appendFixedAttribute(bytes, TELEPORT_DESTINATION, 5);
		appendFixedAttribute(bytes, HOUSE_DOOR_ID, 1);
		appendFixedAttribute(bytes, DEPOT_ID, 2);

		ByteReader reader(std::move(bytes));
		const auto hints = inspect(&reader, true, false, 1);
		check(hints.hasTeleportDestination, "Canary scanner reaches teleport destination after modern attributes");
		check(hints.hasHouseDoorId, "Canary scanner reaches house door ID after modern attributes");
		check(hints.hasDepotId, "Canary scanner reaches depot ID after modern attributes");
		check(reader.tell() == 0, "Canary inspection restores the original node offset");
	}

	void testLegacyTfsLayoutIsUnchanged() {
		using namespace OTBMItemAttributeParser;
		std::vector<uint8_t> bytes;
		appendFixedAttribute(bytes, LEGACY_PODIUM_OUTFIT, 15);
		appendFixedAttribute(bytes, LEGACY_TIER, 1);
		append<uint8_t>(bytes, LEGACY_ATTRIBUTE_MAP);
		append<uint16_t>(bytes, 1);
		appendString(bytes, "key");
		append<uint8_t>(bytes, 1); // legacy string
		append<uint32_t>(bytes, 5);
		bytes.insert(bytes.end(), 5, 'v');
		appendFixedAttribute(bytes, TELEPORT_DESTINATION, 5);

		ByteReader reader(std::move(bytes));
		const auto hints = inspect(&reader, false, false, 1);
		check(hints.hasTeleportDestination, "legacy scanner still skips 15-byte podium and 1-byte tier payloads");
		check(reader.tell() == 0, "legacy inspection restores the original node offset");
	}

	void testInvalidModernPayloadStopsSafely() {
		using namespace OTBMItemAttributeParser;
		std::vector<uint8_t> bytes;
		append<uint8_t>(bytes, CUSTOM);
		append<uint64_t>(bytes, 1'000'001);
		appendFixedAttribute(bytes, TELEPORT_DESTINATION, 5);

		ByteReader reader(std::move(bytes));
		const auto hints = inspect(&reader, true, false, 1);
		check(!hints.hasTeleportDestination, "malformed custom map cannot desynchronise into a false teleport hint");
		check(reader.tell() == 0, "failed inspection also restores the original node offset");
	}
}

int main() {
	testCanaryModernAttributesStayAligned();
	testLegacyTfsLayoutIsUnchanged();
	testInvalidModernPayloadStopsSafely();

	std::cout << "OTBM Item Attribute Parser Tests: " << checks << " checks, " << failures << " failures.\n";
	return failures == 0 ? 0 : 1;
}
