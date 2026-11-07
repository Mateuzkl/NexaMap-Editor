#ifndef NEXAMAP_PATH_ENCODING_H
#define NEXAMAP_PATH_ENCODING_H

#include <filesystem>
#include <string>
#include <string_view>

namespace PathEncoding {

	inline std::filesystem::path FromUtf8(std::string_view value) {
		return std::filesystem::path(std::u8string(value.begin(), value.end()));
	}

	// Sidecar names stored in a map header are relative to the map's folder, and
	// the writers only ever store a bare file name. Anything else in the header is
	// file content, not a path to follow, so it is cut down to its last component:
	// both separators count on every platform because the map may come from
	// another one. Returns empty when no usable name is left.
	inline std::string SidecarBaseName(std::string_view value) {
		value = value.substr(0, value.find('\0'));
#ifdef _WIN32
		const std::string_view separators = "/\\:";
#else
		const std::string_view separators = "/\\";
#endif
		if (const size_t separator = value.find_last_of(separators); separator != std::string_view::npos) {
			value.remove_prefix(separator + 1);
		}
		if (value == "." || value == "..") {
			return {};
		}
		return std::string(value);
	}

} // namespace PathEncoding
#endif
