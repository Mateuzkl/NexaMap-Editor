//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_TERRAIN_STAMP_H_
#define RME_TERRAIN_STAMP_H_

#include "../position.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class CopyBuffer;
class Editor;

struct TerrainStampItem {
	uint16_t id = 0;
	uint16_t count = 1;
	uint16_t actionId = 0;
	uint16_t uniqueId = 0;
	std::string text;
	std::optional<Position> teleportDestination;
	std::optional<uint8_t> doorId;
};

struct TerrainStampTile {
	int dx = 0;
	int dy = 0;
	int dz = 0;
	std::vector<TerrainStampItem> items;
};

struct TerrainStamp {
	std::string name;
	int clientVersion = -1;
	std::vector<TerrainStampTile> tiles;

	[[nodiscard]] bool empty() const {
		return tiles.empty();
	}
	[[nodiscard]] size_t tileCount() const {
		return tiles.size();
	}

	static TerrainStamp FromSelection(Editor& editor, int floor);
	bool LoadIntoCopyBuffer(CopyBuffer& buffer) const;
};

#endif
