//////////////////////////////////////////////////////////////////////
// Safe creation of new XML and Lua monster definitions.
//////////////////////////////////////////////////////////////////////

#include "monster_definition_creation.h"

#include "file_transaction.h"
#include "monster_definition.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <string_view>

namespace {
	std::string Trim(std::string value) {
		const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) { return std::isspace(character); });
		const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) { return std::isspace(character); }).base();
		return first < last ? std::string(first, last) : std::string();
	}

	std::string LowerAscii(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
			return static_cast<char>(character >= 'A' && character <= 'Z' ? character + ('a' - 'A') : character);
		});
		return value;
	}

	bool IsSafeRelativePath(const std::filesystem::path& path) {
		if (path.empty() || path.is_absolute()) {
			return false;
		}
		return std::none_of(path.begin(), path.end(), [](const std::filesystem::path& component) {
			return component == "..";
		});
	}

	std::filesystem::path Normalize(const std::filesystem::path& path) {
		std::error_code filesystemError;
		auto normalized = std::filesystem::weakly_canonical(path, filesystemError);
		if (!filesystemError) {
			return normalized;
		}
		filesystemError.clear();
		normalized = std::filesystem::absolute(path, filesystemError);
		return (filesystemError ? path : normalized).lexically_normal();
	}

	bool IsWithin(const std::filesystem::path& path, const std::filesystem::path& root) {
		const std::filesystem::path relative = Normalize(path).lexically_relative(Normalize(root));
		return relative == "." || IsSafeRelativePath(relative);
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
				case '\'':
					encoded += "&apos;";
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
				default:
					encoded.push_back(character);
					break;
			}
		}
		return encoded;
	}

	std::string XmlTemplate(const std::string& name) {
		const std::string escaped = EncodeXml(name);
		std::string output = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<monster name=\"";
		output += escaped;
		output += "\" nameDescription=\"a ";
		output += escaped;
		output += "\" race=\"blood\" experience=\"0\" speed=\"200\" manacost=\"0\">\n"
				  "\t<health now=\"100\" max=\"100\"/>\n"
				  "\t<look type=\"128\" head=\"0\" body=\"0\" legs=\"0\" feet=\"0\" addons=\"0\" mount=\"0\" corpse=\"3058\"/>\n"
				  "\t<targetchange interval=\"4000\" chance=\"10\"/>\n"
				  "\t<strategy attack=\"100\" defense=\"0\"/>\n"
				  "\t<flags>\n"
				  "\t\t<flag summonable=\"0\"/>\n"
				  "\t\t<flag attackable=\"1\"/>\n"
				  "\t\t<flag hostile=\"1\"/>\n"
				  "\t\t<flag convinceable=\"0\"/>\n"
				  "\t\t<flag pushable=\"0\"/>\n"
				  "\t\t<flag canpushitems=\"0\"/>\n"
				  "\t\t<flag canpushcreatures=\"0\"/>\n"
				  "\t\t<flag targetdistance=\"1\"/>\n"
				  "\t\t<flag staticattack=\"90\"/>\n"
				  "\t\t<flag runonhealth=\"0\"/>\n"
				  "\t</flags>\n"
				  "\t<attacks/>\n"
				  "\t<defenses armor=\"0\" defense=\"0\"/>\n"
				  "\t<elements/>\n"
				  "\t<immunities/>\n"
				  "\t<summons maxSummons=\"0\"/>\n"
				  "\t<voices interval=\"5000\" chance=\"10\"/>\n"
				  "\t<loot/>\n"
				  "</monster>\n";
		return output;
	}

	std::string LuaTemplate(const std::string& name, MonsterCreationProvider provider) {
		const std::string escaped = EncodeLua(name);
		std::string output = "local mType = Game.createMonsterType(\"";
		output += escaped;
		output += "\")\nlocal monster = {}\n\nmonster.name = \"";
		output += escaped;
		output += "\"\nmonster.description = \"a ";
		output += escaped;
		output += "\"\n"
				  "monster.experience = 0\n"
				  "monster.health = 100\n"
				  "monster.maxHealth = 100\n"
				  "monster.race = \"blood\"\n"
				  "monster.corpse = 3058\n"
				  "monster.speed = 200\n"
				  "monster.manaCost = 0\n\n"
				  "monster.outfit = {\n"
				  "\tlookType = 128,\n"
				  "\tlookTypeEx = 0,\n"
				  "\tlookHead = 0,\n"
				  "\tlookBody = 0,\n"
				  "\tlookLegs = 0,\n"
				  "\tlookFeet = 0,\n"
				  "\tlookAddons = 0,\n"
				  "\tlookMount = 0,\n"
				  "}\n\n"
				  "monster.changeTarget = { interval = 4000, chance = 10 }\n"
				  "monster.strategiesTarget = { nearest = 100 }\n"
				  "monster.flags = {\n"
				  "\tsummonable = false,\n"
				  "\tattackable = true,\n"
				  "\thostile = true,\n"
				  "\tconvinceable = false,\n"
				  "\tpushable = false,\n"
				  "\tcanPushItems = false,\n"
				  "\tcanPushCreatures = false,\n"
				  "\tstaticAttackChance = 90,\n"
				  "\ttargetDistance = 1,\n"
				  "\trunHealth = 0,\n"
				  "}\n\n"
				  "monster.attacks = {}\n"
				  "monster.defenses = { defense = 0, armor = 0 }\n"
				  "monster.elements = {}\n"
				  "monster.immunities = {}\n"
				  "monster.maxSummons = 0\n";
		if (provider == MonsterCreationProvider::CanaryLua) {
			output += "monster.summon = { maxSummons = 0, summons = {} }\n";
		} else {
			output += "monster.summons = {}\n";
		}
		output += "monster.voices = { interval = 5000, chance = 10 }\n"
				  "monster.loot = {}\n\n"
				  "mType:register(monster)\n";
		return output;
	}

	std::optional<std::string> ReadSmallFile(const std::filesystem::path& path) {
		std::error_code filesystemError;
		const auto size = std::filesystem::file_size(path, filesystemError);
		if (filesystemError || size > 4 * 1024 * 1024) {
			return std::nullopt;
		}
		std::ifstream stream(path, std::ios::binary);
		if (!stream.is_open()) {
			return std::nullopt;
		}
		return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
	}

	bool UsesNestedCanarySummons(const ServerContentIndex& index) {
		std::size_t inspected = 0;
		for (const ServerContentSource& source : index.entries()) {
			if (source.kind != ServerContentKind::Monster || source.format != ServerContentFormat::Lua || !source.declarationExists) {
				continue;
			}
			const auto contents = ReadSmallFile(source.declarationPath);
			if (contents && (contents->find("monster.summon =") != std::string::npos || contents->find("monster.summon=") != std::string::npos)) {
				return true;
			}
			if (++inspected >= 64) {
				break;
			}
		}
		return false;
	}

	bool ValidateGeneratedSource(
		const std::filesystem::path& staged,
		ServerContentFormat format,
		MonsterCreationProvider provider,
		const std::string& name,
		std::string& error
	) {
		const auto contents = ReadSmallFile(staged);
		if (!contents) {
			error = "Could not reread the staged monster definition for validation.";
			return false;
		}
		if (format == ServerContentFormat::Lua) {
			if (contents->find("monster.summons = { maxSummons") != std::string::npos) {
				error = "The generated Lua uses an unsafe summons shape for the detected registration API.";
				return false;
			}
			if (provider == MonsterCreationProvider::CanaryLua && contents->find("monster.summon = { maxSummons = 0, summons = {} }") == std::string::npos) {
				error = "The generated Lua does not match the detected Canary summon provider.";
				return false;
			}
		}
		ServerContentSource source;
		source.kind = ServerContentKind::Monster;
		source.format = format;
		source.serverType = provider == MonsterCreationProvider::CanaryLua ? ServerType::Canary : ServerType::Tfs;
		source.name = name;
		source.declarationPath = staged;
		source.declarationExists = true;
		source.registered = true;
		source.declarationFingerprint = ResourceFingerprint::Read(staged);
		auto document = MonsterDefinitionDocument::Load(source, error);
		if (!document) {
			error = "Generated monster validation failed: " + error;
			return false;
		}
		if (document->definition().name != name || !ValidateMonsterDefinition(document->definition(), error)) {
			if (error.empty()) {
				error = "Generated monster validation did not recover the requested identity.";
			}
			return false;
		}
		return true;
	}

	bool WriteFile(const std::filesystem::path& path, std::string_view contents, std::string& error) {
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		if (!stream.is_open()) {
			error = "Could not create staged monster source: " + path.string() + ".";
			return false;
		}
		stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
		if (!stream) {
			error = "Could not write staged monster source: " + path.string() + ".";
			return false;
		}
		return true;
	}

	std::optional<std::filesystem::path> FindXmlRegistry(const ServerWorkspace& workspace, const ServerContentIndex& index) {
		std::map<std::filesystem::path, std::size_t> counts;
		for (const ServerContentSource& source : index.entries()) {
			if (source.kind == ServerContentKind::Monster && source.format == ServerContentFormat::Xml && source.registrationPath) {
				++counts[Normalize(*source.registrationPath)];
			}
		}
		if (!counts.empty()) {
			return std::max_element(counts.begin(), counts.end(), [](const auto& left, const auto& right) {
					   return left.second < right.second;
				   })
				->first;
		}
		if (!workspace.monstersDirectory.empty()) {
			return workspace.monstersDirectory / "monsters.xml";
		}
		return std::nullopt;
	}

	std::optional<std::string> ReadFile(const std::filesystem::path& path, std::string& error) {
		std::ifstream stream(path, std::ios::binary);
		if (!stream.is_open()) {
			error = "Could not read monster registry: " + path.string() + ".";
			return std::nullopt;
		}
		return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
	}

	bool AddRegistryEntry(
		const std::filesystem::path& registry,
		const std::filesystem::path& declaration,
		const std::string& name,
		std::string& updated,
		std::string& error
	) {
		std::error_code filesystemError;
		const bool exists = std::filesystem::exists(registry, filesystemError) && !filesystemError;
		if (!exists) {
			updated = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<monsters>\n</monsters>\n";
		} else {
			const auto contents = ReadFile(registry, error);
			if (!contents) {
				return false;
			}
			updated = *contents;
		}

		const std::string lowered = LowerAscii(updated);
		const std::size_t closing = lowered.rfind("</monsters>");
		if (closing == std::string::npos) {
			error = "The monster registry has no closing <monsters> element: " + registry.string() + ".";
			return false;
		}
		const std::filesystem::path relative = Normalize(declaration).lexically_relative(Normalize(registry.parent_path()));
		if (!IsSafeRelativePath(relative)) {
			error = "The new XML monster must be stored below the monster registry directory.";
			return false;
		}
		const std::string entry = "\t<monster name=\"" + EncodeXml(name) + "\" file=\"" + EncodeXml(relative.generic_string()) + "\" />\n";
		updated.insert(closing, entry);
		return true;
	}
}

