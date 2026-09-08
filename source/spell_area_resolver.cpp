//////////////////////////////////////////////////////////////////////
// Workspace-specific resolution of server Lua combat area constants.
//////////////////////////////////////////////////////////////////////

#include "spell_area_resolver.h"

#include "lua_source_scanner.h"
#include "source_text_utils.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <optional>
#include <unordered_set>

namespace {
	std::string Trim(std::string_view value) {
		return SourceText::Trim(value);
	}

	bool IdentifierStart(char value) {
		return std::isalpha(static_cast<unsigned char>(value)) || value == '_';
	}

	bool IdentifierPart(char value) {
		return std::isalnum(static_cast<unsigned char>(value)) || value == '_';
	}

	std::size_t SkipSpaceAndComments(std::string_view text, std::size_t cursor) {
		return LuaSource::SkipTrivia(text, cursor);
	}

	std::optional<std::size_t> BalancedEnd(std::string_view text, std::size_t open, char left, char right) {
		return LuaSource::BalancedEnd(text, open, left, right);
	}

	std::vector<SpellAreaResolver::Definition> ParseDefinitions(const std::filesystem::path& path, std::string_view text) {
		std::vector<SpellAreaResolver::Definition> result;
		for (std::size_t cursor = 0; cursor < text.size();) {
			cursor = SkipSpaceAndComments(text, cursor);
			if (cursor >= text.size()) {
				break;
			}
			if (const auto bracket = LuaSource::OpenLongBracket(text, cursor)) {
				cursor = LuaSource::LongBracketEnd(text, bracket->contentBegin, bracket->equals);
				continue;
			}
			if (text[cursor] == '\'' || text[cursor] == '"') {
				cursor = LuaSource::QuotedStringEnd(text, cursor);
				continue;
			}
			if (!IdentifierStart(text[cursor])) {
				++cursor;
				continue;
			}
			const std::size_t nameBegin = cursor++;
			while (cursor < text.size() && IdentifierPart(text[cursor])) {
				++cursor;
			}
			const std::string name(text.substr(nameBegin, cursor - nameBegin));
			cursor = SkipSpaceAndComments(text, cursor);
			if (cursor >= text.size() || text[cursor] != '=') {
				continue;
			}
			cursor = SkipSpaceAndComments(text, cursor + 1);
			const std::size_t expressionBegin = cursor;
			std::size_t expressionEnd = cursor;
			if (cursor < text.size() && text[cursor] == '{') {
				expressionEnd = BalancedEnd(text, cursor, '{', '}').value_or(cursor);
			} else if (const auto bracket = LuaSource::OpenLongBracket(text, cursor)) {
				expressionEnd = LuaSource::LongBracketEnd(text, bracket->contentBegin, bracket->equals);
			} else if (cursor < text.size() && IdentifierStart(text[cursor])) {
				while (cursor < text.size() && (IdentifierPart(text[cursor]) || text[cursor] == '.')) {
					++cursor;
				}
				expressionEnd = cursor;
				const std::size_t callCursor = SkipSpaceAndComments(text, cursor);
				if (callCursor < text.size() && text[callCursor] == '(') {
					expressionEnd = BalancedEnd(text, callCursor, '(', ')').value_or(callCursor);
				}
			}
			if (expressionEnd > expressionBegin && (name.find("AREA") != std::string::npos || text[expressionBegin] == '{')) {
				result.push_back({ name, Trim(text.substr(expressionBegin, expressionEnd - expressionBegin)), path });
			}
			cursor = std::max(cursor + 1, expressionEnd);
		}
		return result;
	}

	std::vector<MonsterAreaTile> ParseMatrix(std::string_view expression) {
		std::vector<std::vector<int>> rows;
		std::vector<int> row;
		int depth = 0;
		for (std::size_t cursor = 0; cursor < expression.size();) {
			if (expression[cursor] == '{') {
				if (++depth == 2) {
					row.clear();
				}
				++cursor;
			} else if (expression[cursor] == '}') {
				if (depth == 2 && !row.empty()) {
					rows.push_back(row);
				}
				--depth;
				++cursor;
			} else if (depth == 2 && (std::isdigit(static_cast<unsigned char>(expression[cursor])) || expression[cursor] == '-')) {
				const std::size_t begin = cursor++;
				while (cursor < expression.size() && std::isdigit(static_cast<unsigned char>(expression[cursor]))) {
					++cursor;
				}
				int number = 0;
				const auto parsed = std::from_chars(expression.data() + begin, expression.data() + cursor, number);
				if (parsed.ec == std::errc()) {
					row.push_back(number);
				}
			} else {
				++cursor;
			}
		}
		if (rows.empty()) {
			return {};
		}
		int centerX = -1;
		int centerY = -1;
		for (std::size_t y = 0; y < rows.size(); ++y) {
			for (std::size_t x = 0; x < rows[y].size(); ++x) {
				if (rows[y][x] == 2 || rows[y][x] == 3) {
					centerX = static_cast<int>(x);
					centerY = static_cast<int>(y);
				}
			}
		}
		if (centerX < 0) {
			centerY = static_cast<int>(rows.size() / 2);
			centerX = static_cast<int>(rows[centerY].size() / 2);
		}
		std::vector<MonsterAreaTile> tiles;
		for (std::size_t y = 0; y < rows.size(); ++y) {
			for (std::size_t x = 0; x < rows[y].size(); ++x) {
				if (rows[y][x] == 1 || rows[y][x] == 3) {
					tiles.push_back({ static_cast<int>(x) - centerX, static_cast<int>(y) - centerY });
				}
			}
		}
		return tiles;
	}

