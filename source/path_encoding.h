#ifndef NEXAMAP_PATH_ENCODING_H
#define NEXAMAP_PATH_ENCODING_H

#include <filesystem>
#include <string>
#include <string_view>

namespace PathEncoding {

	inline std::filesystem::path FromUtf8(std::string_view value) {
		return std::filesystem::path(std::u8string(value.begin(), value.end()));
	}

} // namespace PathEncoding
#endif
