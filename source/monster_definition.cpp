//////////////////////////////////////////////////////////////////////
// Source-preserving monster definition model for XML and Lua servers.
//////////////////////////////////////////////////////////////////////

#include "monster_definition.h"

#include "ext/pugixml.hpp"
#include "file_transaction.h"
#include "monster_section_codec.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <fstream>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {
	constexpr std::size_t FieldCount = static_cast<std::size_t>(MonsterField::Count);

	enum class LiteralStyle : uint8_t {
		Text,
		Integer,
		Boolean,
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
		LiteralStyle style = LiteralStyle::Text;
		char quote = '\0';
	};

	struct XmlAttribute {
		std::string name;
		std::string value;
		std::size_t begin = 0;
		std::size_t end = 0;
		char quote = '\0';
	};

	struct XmlTag {
		std::string name;
		std::string parent;
		std::vector<XmlAttribute> attributes;
		std::size_t depth = 0;
	};

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
		char quote = '\0';
	};

	std::size_t Index(MonsterField field) {
		return static_cast<std::size_t>(field);
	}

	std::string LowerAscii(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return value;
	}

	bool IsNameStart(unsigned char character) {
		return std::isalpha(character) || character == '_';
	}

	bool IsNamePart(unsigned char character) {
		return std::isalnum(character) || character == '_' || character == '-' || character == ':';
	}

	std::optional<std::string> ReadFile(const std::filesystem::path& path, std::string& error) {
		std::ifstream stream(path, std::ios::binary);
		if (!stream.is_open()) {
			error = "Could not open " + path.string() + ".";
			return std::nullopt;
		}
		stream.seekg(0, std::ios::end);
		const std::streamoff length = stream.tellg();
		if (length < 0 || length > 16 * 1024 * 1024) {
			error = "Monster source is too large to edit safely: " + path.string() + ".";
			return std::nullopt;
		}
		stream.seekg(0, std::ios::beg);
		std::string bytes(static_cast<std::size_t>(length), '\0');
		if (!bytes.empty()) {
			stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		}
		if (!stream) {
			error = "Could not read " + path.string() + ".";
			return std::nullopt;
		}
		return bytes;
	}

	bool WriteFile(const std::filesystem::path& path, std::string_view bytes, std::string& error) {
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		if (!stream.is_open()) {
			error = "Could not stage " + path.string() + ".";
			return false;
		}
		stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		if (!stream) {
			error = "Could not write staged source " + path.string() + ".";
			return false;
		}
		return true;
	}

	std::string DecodeXml(std::string_view value) {
		std::string decoded;
		decoded.reserve(value.size());
		for (std::size_t index = 0; index < value.size();) {
			if (value[index] != '&') {
				decoded.push_back(value[index++]);
				continue;
			}
			const std::size_t end = value.find(';', index + 1);
			if (end == std::string_view::npos) {
				decoded.push_back(value[index++]);
				continue;
			}
			const std::string_view entity = value.substr(index, end - index + 1);
			if (entity == "&amp;") {
				decoded.push_back('&');
			} else if (entity == "&lt;") {
				decoded.push_back('<');
			} else if (entity == "&gt;") {
				decoded.push_back('>');
			} else if (entity == "&quot;") {
				decoded.push_back('"');
			} else if (entity == "&apos;") {
				decoded.push_back('\'');
			} else {
				decoded.append(entity);
			}
			index = end + 1;
		}
		return decoded;
	}

	std::string EncodeXml(std::string_view value, char quote) {
		std::string encoded;
		for (char character : value) {
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

	std::vector<XmlTag> ScanXml(std::string_view bytes) {
		std::vector<XmlTag> tags;
		std::vector<std::string> stack;
		for (std::size_t cursor = 0; cursor < bytes.size();) {
			cursor = bytes.find('<', cursor);
			if (cursor == std::string_view::npos) {
				break;
			}
			if (bytes.substr(cursor, 4) == "<!--") {
				const std::size_t end = bytes.find("-->", cursor + 4);
				cursor = end == std::string_view::npos ? bytes.size() : end + 3;
				continue;
			}
			if (bytes.substr(cursor, 9) == "<![CDATA[") {
				const std::size_t end = bytes.find("]]>", cursor + 9);
				cursor = end == std::string_view::npos ? bytes.size() : end + 3;
				continue;
			}
			std::size_t index = cursor + 1;
			if (index < bytes.size() && (bytes[index] == '?' || bytes[index] == '!')) {
				const std::size_t end = bytes.find('>', index + 1);
				cursor = end == std::string_view::npos ? bytes.size() : end + 1;
				continue;
			}
			if (index < bytes.size() && bytes[index] == '/') {
				const std::size_t end = bytes.find('>', index + 1);
				if (!stack.empty()) {
					stack.pop_back();
				}
				cursor = end == std::string_view::npos ? bytes.size() : end + 1;
				continue;
			}
			while (index < bytes.size() && std::isspace(static_cast<unsigned char>(bytes[index]))) {
				++index;
			}
			const std::size_t nameBegin = index;
			while (index < bytes.size() && IsNamePart(static_cast<unsigned char>(bytes[index]))) {
				++index;
			}
			if (nameBegin == index) {
				++cursor;
				continue;
			}
			XmlTag tag;
			tag.name = LowerAscii(std::string(bytes.substr(nameBegin, index - nameBegin)));
			tag.parent = stack.empty() ? std::string() : stack.back();
			tag.depth = stack.size();
			bool selfClosing = false;
			while (index < bytes.size()) {
				while (index < bytes.size() && std::isspace(static_cast<unsigned char>(bytes[index]))) {
					++index;
				}
				if (index >= bytes.size()) {
					break;
				}
				if (bytes[index] == '>') {
					++index;
					break;
				}
				if (bytes[index] == '/' && index + 1 < bytes.size() && bytes[index + 1] == '>') {
					selfClosing = true;
					index += 2;
					break;
				}
				const std::size_t attributeBegin = index;
				while (index < bytes.size() && IsNamePart(static_cast<unsigned char>(bytes[index]))) {
					++index;
				}
				if (attributeBegin == index) {
					++index;
					continue;
				}
				const std::string attributeName = LowerAscii(std::string(bytes.substr(attributeBegin, index - attributeBegin)));
				while (index < bytes.size() && std::isspace(static_cast<unsigned char>(bytes[index]))) {
					++index;
				}
				if (index >= bytes.size() || bytes[index] != '=') {
					continue;
				}
				++index;
				while (index < bytes.size() && std::isspace(static_cast<unsigned char>(bytes[index]))) {
					++index;
				}
				if (index >= bytes.size() || (bytes[index] != '"' && bytes[index] != '\'')) {
					continue;
				}
				const char quote = bytes[index++];
				const std::size_t valueBegin = index;
				while (index < bytes.size() && bytes[index] != quote) {
					++index;
				}
				if (index >= bytes.size()) {
					break;
				}
				tag.attributes.push_back({ attributeName, DecodeXml(bytes.substr(valueBegin, index - valueBegin)), valueBegin, index, quote });
				++index;
			}
			tags.push_back(std::move(tag));
			if (!selfClosing) {
				stack.push_back(tags.back().name);
			}
			cursor = index;
		}
		return tags;
	}

	const XmlAttribute* FindAttribute(const XmlTag& tag, std::string_view name) {
		const auto found = std::find_if(tag.attributes.begin(), tag.attributes.end(), [name](const XmlAttribute& attribute) {
			return attribute.name == name;
		});
		return found == tag.attributes.end() ? nullptr : &*found;
	}

	std::string DecodeLua(std::string_view value) {
		std::string decoded;
		for (std::size_t index = 0; index < value.size(); ++index) {
			if (value[index] != '\\' || index + 1 >= value.size()) {
				decoded.push_back(value[index]);
				continue;
			}
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
		}
		return decoded;
	}

	std::string EncodeLua(std::string_view value, char quote) {
		std::string encoded;
		for (char character : value) {
			switch (character) {
				case '\\':
					encoded += "\\\\";
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
					if (character == quote) {
						encoded.push_back('\\');
					}
					encoded.push_back(character);
					break;
			}
		}
		return encoded;
	}

	std::optional<std::size_t> LuaLongBracketEnd(std::string_view bytes, std::size_t begin) {
		if (begin >= bytes.size() || bytes[begin] != '[') {
			return std::nullopt;
		}
		std::size_t marker = begin + 1;
		while (marker < bytes.size() && bytes[marker] == '=') {
			++marker;
		}
		if (marker >= bytes.size() || bytes[marker] != '[') {
			return std::nullopt;
		}
		const std::string closing = "]" + std::string(marker - begin - 1, '=') + "]";
		const std::size_t end = bytes.find(closing, marker + 1);
		return end == std::string_view::npos ? bytes.size() : end + closing.size();
	}

	std::vector<LuaToken> ScanLua(std::string_view bytes) {
		std::vector<LuaToken> tokens;
		for (std::size_t index = 0; index < bytes.size();) {
			const unsigned char character = static_cast<unsigned char>(bytes[index]);
			if (std::isspace(character)) {
				++index;
				continue;
			}
			if (bytes.substr(index, 2) == "--") {
				if (const auto longCommentEnd = LuaLongBracketEnd(bytes, index + 2)) {
					index = *longCommentEnd;
				} else {
					const std::size_t end = bytes.find('\n', index + 2);
					index = end == std::string_view::npos ? bytes.size() : end + 1;
				}
				continue;
			}
			if (const auto longStringEnd = LuaLongBracketEnd(bytes, index)) {
				index = *longStringEnd;
				continue;
			}
			if (bytes[index] == '"' || bytes[index] == '\'') {
				const char quote = bytes[index++];
				const std::size_t valueBegin = index;
				while (index < bytes.size()) {
					if (bytes[index] == '\\' && index + 1 < bytes.size()) {
						index += 2;
						continue;
					}
					if (bytes[index] == quote) {
						break;
					}
					++index;
				}
				if (index >= bytes.size()) {
					break;
				}
				tokens.push_back({ LuaTokenKind::String, DecodeLua(bytes.substr(valueBegin, index - valueBegin)), valueBegin, index, quote });
				++index;
				continue;
			}
			if (IsNameStart(character)) {
				const std::size_t begin = index++;
				while (index < bytes.size() && (std::isalnum(static_cast<unsigned char>(bytes[index])) || bytes[index] == '_')) {
					++index;
				}
				tokens.push_back({ LuaTokenKind::Identifier, std::string(bytes.substr(begin, index - begin)), begin, index, '\0' });
				continue;
			}
			if (std::isdigit(character) || (bytes[index] == '-' && index + 1 < bytes.size() && std::isdigit(static_cast<unsigned char>(bytes[index + 1])))) {
				const std::size_t begin = index++;
				while (index < bytes.size() && (std::isalnum(static_cast<unsigned char>(bytes[index])) || bytes[index] == '.' || bytes[index] == 'x' || bytes[index] == 'X')) {
					++index;
				}
				tokens.push_back({ LuaTokenKind::Number, std::string(bytes.substr(begin, index - begin)), begin, index, '\0' });
				continue;
			}
			tokens.push_back({ LuaTokenKind::Symbol, std::string(1, bytes[index]), index, index + 1, '\0' });
			++index;
		}
		return tokens;
	}

	bool IsSymbol(const LuaToken& token, std::string_view value) {
		return token.kind == LuaTokenKind::Symbol && token.value == value;
	}

	std::optional<int> ParseInteger(std::string_view value) {
		int result = 0;
		const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
		if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size()) {
			return std::nullopt;
		}
		return result;
	}

	std::optional<bool> ParseBoolean(std::string_view value) {
		const std::string lowered = LowerAscii(std::string(value));
		if (lowered == "true" || lowered == "yes" || lowered == "1") {
			return true;
		}
		if (lowered == "false" || lowered == "no" || lowered == "0") {
			return false;
		}
		return std::nullopt;
	}

	bool IsStringField(MonsterField field) {
		return field == MonsterField::Name || field == MonsterField::Description || field == MonsterField::Race || field == MonsterField::Skull;
	}

	bool IsBooleanField(MonsterField field) {
		return field >= MonsterField::Summonable && field <= MonsterField::CanPushCreatures;
	}

	std::string FieldValue(const MonsterDefinition& definition, MonsterField field) {
		switch (field) {
			case MonsterField::Name:
				return definition.name;
			case MonsterField::Description:
				return definition.description;
			case MonsterField::Race:
				return definition.race;
			case MonsterField::Health:
				return std::to_string(definition.health);
			case MonsterField::MaxHealth:
				return std::to_string(definition.maxHealth);
			case MonsterField::Experience:
				return std::to_string(definition.experience);
			case MonsterField::Speed:
				return std::to_string(definition.speed);
			case MonsterField::Armor:
				return std::to_string(definition.armor);
			case MonsterField::Defense:
				return std::to_string(definition.defense);
			case MonsterField::TargetDistance:
				return std::to_string(definition.targetDistance);
			case MonsterField::Corpse:
				return std::to_string(definition.corpse);
			case MonsterField::ManaCost:
				return std::to_string(definition.manaCost);
			case MonsterField::Skull:
				return definition.skull;
			case MonsterField::TargetChangeInterval:
				return std::to_string(definition.targetChangeInterval);
			case MonsterField::TargetChangeChance:
				return std::to_string(definition.targetChangeChance);
			case MonsterField::StrategyAttack:
				return std::to_string(definition.strategyAttack);
			case MonsterField::StrategyDefense:
				return std::to_string(definition.strategyDefense);
			case MonsterField::Summonable:
				return definition.summonable ? "true" : "false";
			case MonsterField::Convinceable:
				return definition.convinceable ? "true" : "false";
			case MonsterField::Attackable:
				return definition.attackable ? "true" : "false";
			case MonsterField::Hostile:
				return definition.hostile ? "true" : "false";
			case MonsterField::Pushable:
				return definition.pushable ? "true" : "false";
			case MonsterField::CanPushItems:
				return definition.canPushItems ? "true" : "false";
			case MonsterField::CanPushCreatures:
				return definition.canPushCreatures ? "true" : "false";
			case MonsterField::StaticAttack:
				return std::to_string(definition.staticAttack);
			case MonsterField::LightLevel:
				return std::to_string(definition.lightLevel);
			case MonsterField::LightColor:
				return std::to_string(definition.lightColor);
			case MonsterField::RunOnHealth:
				return std::to_string(definition.runOnHealth);
			case MonsterField::LookType:
				return std::to_string(definition.outfit.lookType);
			case MonsterField::LookTypeEx:
				return std::to_string(definition.outfit.lookTypeEx);
			case MonsterField::LookHead:
				return std::to_string(definition.outfit.head);
			case MonsterField::LookBody:
				return std::to_string(definition.outfit.body);
			case MonsterField::LookLegs:
				return std::to_string(definition.outfit.legs);
			case MonsterField::LookFeet:
				return std::to_string(definition.outfit.feet);
			case MonsterField::LookAddons:
				return std::to_string(definition.outfit.addons);
			case MonsterField::LookMount:
				return std::to_string(definition.outfit.mount);
			case MonsterField::Count:
				break;
		}
		return {};
	}

	bool AssignField(MonsterDefinition& definition, MonsterField field, std::string_view value) {
		if (IsStringField(field)) {
			switch (field) {
				case MonsterField::Name:
					definition.name = value;
					return true;
				case MonsterField::Description:
					definition.description = value;
					return true;
				case MonsterField::Race:
					definition.race = value;
					return true;
				case MonsterField::Skull:
					definition.skull = value;
					return true;
				default:
					return false;
			}
		}
		if (IsBooleanField(field)) {
			const auto parsed = ParseBoolean(value);
			if (!parsed) {
				return false;
			}
			switch (field) {
				case MonsterField::Summonable:
					definition.summonable = *parsed;
					break;
				case MonsterField::Convinceable:
					definition.convinceable = *parsed;
					break;
				case MonsterField::Attackable:
					definition.attackable = *parsed;
					break;
				case MonsterField::Hostile:
					definition.hostile = *parsed;
					break;
				case MonsterField::Pushable:
					definition.pushable = *parsed;
					break;
				case MonsterField::CanPushItems:
					definition.canPushItems = *parsed;
					break;
				case MonsterField::CanPushCreatures:
					definition.canPushCreatures = *parsed;
					break;
				default:
					return false;
			}
			return true;
		}
		const auto parsed = ParseInteger(value);
		if (!parsed) {
			return false;
		}
		switch (field) {
			case MonsterField::Health:
				definition.health = *parsed;
				break;
			case MonsterField::MaxHealth:
				definition.maxHealth = *parsed;
				break;
			case MonsterField::Experience:
				definition.experience = *parsed;
				break;
			case MonsterField::Speed:
				definition.speed = *parsed;
				break;
			case MonsterField::Armor:
				definition.armor = *parsed;
				break;
			case MonsterField::Defense:
				definition.defense = *parsed;
				break;
			case MonsterField::TargetDistance:
				definition.targetDistance = *parsed;
				break;
			case MonsterField::Corpse:
				definition.corpse = *parsed;
				break;
			case MonsterField::ManaCost:
				definition.manaCost = *parsed;
				break;
			case MonsterField::TargetChangeInterval:
				definition.targetChangeInterval = *parsed;
				break;
			case MonsterField::TargetChangeChance:
				definition.targetChangeChance = *parsed;
				break;
			case MonsterField::StrategyAttack:
				definition.strategyAttack = *parsed;
				break;
			case MonsterField::StrategyDefense:
				definition.strategyDefense = *parsed;
				break;
			case MonsterField::StaticAttack:
				definition.staticAttack = *parsed;
				break;
			case MonsterField::LightLevel:
				definition.lightLevel = *parsed;
				break;
			case MonsterField::LightColor:
				definition.lightColor = *parsed;
				break;
			case MonsterField::RunOnHealth:
				definition.runOnHealth = *parsed;
				break;
			case MonsterField::LookType:
				definition.outfit.lookType = *parsed;
				break;
			case MonsterField::LookTypeEx:
				definition.outfit.lookTypeEx = *parsed;
				break;
			case MonsterField::LookHead:
				definition.outfit.head = *parsed;
				break;
			case MonsterField::LookBody:
				definition.outfit.body = *parsed;
				break;
			case MonsterField::LookLegs:
				definition.outfit.legs = *parsed;
				break;
			case MonsterField::LookFeet:
				definition.outfit.feet = *parsed;
				break;
			case MonsterField::LookAddons:
				definition.outfit.addons = *parsed;
				break;
			case MonsterField::LookMount:
				definition.outfit.mount = *parsed;
				break;
			default:
				return false;
		}
		return true;
	}

	std::optional<MonsterField> DirectLuaField(std::string_view name) {
		const std::string field = LowerAscii(std::string(name));
		if (field == "name") {
			return MonsterField::Name;
		}
		if (field == "description") {
			return MonsterField::Description;
		}
		if (field == "race") {
			return MonsterField::Race;
		}
		if (field == "health") {
			return MonsterField::Health;
		}
		if (field == "maxhealth") {
			return MonsterField::MaxHealth;
		}
		if (field == "experience") {
			return MonsterField::Experience;
		}
		if (field == "speed") {
			return MonsterField::Speed;
		}
		if (field == "corpse") {
			return MonsterField::Corpse;
		}
		if (field == "manacost") {
			return MonsterField::ManaCost;
		}
		if (field == "skull") {
			return MonsterField::Skull;
		}
		return std::nullopt;
	}

	std::optional<MonsterField> TableLuaField(std::string_view table, std::string_view name) {
		const std::string group = LowerAscii(std::string(table));
		const std::string field = LowerAscii(std::string(name));
		if (group == "outfit") {
			if (field == "looktype") {
				return MonsterField::LookType;
			}
			if (field == "looktypeex" || field == "lookitem") {
				return MonsterField::LookTypeEx;
			}
			if (field == "lookhead") {
				return MonsterField::LookHead;
			}
			if (field == "lookbody") {
				return MonsterField::LookBody;
			}
			if (field == "looklegs") {
				return MonsterField::LookLegs;
			}
			if (field == "lookfeet") {
				return MonsterField::LookFeet;
			}
			if (field == "lookaddons" || field == "lookaddon") {
				return MonsterField::LookAddons;
			}
			if (field == "lookmount") {
				return MonsterField::LookMount;
			}
		}
		if (group == "flags") {
			if (field == "summonable") {
				return MonsterField::Summonable;
			}
			if (field == "convinceable") {
				return MonsterField::Convinceable;
			}
			if (field == "attackable") {
				return MonsterField::Attackable;
			}
			if (field == "hostile") {
				return MonsterField::Hostile;
			}
			if (field == "pushable") {
				return MonsterField::Pushable;
			}
			if (field == "canpushitems") {
				return MonsterField::CanPushItems;
			}
			if (field == "canpushcreatures") {
				return MonsterField::CanPushCreatures;
			}
			if (field == "targetdistance") {
				return MonsterField::TargetDistance;
			}
			if (field == "staticattackchance" || field == "staticattack") {
				return MonsterField::StaticAttack;
			}
			if (field == "runhealth" || field == "runonhealth") {
				return MonsterField::RunOnHealth;
			}
		}
		if (group == "changetarget" || group == "targetchange") {
			if (field == "interval") {
				return MonsterField::TargetChangeInterval;
			}
			if (field == "chance") {
				return MonsterField::TargetChangeChance;
			}
		}
		if (group == "strategy") {
			if (field == "attack") {
				return MonsterField::StrategyAttack;
			}
			if (field == "defense") {
				return MonsterField::StrategyDefense;
			}
		}
		if (group == "defenses") {
			if (field == "armor") {
				return MonsterField::Armor;
			}
			if (field == "defense") {
				return MonsterField::Defense;
			}
		}
		if (group == "light") {
			if (field == "level") {
				return MonsterField::LightLevel;
			}
			if (field == "color" || field == "colour") {
				return MonsterField::LightColor;
			}
		}
		return std::nullopt;
	}

	std::optional<MonsterField> XmlRootField(std::string_view name) {
		const std::string field = LowerAscii(std::string(name));
		if (field == "name") {
			return MonsterField::Name;
		}
		if (field == "namedescription") {
			return MonsterField::Description;
		}
		if (field == "race") {
			return MonsterField::Race;
		}
		if (field == "experience") {
			return MonsterField::Experience;
		}
		if (field == "speed") {
			return MonsterField::Speed;
		}
		if (field == "manacost") {
			return MonsterField::ManaCost;
		}
		if (field == "skull") {
			return MonsterField::Skull;
		}
		return std::nullopt;
	}

	std::optional<MonsterField> XmlChildField(std::string_view tag, std::string_view name) {
		const std::string group = LowerAscii(std::string(tag));
		const std::string field = LowerAscii(std::string(name));
		if (group == "health") {
			if (field == "now") {
				return MonsterField::Health;
			}
			if (field == "max") {
				return MonsterField::MaxHealth;
			}
		}
		if (group == "look") {
			if (field == "type") {
				return MonsterField::LookType;
			}
			if (field == "typeex" || field == "item" || field == "lookitem" || field == "lookex") {
				return MonsterField::LookTypeEx;
			}
			if (field == "head") {
				return MonsterField::LookHead;
			}
			if (field == "body") {
				return MonsterField::LookBody;
			}
			if (field == "legs") {
				return MonsterField::LookLegs;
			}
			if (field == "feet") {
				return MonsterField::LookFeet;
			}
			if (field == "addons" || field == "addon") {
				return MonsterField::LookAddons;
			}
			if (field == "mount") {
				return MonsterField::LookMount;
			}
			if (field == "corpse") {
				return MonsterField::Corpse;
			}
		}
		if (group == "targetchange") {
			if (field == "interval") {
				return MonsterField::TargetChangeInterval;
			}
			if (field == "chance") {
				return MonsterField::TargetChangeChance;
			}
		}
		if (group == "strategy") {
			if (field == "attack") {
				return MonsterField::StrategyAttack;
			}
			if (field == "defense") {
				return MonsterField::StrategyDefense;
			}
		}
		if (group == "defenses") {
			if (field == "armor") {
				return MonsterField::Armor;
			}
			if (field == "defense") {
				return MonsterField::Defense;
			}
		}
		if (group == "light") {
			if (field == "level") {
				return MonsterField::LightLevel;
			}
			if (field == "color" || field == "colour") {
				return MonsterField::LightColor;
			}
		}
		return std::nullopt;
	}

	std::optional<MonsterField> XmlFlagField(std::string_view name) {
		const std::string field = LowerAscii(std::string(name));
		if (field == "summonable") {
			return MonsterField::Summonable;
		}
		if (field == "convinceable") {
			return MonsterField::Convinceable;
		}
		if (field == "attackable") {
			return MonsterField::Attackable;
		}
		if (field == "hostile") {
			return MonsterField::Hostile;
		}
		if (field == "pushable") {
			return MonsterField::Pushable;
		}
		if (field == "canpushitems") {
			return MonsterField::CanPushItems;
		}
		if (field == "canpushcreatures") {
			return MonsterField::CanPushCreatures;
		}
		if (field == "targetdistance") {
			return MonsterField::TargetDistance;
		}
		if (field == "staticattack") {
			return MonsterField::StaticAttack;
		}
		if (field == "runonhealth") {
			return MonsterField::RunOnHealth;
		}
		if (field == "lightlevel") {
			return MonsterField::LightLevel;
		}
		if (field == "lightcolor" || field == "lightcolour") {
			return MonsterField::LightColor;
		}
		return std::nullopt;
	}
}

