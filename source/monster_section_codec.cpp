//////////////////////////////////////////////////////////////////////
// Structural codecs for the advanced XML and Lua monster sections.
//////////////////////////////////////////////////////////////////////

#include "monster_section_codec.h"

#include "source_text_utils.h"

#include "ext/pugixml.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

namespace {
	constexpr std::size_t SectionCount = static_cast<std::size_t>(MonsterSection::Count);

	std::size_t Index(MonsterSection section) {
		return static_cast<std::size_t>(section);
	}

	std::string LowerAscii(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return value;
	}

	std::string Trim(std::string_view value) {
		const std::size_t begin = value.find_first_not_of(" \t\r\n");
		if (begin == std::string_view::npos) {
			return {};
		}
		const std::size_t end = value.find_last_not_of(" \t\r\n");
		return std::string(value.substr(begin, end - begin + 1));
	}

	std::optional<int> ParseInteger(std::string_view value) {
		int parsed = 0;
		const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
		if (result.ec != std::errc() || result.ptr != value.data() + value.size()) {
			return std::nullopt;
		}
		return parsed;
	}

	std::optional<bool> ParseBoolean(std::string_view value) {
		const std::string lowered = LowerAscii(std::string(value));
		if (lowered == "1" || lowered == "true" || lowered == "yes") {
			return true;
		}
		if (lowered == "0" || lowered == "false" || lowered == "no") {
			return false;
		}
		return std::nullopt;
	}

