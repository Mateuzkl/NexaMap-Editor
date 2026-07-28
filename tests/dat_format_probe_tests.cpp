#include "dat_format_probe.h"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
	int failures = 0;

	void check(bool condition, std::string_view test) {
		if (!condition) {
			std::cerr << "FAILED: " << test << '\n';
			++failures;
		}
	}

	void appendU16(std::vector<uint8_t>& bytes, uint16_t value) {
		bytes.push_back(static_cast<uint8_t>(value));
		bytes.push_back(static_cast<uint8_t>(value >> 8));
	}

	void appendU32(std::vector<uint8_t>& bytes, uint32_t value) {
		bytes.push_back(static_cast<uint8_t>(value));
		bytes.push_back(static_cast<uint8_t>(value >> 8));
		bytes.push_back(static_cast<uint8_t>(value >> 16));
		bytes.push_back(static_cast<uint8_t>(value >> 24));
	}

	void appendThing(std::vector<uint8_t>& bytes, size_t sprite_id_bytes) {
		bytes.push_back(255);
		bytes.insert(bytes.end(), { 1, 1, 1, 1, 1, 1, 1 });
		if (sprite_id_bytes == sizeof(uint16_t)) {
			appendU16(bytes, 1);
		} else {
			appendU32(bytes, 1);
		}
	}

	std::vector<uint8_t> makeDat(size_t sprite_id_bytes, uint16_t outfit_count = 0, uint16_t effect_count = 0, uint16_t distance_count = 0, uint32_t signature = 0x4C2C7993) {
		std::vector<uint8_t> bytes;
		appendU32(bytes, signature);
		appendU16(bytes, 100);
		appendU16(bytes, outfit_count);
		appendU16(bytes, effect_count);
		appendU16(bytes, distance_count);

		const size_t record_count = 1
			+ static_cast<size_t>(outfit_count)
			+ static_cast<size_t>(effect_count)
			+ static_cast<size_t>(distance_count);
		for (size_t index = 0; index < record_count; ++index) {
			appendThing(bytes, sprite_id_bytes);
		}
		return bytes;
	}
}

int main() {
	using DatFormatProbe::SpriteIdWidth;

	const auto uint16_result = DatFormatProbe::probeTibia860(makeDat(sizeof(uint16_t), 1, 1, 1));
	check(uint16_result.success && uint16_result.sprite_id_width == SpriteIdWidth::UInt16, "detects a complete uint16 DAT");

	const auto uint32_result = DatFormatProbe::probeTibia860(makeDat(sizeof(uint32_t)));
	check(uint32_result.success && uint32_result.sprite_id_width == SpriteIdWidth::UInt32, "detects uint32 even when the first post-uint16 byte resembles a valid flag");

	const auto alternate_signature_result = DatFormatProbe::probeTibia860(makeDat(sizeof(uint16_t), 0, 0, 0, 0x4C6A4CBC));
	check(alternate_signature_result.success && alternate_signature_result.sprite_id_width == SpriteIdWidth::UInt16, "accepts the alternate supported Tibia 8.60 signature");

	auto truncated = makeDat(sizeof(uint32_t));
	truncated.pop_back();
	const auto truncated_result = DatFormatProbe::probeTibia860(truncated);
	check(!truncated_result.success && truncated_result.error.find("offset") != std::string::npos, "rejects a truncated DAT with offsets");

	auto unknown_flag = makeDat(sizeof(uint16_t));
	unknown_flag[12] = 34;
	const auto unknown_flag_result = DatFormatProbe::probeTibia860(unknown_flag);
	check(!unknown_flag_result.success && unknown_flag_result.error.find("flag 34") != std::string::npos, "rejects unknown Tibia 8.60 flags");

	auto trailing_bytes = makeDat(sizeof(uint16_t));
	trailing_bytes.push_back(0);
	const auto trailing_result = DatFormatProbe::probeTibia860(trailing_bytes);
	check(!trailing_result.success && trailing_result.error.find("trailing bytes") != std::string::npos, "validates all DAT sections through EOF");

	auto bad_signature = makeDat(sizeof(uint16_t));
	bad_signature[0] = 0;
	const auto signature_result = DatFormatProbe::probeTibia860(bad_signature);
	check(!signature_result.success && signature_result.error.find("signature") != std::string::npos, "rejects non-8.60 DAT signatures");

	if (failures != 0) {
		std::cerr << failures << " DAT format probe test(s) failed.\n";
		return 1;
	}
	std::cout << "7 DAT format probe tests passed.\n";
	return 0;
}