struct MonsterDefinitionDocument::Impl {
	ServerContentSource sourceInfo;
	MonsterDefinition original;
	std::vector<SourceFile> files;
	std::array<std::vector<SourceLocation>, FieldCount> locations;
	std::array<bool, FieldCount> assigned {};
	std::unique_ptr<MonsterSectionCodec> sections;

	void markUnsupported(MonsterField field, const std::string& reason) {
		auto& capability = original.capabilities[Index(field)];
		capability.present = true;
		capability.editable = false;
		capability.limitation = reason;
	}

	void addLiteral(MonsterField field, std::string_view value, const SourceLocation& location, bool coordinated = false) {
		auto& capability = original.capabilities[Index(field)];
		if (!assigned[Index(field)]) {
			if (!AssignField(original, field, value)) {
				markUnsupported(field, "The source value is not a supported literal.");
				assigned[Index(field)] = true;
				return;
			}
			assigned[Index(field)] = true;
			capability.present = true;
			capability.editable = true;
			locations[Index(field)].push_back(location);
			return;
		}
		if (coordinated && capability.editable && FieldValue(original, field) == value) {
			locations[Index(field)].push_back(location);
			return;
		}
		capability.present = true;
		capability.editable = false;
		capability.limitation = "The field has multiple or conflicting source assignments.";
		locations[Index(field)].clear();
	}

