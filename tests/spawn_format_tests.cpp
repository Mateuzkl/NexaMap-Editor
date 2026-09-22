#include "spawn_format.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {
	int failures = 0;
	int checks = 0;

	void check(bool condition, std::string_view test) {
		++checks;
		if (!condition) {
			std::cerr << "FAILED: " << test << '\n';
			++failures;
		}
	}

	void writeFile(const std::filesystem::path& path, std::string_view contents) {
		std::filesystem::create_directories(path.parent_path());
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
	}

	bool hasWarning(const SpawnDocument& document, std::string_view fragment) {
		for (const std::string& warning : document.warnings) {
			if (warning.find(fragment) != std::string::npos) {
				return true;
			}
		}
		return false;
	}

	struct TestDirectory {
		TestDirectory() {
			path = std::filesystem::temp_directory_path() / ("rme_spawn_format_tests_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(path);
		}

		~TestDirectory() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}

		std::filesystem::path path;
	};
}

int main(int argc, char** argv) {
	TestDirectory temporary;
	const SpawnLoadDefaults defaults { 75, 7 };
	SpawnDocument document;
	std::string error;

	const auto loadTfs = [&](std::string_view xml) {
		const std::filesystem::path file = temporary.path / "case-spawn.xml";
		writeFile(file, xml);
		error.clear();
		return SpawnFormatIO::LoadTfs(file, document, error, defaults);
	};
	const auto loadModern = [&](std::string_view monsters, std::string_view npcs) {
		const std::filesystem::path monsterFile = temporary.path / "case-monster.xml";
		const std::filesystem::path npcFile = temporary.path / "case-npc.xml";
		std::filesystem::remove(monsterFile);
		std::filesystem::remove(npcFile);
		if (!monsters.empty()) {
			writeFile(monsterFile, monsters);
		}
		if (!npcs.empty()) {
			writeFile(npcFile, npcs);
		}
		error.clear();
		return SpawnFormatIO::LoadCanaryCrystal(monsterFile, npcFile, document, error, defaults);
	};

	check(loadTfs("<spawns><spawn centerx='100' centery='200' centerz='7' radius='2'><monster name='Rat' x='1' y='-1' z='7' spawntime='30'/></spawn></spawns>") && document.monsterCount() == 1 && document.npcCount() == 0, "TFS monster-only file");
	check(loadTfs("<spawns><spawn centerx='100' centery='200' centerz='7' radius='2'><npc name='Guide' x='0' y='0' z='7' spawntime='30'/></spawn></spawns>") && document.monsterCount() == 0 && document.npcCount() == 1, "TFS NPC-only file");
	check(loadTfs("<spawns><spawn centerx='100' centery='200' centerz='7' radius='2'><monster name='Rat' x='0' y='0' spawntime='30'/><npc name='Guide' x='1' y='0' spawntime='40'/></spawn></spawns>") && document.entryCount() == 2, "TFS mixed monster and NPC file");
	check(document.areas.size() == 1 && document.areas.front().entries.size() == 2, "multiple creatures share one TFS area");
	check(loadTfs("<spawns><spawn centerx='10' centery='20' centerz='3' radius='1'><npc name='Guide' x='0' y='0' direction='2' spawntime='30'/></spawn></spawns>") && document.areas.front().entries.front().hasDirection && document.areas.front().entries.front().direction == 2, "explicit direction is preserved");
	check(loadTfs("<spawns><spawn centerx='10' centery='20' centerz='3' radius='1'><npc name='Guide' x='0' y='0' spawntime='30'/></spawn></spawns>") && !document.areas.front().entries.front().hasDirection && document.areas.front().entries.front().direction == 2, "missing TFS NPC direction uses South server default");
	check(loadTfs("<spawns><spawn centerx='10' centery='20' centerz='3' radius='1'><monster name='Rat' x='0' y='0'/></spawn></spawns>") && !document.areas.front().entries.front().hasDirection && document.areas.front().entries.front().direction == 0, "missing direction keeps North default");
	check(document.areas.front().entries.front().spawnTime == defaults.spawnTime && document.areas.front().entries.front().weight == defaults.monsterWeight, "TFS missing values use editor defaults");
	check(loadTfs("<spawns><spawn centerx='10' centery='20' centerz='3' radius='0'><monster name='Rat' x='0' y='0'/></spawn></spawns>") && document.areas.empty() && hasWarning(document, "between 1 and 255"), "TFS radius zero preserves legacy rejection");
	check(loadTfs("<spawns><spawn centerx='10' centery='20' centerz='2' radius='1'><monster name='Rat' x='0' y='0'/></spawn><spawn centerx='10' centery='20' centerz='8' radius='1'><monster name='Bat' x='0' y='0'/></spawn></spawns>") && document.areas.size() == 2 && document.areas[1].entries.front().z == 8, "multiple floors remain separate");
	check(loadTfs("<spawns><spawn centerx='100' centery='200' centerz='7' radius='2'><monster name='Rat' x='-2' y='2' z='5'/></spawn></spawns>") && document.areas.front().entries.front().x == 98 && document.areas.front().entries.front().y == 202 && document.areas.front().entries.front().z == 7 && hasWarning(document, "real server parser uses centerz"), "offsets are relative and child z is ignored");
	check(loadTfs("<?xml version='1.0' encoding='UTF-8'?><spawns><!--comment--><spawn centerx='1' centery='2' centerz='3' radius='1'><npc name='José &amp; Ana' x='0' y='0'/></spawn></spawns>") && document.areas.front().entries.front().name == "José & Ana", "UTF-8 comments and XML entities");
	check(loadTfs("<spawns><spawn centerx='1' centery='2' centerz='3' radius='1' custom='area'><monster name='Rat' x='0' y='0' custom='entry'/></spawn></spawns>") && document.areas.front().attributes.at("custom") == "area" && document.areas.front().entries.front().attributes.at("custom") == "entry", "extra attributes are retained");
	check(loadTfs("<spawns><spawn centerx='1' centery='2' centerz='3' radius='1'/></spawns>") && document.areas.size() == 1 && document.entryCount() == 0, "empty TFS area loads safely");
	check(!loadTfs("<spawns><spawn></spawns>") && document.areas.empty() && error.find(":1:") != std::string::npos, "malformed TFS XML fails atomically with line");
	check(!loadTfs("<monsters/>") && error.find("found <monsters>") != std::string::npos, "wrong TFS root reports actual root");
	check(loadTfs("<spawns><spawn centerx='bad' centery='2' centerz='3' radius='1'><monster name='Rat' x='0' y='0'/></spawn></spawns>") && document.areas.empty() && hasWarning(document, "non-numeric centerx"), "invalid center is diagnosed");
	check(loadTfs("<spawns><spawn centerx='1' centery='2' centerz='3' radius='1'><monster x='0' y='0'/></spawn></spawns>") && document.entryCount() == 0 && hasWarning(document, "without name"), "missing creature name is skipped");
	check(loadTfs("<spawns><spawn centerx='1' centery='2' centerz='3' radius='1'><monster name='Rat' x='bad' y='0'/></spawn></spawns>") && document.entryCount() == 0 && hasWarning(document, "non-numeric x or y"), "invalid offset is diagnosed");
	check(loadTfs("<spawns><spawn centerx='1' centery='2' centerz='3' radius='1'><npc name='Guide' x='0' y='0' direction='99'/></spawn></spawns>") && !document.areas.front().entries.front().hasDirection && document.areas.front().entries.front().direction == 2 && hasWarning(document, "invalid direction"), "invalid TFS NPC direction falls back to South");
	check(loadTfs("<spawns><spawn centerx='1' centery='2' centerz='3' radius='1'><monster name='Rat' x='0' y='0' spawntime='zero'/></spawn></spawns>") && document.areas.front().entries.front().spawnTime == defaults.spawnTime && hasWarning(document, "invalid spawntime"), "invalid spawntime uses configured default");
	check(loadTfs("<spawns><spawn centerx='10' centery='20' centerz='3' radius='1'><monster name='Rat' x='4' y='0'/></spawn></spawns>") && document.areas.front().radius == 4 && hasWarning(document, "outside the declared radius"), "out-of-radius entry expands only in-memory radius");
	check(loadTfs("<spawns><spawn centerx='10' centery='20' centerz='3' radius='1'><monsters x='0' y='0' spawntime='30'><monster name='Rat' chance='60'/><monster name='Bat' chance='40'/></monsters></spawn></spawns>") && document.monsterCount() == 2 && document.areas.front().entries.front().alternatives.size() == 2, "TFS monster alternatives remain grouped");
	check(loadTfs("<spawns><spawn centerx='65535' centery='65535' centerz='3' radius='1'><monster name='Rat' x='2147483647' y='2147483647'/></spawn></spawns>") && document.entryCount() == 0 && hasWarning(document, "outside the server coordinate range"), "coordinate addition is range-checked without signed overflow");
	const bool collocatedGroupsLoaded = loadTfs("<spawns><spawn centerx='10' centery='20' centerz='3' radius='1'><monsters x='0' y='0' spawntime='30'><monster name='Rat' chance='60'/><monster name='Bat' chance='40'/></monsters><monsters x='0' y='0' spawntime='30'><monster name='Wolf' chance='70'/><monster name='Bear' chance='30'/></monsters></spawn></spawns>") && document.areas.front().entries.size() == 2;
	check(collocatedGroupsLoaded, "collocated TFS chance groups load independently");
	SpawnAlternativeKind forwardKind = SpawnAlternativeKind::None;
	SpawnAlternativeKind reverseKind = SpawnAlternativeKind::None;
	const bool forwardKindsMerged = collocatedGroupsLoaded && MergeSpawnAlternativeKinds(document.areas.front().entries[0].alternativeKind, document.areas.front().entries[1].alternativeKind, forwardKind);
	const bool reverseKindsMerged = collocatedGroupsLoaded && MergeSpawnAlternativeKinds(document.areas.front().entries[1].alternativeKind, document.areas.front().entries[0].alternativeKind, reverseKind);
	check(forwardKindsMerged && reverseKindsMerged && forwardKind == SpawnAlternativeKind::TfsChance && reverseKind == SpawnAlternativeKind::TfsChance, "collocated TFS chance semantics are input-order independent");
	SpawnAlternativeKind incompatibleKind = SpawnAlternativeKind::None;
	check(!MergeSpawnAlternativeKinds(SpawnAlternativeKind::CanaryWeight, SpawnAlternativeKind::TfsChance, incompatibleKind), "incompatible collocated alternative kinds are rejected");
	SpawnAlternativeKind canaryMergeKind = SpawnAlternativeKind::None;
	check(MergeSpawnAlternativeKinds(SpawnAlternativeKind::CanaryWeight, SpawnAlternativeKind::CanaryWeight, canaryMergeKind) && canaryMergeKind == SpawnAlternativeKind::CanaryWeight, "CanaryWeight merges with CanaryWeight");
	canaryMergeKind = SpawnAlternativeKind::None;
	check(MergeSpawnAlternativeKinds(SpawnAlternativeKind::None, SpawnAlternativeKind::CanaryWeight, canaryMergeKind) && canaryMergeKind == SpawnAlternativeKind::CanaryWeight, "None merges with incoming CanaryWeight");
	canaryMergeKind = SpawnAlternativeKind::None;
	check(MergeSpawnAlternativeKinds(SpawnAlternativeKind::CanaryWeight, SpawnAlternativeKind::None, canaryMergeKind) && canaryMergeKind == SpawnAlternativeKind::CanaryWeight, "existing CanaryWeight merges with incoming None");

	check(AuthoritativeAlternativeKind(SpawnFormat::CanaryCrystal, SpawnAlternativeKind::None, true, 1) == SpawnAlternativeKind::CanaryWeight, "Canary with weight normalizes to CanaryWeight");
	check(AuthoritativeAlternativeKind(SpawnFormat::CanaryCrystal, SpawnAlternativeKind::None, false, 2) == SpawnAlternativeKind::CanaryWeight, "Canary with >1 variants normalizes to CanaryWeight");
	check(AuthoritativeAlternativeKind(SpawnFormat::CanaryCrystal, SpawnAlternativeKind::None, false, 1) == SpawnAlternativeKind::None, "Canary with 1 variant without weight normalizes to None");
	check(AuthoritativeAlternativeKind(SpawnFormat::CanaryCrystal, SpawnAlternativeKind::CanaryWeight, false, 1) == SpawnAlternativeKind::CanaryWeight, "Canary with existing CanaryWeight is preserved");
	check(AuthoritativeAlternativeKind(SpawnFormat::Tfs, SpawnAlternativeKind::TfsChance, false, 2) == SpawnAlternativeKind::TfsChance, "TFS TfsChance is preserved");

	const std::string modernMonsters = "<monsters><monster centerx='100' centery='200' centerz='7' radius='2'><monster name='Rat' x='1' y='-1' z='7' spawntime='30' weight='2'/></monster></monsters>";
	const std::string modernNpcs = "<npcs><npc centerx='110' centery='210' centerz='6' radius='1'><npc name='Guide' x='0' y='0' z='6' spawntime='60'/></npc></npcs>";
	check(loadModern(modernMonsters, modernNpcs) && document.monsterCount() == 1 && document.npcCount() == 1 && document.areas.size() == 2, "Canary monster and NPC files load together");
	check(document.areas.front().entries.front().alternativeKind == SpawnAlternativeKind::CanaryWeight, "Canary entry with weight attribute has CanaryWeight alternative kind");

	const std::string collocatedModern = "<monsters><monster centerx='100' centery='200' centerz='7' radius='2'>"
										 "<monster name='Demon' x='1' y='-1' z='7' spawntime='60' weight='10'/>"
										 "<monster name='Dragon' x='1' y='-1' z='7' spawntime='60' weight='20' direction='0'/>"
										 "</monster></monsters>";
	check(loadModern(collocatedModern, {}) && document.monsterCount() == 2 && document.areas.front().entries.size() == 2, "collocated Canary monsters load independently");
	check(document.areas.front().entries[0].alternativeKind == SpawnAlternativeKind::CanaryWeight && document.areas.front().entries[1].alternativeKind == SpawnAlternativeKind::CanaryWeight, "collocated Canary monsters retain CanaryWeight");
	check(!document.areas.front().entries[0].hasDirection && document.areas.front().entries[1].hasDirection && document.areas.front().entries[1].direction == 0, "collocated Canary entries preserve implicit and explicit direction state");

	// Case A: explicit direction then omitted direction round-trip
	const std::string caseAMonsters = "<monsters><monster centerx='100' centery='200' centerz='7' radius='2'>"
									  "<monster name='Demon' x='1' y='-1' z='7' spawntime='60' weight='10' direction='2'/>"
									  "<monster name='Dragon' x='1' y='-1' z='7' spawntime='60' weight='20'/>"
									  "</monster></monsters>";
	check(loadModern(caseAMonsters, {}), "load Case A collocated monsters");
	SpawnWriteResult caseAResult = SpawnFormatIO::SaveCanaryCrystal(document, temporary.path / "caseA-monster.xml", temporary.path / "caseA-npc.xml");
	check(caseAResult.success && SpawnFormatIO::LoadCanaryCrystal(temporary.path / "caseA-monster.xml", temporary.path / "caseA-npc.xml", document, error, defaults), "save/reload Case A collocated monsters");
	check(document.areas.front().entries[0].hasDirection && document.areas.front().entries[0].direction == 2, "Case A: first entry South direction preserved");
	check(!document.areas.front().entries[1].hasDirection && document.areas.front().entries[1].direction == 0, "Case A: second entry omitted direction preserved as North default");

	// Case B: omitted direction then explicit direction round-trip
	const std::string caseBMonsters = "<monsters><monster centerx='100' centery='200' centerz='7' radius='2'>"
									  "<monster name='Demon' x='1' y='-1' z='7' spawntime='60' weight='10'/>"
									  "<monster name='Dragon' x='1' y='-1' z='7' spawntime='60' weight='20' direction='2'/>"
									  "</monster></monsters>";
	check(loadModern(caseBMonsters, {}), "load Case B collocated monsters");
	SpawnWriteResult caseBResult = SpawnFormatIO::SaveCanaryCrystal(document, temporary.path / "caseB-monster.xml", temporary.path / "caseB-npc.xml");
	check(caseBResult.success && SpawnFormatIO::LoadCanaryCrystal(temporary.path / "caseB-monster.xml", temporary.path / "caseB-npc.xml", document, error, defaults), "save/reload Case B collocated monsters");
	check(!document.areas.front().entries[0].hasDirection && document.areas.front().entries[0].direction == 0, "Case B: first entry omitted direction preserved as North default");
	check(document.areas.front().entries[1].hasDirection && document.areas.front().entries[1].direction == 2, "Case B: second entry South direction preserved");

	// Case C: distinct explicit directions
	const std::string caseCMonsters = "<monsters><monster centerx='100' centery='200' centerz='7' radius='2'>"
									  "<monster name='Demon' x='1' y='-1' z='7' spawntime='60' weight='10' direction='1'/>"
									  "<monster name='Dragon' x='1' y='-1' z='7' spawntime='60' weight='20' direction='3'/>"
									  "</monster></monsters>";
	check(loadModern(caseCMonsters, {}), "load Case C distinct explicit directions");
	SpawnWriteResult caseCResult = SpawnFormatIO::SaveCanaryCrystal(document, temporary.path / "caseC-monster.xml", temporary.path / "caseC-npc.xml");
	check(caseCResult.success && SpawnFormatIO::LoadCanaryCrystal(temporary.path / "caseC-monster.xml", temporary.path / "caseC-npc.xml", document, error, defaults), "save/reload Case C collocated monsters");
	check(document.areas.front().entries[0].direction == 1 && document.areas.front().entries[1].direction == 3, "Case C: distinct explicit directions preserved");

	// Case D: distinct spawntimes
	const std::string caseDMonsters = "<monsters><monster centerx='100' centery='200' centerz='7' radius='2'>"
									  "<monster name='Demon' x='1' y='-1' z='7' spawntime='30' weight='10'/>"
									  "<monster name='Dragon' x='1' y='-1' z='7' spawntime='120' weight='20'/>"
									  "</monster></monsters>";
	check(loadModern(caseDMonsters, {}), "load Case D distinct spawntimes");
	SpawnWriteResult caseDResult = SpawnFormatIO::SaveCanaryCrystal(document, temporary.path / "caseD-monster.xml", temporary.path / "caseD-npc.xml");
	check(caseDResult.success && SpawnFormatIO::LoadCanaryCrystal(temporary.path / "caseD-monster.xml", temporary.path / "caseD-npc.xml", document, error, defaults), "save/reload Case D collocated monsters");
	check(document.areas.front().entries[0].spawnTime == 30 && document.areas.front().entries[1].spawnTime == 120, "Case D: distinct spawntimes preserved");
	check(loadModern(modernMonsters, {}) && document.monsterCount() == 1 && document.npcCount() == 0 && hasWarning(document, "NPC spawn file was not found"), "Canary monster-only load is nonfatal");
	check(loadModern({}, modernNpcs) && document.monsterCount() == 0 && document.npcCount() == 1 && hasWarning(document, "monster spawn file was not found"), "Canary NPC-only load is nonfatal");
	check(!document.areas.front().entries.front().hasDirection && document.areas.front().entries.front().direction == 0, "missing Canary NPC direction uses North server default");
	check(loadModern("<monsters><monster centerx='1' centery='2' centerz='3' radius='0'><monster name='Rat' x='0' y='0' spawntime='30'/></monster></monsters>", {}) && document.areas.front().radius == 0, "Canary radius zero is accepted");
	check(loadModern("<monsters><monster centerx='1' centery='2' centerz='3'><monster name='Rat' x='2' y='0' spawntime='30'/></monster></monsters>", {}) && document.areas.front().radius == 2 && hasWarning(document, "smallest radius"), "missing Canary radius becomes smallest containing radius");
	check(!loadModern("<spawns/>", modernNpcs) && document.areas.empty() && error.find("expected root <monsters>") != std::string::npos, "wrong Canary root fails atomically");
	check(!loadModern(modernMonsters, "<npcs><npc></npcs>") && document.areas.empty() && error.find(":1:") != std::string::npos, "malformed second Canary file leaves no partial document");

	check(loadTfs("<spawns><spawn centerx='10' centery='20' centerz='3' radius='1'><npc name='Guide' x='0' y='0' spawntime='30'/></spawn></spawns>"), "prepare TFS NPC direction conversion");
	SpawnWriteResult writeResult = SpawnFormatIO::SaveCanaryCrystal(document, temporary.path / "direction-monster.xml", temporary.path / "direction-npc.xml");
	check(writeResult.success && SpawnFormatIO::LoadCanaryCrystal(temporary.path / "direction-monster.xml", temporary.path / "direction-npc.xml", document, error, defaults) && document.areas.front().entries.front().hasDirection && document.areas.front().entries.front().direction == 2, "TFS South default becomes explicit in Canary XML");
	check(loadModern({}, modernNpcs), "prepare Canary NPC direction conversion");
	writeResult = SpawnFormatIO::SaveTfs(document, temporary.path / "direction-spawn.xml");
	check(writeResult.success && SpawnFormatIO::LoadTfs(temporary.path / "direction-spawn.xml", document, error, defaults) && document.areas.front().entries.front().hasDirection && document.areas.front().entries.front().direction == 0, "Canary North default becomes explicit in TFS XML");

	const std::filesystem::path detectExplicit = temporary.path / "detect-explicit";
	writeFile(detectExplicit / "odd-name.xml", modernMonsters);
	SpawnDetectionResult detection = SpawnFormatIO::Detect(detectExplicit, "odd-name.xml", {}, "sample");
	check(detection.format == SpawnFormat::CanaryCrystal && detection.primaryFile.filename() == "odd-name.xml", "explicit reference is classified by root, not filename");

	const std::filesystem::path detectNpcOnly = temporary.path / "detect-npc-only";
	writeFile(detectNpcOnly / "sample-npc.xml", modernNpcs);
	detection = SpawnFormatIO::Detect(detectNpcOnly, {}, {}, "sample");
	check(detection.format == SpawnFormat::CanaryCrystal && detection.npcFile.filename() == "sample-npc.xml" && detection.primaryFile.filename() == "sample-monster.xml", "fallback detects NPC-only Canary layout and expected companion");

	const std::filesystem::path detectConflict = temporary.path / "detect-conflict";
	writeFile(detectConflict / "sample-spawn.xml", "<spawns/>");
	writeFile(detectConflict / "sample-monster.xml", modernMonsters);
	writeFile(detectConflict / "sample-npc.xml", modernNpcs);
	detection = SpawnFormatIO::Detect(detectConflict, {}, {}, "sample");
	check(detection.conflict && detection.format == SpawnFormat::Tfs && detection.alternateFormat == SpawnFormat::CanaryCrystal, "unreferenced TFS and Canary files produce explicit conflict");

	detection = SpawnFormatIO::Detect(detectConflict, (detectConflict / "sample-spawn.xml").string(), {}, "sample");
	check(!detection.conflict && detection.format == SpawnFormat::Tfs, "absolute OTBM reference wins deterministic conflict resolution");

	const std::filesystem::path detectFamilies = temporary.path / "detect-families";
	writeFile(detectFamilies / "sample-monster.xml", modernMonsters);
	writeFile(detectFamilies / "world-npc.xml", modernNpcs);
	detection = SpawnFormatIO::Detect(detectFamilies, {}, {}, "sample");
	check(detection.format == SpawnFormat::CanaryCrystal && detection.primaryFile.filename() == "sample-monster.xml" && detection.npcFile.filename() == "sample-npc.xml", "fallback never mixes Canary filename families");

	if (argc == 4) {
		const auto started = std::chrono::steady_clock::now();
		check(SpawnFormatIO::LoadTfs(argv[1], document, error, defaults) && document.areas.size() == 573 && document.monsterCount() == 567 && document.npcCount() == 24, "real TFS world-spawn.xml counts");
		const auto tfsDone = std::chrono::steady_clock::now();
		check(SpawnFormatIO::LoadCanaryCrystal(argv[2], argv[3], document, error, defaults) && document.areas.size() == 54786 && document.monsterCount() == 87287 && document.npcCount() == 1046, "real Crystal world monster/NPC counts");
		const auto crystalDone = std::chrono::steady_clock::now();
		std::cout << "Real TFS read: " << std::chrono::duration_cast<std::chrono::milliseconds>(tfsDone - started).count() << " ms\n";
		std::cout << "Real Crystal read: " << std::chrono::duration_cast<std::chrono::milliseconds>(crystalDone - tfsDone).count() << " ms\n";
	}

	if (failures != 0) {
		std::cerr << failures << " of " << checks << " spawn format test(s) failed.\n";
		return 1;
	}
	std::cout << checks << " spawn format tests passed.\n";
	return 0;
}
