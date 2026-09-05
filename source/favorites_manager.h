#ifndef NEXAMAP_FAVORITES_MANAGER_H_
#define NEXAMAP_FAVORITES_MANAGER_H_

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class FavoriteKind { Item,
						  Ground,
						  Wall,
						  WallDecoration,
						  Doodad,
						  Table,
						  Carpet,
						  Creature,
						  Npc,
						  TerrainStamp };
enum class FavoriteCategory { Terrain,
							  Walls,
							  Doodads,
							  Items,
							  Creatures,
							  Npcs,
							  Collections,
							  SavedTerrain,
							  Other };

// Definitions only: never a tile instance, Brush pointer, or session pointer.
struct FavoriteEntry {
	std::string context;
	FavoriteKind kind = FavoriteKind::Item;
	std::string stableId;
	std::string displayName;
	std::string definition;
	FavoriteCategory category = FavoriteCategory::Other;
	std::string tileset;
	uint16_t itemId = 0;
	uint16_t clientId = 0;

	bool sameResource(const FavoriteEntry& other) const;
	bool valid() const;
	bool matchesDefinition(const std::string& activeContext, const std::string& currentDefinition) const;
};

class FavoritesManager {
public:
	explicit FavoritesManager(std::filesystem::path file);
	bool load(std::string& error);
	bool add(const FavoriteEntry& entry, std::string& error);
	bool remove(const FavoriteEntry& entry, std::string& error);
	bool clearContext(const std::string& context, std::string& error);
	bool contains(const FavoriteEntry& entry) const;
	const std::vector<FavoriteEntry>& entries() const {
		return entries_;
	}
	uint64_t revision() const {
		return revision_;
	}
	static const char* categoryName(FavoriteCategory category);
	static FavoriteCategory defaultCategory(FavoriteKind kind);

private:
	bool save(const std::vector<FavoriteEntry>& entries, std::string& error) const;
	bool commit(std::vector<FavoriteEntry> entries, std::string& error);
	std::filesystem::path file_;
	std::vector<FavoriteEntry> entries_;
	uint64_t revision_ = 0;
	bool writable_ = false;
};

#endif
