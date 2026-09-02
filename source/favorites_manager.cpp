#include "favorites_manager.h"
#include "file_transaction.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {
	constexpr size_t FavoriteFileLimit = 8 * 1024 * 1024;
	constexpr size_t FavoriteEntryLimit = 10000;
	bool ValidFavoriteString(const std::string& value, size_t limit, bool allowEmpty = false) {
		return (allowEmpty || !value.empty()) && value.size() <= limit && value.find('\0') == std::string::npos;
	}
}

bool FavoriteEntry::sameResource(const FavoriteEntry& other) const {
	return context == other.context && kind == other.kind && stableId == other.stableId;
}

bool FavoriteEntry::valid() const {
	return kind >= FavoriteKind::Item && kind <= FavoriteKind::TerrainStamp
		&& category >= FavoriteCategory::Terrain && category <= FavoriteCategory::Other
		&& ValidFavoriteString(context, 16384) && ValidFavoriteString(stableId, 512)
		&& ValidFavoriteString(displayName, 1024) && ValidFavoriteString(definition, 16384)
		&& ValidFavoriteString(tileset, 1024, true)
		&& (kind != FavoriteKind::Item || (itemId != 0 && stableId == std::to_string(itemId)));
}

bool FavoriteEntry::matchesDefinition(const std::string& activeContext, const std::string& currentDefinition) const {
	return valid() && context == activeContext && !currentDefinition.empty() && definition == currentDefinition;
}

FavoritesManager::FavoritesManager(std::filesystem::path file) :
	file_(std::move(file)) { }

bool FavoritesManager::load(std::string& error) {
	error.clear();
	writable_ = false;
	try {
		std::error_code ec;
		if (!std::filesystem::exists(file_, ec) && !ec) {
			entries_.clear();
			writable_ = true;
			++revision_;
			return true;
		}
		if (ec || std::filesystem::file_size(file_) > FavoriteFileLimit) {
			error = "Favorites file is unreadable or too large. The original file was preserved.";
			return false;
		}
		std::ifstream input(file_, std::ios::binary);
		const auto json = nlohmann::json::parse(input);
		if (json.at("format") != "nexamap-favorites" || json.at("schemaVersion") != 1 || !json.at("favorites").is_array() || json.at("favorites").size() > FavoriteEntryLimit) {
			error = "Unsupported favorites format. The original file was preserved.";
			return false;
		}
		std::vector<FavoriteEntry> loaded;
		size_t skipped = 0;
		for (const auto& value : json.at("favorites")) {
			try {
				FavoriteEntry entry;
				entry.context = value.at("context").get<std::string>();
				const auto& kind = value.at("kind");
				const auto& category = value.at("category");
				const auto itemId = value.value("itemId", nlohmann::json(0));
				const auto clientId = value.value("clientId", nlohmann::json(0));
				if (!kind.is_number_integer() || kind < 0 || kind > static_cast<int>(FavoriteKind::TerrainStamp)
					|| !category.is_number_integer() || category < 0 || category > static_cast<int>(FavoriteCategory::Other)
					|| !itemId.is_number_integer() || itemId < 0 || itemId > 65535
					|| !clientId.is_number_integer() || clientId < 0 || clientId > 65535) {
					++skipped;
					continue;
				}
				entry.kind = static_cast<FavoriteKind>(kind.get<int>());
				entry.stableId = value.at("id").get<std::string>();
				entry.displayName = value.at("name").get<std::string>();
				entry.definition = value.at("definition").get<std::string>();
				entry.category = static_cast<FavoriteCategory>(category.get<int>());
				entry.tileset = value.value("tileset", std::string());
				entry.itemId = itemId.get<uint16_t>();
				entry.clientId = clientId.get<uint16_t>();
				if (!entry.valid() || std::any_of(loaded.begin(), loaded.end(), [&](const auto& other) { return entry.sameResource(other); })) {
					++skipped;
					continue;
				}
				loaded.push_back(std::move(entry));
			} catch (const nlohmann::json::exception&) {
				++skipped;
			}
		}
		entries_ = std::move(loaded);
		writable_ = true;
		++revision_;
		if (skipped != 0) {
			error = "Skipped " + std::to_string(skipped) + " invalid or duplicate favorites.";
		}
		return true;
	} catch (const std::exception& exception) {
		error = std::string("Could not load favorites; the original file was preserved: ") + exception.what();
		return false;
	}
}