	std::string EncodeXml(std::string_view value) {
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
					encoded += "&quot;";
					break;
				default:
					encoded.push_back(character);
					break;
			}
		}
		return encoded;
	}

	std::string EncodeLua(std::string_view value) {
		std::string encoded;
		for (const char character : value) {
			switch (character) {
				case '\\':
					encoded += "\\\\";
					break;
				case '"':
					encoded += "\\\"";
					break;
				case '\n':
					encoded += "\\n";
					break;
				case '\r':
					encoded += "\\r";
					break;
				case '\t':
					encoded += "\\t";
					break;
				default:
					encoded.push_back(character);
					break;
			}
		}
		return encoded;
	}

	std::string NewlineFor(std::string_view source) {
		return source.find("\r\n") == std::string_view::npos ? "\n" : "\r\n";
	}

	struct SectionState {
		bool present = false;
		bool editable = true;
		std::string limitation;
		std::size_t begin = 0;
		std::size_t end = 0;
	};

	struct XmlTagSpan {
		std::string name;
		std::size_t begin = 0;
		std::size_t openEnd = 0;
		std::size_t closeBegin = 0;
		std::size_t end = 0;
		std::size_t depth = 0;
		bool closing = false;
		bool selfClosing = false;
	};

	bool IsXmlName(unsigned char character) {
		return std::isalnum(character) || character == '_' || character == '-' || character == ':';
	}

	std::vector<XmlTagSpan> ScanXmlSpans(std::string_view source) {
		std::vector<XmlTagSpan> spans;
		std::vector<std::size_t> open;
		for (std::size_t cursor = 0; cursor < source.size();) {
			cursor = source.find('<', cursor);
			if (cursor == std::string_view::npos) {
				break;
			}
			if (source.substr(cursor, 4) == "<!--") {
				const std::size_t end = source.find("-->", cursor + 4);
				cursor = end == std::string_view::npos ? source.size() : end + 3;
				continue;
			}
			if (source.substr(cursor, 9) == "<![CDATA[") {
				const std::size_t end = source.find("]]>", cursor + 9);
				cursor = end == std::string_view::npos ? source.size() : end + 3;
				continue;
			}
			if (cursor + 1 < source.size() && (source[cursor + 1] == '!' || source[cursor + 1] == '?')) {
				const std::size_t end = source.find('>', cursor + 2);
				cursor = end == std::string_view::npos ? source.size() : end + 1;
				continue;
			}
			std::size_t index = cursor + 1;
			const bool closing = index < source.size() && source[index] == '/';
			if (closing) {
				++index;
			}
			while (index < source.size() && std::isspace(static_cast<unsigned char>(source[index]))) {
				++index;
			}
			const std::size_t nameBegin = index;
			while (index < source.size() && IsXmlName(static_cast<unsigned char>(source[index]))) {
				++index;
			}
			if (nameBegin == index) {
				++cursor;
				continue;
			}
			const std::string name = LowerAscii(std::string(source.substr(nameBegin, index - nameBegin)));
			char quote = '\0';
			for (; index < source.size(); ++index) {
				if (quote != '\0') {
					if (source[index] == quote) {
						quote = '\0';
					}
				} else if (source[index] == '"' || source[index] == '\'') {
					quote = source[index];
				} else if (source[index] == '>') {
					break;
				}
			}
			if (index >= source.size()) {
				break;
			}
			const bool selfClosing = !closing && index > cursor && source[index - 1] == '/';
			if (closing) {
				if (!open.empty()) {
					const std::size_t openIndex = open.back();
					open.pop_back();
					spans[openIndex].closeBegin = cursor;
					spans[openIndex].end = index + 1;
				}
			} else {
				const std::size_t spanIndex = spans.size();
				spans.push_back({ name, cursor, index + 1, index + 1, index + 1, open.size(), false, selfClosing });
				if (!selfClosing) {
					open.push_back(spanIndex);
				}
			}
			cursor = index + 1;
		}
		return spans;
	}

	std::string XmlNodeText(const pugi::xml_node& node) {
		std::ostringstream stream;
		node.print(stream, "", pugi::format_raw);
		return stream.str();
	}

	std::vector<MonsterCustomProperty> XmlCustomAttributes(const pugi::xml_node& node, std::initializer_list<std::string_view> known) {
		std::vector<MonsterCustomProperty> properties;
		for (const pugi::xml_attribute& attribute : node.attributes()) {
			const std::string name = attribute.name();
			if (std::find(known.begin(), known.end(), LowerAscii(name)) == known.end()) {
				properties.push_back({ name, attribute.value(), false });
			}
		}
		return properties;
	}

	bool ReadXmlInteger(const pugi::xml_node& node, const char* name, int& target, std::string& limitation) {
		const pugi::xml_attribute attribute = node.attribute(name);
		if (!attribute) {
			return true;
		}
		const auto parsed = ParseInteger(attribute.value());
		if (!parsed) {
			limitation = std::string("The ") + name + " attribute is not an integer literal.";
			return false;
		}
		target = *parsed;
		return true;
	}

	bool ReadXmlBoolean(const pugi::xml_node& node, const char* name, bool& target, bool& used, std::string& limitation) {
		const pugi::xml_attribute attribute = node.attribute(name);
		if (!attribute) {
			return true;
		}
		const auto parsed = ParseBoolean(attribute.value());
		if (!parsed) {
			limitation = std::string("The ") + name + " attribute is not a boolean literal.";
			return false;
		}
		target = *parsed;
		used = true;
		return true;
	}

	void AppendXmlProperty(std::string& output, const MonsterCustomProperty& property) {
		output += " " + property.name + "=\"" + EncodeXml(property.value) + "\"";
	}

	enum class LuaTokenKind : uint8_t {
		Identifier,
		String,
		Number,
		Symbol,
	};

	struct LuaToken {
		LuaTokenKind kind = LuaTokenKind::Symbol;
		std::string value;
		std::size_t begin = 0;
		std::size_t end = 0;
	};

	std::optional<std::size_t> LuaLongBracketEnd(std::string_view source, std::size_t begin) {
		if (begin >= source.size() || source[begin] != '[') {
			return std::nullopt;
		}
		std::size_t marker = begin + 1;
		while (marker < source.size() && source[marker] == '=') {
			++marker;
		}
		if (marker >= source.size() || source[marker] != '[') {
			return std::nullopt;
		}
		const std::string closing = "]" + std::string(marker - begin - 1, '=') + "]";
		const std::size_t end = source.find(closing, marker + 1);
		return end == std::string_view::npos ? source.size() : end + closing.size();
	}

	std::vector<LuaToken> ScanLua(std::string_view source) {
		std::vector<LuaToken> tokens;
		for (std::size_t index = 0; index < source.size();) {
			if (std::isspace(static_cast<unsigned char>(source[index]))) {
				++index;
				continue;
			}
			if (source.substr(index, 2) == "--") {
				if (const auto end = LuaLongBracketEnd(source, index + 2)) {
					index = *end;
				} else {
					const std::size_t lineEnd = source.find('\n', index + 2);
					index = lineEnd == std::string_view::npos ? source.size() : lineEnd + 1;
				}
				continue;
			}
			if (const auto end = LuaLongBracketEnd(source, index)) {
				tokens.push_back({ LuaTokenKind::String, std::string(source.substr(index, *end - index)), index, *end });
				index = *end;
				continue;
			}
			if (source[index] == '"' || source[index] == '\'') {
				const char quote = source[index];
				const std::size_t begin = index++;
				std::string decoded;
				while (index < source.size() && source[index] != quote) {
					if (source[index] == '\\' && index + 1 < source.size()) {
						const char escaped = source[++index];
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
						++index;
					} else {
						decoded.push_back(source[index++]);
					}
				}
				if (index < source.size()) {
					++index;
				}
				tokens.push_back({ LuaTokenKind::String, std::move(decoded), begin, index });
				continue;
			}
			if (std::isalpha(static_cast<unsigned char>(source[index])) || source[index] == '_') {
				const std::size_t begin = index++;
				while (index < source.size()
					   && (std::isalnum(static_cast<unsigned char>(source[index])) || source[index] == '_')) {
					++index;
				}
				tokens.push_back({ LuaTokenKind::Identifier, std::string(source.substr(begin, index - begin)), begin, index });
				continue;
			}
			if (std::isdigit(static_cast<unsigned char>(source[index]))
				|| (source[index] == '-' && index + 1 < source.size() && std::isdigit(static_cast<unsigned char>(source[index + 1])))) {
				const std::size_t begin = index++;
				while (index < source.size()
					   && (std::isalnum(static_cast<unsigned char>(source[index])) || source[index] == '.' || source[index] == 'x'
						   || source[index] == 'X')) {
					++index;
				}
				tokens.push_back({ LuaTokenKind::Number, std::string(source.substr(begin, index - begin)), begin, index });
				continue;
			}
			tokens.push_back({ LuaTokenKind::Symbol, std::string(1, source[index]), index, index + 1 });
			++index;
		}
		return tokens;
	}

	bool Symbol(const LuaToken& token, std::string_view value) {
		return token.kind == LuaTokenKind::Symbol && token.value == value;
	}

	struct LuaTable;

	struct LuaValue {
		enum class Kind : uint8_t {
			String,
			Number,
			Boolean,
			Identifier,
			Table,
			Expression,
		};

		Kind kind = Kind::Expression;
		std::string text;
		std::unique_ptr<LuaTable> table;
	};

	struct LuaEntry {
		std::optional<std::string> key;
		LuaValue value;
		std::string annotation;
	};

	struct LuaTable {
		std::vector<LuaEntry> entries;
	};

	struct ParsedLuaTable {
		std::unique_ptr<LuaTable> table;
		std::size_t closeToken = 0;
	};

	ParsedLuaTable ParseLuaTable(
		const std::vector<LuaToken>& tokens,
		std::string_view source,
		std::size_t openToken,
		std::string& error
	) {
		auto table = std::make_unique<LuaTable>();
		std::size_t index = openToken + 1;
		while (index < tokens.size() && !Symbol(tokens[index], "}")) {
			if (Symbol(tokens[index], ",")) {
				++index;
				continue;
			}
			LuaEntry entry;
			if (index + 1 < tokens.size() && tokens[index].kind == LuaTokenKind::Identifier && Symbol(tokens[index + 1], "=")) {
				entry.key = tokens[index].value;
				index += 2;
			}
			if (index >= tokens.size()) {
				error = "Unexpected end of Lua table.";
				return {};
			}
			if (Symbol(tokens[index], "{")) {
				ParsedLuaTable nested = ParseLuaTable(tokens, source, index, error);
				if (!nested.table) {
					return {};
				}
				entry.value.kind = LuaValue::Kind::Table;
				entry.value.table = std::move(nested.table);
				entry.value.text = std::string(source.substr(tokens[index].begin, tokens[nested.closeToken].end - tokens[index].begin));
				index = nested.closeToken + 1;
			} else {
				const std::size_t valueBegin = index;
				int parentheses = 0;
				int brackets = 0;
				while (index < tokens.size()) {
					if (parentheses == 0 && brackets == 0 && (Symbol(tokens[index], ",") || Symbol(tokens[index], "}"))) {
						break;
					}
					if (Symbol(tokens[index], "(")) {
						++parentheses;
					} else if (Symbol(tokens[index], ")")) {
						--parentheses;
					} else if (Symbol(tokens[index], "[")) {
						++brackets;
					} else if (Symbol(tokens[index], "]")) {
						--brackets;
					}
					++index;
				}
				if (valueBegin == index) {
					error = "Missing Lua table value.";
					return {};
				}
				const LuaToken& first = tokens[valueBegin];
				const LuaToken& last = tokens[index - 1];
				entry.value.text = Trim(source.substr(first.begin, last.end - first.begin));
				if (index == valueBegin + 1) {
					if (first.kind == LuaTokenKind::String) {
						entry.value.kind = LuaValue::Kind::String;
						entry.value.text = first.value;
					} else if (first.kind == LuaTokenKind::Number) {
						entry.value.kind = LuaValue::Kind::Number;
					} else if (first.kind == LuaTokenKind::Identifier && (first.value == "true" || first.value == "false")) {
						entry.value.kind = LuaValue::Kind::Boolean;
					} else if (first.kind == LuaTokenKind::Identifier) {
						entry.value.kind = LuaValue::Kind::Identifier;
					}
				}
			}
			table->entries.push_back(std::move(entry));
			std::size_t triviaBegin = tokens[index - 1].end;
			if (index < tokens.size() && Symbol(tokens[index], ",")) {
				triviaBegin = tokens[index].end;
				++index;
			}
			const std::size_t triviaEnd = index < tokens.size() ? tokens[index].begin : source.size();
			if (triviaEnd > triviaBegin) {
				const std::string_view trivia = source.substr(triviaBegin, triviaEnd - triviaBegin);
				const std::size_t comment = trivia.find("--");
				if (comment != std::string_view::npos) {
					const std::size_t lineEnd = trivia.find_first_of("\r\n", comment);
					table->entries.back().annotation = Trim(trivia.substr(comment, lineEnd == std::string_view::npos ? trivia.size() - comment : lineEnd - comment));
				}
			}
		}
		if (index >= tokens.size()) {
			error = "Unterminated Lua table.";
			return {};
		}
		return { std::move(table), index };
	}

	const LuaEntry* FindLuaField(const LuaTable& table, std::string_view key) {
		const auto found = std::find_if(table.entries.begin(), table.entries.end(), [key](const LuaEntry& entry) {
			return entry.key && SourceText::AsciiCaseEqual(*entry.key, key);
		});
		return found == table.entries.end() ? nullptr : &*found;
	}

	bool ReadLuaInteger(const LuaTable& table, std::string_view key, int& target, std::string& limitation) {
		const LuaEntry* entry = FindLuaField(table, key);
		if (!entry) {
			return true;
		}
		if (entry->value.kind != LuaValue::Kind::Number) {
			limitation = std::string(key) + " is computed instead of an integer literal.";
			return false;
		}
		const auto parsed = ParseInteger(entry->value.text);
		if (!parsed) {
			limitation = std::string(key) + " is not an integer literal.";
			return false;
		}
		target = *parsed;
		return true;
	}

	bool ReadLuaBoolean(const LuaTable& table, std::string_view key, bool& target, bool& used, std::string& limitation) {
		const LuaEntry* entry = FindLuaField(table, key);
		if (!entry) {
			return true;
		}
		if (entry->value.kind != LuaValue::Kind::Boolean) {
			limitation = std::string(key) + " is computed instead of a boolean literal.";
			return false;
		}
		target = entry->value.text == "true";
		used = true;
		return true;
	}

	bool ReadLuaText(const LuaTable& table, std::string_view key, std::string& target, std::string& limitation) {
		const LuaEntry* entry = FindLuaField(table, key);
		if (!entry) {
			return true;
		}
		if (entry->value.kind != LuaValue::Kind::String) {
			limitation = std::string(key) + " is computed instead of a string literal.";
			return false;
		}
		target = entry->value.text;
		return true;
	}

	std::vector<MonsterCustomProperty> LuaCustomFields(const LuaTable& table, std::initializer_list<std::string_view> known) {
		std::vector<MonsterCustomProperty> properties;
		for (const LuaEntry& entry : table.entries) {
			if (!entry.key) {
				continue;
			}
			const std::string lowered = LowerAscii(*entry.key);
			if (std::find(known.begin(), known.end(), lowered) != known.end()) {
				continue;
			}
			properties.push_back({ *entry.key, entry.value.text, entry.value.kind != LuaValue::Kind::String });
		}
		return properties;
	}

	std::string LuaPropertyValue(const MonsterCustomProperty& property) {
		return property.rawValue ? property.value : "\"" + EncodeLua(property.value) + "\"";
	}

	std::string SectionMember(MonsterSection section, bool nestedLuaSummons = false) {
		switch (section) {
			case MonsterSection::Defenses:
				return "defenses";
			case MonsterSection::Resistances:
				return "elements";
			case MonsterSection::Immunities:
				return "immunities";
			case MonsterSection::Loot:
				return "loot";
			case MonsterSection::Summons:
				return nestedLuaSummons ? "summon" : "summons";
			case MonsterSection::Voices:
				return "voices";
			case MonsterSection::Attacks:
				return "attacks";
			default:
				return {};
		}
	}

	bool SectionChanged(MonsterSection section, const MonsterDefinition& original, const MonsterDefinition& edited) {
		switch (section) {
			case MonsterSection::Defenses:
				return original.armor != edited.armor || original.defense != edited.defense
					|| original.defenseProperties != edited.defenseProperties || original.defenseActions != edited.defenseActions;
			case MonsterSection::Resistances:
				return original.resistances != edited.resistances;
			case MonsterSection::Immunities:
				return original.immunities != edited.immunities;
			case MonsterSection::Loot:
				return original.lootProperties != edited.lootProperties || original.loot != edited.loot;
			case MonsterSection::Summons:
				return original.maxSummons != edited.maxSummons || original.summonProperties != edited.summonProperties
					|| original.summons != edited.summons;
			case MonsterSection::Voices:
				return original.voices != edited.voices;
			case MonsterSection::Attacks:
				return original.attackProperties != edited.attackProperties || original.attacks != edited.attacks;
			default:
				return false;
		}
	}
}

struct MonsterSectionCodec::Impl {
	ServerContentFormat format = ServerContentFormat::Unknown;
	std::string source;
	std::string newline;
	std::string indent = "\t";
	std::string monsterVariable = "monster";
	std::array<SectionState, SectionCount> states;
	std::size_t insertionOffset = 0;
	bool hasInsertionAnchor = false;
	bool nestedLuaSummons = false;