	void addXmlAttribute(MonsterField field, const XmlAttribute& attribute, std::size_t file, bool coordinated = false) {
		addLiteral(
			field,
			attribute.value,
			{ file, attribute.begin, attribute.end, IsStringField(field) ? LiteralStyle::Text : (IsBooleanField(field) ? LiteralStyle::Boolean : LiteralStyle::Integer), attribute.quote },
			coordinated
		);
	}

	void parseXmlFile(std::size_t fileIndex, bool declaration, std::string& error) {
		const SourceFile& file = files[fileIndex];
		pugi::xml_document document;
		const pugi::xml_parse_result result = document.load_buffer(file.bytes.data(), file.bytes.size(), pugi::parse_default | pugi::parse_comments);
		if (!result) {
			error = "Malformed XML in " + file.path.string() + ": " + result.description();
			return;
		}
		const std::vector<XmlTag> tags = ScanXml(file.bytes);
		if (declaration) {
			const auto root = std::find_if(tags.begin(), tags.end(), [](const XmlTag& tag) { return tag.depth == 0 && tag.name == "monster"; });
			if (root == tags.end()) {
				error = "The indexed XML source does not contain a monster root element.";
				return;
			}
			for (const XmlAttribute& attribute : root->attributes) {
				if (const auto field = XmlRootField(attribute.name)) {
					addXmlAttribute(*field, attribute, fileIndex);
				}
			}
			for (const XmlTag& tag : tags) {
				if (tag.depth == 1) {
					for (const XmlAttribute& attribute : tag.attributes) {
						if (const auto field = XmlChildField(tag.name, attribute.name)) {
							addXmlAttribute(*field, attribute, fileIndex);
						}
					}
				} else if (tag.name == "flag" && tag.parent == "flags") {
					for (const XmlAttribute& attribute : tag.attributes) {
						if (const auto field = XmlFlagField(attribute.name)) {
							addXmlAttribute(*field, attribute, fileIndex);
						}
					}
				}
			}
			return;
		}

		for (const XmlTag& tag : tags) {
			if (tag.name != "monster") {
				continue;
			}
			const XmlAttribute* fileAttribute = FindAttribute(tag, "file");
			const XmlAttribute* nameAttribute = FindAttribute(tag, "name");
			if (!fileAttribute || !nameAttribute) {
				continue;
			}
			const auto* pathBegin = reinterpret_cast<const char8_t*>(fileAttribute->value.data());
			const std::filesystem::path registered = (file.path.parent_path()
													  / std::filesystem::path(std::u8string(pathBegin, pathBegin + fileAttribute->value.size())))
														 .lexically_normal();
			if (FileSaveTransaction::PathsReferToSameFile(registered, sourceInfo.declarationPath)) {
				addXmlAttribute(MonsterField::Name, *nameAttribute, fileIndex, true);
			}
		}
	}

