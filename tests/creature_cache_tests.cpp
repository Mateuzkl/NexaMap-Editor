#include "main.h"

#include "creature_cache.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>

#include "gui.h"

namespace {
	std::string normalizeCreatureName(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return value;
	}
}

// Stubs for GUI and CreatureDatabase methods needed by creature_cache in test build
wxString GUI::GetLocalDataDirectory() {
	return wxString(std::filesystem::temp_directory_path().wstring());
}

CreatureDatabase g_creatures;

CreatureType::CreatureType() :
	isNpc(false),
	missing(false),
	in_other_tileset(false),
	standard(false),
	name(""),
	brush(nullptr) { }

CreatureType::CreatureType(const CreatureType& ct) = default;
CreatureType& CreatureType::operator=(const CreatureType& ct) = default;
CreatureType::~CreatureType() = default;

CreatureDatabase::CreatureDatabase() = default;
CreatureDatabase::~CreatureDatabase() {
	for (auto& pair : creature_map) {
		delete pair.second;
	}
}

CreatureType* CreatureDatabase::operator[](const std::string& name) {
	auto it = creature_map.find(normalizeCreatureName(name));
	return it != creature_map.end() ? it->second : nullptr;
}

CreatureType* CreatureDatabase::addCreatureType(const std::string& name, bool isNpc, const Outfit& outfit) {
	const std::string normalizedName = normalizeCreatureName(name);
	auto it = creature_map.find(normalizedName);
	if (it != creature_map.end()) {
		delete it->second;
	}
	auto* ct = new CreatureType();
	ct->name = name;
	ct->isNpc = isNpc;
	ct->outfit = outfit;
	creature_map[normalizedName] = ct;
	return ct;
}

void CreatureDatabase::applyWorkspaceCreature(std::unique_ptr<CreatureType> creatureType, bool standard) {
	if (!creatureType) {
		return;
	}
	const std::string normalizedName = normalizeCreatureName(creatureType->name);
	auto iter = creature_map.find(normalizedName);
	if (iter == creature_map.end()) {
		creatureType->standard = standard;
		creature_map[normalizedName] = creatureType.release();
		return;
	}
	CreatureType* current = iter->second;
	*current = *creatureType;
	current->standard = standard;
}

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
			path = std::filesystem::temp_directory_path() / ("nexamap-cache-test-" + std::to_string(stamp));
			std::filesystem::create_directories(path);
		}

		~TemporaryDirectory() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}

		void write(const std::filesystem::path& relative, const std::string& contents = "print('monster')") const {
			const std::filesystem::path target = path / relative;
			std::filesystem::create_directories(target.parent_path());
			std::ofstream stream(target, std::ios::binary);
			stream << contents;
		}

		std::filesystem::path path;
	};
}

