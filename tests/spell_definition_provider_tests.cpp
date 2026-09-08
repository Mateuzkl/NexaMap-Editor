#include "server_content_index.h"
#include "server_workspace.h"
#include "spell_area_library.h"
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
		Write(
			temporary.path / "data/scripts/lib/spell_lib.lua",
			"AREA_CIRCLE3X3 = {\n  {0, 1, 0},\n  {1, 3, 1},\n  {0, 1, 0}\n}\n"
		);
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
		Check(
			document->definition().areaExpression == "AREA_CIRCLE3X3"
				&& document->definition().areaResolutionState == SpellAreaResolutionState::Resolved
				&& document->definition().customAreaTiles.size() == 5,
			"paired Lua static area is resolved from the active workspace library"
		);

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

	void TestWorkspaceAreaResolver() {
		TemporaryDirectory temporary;
		Write(
			temporary.path / "data/scripts/lib/areas.lua",
			"--[=[ AREA_FAKE_COMMENT = {{3}} } ]=]\n"
			"local ignored = [==[ AREA_FAKE_STRING = {{3}} } ]==]\n"
			"AREA_BASE = {\n  {0, 1, 0},\n  {1, 3, 1}\n}\n"
			"AREA_ALIAS = AREA_BASE\n"
			"AREA_DYNAMIC = buildArea(radius)\n"
		);
		ServerWorkspace workspace;
		workspace.rootPath = temporary.path;
		workspace.activeDataDirectory = temporary.path / "data";
		const SpellAreaResolver resolver(workspace);
		Check(
			resolver.stats().filesRead == 1 && resolver.stats().definitionsParsed == 3,
			"area resolver exposes deterministic scan counters (read " + std::to_string(resolver.stats().filesRead)
				+ ", parsed " + std::to_string(resolver.stats().definitionsParsed) + ")"
		);
		const auto literal = resolver.resolve("createCombatArea({{1, 3, 1}})");
		Check(literal.state == SpellAreaResolutionState::Resolved && literal.tiles.size() == 3, "literal createCombatArea resolves exact tiles");
		const auto alias = resolver.resolve("AREA_ALIAS");
		Check(alias.state == SpellAreaResolutionState::Resolved && alias.tiles.size() == 4, "workspace area aliases resolve through their real definition");
		const auto dynamic = resolver.resolve("AREA_DYNAMIC");
		Check(dynamic.state == SpellAreaResolutionState::Unresolved, "dynamic areas stay unresolved instead of falling back to Single");
		const auto missing = resolver.resolve("AREA_NOT_DEFINED");
		Check(missing.state == SpellAreaResolutionState::Unresolved, "missing constants stay unresolved instead of falling back to Single");
		Check(resolver.resolve("AREA_FAKE_COMMENT").state == SpellAreaResolutionState::Unresolved, "long comments with equals do not create area definitions");
		Check(resolver.resolve("AREA_FAKE_STRING").state == SpellAreaResolutionState::Unresolved, "long strings with equals do not create area definitions");
	}

	void TestReusableAreaCreation() {
		TemporaryDirectory temporary;
		const auto libraryPath = temporary.path / "data/scripts/lib/spell_lib.lua";
		const std::string original = "-- preserve helper functions\r\nlocal function helper() return true end\r\n\r\nAREA_EXISTING = {\r\n\t{0, 3, 0},\r\n\t{1, 1, 1},\r\n}\r\n";
		Write(libraryPath, original);
		ServerWorkspace workspace;
		workspace.rootPath = temporary.path;
		workspace.activeDataDirectory = temporary.path / "data";
		SpellAreaResolver resolver(workspace);
		const auto targets = SpellAreaLibrary::DiscoverTargets(workspace, resolver);
		Check(targets.size() == 1 && targets.front().path == libraryPath, "area creator selects the real spell_lib.lua provider");
		if (targets.empty()) {
			return;
		}

		EditableSpellArea area;
		area.name = "AREA_NEXAMAP_TEST";
		area.width = 3;
		area.height = 3;
		area.cells = { 0, 1, 0, 1, 3, 1, 0, 1, 0 };
		SpellAreaLibrarySaveResult result;
		std::string error;
		Check(SpellAreaLibrary::Save(workspace, resolver, targets.front(), area, result, error), "reusable area saves transactionally: " + error);
		const std::string saved = Read(libraryPath);
		Check(saved.starts_with(original), "area creation preserves every existing source byte and CRLF style");
		Check(saved.find("AREA_NEXAMAP_TEST = {\r\n\t{0, 1, 0},") != std::string::npos, "area creation writes the selected matrix in the server library");
		const SpellAreaResolver refreshed(workspace);
		const auto resolved = refreshed.resolve("AREA_NEXAMAP_TEST");
		Check(resolved.state == SpellAreaResolutionState::Resolved && resolved.tiles.size() == 5, "new reusable area resolves to the exact selected tiles");
		const auto refreshedTargets = SpellAreaLibrary::DiscoverTargets(workspace, refreshed);
		Check(!refreshedTargets.empty() && !SpellAreaLibrary::Save(workspace, refreshed, refreshedTargets.front(), area, result, error) && error.find("already exists") != std::string::npos, "duplicate area names are ambiguity-safe");
	}

	void TestVocationProviders() {
		TemporaryDirectory temporary;
		const auto luaPath = temporary.path / "data/scripts/spells/support/vocation.lua";
		const std::string lua = "-- preserve vocation provider\n"
								"local spell = Spell(SPELL_INSTANT)\n"
								"spell:name(\"Vocation Test\")\n"
								"spell:vocation(\"sorcerer;true\", \"custom mage\")\n"
								"customRegistrationHook()\n"
								"spell:register()\n";
		Write(luaPath, lua);
		ServerContentSource luaSource;
		luaSource.kind = ServerContentKind::Spell;
		luaSource.format = ServerContentFormat::Lua;
		luaSource.name = "Vocation Test";
		luaSource.declarationPath = luaPath;
		luaSource.declarationLine = LineOf(lua, "local spell");
		std::string error;
		auto luaDocument = SpellDefinitionDocument::Load(luaSource, error);
		Check(luaDocument != nullptr, "Lua vocation spell opens: " + error);
		if (luaDocument) {
			Check(
				luaDocument->definition().vocationCapability.state == SpellVocationCapabilityState::ExistingEditableLiteral
					&& luaDocument->definition().vocations == std::vector<std::string>({ "sorcerer;true", "custom mage" }),
				"existing Lua vocation list and description flag are editable"
			);
			SpellDefinition edited = luaDocument->definition();
			edited.vocations = { "druid", "custom mage;true" };
			Check(luaDocument->save(edited, error), "Lua vocations save through their existing provider call: " + error);
			const std::string saved = Read(luaPath);
			Check(saved.find("spell:vocation(\"druid\", \"custom mage;true\")") != std::string::npos, "Lua vocation arguments are replaced without regenerating the file");
			Check(saved.find("customRegistrationHook()") != std::string::npos && saved.find("preserve vocation provider") != std::string::npos, "custom Lua around vocations remains preserved");
		}

		const auto globalPath = temporary.path / "data/scripts/spells/support/global.lua";
		const std::string global = "local spell = Spell(SPELL_INSTANT)\nspell:name(\"Global Test\")\ncustomHook()\nspell:register()\n";
		Write(globalPath, global);
		ServerContentSource globalSource = luaSource;
		globalSource.name = "Global Test";
		globalSource.declarationPath = globalPath;
		globalSource.declarationLine = 1;
		auto globalDocument = SpellDefinitionDocument::Load(globalSource, error);
		Check(
			globalDocument && globalDocument->definition().allVocations
				&& globalDocument->definition().vocationCapability.state == SpellVocationCapabilityState::SupportedInsertable,
			"absent Lua vocation call is represented as editable All vocations"
		);
		if (globalDocument) {
			SpellDefinition edited = globalDocument->definition();
			edited.vocations = { "royal paladin", "custom id 42" };
			Check(globalDocument->save(edited, error), "absent Lua vocation declaration is inserted before register: " + error);
			const std::string saved = Read(globalPath);
			Check(
				saved.find("spell:vocation(\"royal paladin\", \"custom id 42\")") < saved.find("spell:register()")
					&& saved.find("customHook()") != std::string::npos,
				"inserted Lua vocation call uses the detected spell variable and preserves custom code"
			);
		}

		const auto dynamicPath = temporary.path / "data/scripts/spells/support/dynamic.lua";
		const std::string dynamic = "local spell = Spell(SPELL_INSTANT)\nspell:name(\"Dynamic Test\")\nspell:vocation(unpack(config.vocations))\nspell:register()\n";
		Write(dynamicPath, dynamic);
		ServerContentSource dynamicSource = luaSource;
		dynamicSource.name = "Dynamic Test";
		dynamicSource.declarationPath = dynamicPath;
		dynamicSource.declarationLine = 1;
		auto dynamicDocument = SpellDefinitionDocument::Load(dynamicSource, error);
		Check(
			dynamicDocument && dynamicDocument->definition().vocationCapability.state == SpellVocationCapabilityState::DynamicReadOnly
				&& !dynamicDocument->definition().vocationCapability.editable,
			"computed Lua vocation declarations remain read-only"
		);

		const auto xmlPath = temporary.path / "data/spells/spells.xml";
		const std::string xml = "<spells>\n  <instant name=\"XML Global\" words=\"exana test\"/>\n</spells>\n";
		Write(xmlPath, xml);
		ServerContentSource xmlSource;
		xmlSource.kind = ServerContentKind::Spell;
		xmlSource.format = ServerContentFormat::Xml;
		xmlSource.name = "XML Global";
		xmlSource.subtype = "instant";
		xmlSource.declarationPath = xmlPath;
		xmlSource.declarationLine = 2;
		auto xmlDocument = SpellDefinitionDocument::Load(xmlSource, error);
		Check(xmlDocument && xmlDocument->definition().allVocations && xmlDocument->definition().vocationCapability.editable, "self-closing XML spells expose insertable vocations");
		if (xmlDocument) {
			SpellDefinition edited = xmlDocument->definition();
			edited.vocations = { "Druid", "Custom Vocation" };
			Check(xmlDocument->save(edited, error), "XML vocations save by expanding the selected spell node: " + error);
			const std::string saved = Read(xmlPath);
			Check(saved.find("<vocation name=\"Druid\"/>") != std::string::npos && saved.find("<vocation name=\"Custom Vocation\"/>") != std::string::npos, "XML standard and custom vocations are inserted");
		}
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
		if (label == "TFS Lua") {
			const ServerContentLookupResult eternalWinter = index.findExact(ServerContentKind::Spell, "Eternal Winter");
			std::string areaError;
			auto eternalWinterDocument = eternalWinter.value()
				? SpellDefinitionDocument::Load(*eternalWinter.value(), areaError, &detection.workspace)
				: nullptr;
			Check(eternalWinterDocument != nullptr, "real Eternal Winter opens: " + areaError);
			if (eternalWinterDocument) {
				const SpellDefinition& spell = eternalWinterDocument->definition();
				Check(spell.areaExpression == "AREA_CIRCLE5X5", "real Eternal Winter retains AREA_CIRCLE5X5");
				Check(
					spell.areaResolutionState == SpellAreaResolutionState::Resolved && !spell.customAreaTiles.empty(),
					"real Eternal Winter resolves AREA_CIRCLE5X5 from spell_lib instead of displaying Single"
				);
			}
		}
		std::cout << label << ": " << indexed << " inspected, " << opened << " opened, " << visual << " with visual metadata.\n";
	}
}

int main(int argc, char** argv) {
	TestLuaSourcePreservation();
	TestExactLuaDeclarationSelection();
	TestXmlRegistryAndLuaImplementation();
	TestWorkspaceAreaResolver();
	TestReusableAreaCreation();
	TestVocationProviders();
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
