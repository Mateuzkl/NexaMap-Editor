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
			path = std::filesystem::temp_directory_path() / ("nexamap-content-index-test-" + std::to_string(stamp));
			std::filesystem::create_directories(path);
		}

		~TemporaryDirectory() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}

		void write(const std::filesystem::path& relative, const std::string& contents) const {
			const std::filesystem::path target = path / relative;
			std::filesystem::create_directories(target.parent_path());
			std::ofstream stream(target, std::ios::binary | std::ios::trunc);
			stream << contents;
		}

		std::filesystem::path path;
	};

	ServerWorkspace WorkspaceFor(const TemporaryDirectory& directory) {
		ServerWorkspace workspace;
		workspace.rootPath = std::filesystem::weakly_canonical(directory.path);
		workspace.monstersDirectory = workspace.rootPath / "data/monsters";
		workspace.npcsDirectory = workspace.rootPath / "data/npc";
		workspace.spellsDirectory = workspace.rootPath / "data/scripts/spells";
		return workspace;
	}

	std::size_t CountKind(const ServerContentIndex& index, ServerContentKind kind) {
		std::size_t count = 0;
		for (const ServerContentSource& source : index.entries()) {
			count += source.kind == kind ? 1 : 0;
		}
		return count;
	}

	void TestLuaDetection() {
		TemporaryDirectory server;
		server.write(
			"data/monsters/amphibics/azure_frog.lua",
			"-- Game.createMonsterType(\"Comment Monster\")\n"
			"local monsterName = \"Azure Frog\"\n"
			"local mType = Game.createMonsterType(monsterName)\n"
			"local monster = {}\n"
			"mType:register(monster)\n"
		);
		server.write(
			"data/monsters/helper.lua",
			"local example = 'Game.createMonsterType(\\\"String Monster\\\")'\n"
		);
		server.write(
			"data/npc/banker.lua",
			"local npcName = 'Banker'\n"
			"local npc = Game.createNpcType(npcName)\n"
			"npc:defaultBehavior()\n"
		);
		server.write("data/npc/scripts/bank.lua", "function onCreatureSay() return true end\n");
		server.write(
			"data/scripts/spells/test.lua",
			"--[[ local fake = Spell(\"instant\") fake:name(\"Comment Spell\") ]]\n"
			"local spell = Spell(\"instant\")\n"
			"spell:name(\"Energy Wave\")\n"
			"spell:register()\n"
			"local rune = Spell(SPELL_RUNE)\n"
			"local runeName = \"Fire Rune\"\n"
			"rune:name(runeName)\n"
			"rune:register()\n"
		);

		const ServerContentIndex index = ServerContentIndex::Build(WorkspaceFor(server));
		Check(CountKind(index, ServerContentKind::Monster) == 1, "Lua scanner ignores monster calls in comments and strings");
		Check(CountKind(index, ServerContentKind::Npc) == 1, "Lua scanner indexes only structural createNpcType definitions");
		Check(CountKind(index, ServerContentKind::Spell) == 2, "Lua scanner indexes multiple registered spells in one source");
		Check(index.findExact(ServerContentKind::Monster, "Azure Frog").unique(), "exact monster lookup finds one source");
		Check(index.findExact(ServerContentKind::Monster, "azure frog").empty(), "exact lookup preserves case");
		Check(index.findCaseInsensitive(ServerContentKind::Monster, "azure frog").unique(), "case-folded lookup is explicit");
		const ServerContentSource* monster = index.findExact(ServerContentKind::Monster, "Azure Frog").value();
		Check(monster != nullptr && monster->registered, "Lua monster registration is detected structurally");
		Check(monster != nullptr && monster->declarationLine == 3, "Lua source metadata records the declaration line");
		Check(monster != nullptr && monster->declarationFingerprint.exists, "Lua source metadata includes its fingerprint");
		Check(monster != nullptr && monster->registrationPath == monster->declarationPath, "Lua source metadata records its exact registration source");
		Check(monster != nullptr && monster->registrationLine == 5, "Lua source metadata records its registration line");
		const ServerContentSource* rune = index.findExact(ServerContentKind::Spell, "Fire Rune").value();
		Check(rune != nullptr && rune->subtype == "SPELL_RUNE", "Lua spell subtype preserves identifier metadata");
		Check(index.capabilities().monsters.luaDefinitions && !index.capabilities().monsters.xmlDefinitions, "monster capability is detected per format");
		Check(index.capabilities().npcs.luaDefinitions && !index.capabilities().npcs.relatedLua, "unrelated NPC helper Lua is not a capability source");
		Check(index.capabilities().spells.luaDefinitions, "Lua spell capability is detected");
		Check(!index.capabilities().isMixed(), "Lua-only server is not marked mixed");
	}

	void TestXmlAndLinkedLuaDetection() {
		TemporaryDirectory server;
		server.write(
			"data/monster/monsters.xml",
			"<?xml version=\"1.0\"?>\n"
			"<monsters>\n"
			"  <monster name=\"Registered Rat\" file=\"rat.xml\"/>\n"
			"  <monster name=\"Missing Ghost\" file=\"missing.xml\"/>\n"
			"</monsters>\n"
		);
		server.write("data/monster/rat.xml", "<monster name=\"Rat\"><health now=\"20\" max=\"20\"/></monster>\n");
		server.write("data/monster/orphan.xml", "<monster name=\"Orphan\"/>\n");
		server.write("data/npc/guide.xml", "<npc name=\"Guide\" script=\"guide.lua\"><look type=\"128\"/></npc>\n");
		server.write("data/npc/scripts/guide.lua", "function onCreatureSay() return true end\n");
		server.write(
			"data/spells/spells.xml",
			"<spells>\n"
			"  <rune name=\"Fireball\" id=\"2302\" script=\"attack/fireball.lua\"/>\n"
			"  <instant name=\"Light\" words=\"utevo lux\"/>\n"
			"</spells>\n"
		);
		server.write("data/spells/scripts/attack/fireball.lua", "function onCastSpell() return true end\n");

		ServerWorkspace workspace;
		workspace.rootPath = std::filesystem::weakly_canonical(server.path);
		workspace.monstersDirectory = workspace.rootPath / "data/monster";
		workspace.npcsDirectory = workspace.rootPath / "data/npc";
		workspace.spellsDirectory = workspace.rootPath / "data/spells";
		const ServerContentIndex index = ServerContentIndex::Build(workspace);

		const ServerContentLookupResult ratLookup = index.findExact(ServerContentKind::Monster, "Rat");
		Check(ratLookup.unique(), "XML monster definition is indexed by its declared name");
		const ServerContentSource* rat = ratLookup.value();
		Check(rat != nullptr && rat->registered, "monster registry is correlated with its definition");
		Check(rat != nullptr && rat->registrationPath && rat->registrationPath->filename() == "monsters.xml", "monster metadata retains the exact registry path");
		Check(rat != nullptr && rat->registrationLine == 3, "monster metadata retains the registry line");
		Check(index.findExact(ServerContentKind::Monster, "Registered Rat").unique(), "registry alias resolves to the same monster source");
		const ServerContentSource* orphan = index.findExact(ServerContentKind::Monster, "Orphan").value();
		Check(orphan != nullptr && !orphan->registered, "unlisted XML monster remains visible as unregistered");
		const ServerContentSource* missing = index.findExact(ServerContentKind::Monster, "Missing Ghost").value();
		Check(missing != nullptr && missing->registered && !missing->declarationExists, "missing registered monster retains registration metadata safely");
		Check(CountKind(index, ServerContentKind::Npc) == 1, "traditional NPC behavior Lua is not indexed as another NPC");
		const ServerContentSource* npc = index.findExact(ServerContentKind::Npc, "Guide").value();
		Check(npc != nullptr && npc->relatedScriptPath && npc->relatedScriptPath->filename() == "guide.lua", "NPC XML resolves its related behavior script");
		const ServerContentSource* spell = index.findExact(ServerContentKind::Spell, "Fireball").value();
		Check(spell != nullptr && spell->subtype == "rune", "spell registry preserves its exact subtype");
		Check(spell != nullptr && spell->relatedScriptFingerprint && spell->relatedScriptFingerprint->exists, "spell registry resolves and fingerprints its implementation");
		Check(index.capabilities().monsters.xmlDefinitions && !index.capabilities().monsters.luaDefinitions, "XML monster capability is category-specific");
		Check(index.capabilities().npcs.xmlDefinitions && index.capabilities().npcs.relatedLua, "NPC capabilities distinguish XML declarations from related Lua");
		Check(index.capabilities().spells.xmlDefinitions && index.capabilities().spells.relatedLua, "spell capabilities distinguish XML registry from related Lua");
		Check(index.capabilities().isMixed(), "paired XML/Lua server is marked mixed");
		server.write(
			"data/monster/monsters.xml",
			"<monsters><monster name=\"Registry Rat\" file=\"rat.xml\"/></monsters>\n"
		);
		const ServerContentIndex registryChanged = ServerContentIndex::RefreshPaths(workspace, index, { workspace.monstersDirectory / "monsters.xml" });
		Check(registryChanged.stats().filesParsed == 1, "targeted registry refresh reparses only the registry");
		Check(registryChanged.findExact(ServerContentKind::Monster, "Registry Rat").unique(), "targeted registry refresh updates aliases");
	}

	void TestAmbiguityAndDiagnostics() {
		TemporaryDirectory server;
		server.write("data/monsters/a.lua", "local a = Game.createMonsterType(\"Duplicate\")\na:register({})\n");
		server.write("data/monsters/b.lua", "local b = Game.createMonsterType(\"Duplicate\")\nb:register({})\n");
		server.write("data/npc/broken.xml", "<npc name=\"Broken\">\n");
		server.write("data/scripts/spells/fake.lua", "local text = \"Spell('instant')\"\n");

		const ServerContentIndex index = ServerContentIndex::Build(WorkspaceFor(server));
		const ServerContentLookupResult duplicates = index.findExact(ServerContentKind::Monster, "Duplicate");
		Check(duplicates.ambiguous() && duplicates.value() == nullptr, "duplicate exact names remain ambiguity-safe");
		Check(!index.diagnostics().empty(), "malformed content is reported without aborting the scan");
		Check(CountKind(index, ServerContentKind::Spell) == 0, "spell-like strings do not create false definitions");
	}

	void TestFingerprintsAndCache() {
		TemporaryDirectory server;
		server.write("data/monsters/rat.lua", "local rat = Game.createMonsterType(\"Rat\")\nrat:register({})\n");
		server.write("data/npc/guide.xml", "<npc name=\"Guide\" script=\"guide.lua\"/>\n");
		server.write("data/npc/scripts/guide.lua", "function onCreatureSay() return true end\n");
		server.write("data/scripts/spells/light.lua", "local spell = Spell(\"instant\")\nspell:name(\"Light\")\nspell:register()\n");
		const ServerWorkspace workspace = WorkspaceFor(server);

		const ServerContentIndex first = ServerContentIndex::Build(workspace);
		Check(first.stats().filesParsed == first.stats().filesDiscovered, "first scan parses every candidate");
		Check(first.stats().fullScans == 1 && first.stats().targetedRefreshes == 0, "cold build records one full scan");
		Check(!first.trackedSourcesChanged(), "fresh source fingerprints match current files");
		const ServerContentLookupResult retainedLookup = first.findExact(ServerContentKind::Monster, "Rat");

		const ServerContentIndex second = ServerContentIndex::Build(workspace, &first);
		Check(second.stats().filesParsed == 0, "unchanged rescan performs no parsing");
		Check(second.stats().filesRead == 0, "unchanged rescan reads zero definition files");
		Check(second.stats().filesReused == second.stats().filesDiscovered, "unchanged rescan reuses every cached file");
		Check(second.stats().cacheRecordsShared == second.stats().filesDiscovered, "unchanged rescan shares every immutable parsed record");
		Check(second.snapshot() == first.snapshot(), "unchanged rescan shares the immutable assembled source snapshot");
		Check(first.sameContentAs(second), "cache reuse does not alter indexed content");

		server.write("data/monsters/rat.lua", "local rat = Game.createMonsterType(\"Cave Rat\")\nrat:register({ description = \"larger\" })\n");
		Check(first.trackedSourcesChanged(), "modified declaration invalidates tracked source fingerprints");
		const auto ratPath = workspace.monstersDirectory / "rat.lua";
		const ServerContentIndex third = ServerContentIndex::RefreshPaths(workspace, second, { ratPath });
		Check(third.stats().targetedRefreshes == 1 && third.stats().fullScans == 0, "single save uses a targeted refresh");
		Check(third.stats().filesParsed == 1 && third.stats().filesRead == 1, "targeted refresh reparses only the modified source");
		Check(third.stats().filesReused + 1 == third.stats().filesDiscovered, "targeted refresh retains unchanged cached sources");
		Check(third.findExact(ServerContentKind::Monster, "Cave Rat").unique(), "reparsed source updates the exact lookup");
		Check(retainedLookup.value() != nullptr && retainedLookup.value()->name == "Rat", "lookup keeps its immutable source snapshot alive after index replacement");

		server.write("data/monsters/new.lua", "local added = Game.createMonsterType(\"New Monster\")\nadded:register({})\n");
		const auto newPath = workspace.monstersDirectory / "new.lua";
		const ServerContentIndex withNewFile = ServerContentIndex::RefreshPaths(workspace, third, { newPath });
		Check(withNewFile.stats().filesParsed == 1, "rescan parses a newly discovered source without invalidating cached files");
		Check(withNewFile.findExact(ServerContentKind::Monster, "New Monster").unique(), "new source is added to exact lookup on rescan");

		const ServerContentIndex beforeScriptChange = withNewFile;
		server.write("data/npc/scripts/guide.lua", "function onCreatureSay() return false end -- changed and larger\n");
		Check(beforeScriptChange.trackedSourcesChanged(), "related Lua implementation participates in source change detection");
		const auto scriptPath = workspace.npcsDirectory / "scripts/guide.lua";
		const ServerContentIndex scriptChanged = ServerContentIndex::RefreshPaths(workspace, beforeScriptChange, { scriptPath });
		const ServerContentSource* guide = scriptChanged.findExact(ServerContentKind::Npc, "Guide").value();
		Check(guide != nullptr && guide->relatedScriptFingerprint && guide->relatedScriptFingerprint->MatchesCurrentFile(), "targeted related-script refresh updates the owning declaration metadata");

		std::filesystem::remove(ratPath);
		const ServerContentIndex deleted = ServerContentIndex::RefreshPaths(workspace, scriptChanged, { ratPath });
		Check(deleted.findExact(ServerContentKind::Monster, "Cave Rat").empty(), "targeted refresh removes a deleted declaration");

		const auto renamedPath = workspace.monstersDirectory / "renamed.lua";
		std::filesystem::rename(newPath, renamedPath);
		const ServerContentIndex renamed = ServerContentIndex::RefreshPaths(workspace, deleted, { newPath, renamedPath });
		const ServerContentSource* renamedMonster = renamed.findExact(ServerContentKind::Monster, "New Monster").value();
		Check(renamedMonster != nullptr && renamedMonster->declarationPath.filename() == "renamed.lua", "targeted refresh handles declaration rename as delete plus create");
	}

	void TestFileLimit() {
		TemporaryDirectory server;
		server.write("data/monsters/a.lua", "local a = Game.createMonsterType(\"A\")\na:register({})\n");
		server.write("data/monsters/b.lua", "local b = Game.createMonsterType(\"B\")\nb:register({})\n");
		ServerContentScanOptions options;
		options.maximumFiles = 1;
		const ServerContentIndex index = ServerContentIndex::Build(WorkspaceFor(server), nullptr, options);
		Check(index.stats().fileLimitReached, "bounded scan reports its file limit");
		Check(index.entries().size() == 1, "bounded scan never indexes beyond its file limit");
	}

	void TestRealServers(const std::filesystem::path& modernRoot, const std::filesystem::path& xmlRoot) {
		const ServerDetectionResult modernDetection = ServerResourceDetector::Detect(modernRoot);
		Check(modernDetection.validRoot, "real modern Lua server root is readable");
		Check(!modernDetection.workspace.spellsDirectory.empty(), "real modern Lua spell root is detected");
		Check(modernDetection.workspace.spellsDirectory == std::filesystem::weakly_canonical(modernRoot / "data/scripts/spells"), "real modern base selects the revscript spell root");
		const ServerContentIndex modern = ServerContentIndex::Build(modernDetection.workspace);
		Check(CountKind(modern, ServerContentKind::Monster) > 1700, "real modern base indexes its Lua monsters");
		Check(CountKind(modern, ServerContentKind::Npc) > 900, "real modern base indexes its Lua NPCs");
		Check(CountKind(modern, ServerContentKind::Spell) > 700, "real modern base indexes its revscript spells");
		Check(modern.capabilities().monsters.luaDefinitions && !modern.capabilities().monsters.xmlDefinitions, "real modern monster capabilities are Lua-only");
		Check(modern.capabilities().npcs.luaDefinitions, "real modern NPC capabilities include Lua definitions");
		Check(modern.capabilities().spells.luaDefinitions, "real modern spell capabilities include Lua definitions");
		Check(modern.findExact(ServerContentKind::Monster, "Azure Frog").unique(), "real modern base resolves a known monster exactly");
		Check(!modern.stats().fileLimitReached, "real modern base completes within scan bounds");
		const ServerContentIndex modernCached = ServerContentIndex::Build(modernDetection.workspace, &modern);
		Check(modernCached.stats().filesParsed == 0 && modernCached.stats().filesReused == modernCached.stats().filesDiscovered, "real modern base fully reuses an unchanged cache");
		std::cout << "Modern real base: " << CountKind(modern, ServerContentKind::Monster) << " monsters, " << CountKind(modern, ServerContentKind::Npc) << " NPCs, " << CountKind(modern, ServerContentKind::Spell) << " spells, " << modern.stats().filesDiscovered << " scanned files.\n";

		const ServerDetectionResult xmlDetection = ServerResourceDetector::Detect(xmlRoot);
		Check(xmlDetection.validRoot, "real XML/mixed server root is readable");
		Check(!xmlDetection.workspace.spellsDirectory.empty(), "real XML/mixed spell root is detected");
		Check(xmlDetection.workspace.spellsDirectory == std::filesystem::weakly_canonical(xmlRoot / "data/spells"), "real XML/mixed base prefers its active spells.xml registry over an example directory");
		const ServerContentIndex xml = ServerContentIndex::Build(xmlDetection.workspace);
		Check(CountKind(xml, ServerContentKind::Monster) > 1300, "real XML/mixed base indexes standalone and registered monsters");
		Check(CountKind(xml, ServerContentKind::Npc) > 150, "real XML/mixed base indexes XML NPC declarations");
		Check(CountKind(xml, ServerContentKind::Spell) > 280, "real XML/mixed base indexes spell registry entries");
		Check(xml.capabilities().monsters.xmlDefinitions, "real XML/mixed monster capabilities include XML");
		Check(xml.capabilities().npcs.xmlDefinitions && xml.capabilities().npcs.relatedLua, "real XML/mixed NPC capabilities include paired Lua behavior");
		Check(xml.capabilities().spells.xmlDefinitions && xml.capabilities().spells.relatedLua, "real XML/mixed spell capabilities include paired Lua implementations");
		Check(xml.capabilities().isMixed(), "real XML/Lua base is reported as mixed");
		Check(!xml.stats().fileLimitReached, "real XML/mixed base completes within scan bounds");
		const ServerContentIndex xmlCached = ServerContentIndex::Build(xmlDetection.workspace, &xml);
		Check(xmlCached.stats().filesParsed == 0 && xmlCached.stats().filesReused == xmlCached.stats().filesDiscovered, "real XML/mixed base fully reuses an unchanged cache");
		std::cout << "XML/mixed real base: " << CountKind(xml, ServerContentKind::Monster) << " monsters, " << CountKind(xml, ServerContentKind::Npc) << " NPCs, " << CountKind(xml, ServerContentKind::Spell) << " spells, " << xml.stats().filesDiscovered << " scanned files.\n";
	}

	void TestRealCanary(const std::filesystem::path& canaryRoot) {
		const ServerDetectionResult detection = ServerResourceDetector::Detect(canaryRoot);
		Check(detection.validRoot, "real Crystal/Canary server root is readable");
		Check(detection.workspace.usesCanaryCrystalLoader(), "real Crystal/Canary server family is detected");
		Check(
			detection.workspace.activeDataDirectory == std::filesystem::weakly_canonical(canaryRoot / "data-global"),
			"real Crystal/Canary base respects config.lua dataPackDirectory"
		);
		Check(
			detection.workspace.monstersDirectory == std::filesystem::weakly_canonical(canaryRoot / "data-global/monster")
				&& detection.workspace.npcsDirectory == std::filesystem::weakly_canonical(canaryRoot / "data-global/npc"),
			"real Crystal/Canary content roots follow the active datapack"
		);
		const ServerContentIndex index = ServerContentIndex::Build(detection.workspace);
		Check(CountKind(index, ServerContentKind::Monster) > 0, "real Crystal/Canary base indexes Lua monsters");
		Check(CountKind(index, ServerContentKind::Npc) > 0, "real Crystal/Canary base indexes Lua NPCs");
		Check(index.capabilities().monsters.luaDefinitions && index.capabilities().npcs.luaDefinitions, "real Crystal/Canary capabilities expose Lua monsters and NPCs");
		Check(index.findExact(ServerContentKind::Monster, "Toad").unique(), "real Crystal/Canary base resolves a known active-datapack monster");
		Check(!index.stats().fileLimitReached, "real Crystal/Canary scan completes within bounds");
		const ServerContentIndex cached = ServerContentIndex::Build(detection.workspace, &index);
		Check(
			cached.stats().filesParsed == 0 && cached.stats().filesReused == cached.stats().filesDiscovered,
			"real Crystal/Canary base fully reuses an unchanged cache"
		);
	}
}

int main(int argc, char** argv) {
	TestLuaDetection();
	TestXmlAndLinkedLuaDetection();
	TestAmbiguityAndDiagnostics();
	TestFingerprintsAndCache();
	TestFileLimit();

	if (argc == 4) {
		TestRealServers(std::filesystem::path(argv[1]), std::filesystem::path(argv[2]));
		TestRealCanary(std::filesystem::path(argv[3]));
	} else if (argc == 3) {
		TestRealServers(std::filesystem::path(argv[1]), std::filesystem::path(argv[2]));
	} else if (argc != 1) {
		std::cerr << "Usage: server_content_index_tests [modern-lua-server xml-mixed-server [canary-crystal-server]]\n";
		return 2;
	}

	if (failures == 0) {
		std::cout << checks << " server content index checks passed.\n";
	}
	return failures == 0 ? 0 : 1;
}
