//////////////////////////////////////////////////////////////////////
// Source-preserving NPC model for XML and Lua Server Workspaces.
//////////////////////////////////////////////////////////////////////

#include "npc_definition.h"

#include "ext/pugixml.hpp"
#include "file_transaction.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <optional>
#include <regex>
#include <string_view>
#include <tuple>

namespace {
	constexpr std::size_t FieldCount = static_cast<std::size_t>(NpcField::Count);

	struct Location {
		std::size_t begin = 0;
		std::size_t end = 0;
		char quote = '\0';
	};

	struct MessageSource {
		Location text;
	};

	struct ShopSource {
		std::optional<Location> name;
		std::optional<Location> id;
		std::optional<Location> buy;
		std::optional<Location> sell;
	};

	struct TravelSource {
		std::optional<Location> keyword;
		std::optional<Location> cost;
		std::optional<Location> x;
		std::optional<Location> y;
		std::optional<Location> z;
		std::optional<Location> premium;
	};

	std::optional<std::string> ReadFile(const std::filesystem::path& path, std::string& error) {
		std::ifstream stream(path, std::ios::binary);
		if (!stream.is_open()) {
			error = "Could not open NPC source " + path.string() + ".";
			return std::nullopt;
		}
		stream.seekg(0, std::ios::end);
		const std::streamoff length = stream.tellg();
		if (length < 0 || length > 16 * 1024 * 1024) {
			error = "NPC source is too large to edit safely: " + path.string() + ".";
			return std::nullopt;
		}
		stream.seekg(0, std::ios::beg);
		std::string contents(static_cast<std::size_t>(length), '\0');
		if (!contents.empty()) {
			stream.read(contents.data(), static_cast<std::streamsize>(contents.size()));
		}
		if (!stream) {
			error = "Could not read NPC source " + path.string() + ".";
			return std::nullopt;
		}
		return contents;
	}

	bool WriteFile(const std::filesystem::path& path, std::string_view contents, std::string& error) {
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		if (!stream.is_open()) {
			error = "Could not stage NPC source " + path.string() + ".";
			return false;
		}
		stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
		if (!stream) {
			error = "Could not write staged NPC source " + path.string() + ".";
			return false;
		}
		return true;
	}

	std::optional<int> Integer(std::string_view value) {
		int result = 0;
		const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
		return parsed.ec == std::errc() && parsed.ptr == value.data() + value.size() ? std::optional<int>(result) : std::nullopt;
	}