	void addLuaToken(MonsterField field, const LuaToken& token, std::size_t fileIndex, bool coordinated = false) {
		LiteralStyle style = LiteralStyle::Integer;
		if (token.kind == LuaTokenKind::String) {
			style = LiteralStyle::Text;
		} else if (token.kind == LuaTokenKind::Identifier && (token.value == "true" || token.value == "false")) {
			style = LiteralStyle::Boolean;
		} else if (token.kind != LuaTokenKind::Number) {
			markUnsupported(field, "The Lua value is computed or uses an unsupported expression.");
			return;
		}
		addLiteral(field, token.value, { fileIndex, token.begin, token.end, style, token.quote }, coordinated);
	}

	void parseLuaFile(std::size_t fileIndex, std::string& error) {
		const SourceFile& file = files[fileIndex];
		const std::vector<LuaToken> tokens = ScanLua(file.bytes);
		std::string monsterVariable = "monster";
		for (std::size_t index = 0; index + 4 < tokens.size(); ++index) {
			if (tokens[index].kind == LuaTokenKind::Identifier && IsSymbol(tokens[index + 1], ":")
				&& LowerAscii(tokens[index + 2].value) == "register" && IsSymbol(tokens[index + 3], "(")
				&& tokens[index + 4].kind == LuaTokenKind::Identifier) {
				monsterVariable = tokens[index + 4].value;
			}
		}

		bool factoryFound = false;
		for (std::size_t index = 0; index + 4 < tokens.size(); ++index) {
			if (LowerAscii(tokens[index].value) == "game" && IsSymbol(tokens[index + 1], ".")
				&& LowerAscii(tokens[index + 2].value) == "createmonstertype" && IsSymbol(tokens[index + 3], "(")
				&& tokens[index + 4].kind == LuaTokenKind::String) {
				addLuaToken(MonsterField::Name, tokens[index + 4], fileIndex, factoryFound);
				factoryFound = true;
			}
		}
		if (!factoryFound) {
			error = "The indexed Lua source does not contain Game.createMonsterType with a literal name.";
			return;
		}

		for (std::size_t index = 0; index + 3 < tokens.size(); ++index) {
			if (tokens[index].kind != LuaTokenKind::Identifier || tokens[index].value != monsterVariable
				|| !IsSymbol(tokens[index + 1], ".") || tokens[index + 2].kind != LuaTokenKind::Identifier || !IsSymbol(tokens[index + 3], "=")) {
				continue;
			}
			const std::string member = tokens[index + 2].value;
			const std::size_t valueIndex = index + 4;
			if (valueIndex >= tokens.size()) {
				break;
			}
			if (const auto field = DirectLuaField(member)) {
				const bool literal = tokens[valueIndex].kind == LuaTokenKind::String || tokens[valueIndex].kind == LuaTokenKind::Number
					|| (tokens[valueIndex].kind == LuaTokenKind::Identifier && (tokens[valueIndex].value == "true" || tokens[valueIndex].value == "false"));
				if (literal) {
					addLuaToken(*field, tokens[valueIndex], fileIndex, *field == MonsterField::Name);
				} else {
					markUnsupported(*field, "The Lua value is computed or uses an unsupported expression.");
				}
				continue;
			}
			if (!IsSymbol(tokens[valueIndex], "{")) {
				continue;
			}
			int depth = 1;
			for (std::size_t item = valueIndex + 1; item + 2 < tokens.size() && depth > 0; ++item) {
				if (IsSymbol(tokens[item], "{")) {
					++depth;
					continue;
				}
				if (IsSymbol(tokens[item], "}")) {
					--depth;
					continue;
				}
				if (depth != 1 || tokens[item].kind != LuaTokenKind::Identifier || !IsSymbol(tokens[item + 1], "=")) {
					continue;
				}
				if (const auto field = TableLuaField(member, tokens[item].value)) {
					const LuaToken& value = tokens[item + 2];
					const bool literal = value.kind == LuaTokenKind::String || value.kind == LuaTokenKind::Number
						|| (value.kind == LuaTokenKind::Identifier && (value.value == "true" || value.value == "false"));
					if (literal) {
						addLuaToken(*field, value, fileIndex);
					} else {
						markUnsupported(*field, "The Lua table value is computed or uses an unsupported expression.");
					}
				}
			}
		}
	}