MonsterCreationProvider DetectMonsterCreationProvider(
	const ServerWorkspace& workspace,
	const ServerContentIndex& index,
	ServerContentFormat format
) {
	if (format == ServerContentFormat::Xml) {
		return MonsterCreationProvider::TfsXml;
	}
	if (workspace.usesCanaryCrystalLoader() || UsesNestedCanarySummons(index)) {
		return MonsterCreationProvider::CanaryLua;
	}
	return MonsterCreationProvider::TfsLua;
}

const char* MonsterCreationProviderName(MonsterCreationProvider provider) {
	switch (provider) {
		case MonsterCreationProvider::TfsXml:
			return "TFS XML";
		case MonsterCreationProvider::TfsLua:
			return "TFS Lua registerMonsterType";
		case MonsterCreationProvider::CanaryLua:
			return "Canary/Crystal Lua";
	}
	return "Unknown";
}

std::string MakeMonsterFileStem(const std::string& name) {
	std::string stem;
	bool separator = false;
	for (const unsigned char character : name) {
		if (std::isalnum(character)) {
			if (separator && !stem.empty()) {
				stem.push_back('_');
			}
			stem.push_back(static_cast<char>(character >= 'A' && character <= 'Z' ? character + ('a' - 'A') : character));
			separator = false;
		} else {
			separator = true;
		}
	}
	return stem.empty() ? "monster" : stem;
}

