#include "server_content_index.h"
#include "server_workspace.h"
#include "spell_definition.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
	int checks = 0;
	int failures = 0;

	void Check(bool condition, const std::string& message) {
		++checks;
		if (!condition) {
			++failures;
			std::cerr << "FAIL: " << message << '\n';
		}
	}

	void Write(const std::filesystem::path& path, const std::string& bytes) {
		std::filesystem::create_directories(path.parent_path());
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		stream << bytes;
	}

	std::string Read(const std::filesystem::path& path) {
		std::ifstream stream(path, std::ios::binary);
		return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
	}

	std::size_t LineOf(const std::string& bytes, const std::string& needle) {
		const std::size_t position = bytes.find(needle);
		return position == std::string::npos ? 0 : 1 + static_cast<std::size_t>(std::count(bytes.begin(), bytes.begin() + position, '\n'));
	}

	struct TemporaryDirectory {
		std::filesystem::path path = std::filesystem::temp_directory_path() / ("nexamap-spell-provider-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		TemporaryDirectory() {
			std::filesystem::create_directories(path);
		}
		~TemporaryDirectory() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	};

	void TestLuaSourcePreservation() {
		TemporaryDirectory temporary;
		const auto path = temporary.path / "data/scripts/spells/attack/flame_wave.lua";
		const std::string bytes = "-- keep this provider comment\n"
								  "local combat = Combat()\n"
								  "combat:setParameter(COMBAT_PARAM_TYPE, COMBAT_FIREDAMAGE)\n"
								  "combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_FIREAREA)\n"
								  "combat:setParameter(COMBAT_PARAM_DISTANCEEFFECT, CONST_ANI_FIRE)\n"
								  "local area = createCombatArea({\n"
								  "    {0, 1, 0},\n"
								  "    {1, 3, 1}\n"
								  "})\n"
								  "combat:setArea(area)\n"
								  "local spell = Spell(SPELL_INSTANT)\n"
								  "spell:name(\"Flame Wave\")\n"
								  "spell:words(\"exevo flam\")\n"
								  "spell:level(20)\n"
								  "spell:isAggressive(true)\n"
								  "spell:needDirection(true)\n"
								  "spell:register()\n";
		Write(path, bytes);
		ServerContentSource source;
		source.kind = ServerContentKind::Spell;
		source.format = ServerContentFormat::Lua;
		source.serverType = ServerType::Tfs;
		source.name = "Flame Wave";
		source.declarationPath = path;
		source.declarationLine = LineOf(bytes, "local spell = Spell");
		source.declarationExists = true;

		std::string error;
		auto document = SpellDefinitionDocument::Load(source, error);
		Check(document != nullptr, "TFS Lua spell opens: " + error);
		if (!document) {
			return;
		}
		Check(document->definition().level == 20 && document->definition().words == "exevo flam", "Lua registration literals are normalized");
		Check(document->definition().combatType == "COMBAT_FIREDAMAGE" && document->definition().effect == "CONST_ME_FIREAREA", "Lua combat parameters are normalized");
		Check(document->definition().customAreaTiles.size() == 4, "literal Lua area matrix is resolved exactly");
		Check(document->definition().capability(SpellField::Effect).editable, "unique effect literal is editable");
		Check(!document->definition().capability(SpellField::Area).editable, "literal area matrix stays source-preserved and read-only");

		SpellDefinition edited = document->definition();
		edited.name = "Flame Wave Test";
		edited.level = 25;
		edited.effect = "CONST_ME_HITBYFIRE";
		Check(document->save(edited, error), "Lua spell saves source patches: " + error);
		const std::string saved = Read(path);
		Check(saved.find("-- keep this provider comment") != std::string::npos, "Lua comments are preserved byte-for-byte");
		Check(saved.find("spell:name(\"Flame Wave Test\")") != std::string::npos && saved.find("spell:level(25)") != std::string::npos, "Lua registration changes are applied");
		Check(saved.find("COMBAT_PARAM_EFFECT, CONST_ME_HITBYFIRE") != std::string::npos, "Lua implementation change is applied");

		SpellDefinition externalEdit = document->definition();
		externalEdit.level = 30;
		Write(path, saved + "-- external change\n");
		Check(!document->save(externalEdit, error) && error.find("changed on disk") != std::string::npos, "fingerprint conflict rejects an external source change");
	}

	void TestExactLuaDeclarationSelection() {
		TemporaryDirectory temporary;
		const auto path = temporary.path / "data/scripts/spells/two.lua";
		const std::string bytes = "local first = Spell(\"instant\")\nfirst:name(\"First\")\nfirst:level(10)\nfirst:register()\n"
								  "local second = Spell(\"instant\")\nsecond:name(\"Second\")\nsecond:level(55)\nsecond:register()\n";
		Write(path, bytes);
		ServerContentSource source;
		source.kind = ServerContentKind::Spell;
		source.format = ServerContentFormat::Lua;
		source.name = "Second";
		source.declarationPath = path;
		source.declarationLine = LineOf(bytes, "local second");
		std::string error;
		auto document = SpellDefinitionDocument::Load(source, error);
		Check(document && document->definition().name == "Second" && document->definition().level == 55, "exact declaration line selects the right spell in a shared Lua file");
	}

	void TestXmlRegistryAndLuaImplementation() {
		TemporaryDirectory temporary;
		const auto registry = temporary.path / "data/spells/spells.xml";
		const auto script = temporary.path / "data/spells/scripts/attack/fireball.lua";
		const std::string xml = "<?xml version=\"1.0\"?>\n<spells>\n"
								"  <!-- preserve registry comment -->\n"
								"  <instant name=\"Fireball\" words=\"adori flam\" lvl=\"12\" mana=\"30\" aggressive=\"1\" script=\"attack/fireball.lua\" custom=\"untouched\">\n"
								"    <vocation name=\"Sorcerer\"/>\n"
								"  </instant>\n</spells>\n";
		const std::string lua = "-- preserve script comment\nlocal combat = Combat()\n"
								"combat:setParameter(COMBAT_PARAM_TYPE, COMBAT_FIREDAMAGE)\n"
								"combat:setParameter(COMBAT_PARAM_EFFECT, CONST_ME_FIREAREA)\n"
								"combat:setArea(createCombatArea(AREA_CIRCLE3X3))\n"
								"function onCastSpell(creature, variant) return combat:execute(creature, variant) end\n";
		Write(registry, xml);
		Write(script, lua);
		ServerContentSource source;
		source.kind = ServerContentKind::Spell;
		source.format = ServerContentFormat::Xml;
		source.serverType = ServerType::Tfs;
		source.name = "Fireball";
		source.subtype = "instant";
		source.declarationPath = registry;
		source.registrationPath = registry;
		source.relatedScriptPath = script;
		source.declarationLine = LineOf(xml, "<instant name");
		std::string error;
		auto document = SpellDefinitionDocument::Load(source, error);
		Check(document != nullptr, "XML registry spell opens with paired Lua: " + error);
		if (!document) {
			return;
		}
		Check(document->hasSeparateImplementation(), "XML registration and Lua implementation remain separate sources");
		Check(document->definition().mana == 30 && document->definition().vocations == std::vector<std::string> { "Sorcerer" }, "XML fields and vocations are normalized");
		Check(document->definition().areaExpression == "AREA_CIRCLE3X3" && document->definition().preview.area.radius == 3, "paired Lua static area is resolved");

		SpellDefinition edited = document->definition();
		edited.name = "Greater Fireball";
		edited.mana = 45;
		edited.effect = "CONST_ME_EXPLOSIONAREA";
		edited.areaExpression = "AREA_CIRCLE2X2";
		Check(document->save(edited, error), "paired XML/Lua transaction saves: " + error);
		const std::string savedXml = Read(registry);
		const std::string savedLua = Read(script);
		Check(savedXml.find("name=\"Greater Fireball\"") != std::string::npos && savedXml.find("mana=\"45\"") != std::string::npos, "XML registry values are patched");
		Check(savedXml.find("custom=\"untouched\"") != std::string::npos && savedXml.find("preserve registry comment") != std::string::npos, "unknown XML and comments are preserved");
		Check(savedLua.find("CONST_ME_EXPLOSIONAREA") != std::string::npos && savedLua.find("AREA_CIRCLE2X2") != std::string::npos && savedLua.find("preserve script comment") != std::string::npos, "paired Lua changes preserve surrounding source");
	}

	void TestRealBase(const std::filesystem::path& root, const std::string& label) {
		const ServerDetectionResult detection = ServerResourceDetector::Detect(root);
		Check(detection.validRoot, label + " real base is detected");
		if (!detection.validRoot) {
			return;
		}
		const ServerContentIndex index = ServerContentIndex::Build(detection.workspace);
		std::size_t indexed = 0;
		std::size_t opened = 0;
		std::size_t visual = 0;
		for (const ServerContentSource& source : index.entries()) {
			if (source.kind != ServerContentKind::Spell || !source.declarationExists) {
				continue;
			}
			++indexed;
			std::string error;
			auto document = SpellDefinitionDocument::Load(source, error);
			if (!document) {
				continue;
			}
			++opened;
			if (!document->definition().combatType.empty() || !document->definition().effect.empty() || !document->definition().areaExpression.empty()) {
				++visual;
			}
			if (opened >= 40 && visual > 0) {
				break;
			}
		}
		Check(indexed > 0, label + " contains indexed global spells");
		Check(opened > 0, label + " opens at least one real spell definition");
		Check(visual > 0, label + " exposes combat visual metadata from a real spell");
		std::cout << label << ": " << indexed << " inspected, " << opened << " opened, " << visual << " with visual metadata.\n";
	}
}

int main(int argc, char** argv) {
	TestLuaSourcePreservation();
	TestExactLuaDeclarationSelection();
	TestXmlRegistryAndLuaImplementation();
	if (argc > 1) {
		TestRealBase(argv[1], "TFS Lua");
	}
	if (argc > 2) {
		TestRealBase(argv[2], "TFS XML");
	}
	if (argc > 3) {
		TestRealBase(argv[3], "Crystal/Canary");
	}
	if (!failures) {
		std::cout << checks << " spell definition provider checks passed.\n";
	}
	return failures ? 1 : 0;
}