	std::string replacement(const MonsterDefinition& edited, MonsterField field, const SourceLocation& location) const {
		const std::string value = FieldValue(edited, field);
		if (location.style == LiteralStyle::Text) {
			return sourceInfo.format == ServerContentFormat::Xml ? EncodeXml(value, location.quote) : EncodeLua(value, location.quote);
		}
		if (location.style == LiteralStyle::Boolean && sourceInfo.format == ServerContentFormat::Xml) {
			const std::string_view oldValue(files[location.file].bytes.data() + location.begin, location.end - location.begin);
			if (oldValue == "1" || oldValue == "0") {
				return value == "true" ? "1" : "0";
			}
			const std::string lowered = LowerAscii(std::string(oldValue));
			if (lowered == "yes" || lowered == "no") {
				return value == "true" ? "yes" : "no";
			}
		}
		return value;
	}
};

const MonsterFieldCapability& MonsterDefinition::capability(MonsterField field) const {
	return capabilities[Index(field)];
}

const MonsterSectionCapability& MonsterDefinition::capability(MonsterSection section) const {
	return sectionCapabilities[static_cast<std::size_t>(section)];
}

std::unique_ptr<MonsterDefinitionDocument> MonsterDefinitionDocument::Load(const ServerContentSource& source, std::string& error) {
	error.clear();
	if (source.kind != ServerContentKind::Monster || (source.format != ServerContentFormat::Xml && source.format != ServerContentFormat::Lua)) {
		error = "This source is not a supported monster definition.";
		return nullptr;
	}
	if (source.declarationPath.empty()) {
		error = "The monster declaration path is empty.";
		return nullptr;
	}

	const auto declaration = ReadFile(source.declarationPath, error);
	if (!declaration) {
		return nullptr;
	}
	std::vector<std::string> files { *declaration };
	if (source.format == ServerContentFormat::Xml && source.registrationPath && !FileSaveTransaction::PathsReferToSameFile(*source.registrationPath, source.declarationPath)) {
		const auto registration = ReadFile(*source.registrationPath, error);
		if (!registration) {
			return nullptr;
		}
		files.push_back(*registration);
	}
	return LoadFromText(source, std::move(files), error);
}