	void limitation(MonsterDefinition& definition, MonsterSection section, std::string reason) {
		SectionState& state = states[Index(section)];
		state.editable = false;
		state.limitation = std::move(reason);
		auto& capability = definition.sectionCapabilities[Index(section)];
		capability.present = state.present;
		capability.editable = false;
		capability.limitation = state.limitation;
	}

	void syncCapabilities(MonsterDefinition& definition) const {
		for (std::size_t index = 0; index < SectionCount; ++index) {
			const SectionState& state = states[index];
			auto& capability = definition.sectionCapabilities[index];
			capability.present = state.present;
			capability.editable = state.editable;
			capability.limitation = state.limitation;
		}
	}
};

namespace {
	pugi::xml_attribute FindXmlAttribute(const pugi::xml_node& node, std::initializer_list<std::string_view> names) {
		for (const pugi::xml_attribute& attribute : node.attributes()) {
			const std::string lowered = LowerAscii(attribute.name());
			if (std::find(names.begin(), names.end(), lowered) != names.end()) {
				return attribute;
			}
		}
		return {};
	}

	bool XmlIntegerAliases(
		const pugi::xml_node& node,
		std::initializer_list<std::string_view> names,
		int& target,
		std::string& limitation
	) {
		const pugi::xml_attribute attribute = FindXmlAttribute(node, names);
		if (!attribute) {
			return true;
		}
		const auto value = ParseInteger(attribute.value());
		if (!value) {
			limitation = std::string(attribute.name()) + " is not an integer literal.";
			return false;
		}
		target = *value;
		return true;
	}

	bool XmlBooleanAliases(
		const pugi::xml_node& node,
		std::initializer_list<std::string_view> names,
		bool& target,
		bool& used,
		std::string& limitation
	) {
		const pugi::xml_attribute attribute = FindXmlAttribute(node, names);
		if (!attribute) {
			return true;
		}
		const auto value = ParseBoolean(attribute.value());
		if (!value) {
			limitation = std::string(attribute.name()) + " is not a boolean literal.";
			return false;
		}
		target = *value;
		used = true;
		return true;
	}

	void ResolveAttackShape(MonsterAttackDefinition& attack) {
		if (attack.area.length > 0) {
			attack.area.shape = MonsterAreaShape::Beam;
		} else if (attack.area.ring > 0) {
			attack.area.shape = MonsterAreaShape::Ring;
		} else if (attack.area.radius > 0) {
			attack.area.shape = MonsterAreaShape::Radius;
		} else if (attack.area.target || attack.area.range > 0) {
			attack.area.shape = MonsterAreaShape::Target;
		} else {
			attack.area.shape = MonsterAreaShape::Single;
		}
	}

	std::optional<MonsterAttackDefinition> ParseXmlAttack(const pugi::xml_node& node, std::string& limitation) {
		MonsterAttackDefinition attack;
		if (const pugi::xml_attribute value = FindXmlAttribute(node, { "name" })) {
			attack.name = value.value();
		}
		if (const pugi::xml_attribute value = FindXmlAttribute(node, { "type" })) {
			attack.type = value.value();
		}
		bool targetUsed = false;
		if (!XmlIntegerAliases(node, { "interval" }, attack.interval, limitation)
			|| !XmlIntegerAliases(node, { "chance" }, attack.chance, limitation)
			|| !XmlIntegerAliases(node, { "min", "mindamage" }, attack.minDamage, limitation)
			|| !XmlIntegerAliases(node, { "max", "maxdamage" }, attack.maxDamage, limitation)
			|| !XmlIntegerAliases(node, { "skill" }, attack.skill, limitation)
			|| !XmlIntegerAliases(node, { "attack" }, attack.attack, limitation)
			|| !XmlIntegerAliases(node, { "range" }, attack.area.range, limitation)
			|| !XmlIntegerAliases(node, { "radius" }, attack.area.radius, limitation)
			|| !XmlIntegerAliases(node, { "ring" }, attack.area.ring, limitation)
			|| !XmlIntegerAliases(node, { "length" }, attack.area.length, limitation)
			|| !XmlIntegerAliases(node, { "spread" }, attack.area.spread, limitation)
			|| !XmlBooleanAliases(node, { "target" }, attack.area.target, targetUsed, limitation)) {
			return std::nullopt;
		}
		attack.customProperties = XmlCustomAttributes(
			node,
			{ "name", "type", "interval", "chance", "min", "mindamage", "max", "maxdamage", "skill", "attack", "range", "radius", "ring", "length", "spread", "target" }
		);
		for (const pugi::xml_node& child : node.children()) {
			if (child.type() == pugi::node_comment) {
				attack.annotation += XmlNodeText(child);
				continue;
			}
			if (child.type() != pugi::node_element) {
				continue;
			}
			if (LowerAscii(child.name()) == "attribute") {
				const pugi::xml_attribute key = FindXmlAttribute(child, { "key" });
				const pugi::xml_attribute value = FindXmlAttribute(child, { "value" });
				const std::string lowered = key ? LowerAscii(key.value()) : std::string();
				if (value && lowered == "areaeffect") {
					attack.effect = value.value();
					continue;
				}
				if (value && lowered == "shooteffect") {
					attack.projectile = value.value();
					continue;
				}
			}
			attack.preservedChildren += XmlNodeText(child);
		}
		ResolveAttackShape(attack);
		return attack;
	}

	std::optional<MonsterLootEntry> ParseXmlLootEntry(const pugi::xml_node& node, std::string& limitation) {
		MonsterLootEntry entry;
		const pugi::xml_attribute name = FindXmlAttribute(node, { "name" });
		const pugi::xml_attribute id = FindXmlAttribute(node, { "id", "itemid" });
		if (name) {
			entry.usesName = true;
			entry.itemName = name.value();
		} else if (id) {
			const auto parsed = ParseInteger(id.value());
			if (!parsed) {
				limitation = "A loot item ID is not an integer literal.";
				return std::nullopt;
			}
			entry.itemId = *parsed;
		} else {
			limitation = "A loot item has neither a literal name nor a literal ID.";
			return std::nullopt;
		}
		if (!XmlIntegerAliases(node, { "chance", "chance1", "chancemax" }, entry.chance, limitation)
			|| !XmlIntegerAliases(node, { "countmax", "maxcount" }, entry.maxCount, limitation)
			|| !XmlIntegerAliases(node, { "subtype", "subType" }, entry.subtype, limitation)
			|| !XmlIntegerAliases(node, { "actionid", "actionId", "aid" }, entry.actionId, limitation)) {
			return std::nullopt;
		}
		const pugi::xml_attribute text = FindXmlAttribute(node, { "text" });
		if (text) {
			entry.text = text.value();
		}
		entry.customProperties = XmlCustomAttributes(
			node,
			{ "name", "id", "itemid", "chance", "chance1", "chancemax", "countmax", "maxcount", "subtype", "actionid", "aid", "text" }
		);
		for (const pugi::xml_node& child : node.children()) {
			if (child.type() == pugi::node_comment) {
				if (!entry.annotation.empty()) {
					entry.annotation += " ";
				}
				entry.annotation += "<!--" + std::string(child.value()) + "-->";
				continue;
			}
			if (child.type() != pugi::node_element) {
				continue;
			}
			const std::string childName = LowerAscii(child.name());
			if (childName == "inside") {
				for (const pugi::xml_node& inside : child.children()) {
					if (inside.type() == pugi::node_comment) {
						if (!entry.children.empty()) {
							if (!entry.children.back().annotation.empty()) {
								entry.children.back().annotation += " ";
							}
							entry.children.back().annotation += "<!--" + std::string(inside.value()) + "-->";
						}
						continue;
					}
					if (inside.type() != pugi::node_element || LowerAscii(inside.name()) != "item") {
						limitation = "A loot container contains an unsupported child node.";
						return std::nullopt;
					}
					auto nested = ParseXmlLootEntry(inside, limitation);
					if (!nested) {
						return std::nullopt;
					}
					entry.children.push_back(std::move(*nested));
				}
			} else {
				limitation = "A loot item contains an unsupported child node.";
				return std::nullopt;
			}
		}
		return entry;
	}

