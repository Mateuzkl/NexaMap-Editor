#include "favorites_manager.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {
	int failures = 0;
	void check(bool value, const char* message) {
		if (!value) {
			std::cerr << "FAILED: " << message << '\n';
			++failures;
		}
	}
	FavoriteEntry item(const std::string& context) {
		return { context, FavoriteKind::Item, "2160", "Crystal coin", "2160:3043:crystal coin", FavoriteCategory::Items, "Containers", 2160, 3043 };
	}
}

int main() {
	const auto directory = std::filesystem::temp_directory_path() / ("nexamap-favorites-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	const auto file = directory / "favorites.json";
	std::string error;
	const auto classic = item("classic-860-server-x");
	auto appearances = item("appearances-crystal-server-y");
	appearances.displayName = "Different resource";
	FavoritesManager manager(file);
	check(manager.load(error), "missing file starts empty");
	check(manager.add(classic, error), "add and save");
	check(!manager.add(classic, error), "duplicate rejected");
	check(manager.add(appearances, error), "same numeric ID in separate context is independent");
	check(!classic.matchesDefinition(appearances.context, classic.definition), "cross-client collision rejected");
	check(!classic.matchesDefinition(classic.context, "2160:9999:other item"), "changed definition rejected");
	check(classic.matchesDefinition(classic.context, classic.definition), "same definition resolves");
	FavoritesManager restarted(file);
	check(restarted.load(error) && restarted.entries().size() == 2, "restart loads both sessions");
	check(restarted.entries()[0].sameResource(classic) && restarted.entries()[1].sameResource(appearances), "addition order survives restart");
	check(restarted.remove(classic, error) && !restarted.contains(classic) && restarted.contains(appearances), "remove preserves other context");
	check(restarted.add(classic, error) && restarted.clearContext(classic.context, error) && restarted.entries().size() == 1, "clear context preserves other sessions");
	FavoriteEntry stamp { classic.context, FavoriteKind::TerrainStamp, "forest", "Forest", "stamp-file-fingerprint", FavoriteCategory::SavedTerrain };
	check(!stamp.matchesDefinition(classic.context, ""), "missing terrain stamp unavailable");
	check(!stamp.matchesDefinition(classic.context, "replacement-stamp"), "replaced stamp unavailable");
	auto invalid = classic;
	invalid.stableId.clear();
	check(!restarted.add(invalid, error), "invalid identity rejected");
	invalid = classic;
	invalid.kind = static_cast<FavoriteKind>(-1);
	check(!restarted.add(invalid, error), "unknown resource kind rejected");
	{
		std::ifstream input(file);
		auto json = nlohmann::json::parse(input);
		json["favorites"].push_back(json["favorites"][0]);
		json["favorites"].push_back({ { "id", "broken" } });
		for (const auto badKind : { nlohmann::json(4294967296ULL), nlohmann::json(0.5), nlohmann::json(-1) }) {
			auto badEntry = json["favorites"][0];
			badEntry["kind"] = badKind;
			badEntry["context"] = "invalid-kind";
			json["favorites"].push_back(badEntry);
		}
		input.close();
		std::ofstream(file) << json.dump();
	}
	check(restarted.load(error) && restarted.entries().size() == 1 && !error.empty(), "invalid and duplicate JSON entries skipped");
	std::ofstream(file) << R"({"format":"nexamap-favorites","schemaVersion":99,"favorites":[]})";
	check(!restarted.load(error) && !restarted.add(classic, error), "unknown schema preserved and writes blocked");
	std::ofstream(file) << "{broken";
	check(!restarted.load(error) && !restarted.remove(appearances, error), "invalid JSON never overwritten");
	std::ifstream input(file);
	std::string original;
	input >> original;
	check(original == "{broken", "corrupt original remains intact");
	input.close();
	const auto blocked = directory / "blocked";
	std::ofstream(blocked) << "file blocks parent directory";
	FavoritesManager failedSave(blocked / "favorites.json");
	check(!failedSave.load(error) || !failedSave.add(classic, error), "write failure reported");
	check(failedSave.entries().empty(), "write failure does not mutate memory");
	std::filesystem::remove(file);
	std::filesystem::remove(blocked);
	std::filesystem::remove(directory);
	std::cout << (failures == 0 ? "Favorites logic tests passed\n" : "Favorites logic tests failed\n");
	return failures == 0 ? 0 : 1;
}