bool FavoritesManager::save(const std::vector<FavoriteEntry>& entries, std::string& error) const {
	error.clear();
	if (!writable_) {
		error = "Favorites could not be loaded. Fix or back up the existing favorites.json before saving.";
		return false;
	}
	try {
		nlohmann::json json = { { "format", "nexamap-favorites" }, { "schemaVersion", 1 }, { "favorites", nlohmann::json::array() } };
		for (const auto& entry : entries) {
			json["favorites"].push_back({ { "context", entry.context }, { "kind", static_cast<int>(entry.kind) }, { "id", entry.stableId }, { "name", entry.displayName }, { "definition", entry.definition }, { "category", static_cast<int>(entry.category) }, { "tileset", entry.tileset }, { "itemId", entry.itemId }, { "clientId", entry.clientId } });
		}
		const std::string serialized = json.dump(2) + '\n';
		if (serialized.size() > FavoriteFileLimit) {
			error = "Favorites file size limit reached.";
			return false;
		}
		if (!file_.parent_path().empty()) {
			std::filesystem::create_directories(file_.parent_path());
		}
		FileSaveTransaction transaction;
		std::ofstream output(transaction.Stage(file_), std::ios::binary | std::ios::trunc);
		output.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
		output.flush();
		if (!output.good()) {
			error = "Could not write the staged favorites file.";
			return false;
		}
		output.close();
		if (output.fail()) {
			error = "Could not close the staged favorites file.";
			return false;
		}
		return transaction.Commit(error);
	} catch (const std::exception& exception) {
		error = std::string("Could not save favorites: ") + exception.what();
		return false;
	}
}

bool FavoritesManager::commit(std::vector<FavoriteEntry> entries, std::string& error) {
	if (!save(entries, error)) {
		return false;
	}
	entries_ = std::move(entries);
	++revision_;
	return true;
}

bool FavoritesManager::contains(const FavoriteEntry& entry) const {
	return std::any_of(entries_.begin(), entries_.end(), [&](const auto& other) { return entry.sameResource(other); });
}

bool FavoritesManager::add(const FavoriteEntry& entry, std::string& error) {
	error.clear();
	if (!entry.valid() || contains(entry) || entries_.size() >= FavoriteEntryLimit) {
		error = "Favorite is invalid, already exists, or the favorites limit was reached.";
		return false;
	}
	auto updated = entries_;
	updated.push_back(entry);
	return commit(std::move(updated), error);
}

bool FavoritesManager::remove(const FavoriteEntry& entry, std::string& error) {
	auto updated = entries_;
	std::erase_if(updated, [&](const auto& other) { return entry.sameResource(other); });
	return commit(std::move(updated), error);
}

bool FavoritesManager::clearContext(const std::string& context, std::string& error) {
	auto updated = entries_;
	std::erase_if(updated, [&](const auto& entry) { return entry.context == context; });
	return commit(std::move(updated), error);
}

const char* FavoritesManager::categoryName(FavoriteCategory category) {
	switch (category) {
		case FavoriteCategory::Terrain:
			return "Terrain";
		case FavoriteCategory::Walls:
			return "Walls & Railings";
		case FavoriteCategory::Doodads:
			return "Doodads";
		case FavoriteCategory::Items:
			return "Items";
		case FavoriteCategory::Creatures:
			return "Creatures";
		case FavoriteCategory::Npcs:
			return "NPCs";
		case FavoriteCategory::Collections:
			return "Collections";
		case FavoriteCategory::SavedTerrain:
			return "Saved Terrain";
		default:
			return "Other";
	}
}

FavoriteCategory FavoritesManager::defaultCategory(FavoriteKind kind) {
	switch (kind) {
		case FavoriteKind::Ground:
			return FavoriteCategory::Terrain;
		case FavoriteKind::Wall:
		case FavoriteKind::WallDecoration:
			return FavoriteCategory::Walls;
		case FavoriteKind::Doodad:
			return FavoriteCategory::Doodads;
		case FavoriteKind::Item:
		case FavoriteKind::Table:
		case FavoriteKind::Carpet:
			return FavoriteCategory::Items;
		case FavoriteKind::Creature:
			return FavoriteCategory::Creatures;
		case FavoriteKind::Npc:
			return FavoriteCategory::Npcs;
		case FavoriteKind::TerrainStamp:
			return FavoriteCategory::SavedTerrain;
		default:
			return FavoriteCategory::Other;
	}
}