	std::string UnwrapCreateCombatArea(std::string expression) {
		expression = Trim(expression);
		const std::string call = "createCombatArea";
		if (expression.starts_with(call)) {
			const auto open = expression.find('(');
			if (open != std::string::npos) {
				const auto close = BalancedEnd(expression, open, '(', ')');
				if (close) {
					return Trim(std::string_view(expression).substr(open + 1, *close - open - 2));
				}
			}
		}
		return expression;
	}
}

SpellAreaResolver::SpellAreaResolver(const ServerWorkspace& workspace) {
	std::vector<std::filesystem::path> roots;
	const auto add = [&](const std::filesystem::path& path) {
		std::error_code error;
		if (!path.empty() && std::filesystem::is_directory(path, error)) {
			roots.push_back(path.lexically_normal());
		}
	};
	add(workspace.activeDataDirectory / "scripts" / "lib");
	add(workspace.activeDataDirectory / "lib");
	for (const char* data : { "data", "data-crystal", "data-global" }) {
		add(workspace.rootPath / data / "scripts" / "lib");
		add(workspace.rootPath / data / "lib");
	}
	std::sort(roots.begin(), roots.end());
	roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
	std::size_t count = 0;
	for (const auto& root : roots) {
		std::error_code error;
		for (std::filesystem::recursive_directory_iterator iterator(root, std::filesystem::directory_options::skip_permission_denied, error), end;
			 iterator != end && count < 2500; iterator.increment(error)) {
			if (error) {
				error.clear();
				continue;
			}
			if (!iterator->is_regular_file(error) || iterator->path().extension() != ".lua") {
				continue;
			}
			++count;
			++scanStats.filesDiscovered;
			if (const auto bytes = SourceText::ReadBoundedFile(iterator->path(), 8 * 1024 * 1024)) {
				++scanStats.filesRead;
				auto parsed = ParseDefinitions(iterator->path(), *bytes);
				scanStats.definitionsParsed += parsed.size();
				definitions.insert(definitions.end(), std::make_move_iterator(parsed.begin()), std::make_move_iterator(parsed.end()));
			}
		}
	}
	for (std::size_t index = 0; index < definitions.size(); ++index) {
		definitionsByName[definitions[index].name].push_back(index);
	}
}

SpellAreaResolution SpellAreaResolver::resolve(std::string_view requested) const {
	std::string expression = UnwrapCreateCombatArea(Trim(requested));
	if (expression.empty()) {
		return { SpellAreaResolutionState::Single, {}, "Single target / no combat area", {} };
	}
	std::unordered_set<std::string> visited;
	std::filesystem::path source;
	for (int depth = 0; depth < 24; ++depth) {
		expression = UnwrapCreateCombatArea(expression);
		if (!expression.empty() && expression.front() == '{') {
			auto tiles = ParseMatrix(expression);
			if (!tiles.empty()) {
				return { SpellAreaResolutionState::Resolved, std::move(tiles), "Resolved " + std::to_string(tiles.size()) + " affected tiles", source };
			}
			return { SpellAreaResolutionState::Unresolved, {}, "Area geometry unavailable", source };
		}
		if (expression.empty() || !IdentifierStart(expression.front())
			|| !std::all_of(expression.begin(), expression.end(), IdentifierPart)) {
			return { SpellAreaResolutionState::Unresolved, {}, "Unresolved dynamic area", source };
		}
		if (!visited.insert(expression).second) {
			return { SpellAreaResolutionState::Unresolved, {}, "Ambiguous or cyclic area alias", source };
		}
		const auto matches = definitionsByName.find(expression);
		if (matches == definitionsByName.end() || matches->second.size() != 1) {
			return { SpellAreaResolutionState::Unresolved, {}, matches == definitionsByName.end() ? "Area geometry unavailable in active Server Workspace" : "Ambiguous area constant in active Server Workspace", {} };
		}
		const Definition& definition = definitions[matches->second.front()];
		expression = definition.expression;
		source = definition.path;
	}
	return { SpellAreaResolutionState::Unresolved, {}, "Area alias chain is too deep", source };
}

const SpellAreaResolverStats& SpellAreaResolver::stats() const {
	return scanStats;
}

const std::vector<SpellAreaResolver::Definition>& SpellAreaResolver::allDefinitions() const {
	return definitions;
}

std::vector<SpellAreaResolver::Definition> SpellAreaResolver::ParseSource(const std::filesystem::path& path, std::string_view text) {
	return ParseDefinitions(path, text);
}

std::vector<MonsterAreaTile> SpellAreaResolver::ParseLiteralMatrix(std::string_view expression) {
	return ParseMatrix(UnwrapCreateCombatArea(Trim(expression)));
}