	void ParseXmlSections(MonsterSectionCodec::Impl& codec, MonsterDefinition& definition, std::string& error) {
		pugi::xml_document document;
		const pugi::xml_parse_result result = document.load_buffer(codec.source.data(), codec.source.size(), pugi::parse_default | pugi::parse_comments);
		if (!result) {
			error = std::string("Malformed monster XML: ") + result.description();
			return;
		}
		const pugi::xml_node root = document.child("monster");
		if (!root) {
			error = "The XML source has no monster root.";
			return;
		}

		const std::vector<XmlTagSpan> spans = ScanXmlSpans(codec.source);
		const auto rootSpan = std::find_if(spans.begin(), spans.end(), [](const XmlTagSpan& span) {
			return span.depth == 0 && span.name == "monster";
		});
		if (rootSpan == spans.end() || rootSpan->selfClosing || rootSpan->closeBegin == 0) {
			error = "The XML monster root cannot accept editable sections.";
			return;
		}
		const std::size_t lineStart = codec.source.rfind('\n', rootSpan->closeBegin);
		if (lineStart != std::string::npos) {
			const std::string rootIndent = codec.source.substr(lineStart + 1, rootSpan->closeBegin - lineStart - 1);
			codec.insertionOffset = lineStart + 1;
			codec.indent = rootIndent + "\t";
		} else {
			codec.insertionOffset = rootSpan->closeBegin;
		}
		codec.hasInsertionAnchor = true;

		const std::array<std::pair<MonsterSection, std::string_view>, SectionCount> names { {
			{ MonsterSection::Defenses, "defenses" },
			{ MonsterSection::Resistances, "elements" },
			{ MonsterSection::Immunities, "immunities" },
			{ MonsterSection::Loot, "loot" },
			{ MonsterSection::Summons, "summons" },
			{ MonsterSection::Voices, "voices" },
			{ MonsterSection::Attacks, "attacks" },
		} };
		for (const auto& [section, name] : names) {
			for (const XmlTagSpan& span : spans) {
				if (span.depth != 1 || span.name != name) {
					continue;
				}
				SectionState& state = codec.states[Index(section)];
				if (state.present) {
					codec.limitation(definition, section, "The source contains this section more than once.");
					continue;
				}
				state.present = true;
				state.begin = span.begin;
				state.end = span.end;
			}
		}

		for (const pugi::xml_node& section : root.children()) {
			if (section.type() != pugi::node_element) {
				continue;
			}
			const std::string name = LowerAscii(section.name());
			std::string limitation;
			if (name == "attacks") {
				definition.attackProperties = XmlCustomAttributes(section, {});
				for (const pugi::xml_node& attackNode : section.children()) {
					if (attackNode.type() == pugi::node_comment) {
						continue;
					}
					if (attackNode.type() != pugi::node_element || LowerAscii(attackNode.name()) != "attack") {
						codec.limitation(definition, MonsterSection::Attacks, "The attacks section contains an unsupported child node.");
						break;
					}
					auto attack = ParseXmlAttack(attackNode, limitation);
					if (!attack) {
						codec.limitation(definition, MonsterSection::Attacks, limitation);
						break;
					}
					definition.attacks.push_back(std::move(*attack));
				}
			} else if (name == "defenses") {
				SectionState& state = codec.states[Index(MonsterSection::Defenses)];
				if (!state.editable) {
					continue;
				}
				if (!XmlIntegerAliases(section, { "armor" }, definition.armor, limitation)
					|| !XmlIntegerAliases(section, { "defense" }, definition.defense, limitation)) {
					codec.limitation(definition, MonsterSection::Defenses, limitation);
					continue;
				}
				definition.defenseProperties = XmlCustomAttributes(section, { "armor", "defense" });
				for (const pugi::xml_node& actionNode : section.children()) {
					if (actionNode.type() == pugi::node_comment) {
						continue;
					}
					if (actionNode.type() != pugi::node_element || LowerAscii(actionNode.name()) != "defense") {
						codec.limitation(definition, MonsterSection::Defenses, "The defenses section contains an unsupported child node.");
						break;
					}
					MonsterDefenseAction action;
					if (const pugi::xml_attribute value = FindXmlAttribute(actionNode, { "name" })) {
						action.name = value.value();
					}
					if (const pugi::xml_attribute value = FindXmlAttribute(actionNode, { "type" })) {
						action.type = value.value();
					}
					if (!XmlIntegerAliases(actionNode, { "interval" }, action.interval, limitation)
						|| !XmlIntegerAliases(actionNode, { "chance" }, action.chance, limitation)
						|| !XmlIntegerAliases(actionNode, { "min", "mindamage" }, action.minDamage, limitation)
						|| !XmlIntegerAliases(actionNode, { "max", "maxdamage" }, action.maxDamage, limitation)) {
						codec.limitation(definition, MonsterSection::Defenses, limitation);
						break;
					}
					if (const pugi::xml_attribute value = FindXmlAttribute(actionNode, { "effect" })) {
						action.effect = value.value();
					}
					bool targetUsed = false;
					if (!XmlBooleanAliases(actionNode, { "target" }, action.target, targetUsed, limitation)) {
						codec.limitation(definition, MonsterSection::Defenses, limitation);
						break;
					}
					action.customProperties = XmlCustomAttributes(
						actionNode,
						{ "name", "type", "interval", "chance", "min", "mindamage", "max", "maxdamage", "effect", "target" }
					);
					for (const pugi::xml_node& child : actionNode.children()) {
						if (child.type() == pugi::node_element || child.type() == pugi::node_comment) {
							action.preservedChildren += XmlNodeText(child);
						}
					}
					definition.defenseActions.push_back(std::move(action));
				}
			} else if (name == "elements") {
				for (const pugi::xml_node& element : section.children()) {
					if (element.type() == pugi::node_comment) {
						continue;
					}
					if (element.type() != pugi::node_element || LowerAscii(element.name()) != "element") {
						codec.limitation(definition, MonsterSection::Resistances, "The elements section contains an unsupported child node.");
						break;
					}
					MonsterResistance resistance;
					for (const pugi::xml_attribute& attribute : element.attributes()) {
						std::string attributeName = LowerAscii(attribute.name());
						const std::string suffix = "percent";
						if (attributeName.size() > suffix.size()
							&& attributeName.substr(attributeName.size() - suffix.size()) == suffix) {
							const auto value = ParseInteger(attribute.value());
							if (!value || !resistance.type.empty()) {
								codec.limitation(definition, MonsterSection::Resistances, "An element has an ambiguous percent attribute.");
								break;
							}
							resistance.type = attributeName.substr(0, attributeName.size() - suffix.size());
							resistance.percent = *value;
						} else {
							resistance.customProperties.push_back({ attribute.name(), attribute.value(), false });
						}
					}
					if (resistance.type.empty()) {
						codec.limitation(definition, MonsterSection::Resistances, "An element has no literal percent attribute.");
						break;
					}
					definition.resistances.push_back(std::move(resistance));
				}
			} else if (name == "immunities") {
				for (const pugi::xml_node& immunityNode : section.children()) {
					if (immunityNode.type() == pugi::node_comment) {
						continue;
					}
					if (immunityNode.type() != pugi::node_element || LowerAscii(immunityNode.name()) != "immunity") {
						codec.limitation(definition, MonsterSection::Immunities, "The immunities section contains an unsupported child node.");
						break;
					}
					MonsterImmunity immunity;
					for (const pugi::xml_attribute& attribute : immunityNode.attributes()) {
						const auto value = ParseBoolean(attribute.value());
						if (value && *value && immunity.type.empty()) {
							immunity.type = LowerAscii(attribute.name());
						} else {
							immunity.customProperties.push_back({ attribute.name(), attribute.value(), false });
						}
					}
					if (immunity.type.empty()) {
						codec.limitation(definition, MonsterSection::Immunities, "An immunity has no literal enabled type.");
						break;
					}
					definition.immunities.push_back(std::move(immunity));
				}
			} else if (name == "loot") {
				definition.lootProperties = XmlCustomAttributes(section, {});
				std::string pendingAnnotation;
				for (const pugi::xml_node& itemNode : section.children()) {
					if (itemNode.type() == pugi::node_comment) {
						if (!definition.loot.empty()) {
							if (!definition.loot.back().annotation.empty()) {
								definition.loot.back().annotation += " ";
							}
							definition.loot.back().annotation += "<!--" + std::string(itemNode.value()) + "-->";
						} else {
							pendingAnnotation += "<!--" + std::string(itemNode.value()) + "-->";
						}
						continue;
					}
					if (itemNode.type() != pugi::node_element || LowerAscii(itemNode.name()) != "item") {
						codec.limitation(definition, MonsterSection::Loot, "The loot section contains an unsupported child node.");
						break;
					}
					auto entry = ParseXmlLootEntry(itemNode, limitation);
					if (!entry) {
						codec.limitation(definition, MonsterSection::Loot, limitation);
						break;
					}
					if (!pendingAnnotation.empty()) {
						entry->annotation += std::exchange(pendingAnnotation, {});
					}
					definition.loot.push_back(std::move(*entry));
				}
			} else if (name == "summons") {
				if (!XmlIntegerAliases(section, { "maxsummons", "max" }, definition.maxSummons, limitation)) {
					codec.limitation(definition, MonsterSection::Summons, limitation);
					continue;
				}
				definition.summonProperties = XmlCustomAttributes(section, { "maxsummons", "max" });
				for (const pugi::xml_node& summonNode : section.children()) {
					if (summonNode.type() == pugi::node_comment) {
						continue;
					}
					if (summonNode.type() != pugi::node_element || LowerAscii(summonNode.name()) != "summon") {
						codec.limitation(definition, MonsterSection::Summons, "The summons section contains an unsupported child node.");
						break;
					}
					MonsterSummon summon;
					if (const pugi::xml_attribute value = FindXmlAttribute(summonNode, { "name" })) {
						summon.name = value.value();
					}
					bool forceUsed = false;
					if (!XmlIntegerAliases(summonNode, { "interval" }, summon.interval, limitation)
						|| !XmlIntegerAliases(summonNode, { "chance" }, summon.chance, limitation)
						|| !XmlIntegerAliases(summonNode, { "max" }, summon.max, limitation)
						|| !XmlBooleanAliases(summonNode, { "force" }, summon.force, forceUsed, limitation)) {
						codec.limitation(definition, MonsterSection::Summons, limitation);
						break;
					}
					summon.customProperties = XmlCustomAttributes(summonNode, { "name", "interval", "chance", "max", "force" });
					definition.summons.push_back(std::move(summon));
				}
			} else if (name == "voices") {
				const pugi::xml_attribute interval = FindXmlAttribute(section, { "interval" });
				const pugi::xml_attribute speed = FindXmlAttribute(section, { "speed" });
				if (interval) {
					if (!XmlIntegerAliases(section, { "interval" }, definition.voices.interval, limitation)) {
						codec.limitation(definition, MonsterSection::Voices, limitation);
						continue;
					}
				} else if (speed && !XmlIntegerAliases(section, { "speed" }, definition.voices.interval, limitation)) {
					codec.limitation(definition, MonsterSection::Voices, limitation);
					continue;
				}
				if (!XmlIntegerAliases(section, { "chance" }, definition.voices.chance, limitation)) {
					codec.limitation(definition, MonsterSection::Voices, limitation);
					continue;
				}
				definition.voices.customProperties = XmlCustomAttributes(section, { "interval", "speed", "chance" });
				for (const pugi::xml_node& voiceNode : section.children()) {
					if (voiceNode.type() == pugi::node_comment) {
						continue;
					}
					if (voiceNode.type() != pugi::node_element || LowerAscii(voiceNode.name()) != "voice") {
						codec.limitation(definition, MonsterSection::Voices, "The voices section contains an unsupported child node.");
						break;
					}
					MonsterVoice voice;
					if (const pugi::xml_attribute value = FindXmlAttribute(voiceNode, { "sentence", "text" })) {
						voice.text = value.value();
					}
					bool yellUsed = false;
					if (!XmlBooleanAliases(voiceNode, { "yell" }, voice.yell, yellUsed, limitation)) {
						codec.limitation(definition, MonsterSection::Voices, limitation);
						break;
					}
					voice.customProperties = XmlCustomAttributes(voiceNode, { "sentence", "text", "yell" });
					definition.voices.entries.push_back(std::move(voice));
				}
			}
		}
		codec.syncCapabilities(definition);
	}

