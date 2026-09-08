//////////////////////////////////////////////////////////////////////
// Small shared helpers for source-preserving server content parsers.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_SOURCE_TEXT_UTILS_H_
#define NEXAMAP_SOURCE_TEXT_UTILS_H_

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace SourceText {
	inline char AsciiLower(char value) {
		return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
	}

	inline std::string AsciiLower(std::string_view value) {
		std::string result(value);
		std::transform(result.begin(), result.end(), result.begin(), [](char character) { return AsciiLower(character); });
		return result;
	}

	inline bool AsciiCaseEqual(std::string_view left, std::string_view right) {
		return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), [](char first, char second) {
				   return AsciiLower(first) == AsciiLower(second);
			   });
	}

	inline std::string_view TrimView(std::string_view value) {
		while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
			value.remove_prefix(1);
		}
		while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
			value.remove_suffix(1);
		}
		return value;
	}

	inline std::string Trim(std::string_view value) {
		return std::string(TrimView(value));
	}

	inline std::string PathUtf8(const std::filesystem::path& path) {
		const auto utf8 = path.generic_u8string();
		return std::string(utf8.begin(), utf8.end());
	}

	inline std::optional<std::string> ReadBoundedFile(const std::filesystem::path& path, std::uintmax_t maximumBytes, std::string* error = nullptr) {
		std::error_code filesystemError;
		const std::uintmax_t size = std::filesystem::file_size(path, filesystemError);
		if (filesystemError || size > maximumBytes) {
			if (error) {
				*error = filesystemError ? "source size could not be read" : "source exceeds the configured size limit";
			}
			return std::nullopt;
		}
		std::ifstream stream(path, std::ios::binary);
		if (!stream) {
			if (error) {
				*error = "source could not be opened";
			}
			return std::nullopt;
		}
		std::string bytes(static_cast<std::size_t>(size), '\0');
		if (!bytes.empty()) {
			stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		}
		if (!stream) {
			if (error) {
				*error = "source could not be read completely";
			}
			return std::nullopt;
		}
		return bytes;
	}

	inline std::string_view Newline(std::string_view source) {
		return source.find("\r\n") == std::string_view::npos ? std::string_view("\n") : std::string_view("\r\n");
	}
}

#endif // NEXAMAP_SOURCE_TEXT_UTILS_H_
