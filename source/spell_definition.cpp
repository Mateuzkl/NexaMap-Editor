//////////////////////////////////////////////////////////////////////
// Source-preserving global spell model for Lua and XML workspaces.
//////////////////////////////////////////////////////////////////////

#include "spell_definition.h"

#include "ext/pugixml.hpp"
#include "file_transaction.h"
#include "spell_area_resolver.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace {
	constexpr std::size_t FieldCount = static_cast<std::size_t>(SpellField::Count);

	enum class ValueSyntax : uint8_t {
		LuaString,
		LuaNumber,
		LuaBoolean,
		LuaIdentifier,
		XmlString,
		XmlNumber,
		XmlBoolean,
	};

	struct SourceFile {
		std::filesystem::path path;
		ResourceFingerprint fingerprint;
		std::string bytes;
	};

	struct SourceLocation {
		std::size_t file = 0;
		std::size_t begin = 0;
		std::size_t end = 0;
		ValueSyntax syntax = ValueSyntax::LuaString;
		char quote = '"';
		std::string original;
	};

	enum class TokenKind : uint8_t {
		Identifier,
		Number,
		String,
		Symbol,
	};

	struct Token {
		TokenKind kind = TokenKind::Symbol;
		std::size_t begin = 0;
		std::size_t end = 0;
		std::size_t line = 1;
		std::string text;
		char quote = '\0';
	};

	struct XmlAttribute {
		std::string name;
		std::string value;
		std::size_t begin = 0;
		std::size_t end = 0;
		char quote = '"';
	};

	struct XmlTag {
		std::string name;
		std::size_t begin = 0;
		std::size_t end = 0;
		std::size_t line = 1;
		bool selfClosing = false;
		std::vector<XmlAttribute> attributes;
	};

	struct XmlVocationSource {
		std::string name;
		std::size_t file = 0;
		std::size_t begin = 0;
		std::size_t end = 0;
	};

	std::size_t Index(SpellField field) {
		return static_cast<std::size_t>(field);
	}

	std::string LowerAscii(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return value;
	}

	bool IsIdentifierStart(unsigned char character) {
		return std::isalpha(character) || character == '_';
	}

	bool IsIdentifierPart(unsigned char character) {
		return std::isalnum(character) || character == '_';
	}

	std::optional<std::string> ReadFile(const std::filesystem::path& path, std::string& error) {
		std::ifstream stream(path, std::ios::binary);
		if (!stream.is_open()) {
			error = "Could not open spell source " + path.string() + ".";
			return std::nullopt;
		}
		stream.seekg(0, std::ios::end);
		const std::streamoff length = stream.tellg();
		if (length < 0 || length > 32 * 1024 * 1024) {
			error = "Spell source is too large to edit safely: " + path.string() + ".";
			return std::nullopt;
		}
		stream.seekg(0, std::ios::beg);
		std::string bytes(static_cast<std::size_t>(length), '\0');
		if (!bytes.empty()) {
			stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		}
		if (!stream) {
			error = "Could not read spell source " + path.string() + ".";
			return std::nullopt;
		}
		return bytes;
	}

	bool WriteFile(const std::filesystem::path& path, std::string_view bytes, std::string& error) {
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		if (!stream.is_open()) {
			error = "Could not stage spell source " + path.string() + ".";
			return false;
		}
		stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		if (!stream) {
			error = "Could not write staged spell source " + path.string() + ".";
			return false;
		}
		return true;
	}

	std::string DecodeLuaString(std::string_view value) {
		std::string decoded;
		for (std::size_t index = 0; index < value.size(); ++index) {
			if (value[index] == '\\' && index + 1 < value.size()) {
				const char escaped = value[++index];
				switch (escaped) {
					case 'n':
						decoded.push_back('\n');
						break;
					case 'r':
						decoded.push_back('\r');
						break;
					case 't':
						decoded.push_back('\t');
						break;
					default:
						decoded.push_back(escaped);
						break;
				}
			} else {
				decoded.push_back(value[index]);
			}
		}
		return decoded;
	}

	std::string EncodeLuaString(std::string_view value, char quote) {
		std::string encoded;
		encoded.push_back(quote);
		for (const char character : value) {
			if (character == '\\' || character == quote) {
				encoded.push_back('\\');
				encoded.push_back(character);
			} else if (character == '\n') {
				encoded += "\\n";
			} else if (character == '\r') {
				encoded += "\\r";
			} else if (character == '\t') {
				encoded += "\\t";
			} else {
				encoded.push_back(character);
			}
		}
		encoded.push_back(quote);
		return encoded;
	}

	std::string DecodeXml(std::string_view value) {
		std::string decoded(value);
		const std::array<std::pair<std::string_view, std::string_view>, 5> entities { {
			{ "&amp;", "&" },
			{ "&lt;", "<" },
			{ "&gt;", ">" },
			{ "&quot;", "\"" },
			{ "&apos;", "'" },
		} };
		for (const auto& [entity, replacement] : entities) {
			for (std::size_t offset = 0; (offset = decoded.find(entity, offset)) != std::string::npos;) {
				decoded.replace(offset, entity.size(), replacement);
				offset += replacement.size();
			}
		}
		return decoded;
	}

	std::string EncodeXml(std::string_view value, char quote) {
		std::string encoded;
		for (const char character : value) {
			switch (character) {
				case '&':
					encoded += "&amp;";
					break;
				case '<':
					encoded += "&lt;";
					break;
				case '>':
					encoded += "&gt;";
					break;
				case '"':
					encoded += quote == '"' ? "&quot;" : "\"";
					break;
				case '\'':
					encoded += quote == '\'' ? "&apos;" : "'";
					break;
				default:
					encoded.push_back(character);
					break;
			}
		}
		return encoded;
	}

	std::vector<Token> TokenizeLua(std::string_view bytes, bool* valid = nullptr) {
		std::vector<Token> tokens;
		std::size_t line = 1;
		bool syntaxValid = true;
		for (std::size_t cursor = 0; cursor < bytes.size();) {
			const unsigned char character = static_cast<unsigned char>(bytes[cursor]);
			if (std::isspace(character)) {
				line += bytes[cursor] == '\n';
				++cursor;
				continue;
			}
			if (bytes.substr(cursor, 2) == "--") {
				if (bytes.substr(cursor, 4) == "--[[") {
					const std::size_t end = bytes.find("]]", cursor + 4);
					if (end == std::string_view::npos) {
						syntaxValid = false;
						break;
					}
					line += static_cast<std::size_t>(std::count(bytes.begin() + cursor, bytes.begin() + end + 2, '\n'));
					cursor = end + 2;
				} else {
					const std::size_t end = bytes.find('\n', cursor + 2);
					cursor = end == std::string_view::npos ? bytes.size() : end;
				}
				continue;
			}
			if (bytes.substr(cursor, 2) == "[[") {
				const std::size_t end = bytes.find("]]", cursor + 2);
				if (end == std::string_view::npos) {
					syntaxValid = false;
					break;
				}
				line += static_cast<std::size_t>(std::count(bytes.begin() + cursor, bytes.begin() + end + 2, '\n'));
				cursor = end + 2;
				continue;
			}
			if (character == '\'' || character == '"') {
				const std::size_t begin = cursor++;
				const char quote = static_cast<char>(character);
				std::string value;
				bool closed = false;
				while (cursor < bytes.size()) {
					if (bytes[cursor] == '\\' && cursor + 1 < bytes.size()) {
						value.append(bytes.substr(cursor, 2));
						cursor += 2;
						continue;
					}
					if (bytes[cursor] == quote) {
						++cursor;
						closed = true;
						break;
					}
					line += bytes[cursor] == '\n';
					value.push_back(bytes[cursor++]);
				}
				if (!closed) {
					syntaxValid = false;
					break;
				}
				tokens.push_back({ TokenKind::String, begin, cursor, line, DecodeLuaString(value), quote });
				continue;
			}
			if (IsIdentifierStart(character)) {
				const std::size_t begin = cursor++;
				while (cursor < bytes.size() && IsIdentifierPart(static_cast<unsigned char>(bytes[cursor]))) {
					++cursor;
				}
				tokens.push_back({ TokenKind::Identifier, begin, cursor, line, std::string(bytes.substr(begin, cursor - begin)), '\0' });
				continue;
			}
			if (std::isdigit(character) || (bytes[cursor] == '-' && cursor + 1 < bytes.size() && std::isdigit(static_cast<unsigned char>(bytes[cursor + 1])))) {
				const std::size_t begin = cursor++;
				while (cursor < bytes.size() && (std::isdigit(static_cast<unsigned char>(bytes[cursor])) || bytes[cursor] == '.')) {
					++cursor;
				}
				tokens.push_back({ TokenKind::Number, begin, cursor, line, std::string(bytes.substr(begin, cursor - begin)), '\0' });
				continue;
			}
			tokens.push_back({ TokenKind::Symbol, cursor, cursor + 1, line, std::string(1, bytes[cursor]), '\0' });
			++cursor;
		}
		if (valid) {
			*valid = syntaxValid;
		}
		return tokens;
	}

	std::optional<std::size_t> MatchingToken(const std::vector<Token>& tokens, std::size_t open, std::string_view left, std::string_view right) {
		int depth = 0;
		for (std::size_t index = open; index < tokens.size(); ++index) {
			if (tokens[index].text == left) {
				++depth;
			} else if (tokens[index].text == right && --depth == 0) {
				return index;
			}
		}
		return std::nullopt;
	}

	std::optional<int> ParseInt(std::string_view value) {
		int result = 0;
		const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
		return error == std::errc() && end == value.data() + value.size() ? std::optional<int>(result) : std::nullopt;
	}

	std::optional<bool> ParseBool(std::string value) {
		value = LowerAscii(std::move(value));
		if (value == "true" || value == "1" || value == "yes") {
			return true;
		}
		if (value == "false" || value == "0" || value == "no") {
			return false;
		}
		return std::nullopt;
	}

	std::vector<XmlTag> ScanXml(std::string_view bytes) {
		std::vector<XmlTag> tags;
		std::size_t line = 1;
		for (std::size_t cursor = 0; cursor < bytes.size();) {
			if (bytes[cursor] != '<') {
				line += bytes[cursor++] == '\n';
				continue;
			}
			const std::size_t begin = cursor;
			const std::size_t tagLine = line;
			if (bytes.substr(cursor, 4) == "<!--") {
				const std::size_t end = bytes.find("-->", cursor + 4);
				const std::size_t next = end == std::string_view::npos ? bytes.size() : end + 3;
				line += static_cast<std::size_t>(std::count(bytes.begin() + cursor, bytes.begin() + next, '\n'));
				cursor = next;
				continue;
			}
			++cursor;
			if (cursor >= bytes.size() || bytes[cursor] == '/' || bytes[cursor] == '?' || bytes[cursor] == '!') {
				const std::size_t end = bytes.find('>', cursor);
				const std::size_t next = end == std::string_view::npos ? bytes.size() : end + 1;
				line += static_cast<std::size_t>(std::count(bytes.begin() + begin, bytes.begin() + next, '\n'));
				cursor = next;
				continue;
			}
			while (cursor < bytes.size() && std::isspace(static_cast<unsigned char>(bytes[cursor]))) {
				line += bytes[cursor++] == '\n';
			}
			const std::size_t nameBegin = cursor;
			while (cursor < bytes.size() && (IsIdentifierPart(static_cast<unsigned char>(bytes[cursor])) || bytes[cursor] == '-')) {
				++cursor;
			}
			XmlTag tag;
			tag.name = LowerAscii(std::string(bytes.substr(nameBegin, cursor - nameBegin)));
			tag.begin = begin;
			tag.line = tagLine;
			while (cursor < bytes.size()) {
				while (cursor < bytes.size() && std::isspace(static_cast<unsigned char>(bytes[cursor]))) {
					line += bytes[cursor++] == '\n';
				}
				if (cursor >= bytes.size()) {
					break;
				}
				if (bytes[cursor] == '>') {
					tag.end = ++cursor;
					break;
				}
				if (bytes[cursor] == '/' && cursor + 1 < bytes.size() && bytes[cursor + 1] == '>') {
					tag.selfClosing = true;
					cursor += 2;
					tag.end = cursor;
					break;
				}
				const std::size_t attributeBegin = cursor;
				while (cursor < bytes.size() && (IsIdentifierPart(static_cast<unsigned char>(bytes[cursor])) || bytes[cursor] == '-')) {
					++cursor;
				}
				std::string attributeName = LowerAscii(std::string(bytes.substr(attributeBegin, cursor - attributeBegin)));
				while (cursor < bytes.size() && std::isspace(static_cast<unsigned char>(bytes[cursor]))) {
					++cursor;
				}
				if (cursor >= bytes.size() || bytes[cursor] != '=') {
					++cursor;
					continue;
				}
				++cursor;
				while (cursor < bytes.size() && std::isspace(static_cast<unsigned char>(bytes[cursor]))) {
					++cursor;
				}
				if (cursor >= bytes.size() || (bytes[cursor] != '"' && bytes[cursor] != '\'')) {
					continue;
				}
				const char quote = bytes[cursor++];
				const std::size_t valueBegin = cursor;
				while (cursor < bytes.size() && bytes[cursor] != quote) {
					++cursor;
				}
				const std::size_t valueEnd = cursor;
				if (cursor < bytes.size()) {
					++cursor;
				}
				tag.attributes.push_back({ std::move(attributeName), DecodeXml(bytes.substr(valueBegin, valueEnd - valueBegin)), valueBegin, valueEnd, quote });
			}
			if (!tag.name.empty() && tag.end > tag.begin) {
				tags.push_back(std::move(tag));
			}
		}
		return tags;
	}

	const XmlAttribute* FindAttribute(const XmlTag& tag, std::string_view name) {
		const auto found = std::find_if(tag.attributes.begin(), tag.attributes.end(), [name](const XmlAttribute& attribute) { return attribute.name == name; });
		return found == tag.attributes.end() ? nullptr : &*found;
	}

	std::string FieldValue(const SpellDefinition& definition, SpellField field) {
		switch (field) {
			case SpellField::Name:
				return definition.name;
			case SpellField::Words:
				return definition.words;
			case SpellField::Group:
				return definition.group;
			case SpellField::Script:
				return definition.script;
			case SpellField::CombatType:
				return definition.combatType;
			case SpellField::Effect:
				return definition.effect;
			case SpellField::Projectile:
				return definition.projectile;
			case SpellField::Area:
				return definition.areaExpression;
			case SpellField::SpellId:
				return std::to_string(definition.spellId);
			case SpellField::RuneId:
				return std::to_string(definition.runeId);
			case SpellField::Level:
				return std::to_string(definition.level);
			case SpellField::MagicLevel:
				return std::to_string(definition.magicLevel);
			case SpellField::Mana:
				return std::to_string(definition.mana);
			case SpellField::ManaPercent:
				return std::to_string(definition.manaPercent);
			case SpellField::Soul:
				return std::to_string(definition.soul);
			case SpellField::Cooldown:
				return std::to_string(definition.cooldown);
			case SpellField::GroupCooldown:
				return std::to_string(definition.groupCooldown);
			case SpellField::Range:
				return std::to_string(definition.range);
			case SpellField::Charges:
				return std::to_string(definition.charges);
			case SpellField::Premium:
				return definition.premium ? "true" : "false";
			case SpellField::Enabled:
				return definition.enabled ? "true" : "false";
			case SpellField::Aggressive:
				return definition.aggressive ? "true" : "false";
			case SpellField::NeedTarget:
				return definition.needTarget ? "true" : "false";
			case SpellField::NeedDirection:
				return definition.needDirection ? "true" : "false";
			case SpellField::BlockWalls:
				return definition.blockWalls ? "true" : "false";
			case SpellField::AllowFarUse:
				return definition.allowFarUse ? "true" : "false";
			case SpellField::NeedLearn:
				return definition.needLearn ? "true" : "false";
			case SpellField::SelfTarget:
				return definition.selfTarget ? "true" : "false";
			case SpellField::Count:
				break;
		}
		return {};
	}

	std::vector<MonsterAreaTile> ParseLiteralArea(std::string_view expression) {
		std::vector<std::vector<int>> rows;
		int depth = 0;
		std::vector<int> row;
		for (std::size_t cursor = 0; cursor < expression.size();) {
			if (expression[cursor] == '{') {
				++depth;
				if (depth == 2) {
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
				if (const auto value = ParseInt(expression.substr(begin, cursor - begin))) {
					row.push_back(*value);
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
}

struct SpellDefinitionDocument::Impl {
	ServerContentSource sourceInfo;
	SpellDefinition original;
	std::vector<SourceFile> files;
	std::array<std::vector<SourceLocation>, FieldCount> locations;
	std::size_t implementationFile = 0;
	std::optional<SourceLocation> luaVocationArguments;
	std::optional<SourceLocation> luaVocationStatement;
	std::vector<XmlVocationSource> xmlVocations;
	std::size_t vocationInsertFile = 0;
	std::size_t vocationInsertPosition = std::string::npos;
	std::string spellVariable;
	std::string xmlSpellTag;
	std::size_t xmlSpellEnd = 0;
	bool xmlSpellSelfClosing = false;
	std::shared_ptr<const SpellAreaResolver> areaResolver;

	void addLocation(SpellField field, SourceLocation location, const std::string& value) {
		auto& capability = original.capabilities[Index(field)];
		if (capability.present) {
			capability.editable = false;
			capability.limitation = "More than one source literal controls this field.";
			locations[Index(field)].push_back(std::move(location));
			return;
		}
		capability.present = true;
		capability.editable = true;
		capability.limitation.clear();
		locations[Index(field)].push_back(std::move(location));
		setValue(field, value);
	}

	void setValue(SpellField field, const std::string& value) {
		const auto number = ParseInt(value).value_or(0);
		const auto boolean = ParseBool(value).value_or(false);
		switch (field) {
			case SpellField::Name:
				original.name = value;
				break;
			case SpellField::Words:
				original.words = value;
				break;
			case SpellField::Group:
				original.group = value;
				break;
			case SpellField::Script:
				original.script = value;
				break;
			case SpellField::SpellId:
				original.spellId = number;
				break;
			case SpellField::RuneId:
				original.runeId = number;
				break;
			case SpellField::Level:
				original.level = number;
				break;
			case SpellField::MagicLevel:
				original.magicLevel = number;
				break;
			case SpellField::Mana:
				original.mana = number;
				break;
			case SpellField::ManaPercent:
				original.manaPercent = number;
				break;
			case SpellField::Soul:
				original.soul = number;
				break;
			case SpellField::Cooldown:
				original.cooldown = number;
				break;
			case SpellField::GroupCooldown:
				original.groupCooldown = number;
				break;
			case SpellField::Range:
				original.range = number;
				break;
			case SpellField::Charges:
				original.charges = number;
				break;
			case SpellField::Premium:
				original.premium = boolean;
				break;
			case SpellField::Enabled:
				original.enabled = boolean;
				break;
			case SpellField::Aggressive:
				original.aggressive = boolean;
				break;
			case SpellField::NeedTarget:
				original.needTarget = boolean;
				break;
			case SpellField::NeedDirection:
				original.needDirection = boolean;
				break;
			case SpellField::BlockWalls:
				original.blockWalls = boolean;
				break;
			case SpellField::AllowFarUse:
				original.allowFarUse = boolean;
				break;
			case SpellField::NeedLearn:
				original.needLearn = boolean;
				break;
			case SpellField::SelfTarget:
				original.selfTarget = boolean;
				break;
			case SpellField::CombatType:
				original.combatType = value;
				break;
			case SpellField::Effect:
				original.effect = value;
				break;
			case SpellField::Projectile:
				original.projectile = value;
				break;
			case SpellField::Area:
				original.areaExpression = value;
				break;
			case SpellField::Count:
				break;
		}
	}

	void addLuaToken(SpellField field, std::size_t file, const Token& token) {
		ValueSyntax syntax;
		if (token.kind == TokenKind::String) {
			syntax = ValueSyntax::LuaString;
		} else if (token.kind == TokenKind::Number) {
			syntax = ValueSyntax::LuaNumber;
		} else if (token.text == "true" || token.text == "false") {
			syntax = ValueSyntax::LuaBoolean;
		} else {
			syntax = ValueSyntax::LuaIdentifier;
		}
		addLocation(field, { file, token.begin, token.end, syntax, token.quote, token.text }, token.text);
	}

	bool parseLuaDeclaration(std::size_t file, std::string& error) {
		const auto tokens = TokenizeLua(files[file].bytes);
		struct Creation {
			std::size_t token;
			std::string variable;
			std::size_t line;
		};
		std::vector<Creation> creations;
		for (std::size_t i = 0; i + 4 < tokens.size(); ++i) {
			if (tokens[i].kind == TokenKind::Identifier && tokens[i + 1].text == "=" && tokens[i + 2].text == "Spell" && tokens[i + 3].text == "(") {
				creations.push_back({ i, tokens[i].text, tokens[i + 2].line });
			}
		}
		std::optional<std::size_t> selected;
		for (std::size_t i = 0; i < creations.size(); ++i) {
			if (sourceInfo.declarationLine && creations[i].line == sourceInfo.declarationLine) {
				selected = i;
				break;
			}
		}
		if (!selected && creations.size() == 1) {
			selected = 0;
		}
		if (!selected) {
			error = "Could not identify the indexed Spell declaration unambiguously at line " + std::to_string(sourceInfo.declarationLine) + ".";
			return false;
		}
		const Creation& creation = creations[*selected];
		spellVariable = creation.variable;
		const std::size_t begin = creation.token;
		const std::size_t end = *selected + 1 < creations.size() ? creations[*selected + 1].token : tokens.size();
		if (begin + 4 < tokens.size()) {
			original.subtype = tokens[begin + 4].text;
		}
		const std::array<std::pair<std::string_view, SpellField>, 23> methods { {
			{ "name", SpellField::Name },
			{ "words", SpellField::Words },
			{ "group", SpellField::Group },
			{ "id", SpellField::SpellId },
			{ "runeId", SpellField::RuneId },
			{ "level", SpellField::Level },
			{ "magicLevel", SpellField::MagicLevel },
			{ "mana", SpellField::Mana },
			{ "manaPercent", SpellField::ManaPercent },
			{ "soul", SpellField::Soul },
			{ "cooldown", SpellField::Cooldown },
			{ "groupCooldown", SpellField::GroupCooldown },
			{ "range", SpellField::Range },
			{ "charges", SpellField::Charges },
			{ "isPremium", SpellField::Premium },
			{ "isEnabled", SpellField::Enabled },
			{ "isAggressive", SpellField::Aggressive },
			{ "needTarget", SpellField::NeedTarget },
			{ "needDirection", SpellField::NeedDirection },
			{ "blockWalls", SpellField::BlockWalls },
			{ "allowFarUse", SpellField::AllowFarUse },
			{ "needLearn", SpellField::NeedLearn },
			{ "selfTarget", SpellField::SelfTarget },
		} };
		for (std::size_t i = begin; i + 4 < end; ++i) {
			if (tokens[i].text != creation.variable || (tokens[i + 1].text != ":" && tokens[i + 1].text != ".") || tokens[i + 3].text != "(") {
				continue;
			}
			const auto found = std::find_if(methods.begin(), methods.end(), [&](const auto& entry) { return entry.first == tokens[i + 2].text; });
			if (found == methods.end()) {
				continue;
			}
			const Token& argument = tokens[i + 4];
			if (argument.kind == TokenKind::String || argument.kind == TokenKind::Number || argument.text == "true" || argument.text == "false") {
				addLuaToken(found->second, file, argument);
			} else {
				auto& capability = original.capabilities[Index(found->second)];
				capability.present = true;
				capability.limitation = "The Lua value is computed rather than a direct literal.";
			}
		}

		std::vector<std::pair<std::size_t, std::size_t>> vocationCalls;
		for (std::size_t i = begin; i + 4 < end; ++i) {
			if (tokens[i].text != creation.variable || tokens[i + 1].text != ":" || tokens[i + 2].text != "vocation" || tokens[i + 3].text != "(") {
				continue;
			}
			if (const auto close = MatchingToken(tokens, i + 3, "(", ")")) {
				vocationCalls.push_back({ i, *close });
			}
		}
		if (vocationCalls.size() > 1) {
			original.vocationCapability = { SpellVocationCapabilityState::Ambiguous, false, "Multiple vocation declarations control this spell." };
		} else if (vocationCalls.size() == 1) {
			const auto [call, close] = vocationCalls.front();
			bool safe = true;
			for (std::size_t token = call + 4; token < close; ++token) {
				if (tokens[token].kind == TokenKind::String) {
					original.vocations.push_back(tokens[token].text);
				} else if (tokens[token].text != ",") {
					safe = false;
				}
			}
			if (safe) {
				luaVocationArguments = SourceLocation { file, tokens[call + 3].end, tokens[close].begin, ValueSyntax::LuaString, '"', {} };
				std::size_t statementBegin = tokens[call].begin;
				if (const std::size_t line = files[file].bytes.rfind('\n', statementBegin); line != std::string::npos
					&& files[file].bytes.substr(line + 1, statementBegin - line - 1).find_first_not_of(" \t\r") == std::string::npos) {
					statementBegin = line + 1;
				}
				std::size_t statementEnd = tokens[close].end;
				if (const std::size_t line = files[file].bytes.find('\n', statementEnd); line != std::string::npos
					&& files[file].bytes.substr(statementEnd, line - statementEnd).find_first_not_of(" \t\r") == std::string::npos) {
					statementEnd = line + 1;
				}
				luaVocationStatement = SourceLocation { file, statementBegin, statementEnd, ValueSyntax::LuaString, '"', {} };
				original.vocationCapability = { SpellVocationCapabilityState::ExistingEditableLiteral, true, {} };
			} else {
				original.vocations.clear();
				original.vocationCapability = { SpellVocationCapabilityState::DynamicReadOnly, false, "The vocation call contains computed arguments." };
			}
		} else {
			original.vocationCapability = { SpellVocationCapabilityState::SupportedInsertable, true, "No vocation call exists; this spell currently allows all vocations." };
		}
		for (std::size_t i = begin; i + 3 < end; ++i) {
			if (tokens[i].text == creation.variable && tokens[i + 1].text == ":" && tokens[i + 2].text == "register" && tokens[i + 3].text == "(") {
				vocationInsertFile = file;
				const std::size_t lineStart = files[file].bytes.rfind('\n', tokens[i].begin);
				vocationInsertPosition = lineStart == std::string::npos ? 0 : lineStart + 1;
				break;
			}
		}
		if (original.name.empty()) {
			error = "The indexed Lua spell has no supported literal name.";
			return false;
		}
		return true;
	}

	bool parseXmlDeclaration(std::size_t file, std::string& error) {
		const auto tags = ScanXml(files[file].bytes);
		std::vector<const XmlTag*> candidates;
		for (const XmlTag& tag : tags) {
			if (tag.name != "instant" && tag.name != "rune" && tag.name != "conjure") {
				continue;
			}
			const XmlAttribute* name = FindAttribute(tag, "name");
			if (name && name->value == sourceInfo.name) {
				candidates.push_back(&tag);
			}
		}
		const XmlTag* selected = nullptr;
		for (const XmlTag* candidate : candidates) {
			if (sourceInfo.declarationLine && candidate->line == sourceInfo.declarationLine) {
				selected = candidate;
				break;
			}
		}
		if (!selected && candidates.size() == 1) {
			selected = candidates.front();
		}
		if (!selected) {
			error = "Could not identify the indexed XML spell node unambiguously.";
			return false;
		}
		original.subtype = selected->name;
		xmlSpellTag = selected->name;
		xmlSpellEnd = selected->end;
		xmlSpellSelfClosing = selected->selfClosing;
		const auto add = [&](SpellField field, const XmlAttribute* attribute, ValueSyntax syntax) {
			if (attribute) {
				addLocation(field, { file, attribute->begin, attribute->end, syntax, attribute->quote, attribute->value }, attribute->value);
			}
		};
		add(SpellField::Name, FindAttribute(*selected, "name"), ValueSyntax::XmlString);
		add(SpellField::Words, FindAttribute(*selected, "words"), ValueSyntax::XmlString);
		add(SpellField::Group, FindAttribute(*selected, "group"), ValueSyntax::XmlString);
		add(SpellField::Script, FindAttribute(*selected, "script"), ValueSyntax::XmlString);
		add(SpellField::SpellId, FindAttribute(*selected, "spellid"), ValueSyntax::XmlNumber);
		add(SpellField::RuneId, FindAttribute(*selected, "id"), ValueSyntax::XmlNumber);
		add(SpellField::Level, FindAttribute(*selected, "lvl"), ValueSyntax::XmlNumber);
		add(SpellField::MagicLevel, FindAttribute(*selected, "maglv"), ValueSyntax::XmlNumber);
		add(SpellField::Mana, FindAttribute(*selected, "mana"), ValueSyntax::XmlNumber);
		add(SpellField::ManaPercent, FindAttribute(*selected, "manapercent"), ValueSyntax::XmlNumber);
		add(SpellField::Soul, FindAttribute(*selected, "soul"), ValueSyntax::XmlNumber);
		const XmlAttribute* cooldown = FindAttribute(*selected, "cooldown");
		add(SpellField::Cooldown, cooldown ? cooldown : FindAttribute(*selected, "exhaustion"), ValueSyntax::XmlNumber);
		add(SpellField::GroupCooldown, FindAttribute(*selected, "groupcooldown"), ValueSyntax::XmlNumber);
		add(SpellField::Range, FindAttribute(*selected, "range"), ValueSyntax::XmlNumber);
		add(SpellField::Charges, FindAttribute(*selected, "charges"), ValueSyntax::XmlNumber);
		add(SpellField::Premium, FindAttribute(*selected, "prem"), ValueSyntax::XmlBoolean);
		add(SpellField::Enabled, FindAttribute(*selected, "enabled"), ValueSyntax::XmlBoolean);
		add(SpellField::Aggressive, FindAttribute(*selected, "aggressive"), ValueSyntax::XmlBoolean);
		add(SpellField::NeedTarget, FindAttribute(*selected, "needtarget"), ValueSyntax::XmlBoolean);
		add(SpellField::NeedDirection, FindAttribute(*selected, "direction"), ValueSyntax::XmlBoolean);
		add(SpellField::BlockWalls, FindAttribute(*selected, "blockwalls"), ValueSyntax::XmlBoolean);
		add(SpellField::AllowFarUse, FindAttribute(*selected, "allowfaruse"), ValueSyntax::XmlBoolean);
		add(SpellField::NeedLearn, FindAttribute(*selected, "needlearn"), ValueSyntax::XmlBoolean);
		add(SpellField::SelfTarget, FindAttribute(*selected, "selftarget"), ValueSyntax::XmlBoolean);

		if (!selected->selfClosing) {
			const std::size_t closing = files[file].bytes.find("</" + selected->name, selected->end);
			const std::size_t nodeEnd = closing == std::string::npos ? selected->end : closing;
			vocationInsertFile = file;
			vocationInsertPosition = nodeEnd;
			for (const XmlTag& tag : tags) {
				if (tag.begin <= selected->end || tag.begin >= nodeEnd || tag.name != "vocation") {
					continue;
				}
				if (const XmlAttribute* name = FindAttribute(tag, "name")) {
					original.vocations.push_back(name->value);
					std::size_t removeBegin = tag.begin;
					const std::size_t lineStart = files[file].bytes.rfind('\n', tag.begin);
					if (lineStart != std::string::npos && files[file].bytes.substr(lineStart + 1, tag.begin - lineStart - 1).find_first_not_of(" \t\r") == std::string::npos) {
						removeBegin = lineStart + 1;
					}
					std::size_t removeEnd = tag.end;
					if (const std::size_t newline = files[file].bytes.find('\n', tag.end); newline != std::string::npos
						&& files[file].bytes.substr(tag.end, newline - tag.end).find_first_not_of(" \t\r") == std::string::npos) {
						removeEnd = newline + 1;
					}
					xmlVocations.push_back({ name->value, file, removeBegin, removeEnd });
				}
			}
			original.vocationCapability = { original.vocations.empty() ? SpellVocationCapabilityState::SupportedInsertable : SpellVocationCapabilityState::ExistingEditableLiteral, true, {} };
		} else {
			vocationInsertFile = file;
			vocationInsertPosition = selected->end >= 2 ? selected->end - 2 : selected->end;
			original.vocationCapability = { SpellVocationCapabilityState::SupportedInsertable, true, "This self-closing spell currently allows all vocations." };
		}
		original.allVocations = original.vocations.empty();
		return true;
	}

	void parseCombat(std::size_t file) {
		const auto tokens = TokenizeLua(files[file].bytes);
		const auto addUniqueParameter = [&](std::string_view parameter, SpellField field) {
			std::vector<const Token*> values;
			for (std::size_t i = 0; i + 4 < tokens.size(); ++i) {
				if (tokens[i].text == "setParameter" && tokens[i + 1].text == "(" && tokens[i + 2].text == parameter && tokens[i + 3].text == ",") {
					values.push_back(&tokens[i + 4]);
				}
			}
			if (values.size() == 1 && (values.front()->kind == TokenKind::Identifier || values.front()->kind == TokenKind::Number || values.front()->kind == TokenKind::String)) {
				addLuaToken(field, file, *values.front());
			} else if (!values.empty()) {
				auto& capability = original.capabilities[Index(field)];
				capability.present = true;
				capability.limitation = "The implementation contains multiple or computed values for this parameter.";
			}
		};
		addUniqueParameter("COMBAT_PARAM_TYPE", SpellField::CombatType);
		addUniqueParameter("COMBAT_PARAM_EFFECT", SpellField::Effect);
		addUniqueParameter("COMBAT_PARAM_DISTANCEEFFECT", SpellField::Projectile);

		std::vector<std::size_t> areas;
		for (std::size_t i = 0; i + 2 < tokens.size(); ++i) {
			if (tokens[i].text == "createCombatArea" && tokens[i + 1].text == "(") {
				areas.push_back(i);
			}
		}
		if (areas.size() == 1) {
			const std::size_t call = areas.front();
			const auto close = MatchingToken(tokens, call + 1, "(", ")");
			if (close && call + 2 < *close) {
				const Token& argument = tokens[call + 2];
				if (argument.kind == TokenKind::Identifier || argument.kind == TokenKind::Number) {
					addLuaToken(SpellField::Area, file, argument);
				} else if (argument.text == "{") {
					const std::size_t expressionEnd = tokens[*close - 1].end;
					original.areaExpression = files[file].bytes.substr(argument.begin, expressionEnd - argument.begin);
					original.customAreaTiles = ParseLiteralArea(original.areaExpression);
					auto& capability = original.capabilities[Index(SpellField::Area)];
					capability.present = true;
					capability.limitation = "Literal area matrices are previewed exactly and preserved; edit them in Source.";
					original.areaStatus = original.customAreaTiles.empty() ? "Custom area matrix could not be resolved safely." : "Custom literal area resolved to " + std::to_string(original.customAreaTiles.size()) + " affected tiles.";
				}
			}
		} else if (!areas.empty()) {
			auto& capability = original.capabilities[Index(SpellField::Area)];
			capability.present = true;
			capability.limitation = "Multiple combat areas exist in this implementation.";
			original.areaStatus = capability.limitation;
		}
	}

	std::string replacement(const SpellDefinition& edited, SpellField field, const SourceLocation& location) const {
		const std::string value = FieldValue(edited, field);
		switch (location.syntax) {
			case ValueSyntax::LuaString:
				return EncodeLuaString(value, location.quote ? location.quote : '"');
			case ValueSyntax::LuaBoolean:
				return value;
			case ValueSyntax::LuaNumber:
				return value;
			case ValueSyntax::LuaIdentifier:
				return value;
			case ValueSyntax::XmlString:
				return EncodeXml(value, location.quote);
			case ValueSyntax::XmlNumber:
				return value;
			case ValueSyntax::XmlBoolean:
				if (location.original == "1" || location.original == "0") {
					return value == "true" ? "1" : "0";
				}
				return value;
		}
		return value;
	}

	void finishPreview(const SpellAreaResolver& resolver) {
		original.preview.name = original.name;
		original.preview.type = original.combatType;
		original.preview.effect = original.effect;
		original.preview.projectile = original.projectile;
		original.allVocations = original.vocations.empty();
		if (!original.areaExpression.empty() && !original.customAreaTiles.empty()) {
			original.areaResolutionState = SpellAreaResolutionState::Resolved;
			original.areaStatus = "Literal combat area resolved to " + std::to_string(original.customAreaTiles.size()) + " affected tiles.";
		} else {
			const SpellAreaResolution resolution = resolver.resolve(original.areaExpression);
			original.areaResolutionState = resolution.state;
			original.customAreaTiles = resolution.tiles;
			original.areaStatus = resolution.description;
			if (resolution.state == SpellAreaResolutionState::Single) {
				original.preview.area = {};
				original.preview.area.range = original.range;
				original.preview.area.target = original.needTarget;
			}
		}
		for (std::size_t field = 0; field < FieldCount; ++field) {
			auto& capability = original.capabilities[field];
			if (!capability.present && capability.limitation.empty()) {
				capability.limitation = "This field is not present as a supported literal in the selected spell source.";
			}
		}
	}
};

const SpellFieldCapability& SpellDefinition::capability(SpellField field) const {
	return capabilities[Index(field)];
}

std::unique_ptr<SpellDefinitionDocument> SpellDefinitionDocument::Load(const ServerContentSource& source, std::string& error) {
	return Load(source, error, nullptr, nullptr);
}

std::unique_ptr<SpellDefinitionDocument> SpellDefinitionDocument::Load(
	const ServerContentSource& source,
	std::string& error,
	const ServerWorkspace* selectedWorkspace
) {
	return Load(source, error, selectedWorkspace, nullptr);
}

std::unique_ptr<SpellDefinitionDocument> SpellDefinitionDocument::Load(
	const ServerContentSource& source,
	std::string& error,
	const ServerWorkspace* selectedWorkspace,
	std::shared_ptr<const SpellAreaResolver> areaResolver
) {
	error.clear();
	if (source.kind != ServerContentKind::Spell || (source.format != ServerContentFormat::Xml && source.format != ServerContentFormat::Lua)) {
		error = "This source is not a supported global spell definition.";
		return nullptr;
	}
	const auto declaration = ReadFile(source.declarationPath, error);
	if (!declaration) {
		return nullptr;
	}
	std::vector<std::string> files { *declaration };
	if (source.relatedScriptPath && !FileSaveTransaction::PathsReferToSameFile(*source.relatedScriptPath, source.declarationPath)) {
		const auto related = ReadFile(*source.relatedScriptPath, error);
		if (!related) {
			return nullptr;
		}
		files.push_back(*related);
	}
	return LoadFromText(source, std::move(files), error, selectedWorkspace, std::move(areaResolver));
}

std::unique_ptr<SpellDefinitionDocument> SpellDefinitionDocument::LoadFromText(
	const ServerContentSource& source,
	std::vector<std::string> files,
	std::string& error,
	const ServerWorkspace* selectedWorkspace,
	std::shared_ptr<const SpellAreaResolver> areaResolver
) {
	auto implementation = std::make_unique<Impl>();
	implementation->sourceInfo = source;
	implementation->files.push_back({ source.declarationPath, ResourceFingerprint::Read(source.declarationPath), std::move(files.front()) });
	if (source.format == ServerContentFormat::Lua) {
		if (!implementation->parseLuaDeclaration(0, error)) {
			return nullptr;
		}
	} else if (!implementation->parseXmlDeclaration(0, error)) {
		return nullptr;
	}
	implementation->implementationFile = 0;
	if (files.size() > 1 && source.relatedScriptPath) {
		implementation->implementationFile = implementation->files.size();
		implementation->files.push_back({ *source.relatedScriptPath, ResourceFingerprint::Read(*source.relatedScriptPath), std::move(files[1]) });
	}
	implementation->parseCombat(implementation->implementationFile);
	ServerWorkspace inferred;
	if (!selectedWorkspace) {
		std::filesystem::path current = source.declarationPath.parent_path();
		while (!current.empty()) {
			const std::string name = LowerAscii(current.filename().string());
			if (name == "data" || name == "data-crystal" || name == "data-global") {
				inferred.activeDataDirectory = current;
				inferred.rootPath = current.parent_path();
				break;
			}
			const auto parent = current.parent_path();
			if (parent == current) {
				break;
			}
			current = parent;
		}
		selectedWorkspace = &inferred;
	}
	if (!areaResolver) {
		areaResolver = std::make_shared<const SpellAreaResolver>(*selectedWorkspace);
	}
	implementation->areaResolver = std::move(areaResolver);
	implementation->finishPreview(*implementation->areaResolver);
	return std::unique_ptr<SpellDefinitionDocument>(new SpellDefinitionDocument(std::move(implementation)));
}

SpellDefinitionDocument::SpellDefinitionDocument(std::unique_ptr<Impl> value) :
	implementation(std::move(value)) { }
SpellDefinitionDocument::~SpellDefinitionDocument() = default;
SpellDefinitionDocument::SpellDefinitionDocument(SpellDefinitionDocument&&) noexcept = default;
SpellDefinitionDocument& SpellDefinitionDocument::operator=(SpellDefinitionDocument&&) noexcept = default;

const SpellDefinition& SpellDefinitionDocument::definition() const {
	return implementation->original;
}
const ServerContentSource& SpellDefinitionDocument::source() const {
	return implementation->sourceInfo;
}
const std::string& SpellDefinitionDocument::declarationText() const {
	return implementation->files.front().bytes;
}
const std::string& SpellDefinitionDocument::implementationText() const {
	return implementation->files[implementation->implementationFile].bytes;
}
const std::filesystem::path& SpellDefinitionDocument::implementationPath() const {
	return implementation->files[implementation->implementationFile].path;
}
bool SpellDefinitionDocument::hasSeparateImplementation() const {
	return implementation->implementationFile != 0;
}

bool SpellDefinitionDocument::hasChanges(const SpellDefinition& edited) const {
	for (std::size_t index = 0; index < FieldCount; ++index) {
		if (FieldValue(implementation->original, static_cast<SpellField>(index)) != FieldValue(edited, static_cast<SpellField>(index))) {
			return true;
		}
	}
	return implementation->original.vocations != edited.vocations;
}

bool SpellDefinitionDocument::save(const SpellDefinition& edited, std::string& error) {
	error.clear();
	if (!ValidateSpellDefinition(edited, error)) {
		return false;
	}
	struct Patch {
		std::size_t begin;
		std::size_t end;
		std::string text;
	};
	std::vector<std::vector<Patch>> patches(implementation->files.size());
	for (std::size_t index = 0; index < FieldCount; ++index) {
		const SpellField field = static_cast<SpellField>(index);
		if (FieldValue(implementation->original, field) == FieldValue(edited, field)) {
			continue;
		}
		const auto& capability = implementation->original.capabilities[index];
		if (!capability.editable || implementation->locations[index].size() != 1) {
			error = std::string(SpellFieldName(field)) + " cannot be saved safely: " + capability.limitation;
			return false;
		}
		const SourceLocation& location = implementation->locations[index].front();
		patches[location.file].push_back({ location.begin, location.end, implementation->replacement(edited, field, location) });
	}
	if (implementation->original.vocations != edited.vocations) {
		if (!implementation->original.vocationCapability.editable) {
			error = "Vocations cannot be saved safely: " + implementation->original.vocationCapability.limitation;
			return false;
		}
		if (implementation->sourceInfo.format == ServerContentFormat::Lua) {
			std::string arguments;
			for (std::size_t index = 0; index < edited.vocations.size(); ++index) {
				if (index) {
					arguments += ", ";
				}
				arguments += EncodeLuaString(edited.vocations[index], '"');
			}
			if (implementation->luaVocationArguments) {
				if (edited.vocations.empty() && implementation->luaVocationStatement) {
					const auto& statement = *implementation->luaVocationStatement;
					patches[statement.file].push_back({ statement.begin, statement.end, {} });
				} else {
					const auto& location = *implementation->luaVocationArguments;
					patches[location.file].push_back({ location.begin, location.end, arguments });
				}
			} else if (!edited.vocations.empty() && implementation->vocationInsertPosition != std::string::npos) {
				const std::string newline = implementation->files[implementation->vocationInsertFile].bytes.find("\r\n") != std::string::npos ? "\r\n" : "\n";
				patches[implementation->vocationInsertFile].push_back({
					implementation->vocationInsertPosition,
					implementation->vocationInsertPosition,
					implementation->spellVariable + ":vocation(" + arguments + ")" + newline,
				});
			} else if (!edited.vocations.empty()) {
				error = "The Lua spell has no safe insertion anchor before register().";
				return false;
			}
		} else {
			std::multiset<std::string> remaining(edited.vocations.begin(), edited.vocations.end());
			for (const XmlVocationSource& source : implementation->xmlVocations) {
				if (const auto found = remaining.find(source.name); found != remaining.end()) {
					remaining.erase(found);
				} else {
					patches[source.file].push_back({ source.begin, source.end, {} });
				}
			}
			const std::string& bytes = implementation->files[implementation->vocationInsertFile].bytes;
			const std::string newline = bytes.find("\r\n") != std::string::npos ? "\r\n" : "\n";
			std::string additions;
			for (const std::string& vocation : remaining) {
				additions += "\t<vocation name=\"" + EncodeXml(vocation, '"') + "\"/>" + newline;
			}
			if (!additions.empty()) {
				if (implementation->xmlSpellSelfClosing && implementation->xmlSpellEnd >= 2) {
					patches[implementation->vocationInsertFile].push_back({
						implementation->xmlSpellEnd - 2,
						implementation->xmlSpellEnd,
						">" + newline + additions + "</" + implementation->xmlSpellTag + ">",
					});
				} else if (implementation->vocationInsertPosition != std::string::npos) {
					patches[implementation->vocationInsertFile].push_back({ implementation->vocationInsertPosition, implementation->vocationInsertPosition, additions });
				} else {
					error = "The XML spell has no safe vocation insertion anchor.";
					return false;
				}
			}
		}
	}
	if (std::all_of(patches.begin(), patches.end(), [](const auto& value) { return value.empty(); })) {
		return true;
	}

	std::vector<std::string> updated;
	for (std::size_t file = 0; file < implementation->files.size(); ++file) {
		const auto current = ReadFile(implementation->files[file].path, error);
		if (!current) {
			return false;
		}
		if (!implementation->files[file].fingerprint.MatchesCurrentFile() || *current != implementation->files[file].bytes) {
			error = "A spell source changed on disk after this editor opened. Reopen it before saving: " + implementation->files[file].path.string() + ".";
			return false;
		}
		updated.push_back(implementation->files[file].bytes);
		auto& filePatches = patches[file];
		std::sort(filePatches.begin(), filePatches.end(), [](const Patch& left, const Patch& right) { return left.begin > right.begin; });
		for (const Patch& patch : filePatches) {
			updated.back().replace(patch.begin, patch.end - patch.begin, patch.text);
		}
		bool validLua = true;
		if (LowerAscii(implementation->files[file].path.extension().string()) == ".lua") {
			TokenizeLua(updated.back(), &validLua);
			if (!validLua) {
				error = "The edited Lua source did not pass lexical validation.";
				return false;
			}
		} else {
			pugi::xml_document document;
			if (!document.load_buffer(updated.back().data(), updated.back().size())) {
				error = "The edited XML registry did not pass structural validation.";
				return false;
			}
		}
	}

	FileSaveTransaction transaction;
	for (std::size_t file = 0; file < implementation->files.size(); ++file) {
		if (!patches[file].empty() && !WriteFile(transaction.Stage(implementation->files[file].path), updated[file], error)) {
			return false;
		}
	}
	if (!transaction.Commit(error)) {
		return false;
	}
	implementation->sourceInfo.declarationFingerprint = ResourceFingerprint::Read(implementation->sourceInfo.declarationPath);
	implementation->sourceInfo.name = edited.name;
	if (implementation->sourceInfo.registrationPath) {
		implementation->sourceInfo.registrationFingerprint = ResourceFingerprint::Read(*implementation->sourceInfo.registrationPath);
	}
	if (implementation->sourceInfo.relatedScriptPath) {
		implementation->sourceInfo.relatedScriptFingerprint = ResourceFingerprint::Read(*implementation->sourceInfo.relatedScriptPath);
	}
	auto refreshed = LoadFromText(implementation->sourceInfo, std::move(updated), error, nullptr, implementation->areaResolver);
	if (!refreshed) {
		return false;
	}
	implementation = std::move(refreshed->implementation);
	return true;
}

const char* SpellFieldName(SpellField field) {
	static constexpr std::array<const char*, FieldCount> Names { {
		"Name",
		"Words",
		"Group",
		"Script",
		"Spell ID",
		"Rune item ID",
		"Required level",
		"Magic level",
		"Mana",
		"Mana percent",
		"Soul",
		"Cooldown",
		"Group cooldown",
		"Range",
		"Charges",
		"Premium",
		"Enabled",
		"Aggressive",
		"Needs target",
		"Needs direction",
		"Blocks walls",
		"Allow far use",
		"Needs learn",
		"Self target",
		"Combat type",
		"Effect",
		"Projectile",
		"Area",
	} };
	return field == SpellField::Count ? "Unknown" : Names[Index(field)];
}

bool ValidateSpellDefinition(const SpellDefinition& definition, std::string& error) {
	if (definition.name.empty()) {
		error = "Spell name cannot be empty.";
		return false;
	}
	if (definition.level < 0 || definition.magicLevel < 0 || definition.mana < 0 || definition.manaPercent < 0 || definition.soul < 0 || definition.cooldown < 0 || definition.groupCooldown < 0 || definition.range < 0 || definition.charges < 0 || definition.spellId < 0 || definition.runeId < 0) {
		error = "Spell numeric values cannot be negative.";
		return false;
	}
	error.clear();
	return true;
}