	bool LuaTextOrIdentifier(const LuaTable& table, std::string_view key, std::string& target, std::string& limitation) {
		const LuaEntry* entry = FindLuaField(table, key);
		if (!entry) {
			return true;
		}
		if (entry->value.kind != LuaValue::Kind::String && entry->value.kind != LuaValue::Kind::Identifier
			&& entry->value.kind != LuaValue::Kind::Number) {
			limitation = std::string(key) + " is a computed expression.";
			return false;
		}
		target = entry->value.text;
		return true;
	}

	bool ParseLuaAttack(const LuaEntry& entry, MonsterAttackDefinition& attack, std::string& limitation) {
		if (entry.key || entry.value.kind != LuaValue::Kind::Table) {
			limitation = "The attacks table contains an unsupported entry.";
			return false;
		}
		const LuaTable& table = *entry.value.table;
		if (!ReadLuaText(table, "name", attack.name, limitation)
			|| !LuaTextOrIdentifier(table, "type", attack.type, limitation)
			|| !ReadLuaInteger(table, "interval", attack.interval, limitation)
			|| !ReadLuaInteger(table, "chance", attack.chance, limitation)
			|| !ReadLuaInteger(table, "minDamage", attack.minDamage, limitation)
			|| !ReadLuaInteger(table, "maxDamage", attack.maxDamage, limitation)
			|| !ReadLuaInteger(table, "skill", attack.skill, limitation)
			|| !ReadLuaInteger(table, "attack", attack.attack, limitation)
			|| !ReadLuaInteger(table, "range", attack.area.range, limitation)
			|| !ReadLuaInteger(table, "radius", attack.area.radius, limitation)
			|| !ReadLuaInteger(table, "ring", attack.area.ring, limitation)
			|| !ReadLuaInteger(table, "length", attack.area.length, limitation)
			|| !ReadLuaInteger(table, "spread", attack.area.spread, limitation)
			|| !LuaTextOrIdentifier(table, "effect", attack.effect, limitation)
			|| !LuaTextOrIdentifier(table, "shootEffect", attack.projectile, limitation)) {
			return false;
		}
		bool targetUsed = false;
		if (!ReadLuaBoolean(table, "target", attack.area.target, targetUsed, limitation)) {
			return false;
		}
		attack.customProperties = LuaCustomFields(
			table,
			{ "name", "type", "interval", "chance", "mindamage", "maxdamage", "skill", "attack", "range", "radius", "ring", "length", "spread", "target", "effect", "shooteffect" }
		);
		attack.annotation = entry.annotation;
		ResolveAttackShape(attack);
		return true;
	}

	bool ParseLuaLootEntry(const LuaTable& table, MonsterLootEntry& entry, std::string& limitation) {
		if (const LuaEntry* name = FindLuaField(table, "name")) {
			if (name->value.kind != LuaValue::Kind::String) {
				limitation = "A loot item name is computed.";
				return false;
			}
			entry.usesName = true;
			entry.itemName = name->value.text;
		} else if (const LuaEntry* id = FindLuaField(table, "id")) {
			if (id->value.kind != LuaValue::Kind::Number) {
				limitation = "A loot item ID is computed.";
				return false;
			}
			const auto parsed = ParseInteger(id->value.text);
			if (!parsed) {
				limitation = "A loot item ID is not an integer literal.";
				return false;
			}
			entry.itemId = *parsed;
		} else {
			limitation = "A loot item has neither a literal name nor a literal ID.";
			return false;
		}
		if (!ReadLuaInteger(table, "chance", entry.chance, limitation)
			|| !ReadLuaInteger(table, "maxCount", entry.maxCount, limitation)
			|| !ReadLuaInteger(table, "subType", entry.subtype, limitation)
			|| !ReadLuaInteger(table, "actionId", entry.actionId, limitation)
			|| !ReadLuaText(table, "text", entry.text, limitation)) {
			return false;
		}
		entry.customProperties = LuaCustomFields(table, { "name", "id", "chance", "maxcount", "subtype", "actionid", "text", "childloot" });
		if (const LuaEntry* children = FindLuaField(table, "childLoot")) {
			if (children->value.kind != LuaValue::Kind::Table) {
				limitation = "childLoot is computed instead of a table.";
				return false;
			}
			for (const LuaEntry& child : children->value.table->entries) {
				if (child.key || child.value.kind != LuaValue::Kind::Table) {
					limitation = "childLoot contains an unsupported entry.";
					return false;
				}
				MonsterLootEntry nested;
				if (!ParseLuaLootEntry(*child.value.table, nested, limitation)) {
					return false;
				}
				nested.annotation = child.annotation;
				entry.children.push_back(std::move(nested));
			}
		}
		return true;
	}

