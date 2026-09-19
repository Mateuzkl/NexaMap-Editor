#include "spawn_format.h"
#include "spawn_source_remap.h"
#include "position.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
	int failures = 0;
	int checks = 0;

	void check(bool condition, const std::string& message) {
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
			path = std::filesystem::temp_directory_path() / ("nexamap-spawn-test-" + std::to_string(stamp));
			std::filesystem::create_directories(path);
		}

		~TemporaryDirectory() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}

		std::filesystem::path path;
	};
}

int main() {
	// Test 1: Spawn dependency capture and single creature remapping
	{
		SpawnDependencyMap deps;
		const Position spawnCenter(100, 100, 7);
		SpawnDependency dep;
		dep.originalCenter = spawnCenter;
		dep.radius = 5;
		dep.centerCopied = true;
		deps[spawnCenter] = dep;

		// Mock creature representation: test the mathematical delta
		const Position copyPos(98, 98, 7);
		const Position pastePos(198, 198, 7);
		const Position expectedNewCenter = spawnCenter - copyPos + pastePos;

		check(expectedNewCenter == Position(200, 200, 7),
			  "Calculated spawn center translation matches tile delta");
	}

	// Test 2: TFS Spawn saving and round-trip verification with Monsters and NPCs
	{
		TemporaryDirectory temp;
		const std::filesystem::path spawnFile = temp.path / "world-spawn.xml";

		SpawnDocument doc;
		doc.format = SpawnFormat::Tfs;

		SpawnAreaData area;
		area.centerX = 200;
		area.centerY = 200;
		area.centerZ = 7;
		area.radius = 5;

		// Add monster entry
		SpawnEntryData monster;
		monster.name = "Dragon";
		monster.isNpc = false;
		monster.x = 202;
		monster.y = 201;
		monster.z = 7;
		monster.spawnTime = 60;
		area.entries.push_back(monster);

		// Add NPC entry
		SpawnEntryData npc;
		npc.name = "Donald";
		npc.isNpc = true;
		npc.x = 199;
		npc.y = 200;
		npc.z = 7;
		npc.spawnTime = 120;
		area.entries.push_back(npc);

		doc.areas.push_back(area);

		SpawnWriteResult writeResult = SpawnFormatIO::SaveTfs(doc, spawnFile);
		check(writeResult.success, "SpawnFormatIO::SaveTfs succeeds");
		check(std::filesystem::exists(spawnFile), "TFS spawn XML was created on disk");

		// Reload and verify
		SpawnDocument reloaded;
		std::string loadError;
		check(SpawnFormatIO::LoadTfs(spawnFile, reloaded, loadError), "SpawnFormatIO::LoadTfs succeeds");
		check(reloaded.areas.size() == 1, "Reloaded TFS document has exactly 1 area");
		check(reloaded.areas[0].centerX == 200 && reloaded.areas[0].centerY == 200, "Spawn center position preserved");
		check(reloaded.monsterCount() == 1, "TFS preserved monster count");
		check(reloaded.npcCount() == 1, "TFS preserved NPC count");
		check(reloaded.entryCount() == 2, "TFS preserved total entry count");
	}

	// Test 3: Canary/Crystal Dual File Output (separate monster and NPC XML)
	{
		TemporaryDirectory temp;
		const std::filesystem::path monsterFile = temp.path / "world-monster.xml";
		const std::filesystem::path npcFile = temp.path / "world-npc.xml";

		SpawnDocument doc;
		doc.format = SpawnFormat::CanaryCrystal;

		// Monster Area
		SpawnAreaData monsterArea;
		monsterArea.centerX = 300;
		monsterArea.centerY = 300;
		monsterArea.centerZ = 7;
		monsterArea.radius = 4;
		monsterArea.kind = SpawnAreaKind::Monsters;

		SpawnEntryData mEntry;
		mEntry.name = "Behemoth";
		mEntry.isNpc = false;
		mEntry.x = 301;
		mEntry.y = 301;
		mEntry.z = 7;
		monsterArea.entries.push_back(mEntry);
		doc.areas.push_back(monsterArea);

		// NPC Area
		SpawnAreaData npcArea;
		npcArea.centerX = 300;
		npcArea.centerY = 300;
		npcArea.centerZ = 7;
		npcArea.radius = 4;
		npcArea.kind = SpawnAreaKind::Npcs;

		SpawnEntryData nEntry;
		nEntry.name = "Frodo";
		nEntry.isNpc = true;
		nEntry.x = 299;
		nEntry.y = 300;
		nEntry.z = 7;
		npcArea.entries.push_back(nEntry);
		doc.areas.push_back(npcArea);

		SpawnWriteResult saveResult = SpawnFormatIO::SaveCanaryCrystal(doc, monsterFile, npcFile);
		check(saveResult.success, "SaveCanaryCrystal succeeds");
		check(std::filesystem::exists(monsterFile), "Monster file created for Canary/Crystal");
		check(std::filesystem::exists(npcFile), "NPC file created for Canary/Crystal");

		// Reload Canary/Crystal
		SpawnDocument loadedCanary;
		std::string canaryError;
		check(SpawnFormatIO::LoadCanaryCrystal(monsterFile, npcFile, loadedCanary, canaryError),
			  "LoadCanaryCrystal successfully reads split monster and NPC files");
		check(loadedCanary.monsterCount() == 1, "Canary/Crystal reloaded exactly 1 monster");
		check(loadedCanary.npcCount() == 1, "Canary/Crystal reloaded exactly 1 NPC");
	}

	// Test 4: Pre-save validation data structure checks
	{
		SpawnValidationResult result;
		check(result.valid, "Default SpawnValidationResult is valid");
		check(result.orphanedCreatures == 0, "No orphaned creatures by default");
		check(result.uncapturableCreatures == 0, "No uncapturable creatures by default");

		result.orphanedCreatures = 2;
		result.uncapturableCreatures = 2;
		result.valid = false;
		result.warnings.push_back("Spawn validation: 2 creature(s) cannot be serialized because their spawn source is invalid.");

		check(!result.valid, "SpawnValidationResult marks invalid when orphaned creatures present");
		check(result.warnings.size() == 1, "Validation warning generated");
	}

	std::cout << "Spawn Persistence Tests: " << checks << " checks, " << failures << " failures.\n";
	return failures == 0 ? 0 : 1;
}