bool CreateMonsterDefinition(
	const ServerWorkspace& workspace,
	const ServerContentIndex& index,
	const MonsterCreationRequest& request,
	MonsterCreationResult& result,
	std::string& error
) {
	error.clear();
	result = {};
	const std::string name = Trim(request.name);
	if (name.empty()) {
		error = "Enter a monster name.";
		return false;
	}
	if (request.format != ServerContentFormat::Xml && request.format != ServerContentFormat::Lua) {
		error = "Choose XML or Lua for the new monster.";
		return false;
	}
	if (!index.findCaseInsensitive(ServerContentKind::Monster, name).empty()) {
		error = "A monster named '" + name + "' already exists in this Server Workspace.";
		return false;
	}
	if (workspace.monstersDirectory.empty() || request.destinationDirectory.empty()) {
		error = "The active Server Workspace has no monster directory.";
		return false;
	}
	const std::filesystem::path monsterRoot = Normalize(workspace.monstersDirectory);
	const std::filesystem::path destinationDirectory = Normalize(request.destinationDirectory);
	std::error_code filesystemError;
	if (!std::filesystem::is_directory(destinationDirectory, filesystemError) || filesystemError || !IsWithin(destinationDirectory, monsterRoot)) {
		error = "Choose an existing directory inside " + monsterRoot.string() + ".";
		return false;
	}

	const std::string extension = request.format == ServerContentFormat::Xml ? ".xml" : ".lua";
	const std::filesystem::path declaration = destinationDirectory / (MakeMonsterFileStem(name) + extension);
	if (std::filesystem::exists(declaration, filesystemError) || filesystemError) {
		error = "The destination file already exists: " + declaration.string() + ".";
		return false;
	}

	std::optional<std::filesystem::path> registry;
	std::string updatedRegistry;
	if (request.format == ServerContentFormat::Xml) {
		registry = FindXmlRegistry(workspace, index);
		if (!registry || !IsWithin(*registry, monsterRoot)
			|| !AddRegistryEntry(*registry, declaration, name, updatedRegistry, error)) {
			if (error.empty()) {
				error = "NexaMap could not determine a safe XML monster registry.";
			}
			return false;
		}
	}

	const MonsterCreationProvider provider = DetectMonsterCreationProvider(workspace, index, request.format);
	FileSaveTransaction transaction;
	const std::filesystem::path stagedDeclaration = transaction.Stage(declaration);
	if (!WriteFile(stagedDeclaration, request.format == ServerContentFormat::Xml ? XmlTemplate(name) : LuaTemplate(name, provider), error)) {
		return false;
	}
	if (!ValidateGeneratedSource(stagedDeclaration, request.format, provider, name, error)) {
		return false;
	}
	if (registry && !WriteFile(transaction.Stage(*registry), updatedRegistry, error)) {
		return false;
	}
	if (!transaction.Commit(error)) {
		return false;
	}

	result.source.kind = ServerContentKind::Monster;
	result.source.format = request.format;
	result.source.serverType = workspace.serverType;
	result.source.name = name;
	result.source.declarationPath = declaration;
	result.source.registrationPath = registry;
	result.source.registered = registry.has_value() || request.format == ServerContentFormat::Lua;
	result.source.declarationExists = true;
	result.source.declarationFingerprint = ResourceFingerprint::Read(declaration);
	result.provider = MonsterCreationProviderName(provider);
	if (registry) {
		result.source.registrationFingerprint = ResourceFingerprint::Read(*registry);
	}
	return true;
}