	bool ParseLuaSection(
		MonsterSectionCodec::Impl& codec,
		MonsterDefinition& definition,
		MonsterSection section,
		const LuaTable& table,
		std::string& limitation
	) {
		switch (section) {
			case MonsterSection::Attacks:
				definition.attackProperties = LuaCustomFields(table, {});
				for (const LuaEntry& entry : table.entries) {
					if (entry.key) {
						continue;
					}
					MonsterAttackDefinition attack;
					if (!ParseLuaAttack(entry, attack, limitation)) {
						return false;
					}
					definition.attacks.push_back(std::move(attack));
				}
				return true;
			case MonsterSection::Defenses:
				if (!ReadLuaInteger(table, "defense", definition.defense, limitation)
					|| !ReadLuaInteger(table, "armor", definition.armor, limitation)) {
					return false;
				}
				definition.defenseProperties = LuaCustomFields(table, { "defense", "armor" });
				for (const LuaEntry& entry : table.entries) {
					if (entry.key) {
						continue;
					}
					if (entry.value.kind != LuaValue::Kind::Table) {
						limitation = "The defenses table contains an unsupported entry.";
						return false;
					}
					MonsterDefenseAction action;
					if (!ReadLuaText(*entry.value.table, "name", action.name, limitation)
						|| !LuaTextOrIdentifier(*entry.value.table, "type", action.type, limitation)
						|| !ReadLuaInteger(*entry.value.table, "interval", action.interval, limitation)
						|| !ReadLuaInteger(*entry.value.table, "chance", action.chance, limitation)
						|| !ReadLuaInteger(*entry.value.table, "minDamage", action.minDamage, limitation)
						|| !ReadLuaInteger(*entry.value.table, "maxDamage", action.maxDamage, limitation)
						|| !LuaTextOrIdentifier(*entry.value.table, "effect", action.effect, limitation)) {
						return false;
					}
					bool targetUsed = false;
					if (!ReadLuaBoolean(*entry.value.table, "target", action.target, targetUsed, limitation)) {
						return false;
					}
					action.customProperties = LuaCustomFields(
						*entry.value.table,
						{ "name", "type", "interval", "chance", "mindamage", "maxdamage", "effect", "target" }
					);
					definition.defenseActions.push_back(std::move(action));
				}
				return true;
			case MonsterSection::Resistances:
				for (const LuaEntry& entry : table.entries) {
					if (entry.key || entry.value.kind != LuaValue::Kind::Table) {
						limitation = "The elements table contains an unsupported entry.";
						return false;
					}
					MonsterResistance resistance;
					if (!LuaTextOrIdentifier(*entry.value.table, "type", resistance.type, limitation)
						|| !ReadLuaInteger(*entry.value.table, "percent", resistance.percent, limitation)) {
						return false;
					}
					resistance.customProperties = LuaCustomFields(*entry.value.table, { "type", "percent" });
					definition.resistances.push_back(std::move(resistance));
				}
				return true;
			case MonsterSection::Immunities:
				for (const LuaEntry& entry : table.entries) {
					if (entry.key || entry.value.kind != LuaValue::Kind::Table) {
						limitation = "The immunities table contains an unsupported entry.";
						return false;
					}
					MonsterImmunity immunity;
					if (!LuaTextOrIdentifier(*entry.value.table, "type", immunity.type, limitation)
						|| !ReadLuaBoolean(
							*entry.value.table,
							"combat",
							immunity.combat,
							immunity.usesCombat,
							limitation
						)
						|| !ReadLuaBoolean(
							*entry.value.table,
							"condition",
							immunity.condition,
							immunity.usesCondition,
							limitation
						)) {
						return false;
					}
					immunity.customProperties = LuaCustomFields(*entry.value.table, { "type", "combat", "condition" });
					definition.immunities.push_back(std::move(immunity));
				}
				return true;
			case MonsterSection::Loot:
				definition.lootProperties = LuaCustomFields(table, {});
				for (const LuaEntry& entry : table.entries) {
					if (entry.key) {
						continue;
					}
					if (entry.value.kind != LuaValue::Kind::Table) {
						limitation = "The loot table contains an unsupported entry.";
						return false;
					}
					MonsterLootEntry loot;
					if (!ParseLuaLootEntry(*entry.value.table, loot, limitation)) {
						return false;
					}
					loot.annotation = entry.annotation;
					definition.loot.push_back(std::move(loot));
				}
				return true;
			case MonsterSection::Summons: {
				if (!ReadLuaInteger(table, "maxSummons", definition.maxSummons, limitation)) {
					return false;
				}
				definition.summonProperties = LuaCustomFields(
					table,
					codec.nestedLuaSummons ? std::initializer_list<std::string_view> { "maxsummons", "summons" }
										   : std::initializer_list<std::string_view> { "maxsummons" }
				);
				const LuaTable* summonEntries = &table;
				if (codec.nestedLuaSummons) {
					const LuaEntry* nested = FindLuaField(table, "summons");
					if (!nested || nested->value.kind != LuaValue::Kind::Table) {
						limitation = "The Canary/Crystal summon section has no literal summons table.";
						return false;
					}
					summonEntries = nested->value.table.get();
				}
				for (const LuaEntry& entry : summonEntries->entries) {
					if (entry.key) {
						if (codec.nestedLuaSummons) {
							limitation = "The nested summons table contains an unsupported named entry.";
							return false;
						}
						continue;
					}
					if (entry.value.kind != LuaValue::Kind::Table) {
						limitation = "The summons table contains an unsupported entry.";
						return false;
					}
					MonsterSummon summon;
					if (!ReadLuaText(*entry.value.table, "name", summon.name, limitation)
						|| !ReadLuaInteger(*entry.value.table, "interval", summon.interval, limitation)
						|| !ReadLuaInteger(*entry.value.table, "chance", summon.chance, limitation)
						|| !ReadLuaInteger(*entry.value.table, codec.nestedLuaSummons ? "count" : "max", summon.max, limitation)) {
						return false;
					}
					bool forceUsed = false;
					if (!ReadLuaBoolean(*entry.value.table, "force", summon.force, forceUsed, limitation)) {
						return false;
					}
					summon.customProperties = LuaCustomFields(*entry.value.table, { "name", "interval", "chance", "max", "count", "force" });
					definition.summons.push_back(std::move(summon));
				}
				return true;
			}
			case MonsterSection::Voices:
				if (!ReadLuaInteger(table, "interval", definition.voices.interval, limitation)
					|| !ReadLuaInteger(table, "chance", definition.voices.chance, limitation)) {
					return false;
				}
				definition.voices.customProperties = LuaCustomFields(table, { "interval", "chance" });
				for (const LuaEntry& entry : table.entries) {
					if (entry.key) {
						continue;
					}
					if (entry.value.kind != LuaValue::Kind::Table) {
						limitation = "The voices table contains an unsupported entry.";
						return false;
					}
					MonsterVoice voice;
					if (!ReadLuaText(*entry.value.table, "text", voice.text, limitation)) {
						return false;
					}
					bool yellUsed = false;
					if (!ReadLuaBoolean(*entry.value.table, "yell", voice.yell, yellUsed, limitation)) {
						return false;
					}
					voice.customProperties = LuaCustomFields(*entry.value.table, { "text", "yell" });
					definition.voices.entries.push_back(std::move(voice));
				}
				return true;
			default:
				return false;
		}
	}

	void ParseLuaSections(MonsterSectionCodec::Impl& codec, MonsterDefinition& definition, std::string& error) {
		const std::vector<LuaToken> tokens = ScanLua(codec.source);
		for (std::size_t index = 0; index + 4 < tokens.size(); ++index) {
			if (tokens[index].kind == LuaTokenKind::Identifier && Symbol(tokens[index + 1], ":")
				&& LowerAscii(tokens[index + 2].value) == "register" && Symbol(tokens[index + 3], "(")
				&& tokens[index + 4].kind == LuaTokenKind::Identifier) {
				codec.monsterVariable = tokens[index + 4].value;
				codec.insertionOffset = tokens[index].begin;
				codec.hasInsertionAnchor = true;
			}
		}
		if (!codec.hasInsertionAnchor) {
			error = "The Lua monster has no literal registration call for inserting sections.";
			return;
		}

		for (std::size_t index = 0; index + 4 < tokens.size(); ++index) {
			if (tokens[index].kind != LuaTokenKind::Identifier || tokens[index].value != codec.monsterVariable
				|| !Symbol(tokens[index + 1], ".") || tokens[index + 2].kind != LuaTokenKind::Identifier
				|| !Symbol(tokens[index + 3], "=") || !Symbol(tokens[index + 4], "{")) {
				continue;
			}
			const std::string member = LowerAscii(tokens[index + 2].value);
			std::optional<MonsterSection> section;
			if (member == "defenses") {
				section = MonsterSection::Defenses;
			} else if (member == "elements") {
				section = MonsterSection::Resistances;
			} else if (member == "immunities") {
				section = MonsterSection::Immunities;
			} else if (member == "loot") {
				section = MonsterSection::Loot;
			} else if (member == "summons" || member == "summon") {
				section = MonsterSection::Summons;
				codec.nestedLuaSummons = member == "summon";
			} else if (member == "voices") {
				section = MonsterSection::Voices;
			} else if (member == "attacks") {
				section = MonsterSection::Attacks;
			}
			if (!section) {
				continue;
			}
			SectionState& state = codec.states[Index(*section)];
			if (state.present) {
				codec.limitation(definition, *section, "The source assigns this section more than once.");
				continue;
			}
			std::string parseError;
			ParsedLuaTable parsed = ParseLuaTable(tokens, codec.source, index + 4, parseError);
			if (!parsed.table) {
				codec.limitation(definition, *section, parseError);
				continue;
			}
			state.present = true;
			state.begin = tokens[index + 4].begin;
			state.end = tokens[parsed.closeToken].end;
			std::string limitation;
			if (!ParseLuaSection(codec, definition, *section, *parsed.table, limitation)) {
				codec.limitation(definition, *section, limitation);
			}
			index = parsed.closeToken;
		}
		codec.syncCapabilities(definition);
	}

	void SerializeXmlLoot(
		std::string& output,
		const MonsterLootEntry& entry,
		const std::string& indent,
		const std::string& newline
	) {
		output += indent + "<item";
		if (entry.usesName) {
			output += " name=\"" + EncodeXml(entry.itemName) + "\"";
		} else {
			output += " id=\"" + std::to_string(entry.itemId) + "\"";
		}
		output += " chance=\"" + std::to_string(entry.chance) + "\"";
		if (entry.maxCount != 1) {
			output += " countmax=\"" + std::to_string(entry.maxCount) + "\"";
		}
		if (entry.subtype != 0) {
			output += " subtype=\"" + std::to_string(entry.subtype) + "\"";
		}
		if (entry.actionId != 0) {
			output += " actionid=\"" + std::to_string(entry.actionId) + "\"";
		}
		if (!entry.text.empty()) {
			output += " text=\"" + EncodeXml(entry.text) + "\"";
		}
		for (const MonsterCustomProperty& property : entry.customProperties) {
			AppendXmlProperty(output, property);
		}
		if (entry.children.empty()) {
			output += "/>";
			if (!entry.annotation.empty()) {
				output += entry.annotation;
			}
			output += newline;
			return;
		}
		output += ">" + newline + indent + "\t<inside>" + newline;
		for (const MonsterLootEntry& child : entry.children) {
			SerializeXmlLoot(output, child, indent + "\t\t", newline);
		}
		output += indent + "\t</inside>" + newline + indent + "</item>";
		if (!entry.annotation.empty()) {
			output += entry.annotation;
		}
		output += newline;
	}

