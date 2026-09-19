#include "creature_cache.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>

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

		check(!CreatureCache::ValidateManifest(cacheDir, luaDir, "monsters"),
			  "ValidateManifest returns false when cache directory does not exist");
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
			f["modifiedAt"] = std::chrono::duration_cast<std::chrono::seconds>(
				entry.last_write_time().time_since_epoch()
			).count();
			files.push_back(f);
		}
		std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
			return a["path"] < b["path"];
		});
		manifest["files"] = files;

		std::ofstream mOut(cacheDir / "manifest_monsters.json");
		mOut << manifest.dump(2);
		mOut.close();

		check(CreatureCache::ValidateManifest(cacheDir, luaDir, "monsters"),
			  "ValidateManifest returns true when files and manifest match");

		// Test 4: Invalidation on file addition
		temp.write("monsters/rat.lua", "rat data");
		check(!CreatureCache::ValidateManifest(cacheDir, luaDir, "monsters"),
			  "ValidateManifest returns false when a new file is added");

		// Remove the added file
		std::filesystem::remove(luaDir / "rat.lua");
		check(CreatureCache::ValidateManifest(cacheDir, luaDir, "monsters"),
			  "ValidateManifest returns true again after added file is removed");

		// Test 5: Invalidation on file modification
		std::this_thread::sleep_for(std::chrono::milliseconds(1100)); // ensure mtime second increments
		temp.write("monsters/demon.lua", "monster data updated with different size");
		check(!CreatureCache::ValidateManifest(cacheDir, luaDir, "monsters"),
			  "ValidateManifest returns false when a file size/mtime changes");

		// Test 6: Invalidation on file deletion
		std::filesystem::remove(luaDir / "dragon.lua");
		check(!CreatureCache::ValidateManifest(cacheDir, luaDir, "monsters"),
			  "ValidateManifest returns false when a file is deleted");
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

		check(!CreatureCache::ValidateManifest(cacheDir, luaDir, "monsters"),
			  "ValidateManifest returns false when schema version does not match");
	}

	// Test 8: Corrupted manifest JSON handling
	{
		TemporaryDirectory temp;
		const auto cacheDir = temp.path / "cache";
		const auto luaDir = temp.path / "monsters";
		temp.write("monsters/demon.lua", "monster data");
		temp.write("cache/monsters.xml", "<creatures></creatures>");
		temp.write("cache/manifest_monsters.json", "{ broken json content !! ");

		check(!CreatureCache::ValidateManifest(cacheDir, luaDir, "monsters"),
			  "ValidateManifest returns false when manifest file is corrupted");
	}

	std::cout << "Creature Cache Tests: " << checks << " checks, " << failures << " failures.\n";
	return failures == 0 ? 0 : 1;
}
