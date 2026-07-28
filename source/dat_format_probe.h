//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_DAT_FORMAT_PROBE_H_
#define RME_DAT_FORMAT_PROBE_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace DatFormatProbe {
	enum class SpriteIdWidth : uint8_t {
		UInt16 = sizeof(uint16_t),
		UInt32 = sizeof(uint32_t),
	};

	struct Result {
		bool success = false;
		SpriteIdWidth sprite_id_width = SpriteIdWidth::UInt16;
		std::string error;

		std::size_t spriteIdBytes() const {
			return static_cast<std::size_t>(sprite_id_width);
		}
	};

	Result probeTibia860(std::span<const uint8_t> bytes);
	Result probeTibia860(const std::filesystem::path& path);
}

#endif // RME_DAT_FORMAT_PROBE_H_