std::unique_ptr<MonsterDefinitionDocument> MonsterDefinitionDocument::LoadFromText(const ServerContentSource& source, std::vector<std::string> files, std::string& error) {
	auto implementation = std::make_unique<Impl>();
	implementation->sourceInfo = source;
	implementation->files.push_back({ source.declarationPath, ResourceFingerprint::Read(source.declarationPath), std::move(files.front()) });
	if (source.format == ServerContentFormat::Xml) {
		implementation->parseXmlFile(0, true, error);
		if (!error.empty()) {
			return nullptr;
		}
		if (files.size() > 1 && source.registrationPath) {
			implementation->files.push_back({ *source.registrationPath, ResourceFingerprint::Read(*source.registrationPath), std::move(files[1]) });
			implementation->parseXmlFile(1, false, error);
			if (!error.empty()) {
				return nullptr;
			}
		}
	} else {
		implementation->parseLuaFile(0, error);
		if (!error.empty()) {
			return nullptr;
		}
	}

	implementation->sections = MonsterSectionCodec::Parse(source.format, source.serverType, implementation->files.front().bytes, implementation->original, error);
	if (!implementation->sections) {
		return nullptr;
	}

	for (std::size_t field = 0; field < FieldCount; ++field) {
		auto& capability = implementation->original.capabilities[field];
		if (!capability.present) {
			capability.limitation = "This field is not present as a supported literal in the source.";
		}
	}
	return std::unique_ptr<MonsterDefinitionDocument>(new MonsterDefinitionDocument(std::move(implementation)));
}