	std::string Lower(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
			return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
		});
		return value;
	}

	std::string DecodeXml(std::string value) {
		const std::array<std::pair<std::string_view, std::string_view>, 5> entities { {
			{ "&quot;", "\"" },
			{ "&apos;", "'" },
			{ "&lt;", "<" },
			{ "&gt;", ">" },
			{ "&amp;", "&" },
		} };
		for (const auto& [entity, decoded] : entities) {
			for (std::size_t position = 0; (position = value.find(entity, position)) != std::string::npos;) {
				value.replace(position, entity.size(), decoded);
				position += decoded.size();
			}
		}
		return value;
	}

	std::string EncodeXml(std::string_view value, char quote) {
		std::string result;
		for (const char c : value) {
			switch (c) {
				case '&':
					result += "&amp;";
					break;
				case '<':
					result += "&lt;";
					break;
				case '>':
					result += "&gt;";
					break;
				case '"':
					result += quote == '"' ? "&quot;" : "\"";
					break;
				case '\'':
					result += quote == '\'' ? "&apos;" : "'";
					break;
				default:
					result.push_back(c);
					break;
			}
		}
		return result;
	}

	std::string EncodeLua(std::string_view value, char quote) {
		std::string result;
		for (const char c : value) {
			if (c == '\\' || c == quote) {
				result.push_back('\\');
			}
			if (c == '\n') {
				result += "\\n";
			} else if (c == '\r') {
				result += "\\r";
			} else {
				result.push_back(c);
			}
		}
		return result;
	}

	std::optional<std::smatch> First(const std::string& text, const std::regex& expression) {
		std::smatch match;
		return std::regex_search(text, match, expression) ? std::optional<std::smatch>(match) : std::nullopt;
	}

	Location Capture(const std::smatch& match, std::size_t group, std::size_t base = 0, char quote = '\0') {
		return { base + static_cast<std::size_t>(match.position(group)), base + static_cast<std::size_t>(match.position(group) + match.length(group)), quote };
	}

	std::optional<Location> Attribute(const std::string& text, std::string_view tag, std::string_view attribute, std::string& value) {
		const std::regex tagExpression("<\\s*" + std::string(tag) + "\\b[^>]*>", std::regex::icase);
		const auto tagMatch = First(text, tagExpression);
		if (!tagMatch) {
			return std::nullopt;
		}
		const std::string body = tagMatch->str(0);
		const std::regex attributeExpression("\\b" + std::string(attribute) + R"(\s*=\s*(["'])(.*?)\1)", std::regex::icase);
		const auto attributeMatch = First(body, attributeExpression);
		if (!attributeMatch) {
			return std::nullopt;
		}
		value = DecodeXml(attributeMatch->str(2));
		return Capture(*attributeMatch, 2, static_cast<std::size_t>(tagMatch->position(0)), attributeMatch->str(1)[0]);
	}

	std::optional<Location> LuaString(const std::string& text, const std::string& prefix, std::string& value) {
		const std::regex expression(prefix + R"(\s*=\s*(["'])(.*?)\1)", std::regex::icase);
		const auto match = First(text, expression);
		if (!match) {
			return std::nullopt;
		}
		value = match->str(2);
		return Capture(*match, 2, 0, match->str(1)[0]);
	}

	std::optional<Location> LuaNumber(const std::string& text, const std::string& prefix, int& value) {
		const std::regex expression(prefix + R"(\s*=\s*(-?\d+))", std::regex::icase);
		const auto match = First(text, expression);
		if (!match) {
			return std::nullopt;
		}
		value = Integer(match->str(1)).value_or(value);
		return Capture(*match, 1);
	}

	std::optional<Location> LuaBoolean(const std::string& text, const std::string& prefix, bool& value) {
		const std::regex expression(prefix + R"(\s*=\s*(true|false))", std::regex::icase);
		const auto match = First(text, expression);
		if (!match) {
			return std::nullopt;
		}
		value = Lower(match->str(1)) == "true";
		return Capture(*match, 1);
	}

	std::optional<Location> LuaMethodNumber(const std::string& text, const std::string& object, const std::string& method, int& value) {
		const std::regex expression("\\b" + object + "\\s*:\\s*" + method + R"(\s*\(\s*(-?\d+)\s*\))", std::regex::icase);
		const auto match = First(text, expression);
		if (!match) {
			return std::nullopt;
		}
		value = Integer(match->str(1)).value_or(value);
		return Capture(*match, 1);
	}

	std::optional<Location> LuaMethodBoolean(const std::string& text, const std::string& object, const std::string& method, bool& value) {
		const std::regex expression("\\b" + object + "\\s*:\\s*" + method + R"(\s*\(\s*(true|false)\s*\))", std::regex::icase);
		const auto match = First(text, expression);
		if (!match) {
			return std::nullopt;
		}
		value = Lower(match->str(1)) == "true";
		return Capture(*match, 1);
	}

	std::optional<std::pair<std::size_t, std::size_t>> LuaTable(const std::string& text, const std::regex& startExpression) {
		const auto start = First(text, startExpression);
		if (!start) {
			return std::nullopt;
		}
		const std::size_t open = text.find('{', static_cast<std::size_t>(start->position(0)));
		if (open == std::string::npos) {
			return std::nullopt;
		}
		int depth = 0;
		char quote = '\0';
		bool escaped = false;
		for (std::size_t cursor = open; cursor < text.size(); ++cursor) {
			const char character = text[cursor];
			if (quote) {
				if (escaped) {
					escaped = false;
				} else if (character == '\\') {
					escaped = true;
				} else if (character == quote) {
					quote = '\0';
				}
				continue;
			}
			if (character == '\'' || character == '"') {
				quote = character;
			} else if (character == '{') {
				++depth;
			} else if (character == '}' && --depth == 0) {
				return std::pair(open, cursor);
			}
		}
		return std::nullopt;
	}

	std::size_t LineStart(const std::string& text, std::size_t position) {
		const std::size_t newline = position == 0 ? std::string::npos : text.rfind('\n', position - 1);
		return newline == std::string::npos ? 0 : newline + 1;
	}

	std::size_t LineEnd(const std::string& text, std::size_t position) {
		const std::size_t newline = text.find('\n', position);
		return newline == std::string::npos ? text.size() : newline + 1;
	}

	std::string Newline(const std::string& text) {
		return text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
	}

	std::size_t MatchCount(const std::string& text, const std::regex& expression) {
		return static_cast<std::size_t>(std::distance(std::sregex_iterator(text.begin(), text.end(), expression), std::sregex_iterator()));
	}

	bool HasLuaAssignment(const std::string& text, const std::string& prefix) {
		return std::regex_search(text, std::regex(prefix + R"(\s*=)", std::regex::icase));
	}

	bool HasLuaMethod(const std::string& text, const std::string& object, const std::string& method) {
		return std::regex_search(text, std::regex("\\b" + object + "\\s*:\\s*" + method + R"(\s*\()", std::regex::icase));
	}
}

struct NpcDefinitionDocument::Impl {
	enum class LuaProvider : uint8_t { None,
									   ConfigTable,
									   DirectMethods };

	ServerContentSource sourceInfo;
	ResourceFingerprint fingerprint;
	std::string bytes;
	NpcDefinition original;
	std::array<std::vector<Location>, FieldCount> fields;
	std::vector<MessageSource> messages;
	std::vector<ShopSource> shop;
	std::vector<TravelSource> travel;
	LuaProvider luaProvider = LuaProvider::None;
	std::string luaObject;
	std::size_t scalarInsert = std::string::npos;
	std::size_t directInsert = std::string::npos;
	std::optional<std::pair<std::size_t, std::size_t>> outfitTable;
	std::optional<std::pair<std::size_t, std::size_t>> flagsTable;
};

namespace {
	void SetCapability(
		NpcDefinitionDocument::Impl& impl,
		NpcField field,
		const std::optional<Location>& location,
		NpcFieldCapability::State missingState = NpcFieldCapability::State::Unsupported,
		std::string missingReason = "This server provider does not expose this field."
	) {
		auto& capability = impl.original.capabilities[static_cast<std::size_t>(field)];
		if (location) {
			impl.fields[static_cast<std::size_t>(field)].push_back(*location);
		}
		capability.present = location.has_value();
		capability.state = location ? NpcFieldCapability::State::ExistingEditableLiteral : missingState;
		capability.editable = location.has_value() || missingState == NpcFieldCapability::State::SupportedInsertable;
		capability.limitation = location ? std::string() : std::move(missingReason);
	}

	void MarkDynamic(NpcDefinitionDocument::Impl& impl, NpcField field, const std::string& reason) {
		auto& capability = impl.original.capabilities[static_cast<std::size_t>(field)];
		capability.state = NpcFieldCapability::State::DynamicReadOnly;
		capability.present = true;
		capability.editable = false;
		capability.limitation = reason;
	}

	void MarkAmbiguous(NpcDefinitionDocument::Impl& impl, NpcField field, const std::string& reason) {
		auto& capability = impl.original.capabilities[static_cast<std::size_t>(field)];
		capability.state = NpcFieldCapability::State::Ambiguous;
		capability.present = true;
		capability.editable = false;
		capability.limitation = reason;
		impl.fields[static_cast<std::size_t>(field)].clear();
	}

	void SetLuaCapability(
		NpcDefinitionDocument::Impl& impl,
		NpcField field,
		const std::optional<Location>& location,
		bool supported,
		bool expressionPresent,
		bool ambiguous = false
	) {
		if (ambiguous) {
			MarkAmbiguous(impl, field, "More than one matching declaration was found; edit the source directly.");
		} else if (location) {
			SetCapability(impl, field, location);
		} else if (expressionPresent) {
			MarkDynamic(impl, field, "This value is computed by Lua and is preserved read-only.");
		} else if (supported) {
			SetCapability(
				impl,
				field,
				std::nullopt,
				NpcFieldCapability::State::SupportedInsertable,
				"This property is absent and will be inserted using the detected server API."
			);
		} else {
			SetCapability(impl, field, std::nullopt);
		}
	}

	void ParseXml(NpcDefinitionDocument::Impl& impl, std::string& error) {
		pugi::xml_document document;
		const auto parsed = document.load_buffer(impl.bytes.data(), impl.bytes.size());
		if (!parsed || !document.child("npc")) {
			error = "NPC XML is malformed or has no <npc> root.";
			return;
		}
		std::string value;
		auto stringField = [&](NpcField field, std::string_view tag, std::string_view attribute, std::string& target) {
			auto location = Attribute(impl.bytes, tag, attribute, value);
			if (location) {
				target = value;
			}
			SetCapability(impl, field, location);
		};
		auto numberField = [&](NpcField field, std::string_view tag, std::string_view attribute, int& target) {
			auto location = Attribute(impl.bytes, tag, attribute, value);
			if (location) {
				target = Integer(value).value_or(target);
			}
			SetCapability(impl, field, location);
		};
		stringField(NpcField::Name, "npc", "name", impl.original.name);
		stringField(NpcField::Script, "npc", "script", impl.original.script);
		numberField(NpcField::WalkInterval, "npc", "walkinterval", impl.original.walkInterval);
		numberField(NpcField::Speed, "npc", "speed", impl.original.speed);
		auto floor = Attribute(impl.bytes, "npc", "floorchange", value);
		if (floor) {
			impl.original.floorChange = value == "1" || Lower(value) == "true";
		}
		SetCapability(impl, NpcField::FloorChange, floor);
		numberField(NpcField::Direction, "npc", "lookdir", impl.original.direction);
		numberField(NpcField::Health, "health", "now", impl.original.health);
		numberField(NpcField::MaxHealth, "health", "max", impl.original.maxHealth);
		numberField(NpcField::LookType, "look", "type", impl.original.lookType);
		numberField(NpcField::LookTypeEx, "look", "typeex", impl.original.lookTypeEx);
		numberField(NpcField::LookHead, "look", "head", impl.original.lookHead);
		numberField(NpcField::LookBody, "look", "body", impl.original.lookBody);
		numberField(NpcField::LookLegs, "look", "legs", impl.original.lookLegs);
		numberField(NpcField::LookFeet, "look", "feet", impl.original.lookFeet);
		numberField(NpcField::LookAddons, "look", "addons", impl.original.lookAddons);
		numberField(NpcField::LookMount, "look", "mount", impl.original.lookMount);
		SetCapability(impl, NpcField::Description, std::nullopt);
		SetCapability(impl, NpcField::WalkRadius, std::nullopt);

		const std::regex parameter(R"(<\s*parameter\b[^>]*\bkey\s*=\s*(["'])(.*?)\1[^>]*\bvalue\s*=\s*(["'])(.*?)\3[^>]*/?>)", std::regex::icase);
		for (std::sregex_iterator iterator(impl.bytes.begin(), impl.bytes.end(), parameter), end; iterator != end; ++iterator) {
			const std::smatch& match = *iterator;
			const std::string key = Lower(match.str(2));
			const Location location = Capture(match, 4, 0, match.str(3)[0]);
			if (key.starts_with("message_")) {
				impl.original.messages.push_back({ key, DecodeXml(match.str(4)), true });
				impl.messages.push_back({ location });
			} else if (key == "shop_buyable" || key == "shop_sellable") {
				const bool buying = key == "shop_buyable";
				const std::string raw = match.str(4);
				const std::regex entry(R"(([^,;\r\n]+)\s*,\s*(\d+)\s*,\s*(\d+))");
				for (std::sregex_iterator item(raw.begin(), raw.end(), entry), itemEnd; item != itemEnd; ++item) {
					NpcShopEntry shopEntry;
					shopEntry.itemName = (*item).str(1);
					shopEntry.itemId = Integer((*item).str(2)).value_or(0);
					const int price = Integer((*item).str(3)).value_or(0);
					(buying ? shopEntry.buy : shopEntry.sell) = price;
					shopEntry.editable = true;
					ShopSource source;
					source.name = Capture(*item, 1, location.begin);
					source.id = Capture(*item, 2, location.begin);
					(buying ? source.buy : source.sell) = Capture(*item, 3, location.begin);
					impl.original.shop.push_back(std::move(shopEntry));
					impl.shop.push_back(std::move(source));
				}
			}
		}
		impl.original.behaviorNotes.push_back(impl.sourceInfo.relatedScriptPath ? "Behavior is implemented by the related Lua script and is preserved read-only." : "No related behavior script was indexed.");
	}

	void ParseLua(NpcDefinitionDocument::Impl& impl, std::string& error) {
		if (impl.bytes.find("Game.createNpcType") == std::string::npos) {
			error = "Lua file does not contain a structural Game.createNpcType NPC definition.";
			return;
		}
		const std::regex createWithObject(R"(local\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*Game\.createNpcType\s*\()", std::regex::icase);
		const auto createObject = First(impl.bytes, createWithObject);
		if (!createObject) {
			error = "Lua NPC has no assignable Game.createNpcType provider object.";
			return;
		}
		impl.luaObject = createObject->str(1);
		impl.luaProvider = impl.bytes.find("npcConfig") != std::string::npos
				&& std::regex_search(impl.bytes, std::regex("\\b" + impl.luaObject + R"(\s*:\s*register\s*\(\s*npcConfig\s*\))", std::regex::icase))
			? NpcDefinitionDocument::Impl::LuaProvider::ConfigTable
			: NpcDefinitionDocument::Impl::LuaProvider::DirectMethods;
		impl.outfitTable = LuaTable(
			impl.bytes,
			impl.luaProvider == NpcDefinitionDocument::Impl::LuaProvider::ConfigTable
				? std::regex(R"(npcConfig\.outfit\s*=\s*\{)", std::regex::icase)
				: std::regex("\\b" + impl.luaObject + R"(\s*:\s*outfit\s*\(\s*\{)", std::regex::icase)
		);
		if (impl.luaProvider == NpcDefinitionDocument::Impl::LuaProvider::ConfigTable) {
			impl.flagsTable = LuaTable(impl.bytes, std::regex(R"(npcConfig\.flags\s*=\s*\{)", std::regex::icase));
			const std::regex anchor(R"(npcConfig\.(?:outfit|flags)\s*=|\b[A-Za-z_][A-Za-z0-9_]*\s*:\s*register\s*\(\s*npcConfig)", std::regex::icase);
			if (const auto found = First(impl.bytes, anchor)) {
				impl.scalarInsert = LineStart(impl.bytes, static_cast<std::size_t>(found->position(0)));
			}
		} else {
			const std::regex anchor("\\b" + impl.luaObject + R"(\s*:\s*(?:outfit|defaultBehavior|speechBubble)\s*\()", std::regex::icase);
			if (const auto found = First(impl.bytes, anchor)) {
				impl.directInsert = LineStart(impl.bytes, static_cast<std::size_t>(found->position(0)));
			} else {
				impl.directInsert = LineEnd(impl.bytes, static_cast<std::size_t>(createObject->position(0) + createObject->length(0)));
			}
		}

		auto internalName = LuaString(impl.bytes, R"(local\s+internalNpcName)", impl.original.name);
		if (internalName) {
			impl.fields[static_cast<std::size_t>(NpcField::Name)].push_back(*internalName);
		}
		const std::regex createExpression(R"(Game\.createNpcType\s*\(\s*(["'])(.*?)\1)", std::regex::icase);
		if (const auto create = First(impl.bytes, createExpression); create && !internalName) {
			impl.original.name = create->str(2);
			impl.fields[static_cast<std::size_t>(NpcField::Name)].push_back(Capture(*create, 2, 0, create->str(1)[0]));
		}
		auto& nameCapability = impl.original.capabilities[static_cast<std::size_t>(NpcField::Name)];
		nameCapability.present = !impl.fields[static_cast<std::size_t>(NpcField::Name)].empty();
		nameCapability.editable = nameCapability.present;
		nameCapability.state = nameCapability.present ? NpcFieldCapability::State::ExistingEditableLiteral : NpcFieldCapability::State::DynamicReadOnly;
		if (!nameCapability.present) {
			nameCapability.limitation = "The NPC name is computed by Lua.";
		}

		const bool config = impl.luaProvider == NpcDefinitionDocument::Impl::LuaProvider::ConfigTable;
		const auto assignment = [&](NpcField field, const std::string& prefix, auto reader, auto& target, bool supported = true) {
			const std::regex occurrence(prefix + R"(\s*=)", std::regex::icase);
			const std::size_t count = MatchCount(impl.bytes, occurrence);
			const auto location = reader(impl.bytes, prefix, target);
			SetLuaCapability(impl, field, location, supported, count > 0, count > 1);
		};
		const auto method = [&](NpcField field, const std::string& name, auto reader, auto& target, bool supported = true) {
			const std::regex occurrence("\\b" + impl.luaObject + "\\s*:\\s*" + name + R"(\s*\()", std::regex::icase);
			const std::size_t count = MatchCount(impl.bytes, occurrence);
			const auto location = reader(impl.bytes, impl.luaObject, name, target);
			SetLuaCapability(impl, field, location, supported, count > 0, count > 1);
		};

		if (config) {
			assignment(NpcField::Description, R"(npcConfig\.description)", LuaString, impl.original.description);
			if (impl.original.capability(NpcField::Description).state == NpcFieldCapability::State::DynamicReadOnly
				&& impl.bytes.find("npcConfig.description = internalNpcName") != std::string::npos) {
				impl.original.description = impl.original.name;
			}
			assignment(NpcField::Health, R"(npcConfig\.health)", LuaNumber, impl.original.health);
			assignment(NpcField::MaxHealth, R"(npcConfig\.maxHealth)", LuaNumber, impl.original.maxHealth);
			if (impl.original.capability(NpcField::MaxHealth).state == NpcFieldCapability::State::DynamicReadOnly
				&& impl.bytes.find("npcConfig.maxHealth = npcConfig.health") != std::string::npos) {
				impl.original.maxHealth = impl.original.health;
			}
			assignment(NpcField::WalkInterval, R"(npcConfig\.walkInterval)", LuaNumber, impl.original.walkInterval);
			assignment(NpcField::WalkRadius, R"(npcConfig\.walkRadius)", LuaNumber, impl.original.walkRadius);
			assignment(NpcField::Speed, R"(npcConfig\.walkSpeed)", LuaNumber, impl.original.speed);
			assignment(NpcField::FloorChange, R"(floorchange)", LuaBoolean, impl.original.floorChange);
		} else {
			SetCapability(impl, NpcField::Description, std::nullopt);
			method(NpcField::Health, "health", LuaMethodNumber, impl.original.health);
			method(NpcField::MaxHealth, "maxHealth", LuaMethodNumber, impl.original.maxHealth);
			method(NpcField::WalkInterval, "walkInterval", LuaMethodNumber, impl.original.walkInterval);
			method(NpcField::WalkRadius, "spawnRadius", LuaMethodNumber, impl.original.walkRadius);
			method(NpcField::Speed, "walkSpeed", LuaMethodNumber, impl.original.speed);
			method(NpcField::FloorChange, "floorChange", LuaMethodBoolean, impl.original.floorChange);
		}
		SetCapability(impl, NpcField::Script, std::nullopt);

		for (const auto [field, prefix, target] : {
				 std::tuple(NpcField::LookType, std::string(R"(lookType)"), &impl.original.lookType),
				 std::tuple(NpcField::LookTypeEx, std::string(R"(lookTypeEx)"), &impl.original.lookTypeEx),
				 std::tuple(NpcField::LookHead, std::string(R"(lookHead)"), &impl.original.lookHead),
				 std::tuple(NpcField::LookBody, std::string(R"(lookBody)"), &impl.original.lookBody),
				 std::tuple(NpcField::LookLegs, std::string(R"(lookLegs)"), &impl.original.lookLegs),
				 std::tuple(NpcField::LookFeet, std::string(R"(lookFeet)"), &impl.original.lookFeet),
				 std::tuple(NpcField::LookAddons, std::string(R"((?:lookAddons|addons))"), &impl.original.lookAddons),
				 std::tuple(NpcField::LookMount, std::string(R"((?:lookMount|mount))"), &impl.original.lookMount),
			 }) {
			const std::size_t count = MatchCount(impl.bytes, std::regex("\\b" + prefix + R"(\s*=)", std::regex::icase));
			const auto location = LuaNumber(impl.bytes, "\\b" + prefix, *target);
			SetLuaCapability(impl, field, location, impl.outfitTable.has_value(), count > 0, count > 1);
		}
		SetCapability(impl, NpcField::Direction, std::nullopt);

		const std::regex message(R"(npcHandler\s*:\s*setMessage\s*\(\s*(MESSAGE_[A-Z_]+)\s*,\s*(["'])(.*?)\2\s*\))");
		for (std::sregex_iterator iterator(impl.bytes.begin(), impl.bytes.end(), message), end; iterator != end; ++iterator) {
			impl.original.messages.push_back({ (*iterator).str(1), (*iterator).str(3), true });
			impl.messages.push_back({ Capture(*iterator, 3, 0, (*iterator).str(2)[0]) });
		}
		const std::regex response(R"((?:setGreetResponse|setFarewellResponse)\s*\(\s*(["'])(.*?)\1\s*\))");
		for (std::sregex_iterator iterator(impl.bytes.begin(), impl.bytes.end(), response), end; iterator != end; ++iterator) {
			const std::string key = (*iterator).str(0).find("Greet") != std::string::npos ? "GREET_RESPONSE" : "FAREWELL_RESPONSE";
			impl.original.messages.push_back({ key, (*iterator).str(2), true });
			impl.messages.push_back({ Capture(*iterator, 2, 0, (*iterator).str(1)[0]) });
		}

		const std::regex shopEntry(R"(\{\s*itemName\s*=\s*(["'])(.*?)\1\s*,\s*clientId\s*=\s*(\d+)([^{}]*)\})", std::regex::icase);
		for (std::sregex_iterator iterator(impl.bytes.begin(), impl.bytes.end(), shopEntry), end; iterator != end; ++iterator) {
			const std::smatch& match = *iterator;
			NpcShopEntry entry;
			entry.itemName = match.str(2);
			entry.itemId = Integer(match.str(3)).value_or(0);
			entry.editable = true;
			ShopSource source;
			source.name = Capture(match, 2, 0, match.str(1)[0]);
			source.id = Capture(match, 3);
			const std::string tail = match.str(4);
			const std::size_t tailBase = static_cast<std::size_t>(match.position(4));
			if (const auto buy = First(tail, std::regex(R"(\bbuy\s*=\s*(\d+))", std::regex::icase))) {
				entry.buy = Integer(buy->str(1)).value_or(0);
				source.buy = Capture(*buy, 1, tailBase);
			}
			if (const auto sell = First(tail, std::regex(R"(\bsell\s*=\s*(\d+))", std::regex::icase))) {
				entry.sell = Integer(sell->str(1)).value_or(0);
				source.sell = Capture(*sell, 1, tailBase);
			}
			impl.original.shop.push_back(std::move(entry));
			impl.shop.push_back(std::move(source));
		}

		const std::regex travel(R"(addTravelKeyword\s*\(\s*(["'])(.*?)\1\s*,\s*(\d+)\s*,\s*Position\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\))", std::regex::icase);
		for (std::sregex_iterator iterator(impl.bytes.begin(), impl.bytes.end(), travel), end; iterator != end; ++iterator) {
			NpcTravelEntry entry { (*iterator).str(2), Integer((*iterator).str(3)).value_or(0), Integer((*iterator).str(4)).value_or(0), Integer((*iterator).str(5)).value_or(0), Integer((*iterator).str(6)).value_or(0), false, true };
			TravelSource source;
			source.keyword = Capture(*iterator, 2, 0, (*iterator).str(1)[0]);
			source.cost = Capture(*iterator, 3);
			source.x = Capture(*iterator, 4);
			source.y = Capture(*iterator, 5);
			source.z = Capture(*iterator, 6);
			impl.original.travel.push_back(std::move(entry));
			impl.travel.push_back(std::move(source));
		}
		const std::regex keywordTravel(R"(local\s+(\w+)\s*=\s*keywordHandler\s*:\s*addKeyword\s*\(\s*\{\s*(["'])(.*?)\2[\s\S]*?\1\s*:\s*addChildKeyword\s*\([\s\S]*?StdModule\.travel[\s\S]*?cost\s*=\s*(\d+)[\s\S]*?destination\s*=\s*Position\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)[\s\S]*?\))", std::regex::icase);
		for (std::sregex_iterator iterator(impl.bytes.begin(), impl.bytes.end(), keywordTravel), end; iterator != end; ++iterator) {
			NpcTravelEntry entry { (*iterator).str(3), Integer((*iterator).str(4)).value_or(0), Integer((*iterator).str(5)).value_or(0), Integer((*iterator).str(6)).value_or(0), Integer((*iterator).str(7)).value_or(0), false, true };
			TravelSource source;
			source.keyword = Capture(*iterator, 3, 0, (*iterator).str(2)[0]);
			source.cost = Capture(*iterator, 4);
			source.x = Capture(*iterator, 5);
			source.y = Capture(*iterator, 6);
			source.z = Capture(*iterator, 7);
			impl.original.travel.push_back(std::move(entry));
			impl.travel.push_back(std::move(source));
		}
		const std::regex destinationEntry(R"((\w+)\s*=\s*\{\s*position\s*=\s*Position\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)\s*,\s*(?:money|cost)\s*=\s*(\d+)([^{}]*)\})", std::regex::icase);
		for (std::sregex_iterator iterator(impl.bytes.begin(), impl.bytes.end(), destinationEntry), end; iterator != end; ++iterator) {
			NpcTravelEntry entry { (*iterator).str(1), Integer((*iterator).str(5)).value_or(0), Integer((*iterator).str(2)).value_or(0), Integer((*iterator).str(3)).value_or(0), Integer((*iterator).str(4)).value_or(0), false, true };
			TravelSource source;
			source.keyword = Capture(*iterator, 1);
			source.x = Capture(*iterator, 2);
			source.y = Capture(*iterator, 3);
			source.z = Capture(*iterator, 4);
			source.cost = Capture(*iterator, 5);
			const std::string tail = (*iterator).str(6);
			const std::size_t base = static_cast<std::size_t>((*iterator).position(6));
			if (const auto premium = First(tail, std::regex(R"(premium\s*=\s*(true|false))", std::regex::icase))) {
				entry.premium = Lower(premium->str(1)) == "true";
				source.premium = Capture(*premium, 1, base);
			}
			impl.original.travel.push_back(std::move(entry));
			impl.travel.push_back(std::move(source));
		}
		impl.original.behaviorNotes.push_back("Callbacks, keyword graphs, storage checks and computed Lua behavior remain preserved read-only.");
	}

	std::string FieldValue(const NpcDefinition& definition, NpcField field) {
		switch (field) {
			case NpcField::Name:
				return definition.name;
			case NpcField::Description:
				return definition.description;
			case NpcField::Script:
				return definition.script;
			case NpcField::Health:
				return std::to_string(definition.health);
			case NpcField::MaxHealth:
				return std::to_string(definition.maxHealth);
			case NpcField::WalkInterval:
				return std::to_string(definition.walkInterval);
			case NpcField::WalkRadius:
				return std::to_string(definition.walkRadius);
			case NpcField::Speed:
				return std::to_string(definition.speed);
			case NpcField::FloorChange:
				return definition.floorChange ? "true" : "false";
			case NpcField::LookType:
				return std::to_string(definition.lookType);
			case NpcField::LookTypeEx:
				return std::to_string(definition.lookTypeEx);
			case NpcField::LookHead:
				return std::to_string(definition.lookHead);
			case NpcField::LookBody:
				return std::to_string(definition.lookBody);
			case NpcField::LookLegs:
				return std::to_string(definition.lookLegs);
			case NpcField::LookFeet:
				return std::to_string(definition.lookFeet);
			case NpcField::LookAddons:
				return std::to_string(definition.lookAddons);
			case NpcField::LookMount:
				return std::to_string(definition.lookMount);
			case NpcField::Direction:
				return std::to_string(definition.direction);
			case NpcField::Count:
				break;
		}
		return {};
	}

	std::optional<std::pair<std::size_t, std::string>> LuaInsertion(
		const NpcDefinitionDocument::Impl& impl,
		NpcField field,
		const std::string& value
	) {
		const std::string newline = Newline(impl.bytes);
		const auto tableField = [&](const std::optional<std::pair<std::size_t, std::size_t>>& table, const std::string& key) -> std::optional<std::pair<std::size_t, std::string>> {
			if (!table) {
				return std::nullopt;
			}
			const std::size_t open = table->first;
			const std::size_t close = table->second;
			if (impl.bytes.find('\n', open) < close) {
				return std::pair(LineStart(impl.bytes, close), "\t" + key + " = " + value + "," + newline);
			}
			const bool empty = impl.bytes.find_first_not_of(" \t\r\n", open + 1) >= close;
			return std::pair(close, empty ? (" " + key + " = " + value + " ") : (", " + key + " = " + value));
		};
		if (field >= NpcField::LookType && field <= NpcField::LookMount) {
			static constexpr std::array<const char*, 8> keys {
				"lookType", "lookTypeEx", "lookHead", "lookBody", "lookLegs", "lookFeet", "lookAddons", "lookMount"
			};
			return tableField(impl.outfitTable, keys[static_cast<std::size_t>(field) - static_cast<std::size_t>(NpcField::LookType)]);
		}
		if (impl.luaProvider == NpcDefinitionDocument::Impl::LuaProvider::ConfigTable) {
			if (field == NpcField::FloorChange) {
				return tableField(impl.flagsTable, "floorchange");
			}
			if (impl.scalarInsert == std::string::npos) {
				return std::nullopt;
			}
			std::string key;
			switch (field) {
				case NpcField::Description:
					key = "description";
					break;
				case NpcField::Health:
					key = "health";
					break;
				case NpcField::MaxHealth:
					key = "maxHealth";
					break;
				case NpcField::WalkInterval:
					key = "walkInterval";
					break;
				case NpcField::WalkRadius:
					key = "walkRadius";
					break;
				case NpcField::Speed:
					key = "walkSpeed";
					break;
				default:
					return std::nullopt;
			}
			const std::string encoded = field == NpcField::Description ? ("\"" + EncodeLua(value, '"') + "\"") : value;
			return std::pair(impl.scalarInsert, "npcConfig." + key + " = " + encoded + newline);
		}
		if (impl.luaProvider == NpcDefinitionDocument::Impl::LuaProvider::DirectMethods && impl.directInsert != std::string::npos) {
			std::string method;
			switch (field) {
				case NpcField::Health:
					method = "health";
					break;
				case NpcField::MaxHealth:
					method = "maxHealth";
					break;
				case NpcField::WalkInterval:
					method = "walkInterval";
					break;
				case NpcField::WalkRadius:
					method = "spawnRadius";
					break;
				case NpcField::Speed:
					method = "walkSpeed";
					break;
				case NpcField::FloorChange:
					method = "floorChange";
					break;
				default:
					return std::nullopt;
			}
			return std::pair(impl.directInsert, impl.luaObject + ":" + method + "(" + value + ")" + newline);
		}
		return std::nullopt;
	}
}

const NpcFieldCapability& NpcDefinition::capability(NpcField field) const {
	return capabilities[static_cast<std::size_t>(field)];
}

std::unique_ptr<NpcDefinitionDocument> NpcDefinitionDocument::Load(const ServerContentSource& source, std::string& error) {
	error.clear();
	if (source.kind != ServerContentKind::Npc || (source.format != ServerContentFormat::Xml && source.format != ServerContentFormat::Lua)) {
		error = "Unsupported NPC source format.";
		return nullptr;
	}
	const auto bytes = ReadFile(source.declarationPath, error);
	if (!bytes) {
		return nullptr;
	}
	return LoadFromText(source, *bytes, error);
}

std::unique_ptr<NpcDefinitionDocument> NpcDefinitionDocument::LoadFromText(const ServerContentSource& source, std::string bytes, std::string& error) {
	auto impl = std::make_unique<Impl>();
	impl->sourceInfo = source;
	impl->fingerprint = ResourceFingerprint::Read(source.declarationPath);
	impl->bytes = std::move(bytes);
	if (source.format == ServerContentFormat::Xml) {
		ParseXml(*impl, error);
	} else {
		ParseLua(*impl, error);
	}
	if (!error.empty()) {
		return nullptr;
	}
	return std::unique_ptr<NpcDefinitionDocument>(new NpcDefinitionDocument(std::move(impl)));
}

NpcDefinitionDocument::NpcDefinitionDocument(std::unique_ptr<Impl> value) :
	implementation(std::move(value)) { }
NpcDefinitionDocument::~NpcDefinitionDocument() = default;
NpcDefinitionDocument::NpcDefinitionDocument(NpcDefinitionDocument&&) noexcept = default;
NpcDefinitionDocument& NpcDefinitionDocument::operator=(NpcDefinitionDocument&&) noexcept = default;
const NpcDefinition& NpcDefinitionDocument::definition() const {
	return implementation->original;
}
const ServerContentSource& NpcDefinitionDocument::source() const {
	return implementation->sourceInfo;
}
const std::string& NpcDefinitionDocument::sourceText() const {
	return implementation->bytes;
}

bool NpcDefinitionDocument::hasChanges(const NpcDefinition& edited) const {
	for (std::size_t i = 0; i < FieldCount; ++i) {
		if (FieldValue(implementation->original, static_cast<NpcField>(i)) != FieldValue(edited, static_cast<NpcField>(i))) {
			return true;
		}
	}
	return implementation->original.messages != edited.messages || implementation->original.shop != edited.shop || implementation->original.travel != edited.travel;
}

bool NpcDefinitionDocument::save(const NpcDefinition& edited, std::string& error) {
	if (!ValidateNpcDefinition(edited, error)) {
		return false;
	}
	struct Patch {
		std::size_t begin;
		std::size_t end;
		std::string value;
	};
	std::vector<Patch> patches;
	const auto add = [&](const Location& location, std::string value) { patches.push_back({ location.begin, location.end, std::move(value) }); };
	for (std::size_t i = 0; i < FieldCount; ++i) {
		const NpcField field = static_cast<NpcField>(i);
		const std::string before = FieldValue(implementation->original, field), after = FieldValue(edited, field);
		if (before == after) {
			continue;
		}
		const auto& capability = implementation->original.capability(field);
		if (!capability.editable) {
			error = std::string(NpcFieldName(field)) + " cannot be edited safely in this provider.";
			return false;
		}
		if (implementation->fields[i].empty()) {
			const auto insertion = implementation->sourceInfo.format == ServerContentFormat::Lua
				? LuaInsertion(*implementation, field, after)
				: std::nullopt;
			if (!insertion) {
				error = std::string(NpcFieldName(field)) + " has no safe insertion anchor in this provider.";
				return false;
			}
			add({ insertion->first, insertion->first, '\0' }, insertion->second);
			continue;
		}
		for (const Location& location : implementation->fields[i]) {
			add(location, location.quote ? (implementation->sourceInfo.format == ServerContentFormat::Xml ? EncodeXml(after, location.quote) : EncodeLua(after, location.quote)) : (field == NpcField::FloorChange && implementation->sourceInfo.format == ServerContentFormat::Xml ? (edited.floorChange ? "1" : "0") : after));
		}
	}
	if (edited.messages.size() != implementation->original.messages.size() || edited.shop.size() != implementation->original.shop.size() || edited.travel.size() != implementation->original.travel.size()) {
		error = "Adding or removing behavior rows is disabled because this source contains custom code. Edit existing literal rows instead.";
		return false;
	}
	for (std::size_t i = 0; i < edited.messages.size(); ++i) {
		if (edited.messages[i].text != implementation->original.messages[i].text) {
			add(implementation->messages[i].text, implementation->sourceInfo.format == ServerContentFormat::Xml ? EncodeXml(edited.messages[i].text, implementation->messages[i].text.quote) : EncodeLua(edited.messages[i].text, implementation->messages[i].text.quote));
		}
	}
	for (std::size_t i = 0; i < edited.shop.size(); ++i) {
		const auto& old = implementation->original.shop[i];
		const auto& now = edited.shop[i];
		const auto& loc = implementation->shop[i];
		if (old.itemName != now.itemName && loc.name) {
			add(*loc.name, loc.name->quote ? (implementation->sourceInfo.format == ServerContentFormat::Xml ? EncodeXml(now.itemName, loc.name->quote) : EncodeLua(now.itemName, loc.name->quote)) : now.itemName);
		}
		if (old.itemId != now.itemId && loc.id) {
			add(*loc.id, std::to_string(now.itemId));
		}
		if (old.buy != now.buy) {
			if (!loc.buy) {
				error = "This shop row has no literal buy price to edit.";
				return false;
			}
			add(*loc.buy, std::to_string(now.buy));
		}
		if (old.sell != now.sell) {
			if (!loc.sell) {
				error = "This shop row has no literal sell price to edit.";
				return false;
			}
			add(*loc.sell, std::to_string(now.sell));
		}
	}
	for (std::size_t i = 0; i < edited.travel.size(); ++i) {
		const auto& old = implementation->original.travel[i];
		const auto& now = edited.travel[i];
		const auto& loc = implementation->travel[i];
		if (old.keyword != now.keyword && loc.keyword) {
			add(*loc.keyword, EncodeLua(now.keyword, loc.keyword->quote));
		}
		if (old.cost != now.cost && loc.cost) {
			add(*loc.cost, std::to_string(now.cost));
		}
		if (old.x != now.x && loc.x) {
			add(*loc.x, std::to_string(now.x));
		}
		if (old.y != now.y && loc.y) {
			add(*loc.y, std::to_string(now.y));
		}
		if (old.z != now.z && loc.z) {
			add(*loc.z, std::to_string(now.z));
		}
		if (old.premium != now.premium) {
			if (!loc.premium) {
				error = "This travel row has no literal premium flag to edit.";
				return false;
			}
			add(*loc.premium, now.premium ? "true" : "false");
		}
	}
	if (patches.empty()) {
		return true;
	}
	const auto current = ReadFile(implementation->sourceInfo.declarationPath, error);
	if (!current || !implementation->fingerprint.MatchesCurrentFile() || *current != implementation->bytes) {
		error = "The NPC source changed on disk after the editor opened. Reopen it before saving.";
		return false;
	}
	std::stable_sort(patches.begin(), patches.end(), [](const Patch& a, const Patch& b) { return a.begin > b.begin; });
	std::string updated = implementation->bytes;
	for (const Patch& patch : patches) {
		updated.replace(patch.begin, patch.end - patch.begin, patch.value);
	}
	FileSaveTransaction transaction;
	if (!WriteFile(transaction.Stage(implementation->sourceInfo.declarationPath), updated, error) || !transaction.Commit(error)) {
		return false;
	}
	implementation->sourceInfo.declarationFingerprint = ResourceFingerprint::Read(implementation->sourceInfo.declarationPath);
	auto refreshed = LoadFromText(implementation->sourceInfo, std::move(updated), error);
	if (!refreshed) {
		return false;
	}
	implementation = std::move(refreshed->implementation);
	return true;
}

const char* NpcFieldName(NpcField field) {
	static constexpr std::array<const char*, FieldCount> names { "Name", "Description", "Script", "Health", "Max health", "Walk interval", "Walk radius", "Speed", "Floor change", "Look type", "Look type ex", "Head", "Body", "Legs", "Feet", "Addons", "Mount", "Direction" };
	return names[static_cast<std::size_t>(field)];
}

bool ValidateNpcDefinition(const NpcDefinition& definition, std::string& error) {
	error.clear();
	if (definition.name.empty()) {
		error = "NPC name cannot be empty.";
		return false;
	}
	if (definition.health < 0 || definition.maxHealth < 0 || definition.maxHealth < definition.health) {
		error = "NPC health must be non-negative and max health cannot be lower than current health.";
		return false;
	}
	if (definition.walkInterval < 0 || definition.walkRadius < 0 || definition.speed < 0) {
		error = "NPC movement values cannot be negative.";
		return false;
	}
	for (const NpcShopEntry& entry : definition.shop) {
		if (entry.itemId <= 0 || entry.buy < 0 || entry.sell < 0) {
			error = "Shop rows need a valid item ID and non-negative prices.";
			return false;
		}
	}
	for (const NpcTravelEntry& entry : definition.travel) {
		if (entry.keyword.empty() || entry.cost < 0 || entry.z < 0 || entry.z > 15) {
			error = "Travel rows need a keyword, non-negative cost and floor 0-15.";
			return false;
		}
	}
	return true;
}