int main() {
	// Test 1: Workspace key hashing stability and differentiation
	{
		CreatureCache::WorkspaceKey key1;
		key1.serverRoot = "C:/Server/ot";
		key1.monstersDir = "C:/Server/ot/data/monster";
		key1.npcsDir = "C:/Server/ot/data/npc";
		key1.clientProfile = "13.40";

		CreatureCache::WorkspaceKey key2 = key1;
		check(key1.computeHash() == key2.computeHash(), "WorkspaceKey hash is deterministic for identical keys");

		key2.serverRoot = "C:/Server/other_ot";
		check(key1.computeHash() != key2.computeHash(), "WorkspaceKey hash changes when serverRoot changes");

		key2 = key1;
		key2.monstersDir = "C:/Server/ot/data/monster_custom";
		check(key1.computeHash() != key2.computeHash(), "WorkspaceKey hash changes when monstersDir changes");

		key2 = key1;
		key2.npcsDir = "C:/Server/ot/data/npc_custom";
		check(key1.computeHash() != key2.computeHash(), "WorkspaceKey hash changes when npcsDir changes");

		key2 = key1;
		key2.clientProfile = "12.86";
		check(key1.computeHash() != key2.computeHash(), "WorkspaceKey hash changes when profile changes");
	}

	// Test 2: ValidateManifest on non-existent cache directory
	{
		TemporaryDirectory temp;
		const auto cacheDir = temp.path / "cache";
		const auto luaDir = temp.path / "monsters";
		temp.write("monsters/demon.lua", "monster data");

		check(!CreatureCache::ValidateManifest(cacheDir, luaDir, "monsters"), "ValidateManifest returns false when cache directory does not exist");
	}

	// Test 3: ValidateManifest with valid manifest and XML
	{
		TemporaryDirectory temp;
		const auto cacheDir = temp.path / "cache";
		const auto luaDir = temp.path / "monsters";
		temp.write("monsters/demon.lua", "monster data");
		temp.write("monsters/dragon.lua", "dragon data");

		std::filesystem::create_directories(cacheDir);

		// Write dummy cache XML
		temp.write("cache/monsters.xml", "<creatures></creatures>");

		// Write valid manifest matching files
		nlohmann::json manifest;
		manifest["schemaVersion"] = CreatureCache::SchemaVersion;
		manifest["kind"] = "monsters";
		manifest["luaDirectory"] = luaDir.generic_string();

		nlohmann::json files = nlohmann::json::array();
		for (const auto& entry : std::filesystem::directory_iterator(luaDir)) {
			nlohmann::json f;
			f["path"] = entry.path().filename().generic_string();
			f["size"] = entry.file_size();
			f["modifiedAt"] = static_cast<int64_t>(entry.last_write_time().time_since_epoch().count());
			files.push_back(f);
		}
		std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
			return a["path"] < b["path"];
		});
		manifest["files"] = files;

		std::ofstream mOut(cacheDir / "manifest_monsters.json");
		mOut << manifest.dump(2);
		mOut.close();

		check(CreatureCache::ValidateManifest(cacheDir, luaDir, "monsters"), "ValidateManifest returns true when files and manifest match");

		// Test 4: Invalidation on file addition
		temp.write("monsters/rat.lua", "rat data");
		check(!CreatureCache::ValidateManifest(cacheDir, luaDir, "monsters"), "ValidateManifest returns false when a new file is added");

		// Remove the added file
		std::filesystem::remove(luaDir / "rat.lua");
		check(CreatureCache::ValidateManifest(cacheDir, luaDir, "monsters"), "ValidateManifest returns true again after added file is removed");

		// Test 5: Invalidation on same-size sub-second file edit (no sleep needed)
		{
			const auto demonPath = luaDir / "demon.lua";
			const auto oldFtime = std::filesystem::last_write_time(demonPath);
			// Write same-size content ("monster date" is 12 bytes, same as "monster data")
			temp.write("monsters/demon.lua", "monster date");
			auto newFtime = std::filesystem::last_write_time(demonPath);
			if (newFtime == oldFtime) {
				std::filesystem::last_write_time(demonPath, oldFtime + std::filesystem::file_time_type::duration(100));
			}
			check(!CreatureCache::ValidateManifest(cacheDir, luaDir, "monsters"), "ValidateManifest returns false when a file is modified within the same second with identical size");
		}

		// Test 6: Invalidation on file deletion
		std::filesystem::remove(luaDir / "dragon.lua");
		check(!CreatureCache::ValidateManifest(cacheDir, luaDir, "monsters"), "ValidateManifest returns false when a file is deleted");
	}

	// Test 7: Schema version mismatch invalidation
	{
		TemporaryDirectory temp;
		const auto cacheDir = temp.path / "cache";
		const auto luaDir = temp.path / "monsters";
		temp.write("monsters/demon.lua", "monster data");
		temp.write("cache/monsters.xml", "<creatures></creatures>");

		nlohmann::json manifest;
		manifest["schemaVersion"] = CreatureCache::SchemaVersion + 999;
		manifest["kind"] = "monsters";
		manifest["luaDirectory"] = luaDir.generic_string();
		manifest["files"] = nlohmann::json::array();

		std::filesystem::create_directories(cacheDir);
		std::ofstream mOut(cacheDir / "manifest_monsters.json");
		mOut << manifest.dump(2);
		mOut.close();

		check(!CreatureCache::ValidateManifest(cacheDir, luaDir, "monsters"), "ValidateManifest returns false when schema version does not match");
	}

	// Test 8: Corrupted manifest JSON handling
	{
		TemporaryDirectory temp;
		const auto cacheDir = temp.path / "cache";
		const auto luaDir = temp.path / "monsters";
		temp.write("monsters/demon.lua", "monster data");
		temp.write("cache/monsters.xml", "<creatures></creatures>");
		temp.write("cache/manifest_monsters.json", "{ broken json content !! ");

		check(!CreatureCache::ValidateManifest(cacheDir, luaDir, "monsters"), "ValidateManifest returns false when manifest file is corrupted");
	}

	// Test 9: SafeReplaceFile replaces existing file cleanly
	{
		TemporaryDirectory temp;
		const auto fileA = temp.path / "target.txt";
		const auto fileB = temp.path / "source.txt";
		temp.write("target.txt", "Initial content");
		temp.write("source.txt", "Updated content");

		std::string err;
		check(CreatureCache::SafeReplaceFile(fileB, fileA, err), "SafeReplaceFile successfully replaces existing file");
		check(!std::filesystem::exists(fileB), "SafeReplaceFile removes source after replace");
		std::ifstream reader(fileA);
		std::string content;
		std::getline(reader, content);
		check(content == "Updated content", "SafeReplaceFile content matches source");
	}

	// Test 9b: replacement failure preserves the last valid destination
	{
		TemporaryDirectory temp;
		const auto target = temp.path / "target.txt";
		const auto missingSource = temp.path / "missing.txt";
		temp.write("target.txt", "Last valid cache");

		std::string err;
		check(!CreatureCache::SafeReplaceFile(missingSource, target, err), "SafeReplaceFile reports replacement failure");
		std::ifstream reader(target);
		std::string content;
		std::getline(reader, content);
		check(content == "Last valid cache", "SafeReplaceFile preserves the existing destination after failure");
	}

	// Test 10: Cache equivalence and override semantics
	{
		TemporaryDirectory temp;
		const auto cacheDir = temp.path / "cache";
		const auto luaDir = temp.path / "monsters";
		temp.write("monsters/amazon.lua", "amazon");
		temp.write("monsters/behemoth.lua", "behemoth");

		CreatureDatabase db;
		// Bundled creature: Amazon with looktype 130
		Outfit bundledOutfit;
		bundledOutfit.lookType = 130;
		CreatureType* bundledAmazon = db.addCreatureType("Amazon", false, bundledOutfit);
		bundledAmazon->standard = true;

		// Bundled creature: Demon with looktype 35
		Outfit bundledDemonOutfit;
		bundledDemonOutfit.lookType = 35;
		CreatureType* bundledDemon = db.addCreatureType("Demon", false, bundledDemonOutfit);
		bundledDemon->standard = true;

		// Simulated server import: Amazon with looktype 500 (overriding bundled)
		// and Behemoth with looktype 55
		std::vector<CreatureDatabase::ImportedCreatureRecord> serverImported;
		{
			Outfit serverAmazon;
			serverAmazon.lookType = 500;
			serverImported.push_back({ "amazon", "Amazon", false, serverAmazon });

			Outfit serverBehemoth;
			serverBehemoth.lookType = 55;
			serverImported.push_back({ "behemoth", "Behemoth", false, serverBehemoth });
		}

		// Apply server import to db (Run A: Full import)
		for (const auto& rec : serverImported) {
			auto ct = std::make_unique<CreatureType>();
			ct->name = rec.name;
			ct->isNpc = rec.isNpc;
			ct->outfit = rec.outfit;
			db.applyWorkspaceCreature(std::move(ct), true);
		}

		check(db["Amazon"] != nullptr && db["Amazon"]->outfit.lookType == 500, "Run A: Server Amazon overrides bundled Amazon (lookType 500)");
		check(db["Demon"] != nullptr && db["Demon"]->outfit.lookType == 35, "Run A: Bundled Demon preserved");
		check(db["Behemoth"] != nullptr && db["Behemoth"]->outfit.lookType == 55, "Run A: Server Behemoth inserted");

		// Save serverImported to cache (only server workspace creatures, NOT Demon)
		check(CreatureCache::SaveCache(cacheDir, luaDir, "monsters", serverImported), "SaveCache succeeds with imported records");

		// Inspect cache XML: Demon must NOT be in cache XML
		{
			std::ifstream xmlIn(cacheDir / "monsters.xml");
			std::string xmlStr((std::istreambuf_iterator<char>(xmlIn)), std::istreambuf_iterator<char>());
			check(xmlStr.find("name=\"Demon\"") == std::string::npos, "Cache XML does NOT contain bundled Demon");
			check(xmlStr.find("name=\"Amazon\"") != std::string::npos, "Cache XML contains server Amazon");
			check(xmlStr.find("name=\"Behemoth\"") != std::string::npos, "Cache XML contains server Behemoth");
		}

		// Run B: Reset database, load same bundled creatures, then LoadCached
		CreatureDatabase db2;
		CreatureType* bAmazon2 = db2.addCreatureType("Amazon", false, bundledOutfit);
		bAmazon2->standard = true;
		CreatureType* bDemon2 = db2.addCreatureType("Demon", false, bundledDemonOutfit);
		bDemon2->standard = true;

		size_t loadedCount = 0;
		check(CreatureCache::LoadCached(db2, cacheDir, luaDir, "monsters", &loadedCount), "LoadCached succeeds");
		check(loadedCount == 2, "LoadCached loaded exactly 2 server creatures");

		// Equivalence asserts: Run A == Run B
		check(db2["Amazon"] != nullptr && db2["Amazon"]->outfit.lookType == 500, "Run B: Cache hit server Amazon overrides bundled Amazon with lookType 500");
		check(db2["Demon"] != nullptr && db2["Demon"]->outfit.lookType == 35, "Run B: Cache hit preserves bundled Demon");
		check(db2["Behemoth"] != nullptr && db2["Behemoth"]->outfit.lookType == 55, "Run B: Cache hit inserts server Behemoth");
		check(db2["Amazon"]->standard == true && db2["Behemoth"]->standard == true, "Run B: Standard flags match");
	}

	// Test 11: Malformed JSON manifest robustness (Issue 1)
	{
		TemporaryDirectory temp;
		const auto cacheDir = temp.path / "cache";
		const auto luaDir = temp.path / "monsters";
		temp.write("monsters/orc.lua", "orc");

		// Case A: string schemaVersion
		{
			std::filesystem::create_directories(cacheDir);
			std::ofstream xml(cacheDir / "monsters.xml");
			xml << "<creatures><creature name=\"Orc\"/></creatures>";
			std::ofstream manifest(cacheDir / "manifest_monsters.json");
			manifest << "{\"schemaVersion\": \"invalid_string\", \"files\": []}";
			manifest.close();

			check(!CreatureCache::ValidateManifest(cacheDir, luaDir, "monsters"), "ValidateManifest returns false on string schemaVersion without throwing");
		}

		// Case B: files is not an array
		{
			std::ofstream manifest(cacheDir / "manifest_monsters.json");
			manifest << "{\"schemaVersion\": 2, \"files\": \"not_an_array\"}";
			manifest.close();

			check(!CreatureCache::ValidateManifest(cacheDir, luaDir, "monsters"), "ValidateManifest returns false when files is not an array without throwing");
		}

		// Case C: root JSON is an array, not object
		{
			std::ofstream manifest(cacheDir / "manifest_monsters.json");
			manifest << "[1, 2, 3]";
			manifest.close();

			check(!CreatureCache::ValidateManifest(cacheDir, luaDir, "monsters"), "ValidateManifest returns false when root JSON is an array without throwing");
		}
	}

	// Test 12: malformed cache XML is rejected transactionally
	{
		TemporaryDirectory temp;
		const auto cacheDir = temp.path / "cache";
		const auto luaDir = temp.path / "monsters";
		temp.write("cache/monsters.xml", "<creatures><creature name=\"Orc\" type=\"npc\" looktype=\"abc\"/></creatures>");
		temp.write("cache/manifest_monsters.json", "{}");

		CreatureDatabase db;
		size_t loadedCount = 0;
		check(!CreatureCache::LoadCached(db, cacheDir, luaDir, "monsters", &loadedCount), "LoadCached rejects a record whose type does not match the cache kind");
		check(db["Orc"] == nullptr, "Malformed cache records are not partially applied");
		check(!std::filesystem::exists(cacheDir / "monsters.xml"), "Malformed cache XML is invalidated");
	}

	std::cout << "Creature Cache Tests: " << checks << " checks, " << failures << " failures.\n";
	return failures == 0 ? 0 : 1;
}