MonsterDefinitionDocument::MonsterDefinitionDocument(std::unique_ptr<Impl> implementation) :
	implementation(std::move(implementation)) { }

MonsterDefinitionDocument::~MonsterDefinitionDocument() = default;
MonsterDefinitionDocument::MonsterDefinitionDocument(MonsterDefinitionDocument&&) noexcept = default;
MonsterDefinitionDocument& MonsterDefinitionDocument::operator=(MonsterDefinitionDocument&&) noexcept = default;

const MonsterDefinition& MonsterDefinitionDocument::definition() const {
	return implementation->original;
}

const ServerContentSource& MonsterDefinitionDocument::source() const {
	return implementation->sourceInfo;
}

const std::string& MonsterDefinitionDocument::sourceText() const {
	return implementation->files.front().bytes;
}

bool MonsterDefinitionDocument::hasChanges(const MonsterDefinition& edited) const {
	for (std::size_t index = 0; index < FieldCount; ++index) {
		const auto field = static_cast<MonsterField>(index);
		if (FieldValue(implementation->original, field) != FieldValue(edited, field)) {
			return true;
		}
	}
	return implementation->sections && implementation->sections->hasChanges(implementation->original, edited);
}

bool MonsterDefinitionDocument::save(const MonsterDefinition& edited, std::string& error) {
	error.clear();
	if (!ValidateMonsterDefinition(edited, error)) {
		return false;
	}
	struct Patch {
		std::size_t begin = 0;
		std::size_t end = 0;
		std::string replacement;
	};
	std::vector<std::vector<Patch>> patches(implementation->files.size());
	std::vector<MonsterTextPatch> sectionPatches;
	if (implementation->sections
		&& !implementation->sections->buildPatches(implementation->original, edited, sectionPatches, error)) {
		return false;
	}
	for (MonsterTextPatch& patch : sectionPatches) {
		patches.front().push_back({ patch.begin, patch.end, std::move(patch.replacement) });
	}
	const auto coveredBySectionPatch = [&sectionPatches](const SourceLocation& location) {
		return location.file == 0
			&& std::any_of(sectionPatches.begin(), sectionPatches.end(), [&location](const MonsterTextPatch& patch) {
				   return patch.begin < patch.end && location.begin >= patch.begin && location.end <= patch.end;
			   });
	};
	for (std::size_t index = 0; index < FieldCount; ++index) {
		const auto field = static_cast<MonsterField>(index);
		if (FieldValue(implementation->original, field) == FieldValue(edited, field)) {
			continue;
		}
		const auto& capability = implementation->original.capabilities[index];
		const bool replacedBySection = std::any_of(
			implementation->locations[index].begin(),
			implementation->locations[index].end(),
			coveredBySectionPatch
		);
		if (replacedBySection) {
			continue;
		}
		if (!capability.editable || implementation->locations[index].empty()) {
			error = std::string(MonsterFieldName(field)) + " cannot be saved safely: " + capability.limitation;
			return false;
		}
		for (const SourceLocation& location : implementation->locations[index]) {
			if (coveredBySectionPatch(location)) {
				continue;
			}
			patches[location.file].push_back({ location.begin, location.end, implementation->replacement(edited, field, location) });
		}
	}
	if (std::all_of(patches.begin(), patches.end(), [](const auto& filePatches) { return filePatches.empty(); })) {
		return true;
	}

	std::vector<std::string> updated;
	updated.reserve(implementation->files.size());
	for (std::size_t fileIndex = 0; fileIndex < implementation->files.size(); ++fileIndex) {
		const SourceFile& file = implementation->files[fileIndex];
		const auto current = ReadFile(file.path, error);
		if (!current) {
			return false;
		}
		if (!file.fingerprint.MatchesCurrentFile() || *current != file.bytes) {
			error = "The source changed on disk after the editor opened. Reopen the monster before saving: " + file.path.string() + ".";
			return false;
		}
		updated.push_back(file.bytes);
		auto& filePatches = patches[fileIndex];
		std::sort(filePatches.begin(), filePatches.end(), [](const Patch& left, const Patch& right) { return left.begin > right.begin; });
		for (const Patch& patch : filePatches) {
			updated.back().replace(patch.begin, patch.end - patch.begin, patch.replacement);
		}
	}

	FileSaveTransaction transaction;
	for (std::size_t fileIndex = 0; fileIndex < implementation->files.size(); ++fileIndex) {
		if (patches[fileIndex].empty()) {
			continue;
		}
		if (!WriteFile(transaction.Stage(implementation->files[fileIndex].path), updated[fileIndex], error)) {
			return false;
		}
	}
	if (!transaction.Commit(error)) {
		return false;
	}

	implementation->sourceInfo.declarationFingerprint = ResourceFingerprint::Read(implementation->sourceInfo.declarationPath);
	if (implementation->sourceInfo.registrationPath) {
		implementation->sourceInfo.registrationFingerprint = ResourceFingerprint::Read(*implementation->sourceInfo.registrationPath);
	}
	auto refreshed = LoadFromText(implementation->sourceInfo, std::move(updated), error);
	if (!refreshed) {
		return false;
	}
	implementation = std::move(refreshed->implementation);
	return true;
}