	std::string SerializeXmlSection(
		MonsterSection section,
		const MonsterDefinition& definition,
		const std::string& indent,
		const std::string& newline
	) {
		std::string output;
		const std::string child = indent + "\t";
		switch (section) {
			case MonsterSection::Attacks:
				output = "<attacks";
				for (const MonsterCustomProperty& property : definition.attackProperties) {
					AppendXmlProperty(output, property);
				}
				if (definition.attacks.empty()) {
					return output + "/>";
				}
				output += ">" + newline;
				for (const MonsterAttackDefinition& attack : definition.attacks) {
					output += child + "<attack";
					if (!attack.name.empty()) {
						output += " name=\"" + EncodeXml(attack.name) + "\"";
					}
					if (!attack.type.empty()) {
						output += " type=\"" + EncodeXml(attack.type) + "\"";
					}
					output += " interval=\"" + std::to_string(attack.interval) + "\" chance=\"" + std::to_string(attack.chance)
						+ "\" min=\"" + std::to_string(attack.minDamage) + "\" max=\"" + std::to_string(attack.maxDamage) + "\"";
					if (attack.skill != 0) {
						output += " skill=\"" + std::to_string(attack.skill) + "\"";
					}
					if (attack.attack != 0) {
						output += " attack=\"" + std::to_string(attack.attack) + "\"";
					}
					if (attack.area.range != 0) {
						output += " range=\"" + std::to_string(attack.area.range) + "\"";
					}
					if (attack.area.radius != 0) {
						output += " radius=\"" + std::to_string(attack.area.radius) + "\"";
					}
					if (attack.area.ring != 0) {
						output += " ring=\"" + std::to_string(attack.area.ring) + "\"";
					}
					if (attack.area.length != 0) {
						output += " length=\"" + std::to_string(attack.area.length) + "\"";
					}
					if (attack.area.spread != 0) {
						output += " spread=\"" + std::to_string(attack.area.spread) + "\"";
					}
					if (attack.area.target) {
						output += " target=\"1\"";
					}
					for (const MonsterCustomProperty& property : attack.customProperties) {
						AppendXmlProperty(output, property);
					}
					const bool hasChildren = !attack.effect.empty() || !attack.projectile.empty() || !attack.preservedChildren.empty() || !attack.annotation.empty();
					if (!hasChildren) {
						output += "/>" + newline;
						continue;
					}
					output += ">";
					if (!attack.effect.empty()) {
						output += "<attribute key=\"areaEffect\" value=\"" + EncodeXml(attack.effect) + "\"/>";
					}
					if (!attack.projectile.empty()) {
						output += "<attribute key=\"shootEffect\" value=\"" + EncodeXml(attack.projectile) + "\"/>";
					}
					output += attack.preservedChildren + attack.annotation + "</attack>" + newline;
				}
				output += indent + "</attacks>";
				return output;
			case MonsterSection::Defenses:
				output = "<defenses armor=\"" + std::to_string(definition.armor) + "\" defense=\""
					+ std::to_string(definition.defense) + "\"";
				for (const MonsterCustomProperty& property : definition.defenseProperties) {
					AppendXmlProperty(output, property);
				}
				if (definition.defenseActions.empty()) {
					return output + "/>";
				}
				output += ">" + newline;
				for (const MonsterDefenseAction& action : definition.defenseActions) {
					output += child + "<defense";
					if (!action.name.empty()) {
						output += " name=\"" + EncodeXml(action.name) + "\"";
					}
					if (!action.type.empty()) {
						output += " type=\"" + EncodeXml(action.type) + "\"";
					}
					output += " interval=\"" + std::to_string(action.interval) + "\" chance=\""
						+ std::to_string(action.chance) + "\" min=\"" + std::to_string(action.minDamage) + "\" max=\""
						+ std::to_string(action.maxDamage) + "\"";
					if (!action.effect.empty()) {
						output += " effect=\"" + EncodeXml(action.effect) + "\"";
					}
					if (action.target) {
						output += " target=\"1\"";
					}
					for (const MonsterCustomProperty& property : action.customProperties) {
						AppendXmlProperty(output, property);
					}
					if (action.preservedChildren.empty()) {
						output += "/>" + newline;
					} else {
						output += ">" + action.preservedChildren + "</defense>" + newline;
					}
				}
				output += indent + "</defenses>";
				return output;
			case MonsterSection::Resistances:
				output = "<elements>";
				if (!definition.resistances.empty()) {
					output += newline;
				}
				for (const MonsterResistance& resistance : definition.resistances) {
					output += child + "<element " + resistance.type + "Percent=\"" + std::to_string(resistance.percent) + "\"";
					for (const MonsterCustomProperty& property : resistance.customProperties) {
						AppendXmlProperty(output, property);
					}
					output += "/>" + newline;
				}
				output += definition.resistances.empty() ? "</elements>" : indent + "</elements>";
				return output;
			case MonsterSection::Immunities:
				output = "<immunities>";
				if (!definition.immunities.empty()) {
					output += newline;
				}
				for (const MonsterImmunity& immunity : definition.immunities) {
					output += child + "<immunity " + immunity.type + "=\"1\"";
					for (const MonsterCustomProperty& property : immunity.customProperties) {
						AppendXmlProperty(output, property);
					}
					output += "/>" + newline;
				}
				output += definition.immunities.empty() ? "</immunities>" : indent + "</immunities>";
				return output;
			case MonsterSection::Loot:
				output = "<loot";
				for (const MonsterCustomProperty& property : definition.lootProperties) {
					AppendXmlProperty(output, property);
				}
				if (definition.loot.empty()) {
					return output + "/>";
				}
				output += ">" + newline;
				for (const MonsterLootEntry& entry : definition.loot) {
					SerializeXmlLoot(output, entry, child, newline);
				}
				output += indent + "</loot>";
				return output;
			case MonsterSection::Summons:
				output = "<summons maxSummons=\"" + std::to_string(definition.maxSummons) + "\"";
				for (const MonsterCustomProperty& property : definition.summonProperties) {
					AppendXmlProperty(output, property);
				}
				if (definition.summons.empty()) {
					return output + "/>";
				}
				output += ">" + newline;
				for (const MonsterSummon& summon : definition.summons) {
					output += child + "<summon name=\"" + EncodeXml(summon.name) + "\" interval=\""
						+ std::to_string(summon.interval) + "\" chance=\"" + std::to_string(summon.chance) + "\"";
					if (summon.max != 0) {
						output += " max=\"" + std::to_string(summon.max) + "\"";
					}
					if (summon.force) {
						output += " force=\"1\"";
					}
					for (const MonsterCustomProperty& property : summon.customProperties) {
						AppendXmlProperty(output, property);
					}
					output += "/>" + newline;
				}
				output += indent + "</summons>";
				return output;
			case MonsterSection::Voices:
				output = "<voices interval=\"" + std::to_string(definition.voices.interval) + "\" chance=\""
					+ std::to_string(definition.voices.chance) + "\"";
				for (const MonsterCustomProperty& property : definition.voices.customProperties) {
					AppendXmlProperty(output, property);
				}
				if (definition.voices.entries.empty()) {
					return output + "/>";
				}
				output += ">" + newline;
				for (const MonsterVoice& voice : definition.voices.entries) {
					output += child + "<voice sentence=\"" + EncodeXml(voice.text) + "\" yell=\""
						+ std::string(voice.yell ? "1" : "0") + "\"";
					for (const MonsterCustomProperty& property : voice.customProperties) {
						AppendXmlProperty(output, property);
					}
					output += "/>" + newline;
				}
				output += indent + "</voices>";
				return output;
			default:
				return {};
		}
	}

	std::string LuaEnumOrString(std::string_view value) {
		if (value.starts_with("COMBAT_") || value.starts_with("CONST_")) {
			return std::string(value);
		}
		return "\"" + EncodeLua(value) + "\"";
	}

	void AppendLuaProperties(
		std::string& output,
		const std::vector<MonsterCustomProperty>& properties,
		const std::string& indent,
		const std::string& newline
	) {
		for (const MonsterCustomProperty& property : properties) {
			output += indent + property.name + " = " + LuaPropertyValue(property) + "," + newline;
		}
	}

	void SerializeLuaLoot(
		std::string& output,
		const MonsterLootEntry& entry,
		const std::string& indent,
		const std::string& newline
	) {
		output += indent + "{" + newline;
		const std::string field = indent + "\t";
		if (entry.usesName) {
			output += field + "name = \"" + EncodeLua(entry.itemName) + "\"," + newline;
		} else {
			output += field + "id = " + std::to_string(entry.itemId) + "," + newline;
		}
		output += field + "chance = " + std::to_string(entry.chance) + "," + newline;
		if (entry.maxCount != 1) {
			output += field + "maxCount = " + std::to_string(entry.maxCount) + "," + newline;
		}
		if (entry.subtype != 0) {
			output += field + "subType = " + std::to_string(entry.subtype) + "," + newline;
		}
		if (entry.actionId != 0) {
			output += field + "actionId = " + std::to_string(entry.actionId) + "," + newline;
		}
		if (!entry.text.empty()) {
			output += field + "text = \"" + EncodeLua(entry.text) + "\"," + newline;
		}
		AppendLuaProperties(output, entry.customProperties, field, newline);
		if (!entry.children.empty()) {
			output += field + "childLoot = {" + newline;
			for (const MonsterLootEntry& child : entry.children) {
				SerializeLuaLoot(output, child, field + "\t", newline);
			}
			output += field + "}," + newline;
		}
		output += indent + "},";
		if (!entry.annotation.empty()) {
			output += " " + entry.annotation;
		}
		output += newline;
	}

