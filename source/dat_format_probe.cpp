//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "dat_format_probe.h"

#include <array>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace DatFormatProbe {
	namespace {
		constexpr std::array<uint32_t, 2> TIBIA_860_DAT_SIGNATURES = {
			0x4C2C7993,
			0x4C6A4CBC,
		};
		constexpr uint16_t FIRST_ITEM_ID = 100;
		constexpr uint8_t LAST_FLAG = 255;
		constexpr uint8_t MARKET_FLAG = 33;
		constexpr size_t MAX_DAT_FILE_SIZE = 128ULL * 1024ULL * 1024ULL;
		constexpr size_t MAX_SPRITES_PER_THING = 65'536;

		struct ParseError {
			size_t offset = 0;
			std::string message;

			void set(size_t error_offset, std::string error_message) {
				if (message.empty()) {
					offset = error_offset;
					message = std::move(error_message);
				}
			}
		};

		class Reader {
		public:
			explicit Reader(std::span<const uint8_t> input) :
				bytes(input) {
			}

			size_t position() const {
				return offset;
			}

			size_t remaining() const {
				return bytes.size() - offset;
			}

			bool readU8(uint8_t& value) {
				if (remaining() < sizeof(value)) {
					return false;
				}
				value = bytes[offset++];
				return true;
			}

			bool readU16(uint16_t& value) {
				if (remaining() < sizeof(value)) {
					return false;
				}
				value = static_cast<uint16_t>(bytes[offset])
					| static_cast<uint16_t>(bytes[offset + 1] << 8);
				offset += sizeof(value);
				return true;
			}

			bool readU32(uint32_t& value) {
				if (remaining() < sizeof(value)) {
					return false;
				}
				value = static_cast<uint32_t>(bytes[offset])
					| (static_cast<uint32_t>(bytes[offset + 1]) << 8)
					| (static_cast<uint32_t>(bytes[offset + 2]) << 16)
					| (static_cast<uint32_t>(bytes[offset + 3]) << 24);
				offset += sizeof(value);
				return true;
			}

			bool skip(size_t count) {
				if (count > remaining()) {
					return false;
				}
				offset += count;
				return true;
			}

		private:
			std::span<const uint8_t> bytes;
			size_t offset = 0;
		};

		bool skipFlagPayload(Reader& reader, uint8_t flag, ParseError& error) {
			size_t payload_size = 0;
			switch (flag) {
				case 0: // Ground
				case 8: // Writable
				case 9: // WritableOnce
				case 25: // Elevation
				case 28: // Minimap
				case 29: // LensHelp
				case 32: // Cloth
					payload_size = sizeof(uint16_t);
					break;

				case 21: // Light
				case 24: // Displacement
					payload_size = sizeof(uint16_t) * 2;
					break;

				case MARKET_FLAG: {
					if (!reader.skip(6)) {
						error.set(reader.position(), "truncated market header");
						return false;
					}
					uint16_t name_length = 0;
					if (!reader.readU16(name_length)) {
						error.set(reader.position(), "truncated market name length");
						return false;
					}
					if (!reader.skip(static_cast<size_t>(name_length) + 4)) {
						error.set(reader.position(), "truncated market name or trade-as fields");
						return false;
					}
					return true;
				}

				default:
					return true;
			}

			if (!reader.skip(payload_size)) {
				error.set(reader.position(), "truncated DAT flag payload");
				return false;
			}
			return true;
		}

		bool multiplyDimension(size_t& product, uint8_t value, ParseError& error, size_t offset) {
			if (value == 0) {
				error.set(offset, "zero sprite dimension");
				return false;
			}
			if (product > MAX_SPRITES_PER_THING / value) {
				error.set(offset, "sprite dimension product exceeds the safety limit");
				return false;
			}
			product *= value;
			return true;
		}

		bool parseThing(Reader& reader, size_t sprite_id_bytes, ParseError& error) {
			bool reached_last_flag = false;
			for (size_t flag_count = 0; flag_count < LAST_FLAG; ++flag_count) {
				const size_t flag_offset = reader.position();
				uint8_t flag = 0;
				if (!reader.readU8(flag)) {
					error.set(flag_offset, "truncated DAT flags");
					return false;
				}
				if (flag == LAST_FLAG) {
					reached_last_flag = true;
					break;
				}
				if (flag > MARKET_FLAG) {
					error.set(flag_offset, "unknown Tibia 8.60 DAT flag " + std::to_string(flag));
					return false;
				}
				if (!skipFlagPayload(reader, flag, error)) {
					return false;
				}
			}

			if (!reached_last_flag) {
				error.set(reader.position(), "DAT flags do not terminate");
				return false;
			}

			const size_t dimensions_offset = reader.position();
			uint8_t width = 0;
			uint8_t height = 0;
			if (!reader.readU8(width) || !reader.readU8(height)) {
				error.set(dimensions_offset, "truncated sprite dimensions");
				return false;
			}
			if ((width > 1 || height > 1) && !reader.skip(1)) {
				error.set(reader.position(), "truncated exact-size byte");
				return false;
			}

			std::array<uint8_t, 5> dimensions {};
			for (uint8_t& dimension : dimensions) {
				if (!reader.readU8(dimension)) {
					error.set(reader.position(), "truncated sprite layout");
					return false;
				}
			}

			size_t sprite_count = 1;
			if (!multiplyDimension(sprite_count, width, error, dimensions_offset)
				|| !multiplyDimension(sprite_count, height, error, dimensions_offset + 1)) {
				return false;
			}
			for (uint8_t dimension : dimensions) {
				if (!multiplyDimension(sprite_count, dimension, error, reader.position())) {
					return false;
				}
			}

			if (sprite_count > (std::numeric_limits<size_t>::max)() / sprite_id_bytes
				|| !reader.skip(sprite_count * sprite_id_bytes)) {
				error.set(reader.position(), "truncated sprite ID list");
				return false;
			}
			return true;
		}

		bool parseLayout(std::span<const uint8_t> bytes, size_t sprite_id_bytes, ParseError& error) {
			Reader reader(bytes);
			uint32_t signature = 0;
			uint16_t item_count = 0;
			uint16_t outfit_count = 0;
			uint16_t effect_count = 0;
			uint16_t distance_effect_count = 0;
			if (!reader.readU32(signature)
				|| !reader.readU16(item_count)
				|| !reader.readU16(outfit_count)
				|| !reader.readU16(effect_count)
				|| !reader.readU16(distance_effect_count)) {
				error.set(reader.position(), "truncated DAT header");
				return false;
			}
			if (signature != TIBIA_860_DAT_SIGNATURES[0] && signature != TIBIA_860_DAT_SIGNATURES[1]) {
				std::ostringstream message;
				message << "unsupported DAT signature 0x" << std::hex << std::uppercase << signature
						<< "; expected Tibia 8.60 signature 0x" << TIBIA_860_DAT_SIGNATURES[0]
						<< " or 0x" << TIBIA_860_DAT_SIGNATURES[1];
				error.set(0, message.str());
				return false;
			}
			if (item_count < FIRST_ITEM_ID) {
				error.set(4, "invalid item count " + std::to_string(item_count));
				return false;
			}

			const size_t item_record_count = static_cast<size_t>(item_count) - FIRST_ITEM_ID + 1;
			const size_t record_count = item_record_count
				+ static_cast<size_t>(outfit_count)
				+ static_cast<size_t>(effect_count)
				+ static_cast<size_t>(distance_effect_count);

			for (size_t index = 0; index < record_count; ++index) {
				if (!parseThing(reader, sprite_id_bytes, error)) {
					if (!error.message.empty()) {
						error.message = "thing " + std::to_string(index + 1) + ": " + error.message;
					}
					return false;
				}
			}

			if (reader.remaining() != 0) {
				error.set(reader.position(), "unexpected trailing bytes after all DAT sections");
				return false;
			}
			return true;
		}

		std::string describeFailure(const ParseError& error) {
			return error.message + " at offset " + std::to_string(error.offset);
		}
	}

	Result probeTibia860(std::span<const uint8_t> bytes) {
		ParseError uint16_error;
		ParseError uint32_error;
		const bool uint16_valid = parseLayout(bytes, sizeof(uint16_t), uint16_error);
		const bool uint32_valid = parseLayout(bytes, sizeof(uint32_t), uint32_error);

		if (uint16_valid == uint32_valid) {
			Result result;
			if (uint16_valid) {
				result.error = "ambiguous DAT layout: both uint16 and uint32 sprite IDs parsed successfully";
			} else {
				result.error = "DAT does not match a complete Tibia 8.60 layout (uint16: "
					+ describeFailure(uint16_error)
					+ "; uint32: " + describeFailure(uint32_error) + ")";
			}
			return result;
		}

		Result result;
		result.success = true;
		result.sprite_id_width = uint32_valid ? SpriteIdWidth::UInt32 : SpriteIdWidth::UInt16;
		return result;
	}

	Result probeTibia860(const std::filesystem::path& path) {
		std::ifstream stream(path, std::ios::binary | std::ios::ate);
		if (!stream) {
			return { false, SpriteIdWidth::UInt16, "could not open '" + path.string() + "'" };
		}

		const std::streamoff end_position = stream.tellg();
		if (end_position <= 0) {
			return { false, SpriteIdWidth::UInt16, "DAT file is empty or its size could not be read" };
		}
		const auto file_size = static_cast<uint64_t>(end_position);
		if (file_size > MAX_DAT_FILE_SIZE) {
			return { false, SpriteIdWidth::UInt16, "DAT file exceeds the 128 MiB safety limit" };
		}

		std::vector<uint8_t> bytes(static_cast<size_t>(file_size));
		stream.seekg(0, std::ios::beg);
		stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
			return { false, SpriteIdWidth::UInt16, "DAT file could not be read completely" };
		}
		return probeTibia860(bytes);
	}
}