const char* MonsterFieldName(MonsterField field) {
	static constexpr std::array<const char*, FieldCount> Names {
		"Name",
		"Description",
		"Race",
		"Health",
		"Max health",
		"Experience",
		"Speed",
		"Armor",
		"Defense",
		"Target distance",
		"Corpse",
		"Mana cost",
		"Skull",
		"Target change interval",
		"Target change chance",
		"Strategy attack",
		"Strategy defense",
		"Summonable",
		"Convinceable",
		"Attackable",
		"Hostile",
		"Pushable",
		"Can push items",
		"Can push creatures",
		"Static attack",
		"Light level",
		"Light color",
		"Run on health",
		"Look type",
		"Look type ex",
		"Head",
		"Body",
		"Legs",
		"Feet",
		"Addons",
		"Mount",
	};
	const std::size_t index = Index(field);
	return index < Names.size() ? Names[index] : "Unknown";
}

const char* MonsterSectionName(MonsterSection section) {
	static constexpr std::array<const char*, static_cast<std::size_t>(MonsterSection::Count)> Names {
		"Defenses",
		"Resistances",
		"Immunities",
		"Loot",
		"Summons",
		"Voices",
		"Attacks",
	};
	const std::size_t index = static_cast<std::size_t>(section);
	return index < Names.size() ? Names[index] : "Unknown";
}

bool ValidateMonsterDefinition(const MonsterDefinition& definition, std::string& error) {
	error.clear();
	const auto validChance = [&error](int chance, const std::string& label) {
		if (chance < 0 || chance > 100000000) {
			error = label + " chance must be between 0 and 100000000.";
			return false;
		}
		return true;
	};
	const auto validInterval = [&error](int interval, const std::string& label) {
		if (interval < 0) {
			error = label + " interval cannot be negative.";
			return false;
		}
		return true;
	};
	for (const MonsterAttackDefinition& attack : definition.attacks) {
		if (attack.name.empty() && attack.type.empty()) {
			error = "Every attack needs a name or combat type.";
			return false;
		}
		if (!validInterval(attack.interval, "Attack") || !validChance(attack.chance, "Attack")) {
			return false;
		}
		if (attack.area.range < 0 || attack.area.radius < 0 || attack.area.ring < 0 || attack.area.length < 0 || attack.area.spread < 0) {
			error = "Attack range and area values cannot be negative.";
			return false;
		}
	}
	for (const MonsterDefenseAction& action : definition.defenseActions) {
		if (action.name.empty() && action.type.empty()) {
			error = "Every defense action needs a name or type.";
			return false;
		}
		if (!validInterval(action.interval, "Defense action") || !validChance(action.chance, "Defense action")) {
			return false;
		}
	}
	for (const MonsterResistance& resistance : definition.resistances) {
		if (resistance.type.empty()) {
			error = "Every resistance needs a type.";
			return false;
		}
		if (resistance.percent < -100 || resistance.percent > 100) {
			error = "Resistance percent must be between -100 and 100.";
			return false;
		}
	}
	for (const MonsterImmunity& immunity : definition.immunities) {
		if (immunity.type.empty()) {
			error = "Every immunity needs a type.";
			return false;
		}
	}
	const auto validateLoot = [&](const auto& self, const std::vector<MonsterLootEntry>& entries) -> bool {
		for (const MonsterLootEntry& entry : entries) {
			if ((entry.usesName && entry.itemName.empty()) || (!entry.usesName && entry.itemId <= 0)) {
				error = "Every loot entry needs a valid item name or ID.";
				return false;
			}
			if (!validChance(entry.chance, "Loot item")) {
				return false;
			}
			if (entry.maxCount < 1) {
				error = "Loot max count must be at least 1.";
				return false;
			}
			if (!self(self, entry.children)) {
				return false;
			}
		}
		return true;
	};
	if (!validateLoot(validateLoot, definition.loot)) {
		return false;
	}
	if (definition.maxSummons < 0) {
		error = "Maximum summons cannot be negative.";
		return false;
	}
	for (const MonsterSummon& summon : definition.summons) {
		if (summon.name.empty()) {
			error = "Every summon needs a creature name.";
			return false;
		}
		if (!validInterval(summon.interval, "Summon") || !validChance(summon.chance, "Summon") || summon.max < 0) {
			if (error.empty()) {
				error = "Summon max cannot be negative.";
			}
			return false;
		}
	}
	if (!validInterval(definition.voices.interval, "Voices") || !validChance(definition.voices.chance, "Voices")) {
		return false;
	}
	for (const MonsterVoice& voice : definition.voices.entries) {
		if (voice.text.empty()) {
			error = "Voice text cannot be empty.";
			return false;
		}
	}
	return true;
}
