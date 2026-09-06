#include "monster_definition.h"
#include "server_content_index.h"
#include "server_workspace.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
	int failures = 0;
	int checks = 0;

	void Check(bool condition, const std::string& message) {
		++checks;
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	}

	class TemporaryDirectory {
	public:
		TemporaryDirectory() {
			const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
			path = std::filesystem::temp_directory_path() / ("nexamap-monster-provider-test-" + std::to_string(stamp));
			std::filesystem::create_directories(path);
		}

		~TemporaryDirectory() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}

		std::filesystem::path write(const std::filesystem::path& relative, const std::string& contents) const {
			const std::filesystem::path target = path / relative;
			std::filesystem::create_directories(target.parent_path());
			std::ofstream stream(target, std::ios::binary | std::ios::trunc);
			stream << contents;
			return target;
		}

		std::string read(const std::filesystem::path& relative) const {
			std::ifstream stream(path / relative, std::ios::binary);
			return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
		}

		std::filesystem::path path;
	};

	ServerContentSource SourceFor(
		ServerContentFormat format,
		const std::filesystem::path& declaration,
		const std::optional<std::filesystem::path>& registration = std::nullopt
	) {
		ServerContentSource source;
		source.kind = ServerContentKind::Monster;
		source.format = format;
		source.name = "Demon";
		source.declarationPath = std::filesystem::weakly_canonical(declaration);
		source.declarationFingerprint = ResourceFingerprint::Read(source.declarationPath);
		source.declarationExists = true;
		source.registered = registration.has_value();
		if (registration) {
			source.registrationPath = std::filesystem::weakly_canonical(*registration);
			source.registrationFingerprint = ResourceFingerprint::Read(*source.registrationPath);
		}
		return source;
	}

	void TestXmlPreservingSave() {
		TemporaryDirectory server;
		const std::string declaration = "<?xml version=\"1.0\"?>\r\n"
										"<!-- keep this exact comment -->\r\n"
										"<monster name=\"Demon\" nameDescription='a demon' race=\"fire\" experience=\"6000\" speed=\"256\" custom=\"untouched\">\r\n"
										"  <health now=\"8200\" max=\"8200\" />\r\n"
										"  <look type=\"35\" head=\"1\" body=\"2\" legs=\"3\" feet=\"4\" addons=\"0\" corpse=\"5995\" />\r\n"
										"  <targetchange interval=\"4000\" chance=\"20\" />\r\n"
										"  <strategy attack=\"100\" defense=\"0\" />\r\n"
										"  <flags><flag attackable=\"1\"/><flag hostile=\"1\"/><flag canpushitems=\"1\"/><flag lightlevel=\"2\"/></flags>\r\n"
										"  <defenses armor=\"44\" defense=\"43\"><defense name=\"custom\"/></defenses>\r\n"
										"  <unknown foo=\"bar\" />\r\n"
										"</monster>\r\n";
		const std::filesystem::path monster = server.write("data/monster/demons/Demon.xml", declaration);
		const std::filesystem::path registry = server.write(
			"data/monster/monsters.xml",
			"<monsters>\n  <monster name=\"Demon\" file=\"demons/Demon.xml\"/>\n</monsters>\n"
		);

		std::string error;
		auto document = MonsterDefinitionDocument::Load(SourceFor(ServerContentFormat::Xml, monster, registry), error);
		Check(document != nullptr, "XML monster loads: " + error);
		if (!document) {
			return;
		}
		Check(document->definition().health == 8200 && document->definition().outfit.lookType == 35, "XML Main and Look values are normalized");
		Check(document->definition().strategyAttack == 100 && document->definition().lightLevel == 2, "XML strategy and light flags are normalized");
		Check(document->definition().capability(MonsterField::Health).editable, "XML literal health is editable");
		Check(!document->definition().capability(MonsterField::ManaCost).editable, "absent XML field is read-only");

		const std::string originalRegistry = server.read("data/monster/monsters.xml");
		Check(document->save(document->definition(), error), "unchanged XML save succeeds");
		Check(server.read("data/monster/demons/Demon.xml") == declaration, "unchanged XML remains byte-identical");
		Check(server.read("data/monster/monsters.xml") == originalRegistry, "unchanged registry remains byte-identical");

		MonsterDefinition edited = document->definition();
		edited.name = "Demon Prime";
		edited.description = "a demon & guardian";
		edited.health = 9001;
		edited.outfit.lookType = 36;
		edited.attackable = false;
		edited.strategyDefense = 25;
		Check(document->save(edited, error), "supported XML fields save transactionally: " + error);
		const std::string saved = server.read("data/monster/demons/Demon.xml");
		Check(saved.find("<!-- keep this exact comment -->") != std::string::npos, "XML comments are preserved");
		Check(saved.find("custom=\"untouched\"") != std::string::npos && saved.find("<unknown foo=\"bar\" />") != std::string::npos, "unknown XML content is preserved");
		Check(saved.find("name=\"Demon Prime\"") != std::string::npos && saved.find("now=\"9001\"") != std::string::npos, "known XML literals are replaced in place");
		Check(saved.find("nameDescription='a demon &amp; guardian'") != std::string::npos, "XML text is escaped without changing quote style");
		Check(saved.find("attackable=\"0\"") != std::string::npos, "XML boolean representation is preserved");
		Check(saved.find("defense=\"25\"") != std::string::npos, "XML strategy is saved in place");
		Check(server.read("data/monster/monsters.xml").find("name=\"Demon Prime\"") != std::string::npos, "registered XML name is updated in the same transaction");
	}

	void TestLuaPreservingSave() {
		TemporaryDirectory server;
		const std::string source = "-- custom prologue must survive\n"
								   "local documentation = [=[ monster.health = 1 ]=]\n"
								   "--[=[ monster.health = 2 ]=]\n"
								   "local mType = Game.createMonsterType(\"Demon\")\n"
								   "local monster = {}\n"
								   "monster.name = \"Demon\"\n"
								   "monster.description = prefix .. \" demon\"\n"
								   "monster.health = 8200\n"
								   "monster.maxHealth = 8200\n"
								   "monster.experience = 6000\n"
								   "monster.outfit = { lookType = 35, lookHead = 0, lookBody = 1, lookLegs = 2, lookFeet = 3, lookAddons = 0 }\n"
								   "monster.flags = { attackable = true, hostile = true, targetDistance = 1, customFlag = computeFlag() }\n"
								   "monster.changeTarget = { interval = 4000, chance = 20 }\n"
								   "monster.custom = makeCustom({ nested = true })\n"
								   "mType:register(monster)\n";
		const std::filesystem::path monster = server.write("data/monsters/demon.lua", source);
		std::string error;
		auto document = MonsterDefinitionDocument::Load(SourceFor(ServerContentFormat::Lua, monster), error);
		Check(document != nullptr, "Lua monster loads: " + error);
		if (!document) {
			return;
		}
		Check(document->definition().health == 8200 && document->definition().outfit.body == 1, "Lua Main and Look values are normalized");
		Check(!document->definition().capability(MonsterField::Description).editable, "computed Lua value is read-only");
		Check(document->definition().capability(MonsterField::Name).editable, "coordinated Lua name literals are editable");

		MonsterDefinition invalid = document->definition();
		invalid.description = "unsafe rewrite";
		Check(!document->save(invalid, error) && error.find("Description") != std::string::npos, "computed Lua field cannot be overwritten");
		Check(server.read("data/monsters/demon.lua") == source, "rejected Lua edit leaves the source byte-identical");

		MonsterDefinition edited = document->definition();
		edited.name = "Demon Prime";
		edited.health = 9001;
		edited.outfit.body = 7;
		edited.attackable = false;
		Check(document->save(edited, error), "supported Lua literals save transactionally: " + error);
		const std::string saved = server.read("data/monsters/demon.lua");
		Check(saved.find("Game.createMonsterType(\"Demon Prime\")") != std::string::npos && saved.find("monster.name = \"Demon Prime\"") != std::string::npos, "coordinated Lua name literals stay consistent");
		Check(saved.find("monster.health = 9001") != std::string::npos && saved.find("lookBody = 7") != std::string::npos, "known Lua literals are replaced in place");
		Check(saved.find("customFlag = computeFlag()") != std::string::npos && saved.find("monster.custom = makeCustom({ nested = true })") != std::string::npos, "custom Lua expressions are preserved");
	}

	void TestAmbiguousAndExternalChanges() {
		TemporaryDirectory server;
		ServerContentSource registered;
		registered.registered = true;
		ServerContentSource unregistered;
		ServerContentLookupResult authoritative;
		authoritative.matches = { &unregistered, &registered };
		Check(authoritative.uniqueRegisteredValue() == &registered, "one registered source safely resolves otherwise ambiguous matches");
		ServerContentSource secondRegistered;
		secondRegistered.registered = true;
		authoritative.matches.push_back(&secondRegistered);
		Check(authoritative.uniqueRegisteredValue() == nullptr, "multiple registered sources remain ambiguous");

		const std::filesystem::path duplicate = server.write(
			"duplicate.lua",
			"local m = Game.createMonsterType(\"Demon\")\nlocal monster = {}\nmonster.health = 10\nmonster.health = 20\nm:register(monster)\n"
		);
		std::string error;
		auto document = MonsterDefinitionDocument::Load(SourceFor(ServerContentFormat::Lua, duplicate), error);
		Check(document != nullptr, "Lua with duplicate assignments still opens for inspection");
		if (document) {
			Check(!document->definition().capability(MonsterField::Health).editable, "duplicate Lua assignment is ambiguity-safe");
		}

		const std::filesystem::path external = server.write(
			"external.xml",
			"<monster name=\"Demon\"><health now=\"10\" max=\"10\"/><look type=\"35\"/></monster>\n"
		);
		document = MonsterDefinitionDocument::Load(SourceFor(ServerContentFormat::Xml, external), error);
		Check(document != nullptr, "XML opens before external modification");
		if (document) {
			server.write("external.xml", "<monster name=\"Demon\"><health now=\"11\" max=\"10\"/><look type=\"35\"/></monster>\n");
			MonsterDefinition edited = document->definition();
			edited.health = 12;
			Check(!document->save(edited, error) && error.find("changed on disk") != std::string::npos, "external source modification refuses to save");
		}

		const std::filesystem::path malformed = server.write("broken.xml", "<monster name=\"Broken\">");
		Check(!MonsterDefinitionDocument::Load(SourceFor(ServerContentFormat::Xml, malformed), error), "malformed XML is rejected");
	}

	void TestRealServers(const std::filesystem::path& modernRoot, const std::filesystem::path& xmlRoot) {
		const ServerDetectionResult modernDetection = ServerResourceDetector::Detect(modernRoot);
		const ServerContentIndex modern = ServerContentIndex::Build(modernDetection.workspace);
		const auto modernDemon = modern.findExact(ServerContentKind::Monster, "Demon");
		Check(modernDemon.unique(), "real modern base resolves Demon uniquely");
		std::string error;
		auto document = modernDemon.unique() ? MonsterDefinitionDocument::Load(*modernDemon.value(), error) : nullptr;
		Check(document != nullptr, "real modern Lua Demon opens: " + error);
		if (document) {
			Check(document->definition().capability(MonsterField::Health).editable, "real modern Lua health is editable");
			Check(document->definition().capability(MonsterField::LookType).editable, "real modern Lua lookType is editable");
		}

		const ServerDetectionResult xmlDetection = ServerResourceDetector::Detect(xmlRoot);
		const ServerContentIndex xml = ServerContentIndex::Build(xmlDetection.workspace);
		const auto xmlDemon = xml.findExact(ServerContentKind::Monster, "Demon");
		Check(xmlDemon.ambiguous() && xmlDemon.uniqueRegisteredValue() != nullptr, "real XML duplicate names resolve only through one authoritative registry entry");
		const ServerContentSource* uniqueXmlMonster = nullptr;
		document.reset();
		for (const ServerContentSource& source : xml.entries()) {
			if (source.kind == ServerContentKind::Monster && source.format == ServerContentFormat::Xml
				&& xml.findExact(ServerContentKind::Monster, source.name).unique()) {
				auto candidate = MonsterDefinitionDocument::Load(source, error);
				if (candidate && candidate->definition().capability(MonsterField::Health).editable
					&& candidate->definition().capability(MonsterField::LookType).editable) {
					uniqueXmlMonster = &source;
					document = std::move(candidate);
					break;
				}
			}
		}
		Check(uniqueXmlMonster != nullptr, "real XML base exposes an ambiguity-safe monster with Main and Look literals");
		Check(document != nullptr, "real XML monster opens: " + error);
		if (document) {
			Check(document->definition().capability(MonsterField::Health).editable, "real XML health is editable");
			Check(document->definition().capability(MonsterField::LookType).editable, "real XML lookType is editable");
		}
	}
}

int main(int argc, char** argv) {
	TestXmlPreservingSave();
	TestLuaPreservingSave();
	TestAmbiguousAndExternalChanges();
	if (argc == 3) {
		TestRealServers(std::filesystem::path(argv[1]), std::filesystem::path(argv[2]));
	} else if (argc != 1) {
		std::cerr << "Usage: monster_definition_provider_tests [modern-lua-server xml-server]\n";
		return 2;
	}
	if (failures == 0) {
		std::cout << checks << " monster definition provider checks passed.\n";
	}
	return failures == 0 ? 0 : 1;
}