	std::string SerializeLuaSection(
		MonsterSection section,
		const MonsterDefinition& definition,
		const std::string& indent,
		const std::string& newline,
		bool nestedLuaSummons
	) {
		std::string output = "{";
		const std::string field = indent;
		bool any = false;
		auto begin = [&]() {
			if (!any) {
				output += newline;
				any = true;
			}
		};
		switch (section) {
			case MonsterSection::Attacks:
				for (const MonsterCustomProperty& property : definition.attackProperties) {
					begin();
					output += field + property.name + " = " + LuaPropertyValue(property) + "," + newline;
				}
				for (const MonsterAttackDefinition& attack : definition.attacks) {
					begin();
					output += field + "{" + newline;
					const std::string nested = field + "\t";
					if (!attack.name.empty()) {
						output += nested + "name = \"" + EncodeLua(attack.name) + "\"," + newline;
					}
					if (!attack.type.empty()) {
						output += nested + "type = " + LuaEnumOrString(attack.type) + "," + newline;
					}
					output += nested + "interval = " + std::to_string(attack.interval) + "," + newline;
					output += nested + "chance = " + std::to_string(attack.chance) + "," + newline;
					output += nested + "minDamage = " + std::to_string(attack.minDamage) + "," + newline;
					output += nested + "maxDamage = " + std::to_string(attack.maxDamage) + "," + newline;
					if (attack.skill != 0) {
						output += nested + "skill = " + std::to_string(attack.skill) + "," + newline;
					}
					if (attack.attack != 0) {
						output += nested + "attack = " + std::to_string(attack.attack) + "," + newline;
					}
					if (attack.area.range != 0) {
						output += nested + "range = " + std::to_string(attack.area.range) + "," + newline;
					}
					if (attack.area.radius != 0) {
						output += nested + "radius = " + std::to_string(attack.area.radius) + "," + newline;
					}
					if (attack.area.ring != 0) {
						output += nested + "ring = " + std::to_string(attack.area.ring) + "," + newline;
					}
					if (attack.area.length != 0) {
						output += nested + "length = " + std::to_string(attack.area.length) + "," + newline;
					}
					if (attack.area.spread != 0) {
						output += nested + "spread = " + std::to_string(attack.area.spread) + "," + newline;
					}
					if (attack.area.target) {
						output += nested + "target = true," + newline;
					}
					if (!attack.effect.empty()) {
						output += nested + "effect = " + LuaEnumOrString(attack.effect) + "," + newline;
					}
					if (!attack.projectile.empty()) {
						output += nested + "shootEffect = " + LuaEnumOrString(attack.projectile) + "," + newline;
					}
					AppendLuaProperties(output, attack.customProperties, nested, newline);
					output += field + "},";
					if (!attack.annotation.empty()) {
						output += " " + attack.annotation;
					}
					output += newline;
				}
				break;
			case MonsterSection::Defenses:
				begin();
				output += field + "defense = " + std::to_string(definition.defense) + "," + newline;
				output += field + "armor = " + std::to_string(definition.armor) + "," + newline;
				AppendLuaProperties(output, definition.defenseProperties, field, newline);
				for (const MonsterDefenseAction& action : definition.defenseActions) {
					output += field + "{" + newline;
					const std::string nested = field + "\t";
					if (!action.name.empty()) {
						output += nested + "name = \"" + EncodeLua(action.name) + "\"," + newline;
					}
					if (!action.type.empty()) {
						output += nested + "type = " + LuaEnumOrString(action.type) + "," + newline;
					}
					output += nested + "interval = " + std::to_string(action.interval) + "," + newline;
					output += nested + "chance = " + std::to_string(action.chance) + "," + newline;
					output += nested + "minDamage = " + std::to_string(action.minDamage) + "," + newline;
					output += nested + "maxDamage = " + std::to_string(action.maxDamage) + "," + newline;
					if (!action.effect.empty()) {
						output += nested + "effect = " + LuaEnumOrString(action.effect) + "," + newline;
					}
					output += nested + "target = " + std::string(action.target ? "true" : "false") + "," + newline;
					AppendLuaProperties(output, action.customProperties, nested, newline);
					output += field + "}," + newline;
				}
				break;
			case MonsterSection::Resistances:
				for (const MonsterResistance& resistance : definition.resistances) {
					begin();
					output += field + "{ type = " + LuaEnumOrString(resistance.type) + ", percent = "
						+ std::to_string(resistance.percent);
					for (const MonsterCustomProperty& property : resistance.customProperties) {
						output += ", " + property.name + " = " + LuaPropertyValue(property);
					}
					output += " }," + newline;
				}
				break;
			case MonsterSection::Immunities:
				for (const MonsterImmunity& immunity : definition.immunities) {
					begin();
					output += field + "{ type = \"" + EncodeLua(immunity.type) + "\"";
					if (immunity.usesCombat) {
						output += ", combat = " + std::string(immunity.combat ? "true" : "false");
					}
					if (immunity.usesCondition) {
						output += ", condition = " + std::string(immunity.condition ? "true" : "false");
					}
					for (const MonsterCustomProperty& property : immunity.customProperties) {
						output += ", " + property.name + " = " + LuaPropertyValue(property);
					}
					output += " }," + newline;
				}
				break;
			case MonsterSection::Loot:
				for (const MonsterCustomProperty& property : definition.lootProperties) {
					begin();
					output += field + property.name + " = " + LuaPropertyValue(property) + "," + newline;
				}
				for (const MonsterLootEntry& entry : definition.loot) {
					begin();
					SerializeLuaLoot(output, entry, field, newline);
				}
				break;
			case MonsterSection::Summons:
				begin();
				output += field + "maxSummons = " + std::to_string(definition.maxSummons) + "," + newline;
				AppendLuaProperties(output, definition.summonProperties, field, newline);
				if (nestedLuaSummons) {
					output += field + "summons = {" + newline;
				}
				{
					const std::string summonIndent = nestedLuaSummons ? field + "\t" : field;
					for (const MonsterSummon& summon : definition.summons) {
						output += summonIndent + "{ name = \"" + EncodeLua(summon.name) + "\", interval = "
							+ std::to_string(summon.interval) + ", chance = " + std::to_string(summon.chance);
						if (summon.max != 0) {
							output += nestedLuaSummons ? ", count = " : ", max = ";
							output += std::to_string(summon.max);
						}
						if (summon.force) {
							output += ", force = true";
						}
						for (const MonsterCustomProperty& property : summon.customProperties) {
							output += ", " + property.name + " = " + LuaPropertyValue(property);
						}
						output += " }," + newline;
					}
				}
				if (nestedLuaSummons) {
					output += field + "}," + newline;
				}
				break;
			case MonsterSection::Voices:
				begin();
				output += field + "interval = " + std::to_string(definition.voices.interval) + "," + newline;
				output += field + "chance = " + std::to_string(definition.voices.chance) + "," + newline;
				AppendLuaProperties(output, definition.voices.customProperties, field, newline);
				for (const MonsterVoice& voice : definition.voices.entries) {
					output += field + "{ text = \"" + EncodeLua(voice.text) + "\", yell = "
						+ std::string(voice.yell ? "true" : "false");
					for (const MonsterCustomProperty& property : voice.customProperties) {
						output += ", " + property.name + " = " + LuaPropertyValue(property);
					}
					output += " }," + newline;
				}
				break;
			default:
				break;
		}
		if (any) {
			output += indent.substr(0, indent.size() >= 1 ? indent.size() - 1 : 0);
		}
		output += "}";
		return output;
	}
}

std::unique_ptr<MonsterSectionCodec> MonsterSectionCodec::Parse(
	ServerContentFormat format,
	ServerType serverType,
	std::string_view source,
	MonsterDefinition& definition,
	std::string& error
) {
	error.clear();
	auto implementation = std::make_unique<Impl>();
	implementation->format = format;
	implementation->nestedLuaSummons = UsesCanaryCrystalLoader(serverType);
	implementation->source = std::string(source);
	implementation->newline = NewlineFor(source);
	if (format == ServerContentFormat::Xml) {
		ParseXmlSections(*implementation, definition, error);
	} else if (format == ServerContentFormat::Lua) {
		ParseLuaSections(*implementation, definition, error);
	} else {
		error = "Unsupported monster section source format.";
	}
	if (!error.empty()) {
		return nullptr;
	}
	return std::unique_ptr<MonsterSectionCodec>(new MonsterSectionCodec(std::move(implementation)));
}

MonsterSectionCodec::MonsterSectionCodec(std::unique_ptr<Impl> implementation) :
	implementation(std::move(implementation)) { }

MonsterSectionCodec::~MonsterSectionCodec() = default;
MonsterSectionCodec::MonsterSectionCodec(MonsterSectionCodec&&) noexcept = default;
MonsterSectionCodec& MonsterSectionCodec::operator=(MonsterSectionCodec&&) noexcept = default;

bool MonsterSectionCodec::hasChanges(const MonsterDefinition& original, const MonsterDefinition& edited) const {
	for (std::size_t index = 0; index < SectionCount; ++index) {
		if (SectionChanged(static_cast<MonsterSection>(index), original, edited)) {
			return true;
		}
	}
	return false;
}

bool MonsterSectionCodec::buildPatches(
	const MonsterDefinition& original,
	const MonsterDefinition& edited,
	std::vector<MonsterTextPatch>& patches,
	std::string& error
) const {
	error.clear();
	std::string insertion;
	for (std::size_t index = 0; index < SectionCount; ++index) {
		const MonsterSection section = static_cast<MonsterSection>(index);
		if (!SectionChanged(section, original, edited)) {
			continue;
		}
		const SectionState& state = implementation->states[index];
		if (!state.editable) {
			error = std::string(MonsterSectionName(section)) + " cannot be saved safely: " + state.limitation;
			return false;
		}
		const std::string serialized = implementation->format == ServerContentFormat::Xml
			? SerializeXmlSection(section, edited, implementation->indent, implementation->newline)
			: SerializeLuaSection(section, edited, implementation->indent, implementation->newline, implementation->nestedLuaSummons);
		if (state.present) {
			patches.push_back({ state.begin, state.end, serialized });
		} else {
			if (!implementation->hasInsertionAnchor) {
				error = std::string(MonsterSectionName(section)) + " has no safe insertion point.";
				return false;
			}
			if (implementation->format == ServerContentFormat::Xml) {
				insertion += implementation->indent + serialized + implementation->newline;
			} else {
				insertion += implementation->monsterVariable + "." + SectionMember(section, implementation->nestedLuaSummons) + " = " + serialized
					+ implementation->newline;
			}
		}
	}
	if (!insertion.empty()) {
		patches.push_back({ implementation->insertionOffset, implementation->insertionOffset, std::move(insertion) });
	}
	return true;
}
