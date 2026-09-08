//////////////////////////////////////////////////////////////////////
// Offset-preserving Lua lexical helpers shared by domain parsers.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_LUA_SOURCE_SCANNER_H_
#define NEXAMAP_LUA_SOURCE_SCANNER_H_

#include <algorithm>
#include <cctype>
#include <optional>
#include <string_view>

namespace LuaSource {
	struct LongBracket {
		std::size_t equals = 0;
		std::size_t contentBegin = 0;
	};

	inline std::optional<LongBracket> OpenLongBracket(std::string_view source, std::size_t offset) {
		if (offset >= source.size() || source[offset] != '[') {
			return std::nullopt;
		}
		std::size_t cursor = offset + 1;
		while (cursor < source.size() && source[cursor] == '=') {
			++cursor;
		}
		if (cursor >= source.size() || source[cursor] != '[') {
			return std::nullopt;
		}
		return LongBracket { cursor - offset - 1, cursor + 1 };
	}

	inline std::size_t LongBracketEnd(std::string_view source, std::size_t contentBegin, std::size_t equals) {
		for (std::size_t cursor = contentBegin; cursor < source.size(); ++cursor) {
			if (source[cursor] != ']') {
				continue;
			}
			std::size_t end = cursor + 1;
			std::size_t foundEquals = 0;
			while (end < source.size() && source[end] == '=') {
				++foundEquals;
				++end;
			}
			if (foundEquals == equals && end < source.size() && source[end] == ']') {
				return end + 1;
			}
		}
		return source.size();
	}

	inline std::size_t SkipTrivia(std::string_view source, std::size_t cursor) {
		while (cursor < source.size()) {
			if (std::isspace(static_cast<unsigned char>(source[cursor]))) {
				++cursor;
				continue;
			}
			if (cursor + 1 >= source.size() || source[cursor] != '-' || source[cursor + 1] != '-') {
				break;
			}
			cursor += 2;
			if (const auto bracket = OpenLongBracket(source, cursor)) {
				cursor = LongBracketEnd(source, bracket->contentBegin, bracket->equals);
			} else {
				const std::size_t end = source.find('\n', cursor);
				cursor = end == std::string_view::npos ? source.size() : end + 1;
			}
		}
		return cursor;
	}

	inline std::size_t QuotedStringEnd(std::string_view source, std::size_t cursor) {
		if (cursor >= source.size() || (source[cursor] != '\'' && source[cursor] != '"')) {
			return cursor;
		}
		const char quote = source[cursor++];
		while (cursor < source.size()) {
			if (source[cursor] == '\\' && cursor + 1 < source.size()) {
				cursor += 2;
			} else if (source[cursor++] == quote) {
				break;
			}
		}
		return cursor;
	}

	inline std::optional<std::size_t> BalancedEnd(std::string_view source, std::size_t open, char left, char right) {
		int depth = 0;
		for (std::size_t cursor = open; cursor < source.size();) {
			if (source[cursor] == '-' && cursor + 1 < source.size() && source[cursor + 1] == '-') {
				cursor = SkipTrivia(source, cursor);
				continue;
			}
			if (source[cursor] == '\'' || source[cursor] == '"') {
				cursor = QuotedStringEnd(source, cursor);
				continue;
			}
			if (const auto bracket = OpenLongBracket(source, cursor)) {
				cursor = LongBracketEnd(source, bracket->contentBegin, bracket->equals);
				continue;
			}
			if (source[cursor] == left) {
				++depth;
			} else if (source[cursor] == right && --depth == 0) {
				return cursor + 1;
			}
			++cursor;
		}
		return std::nullopt;
	}
}

#endif // NEXAMAP_LUA_SOURCE_SCANNER_H_
