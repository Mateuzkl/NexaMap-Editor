//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#ifndef RME_IOMINIMAP_H_
#define RME_IOMINIMAP_H_

#include "definitions.h" // MAP_LAYERS, MAP_MAX_LAYER, GROUND_LAYER
#include "tile.h" // Position, INVALID_MINIMAP_COLOR

#include <array>
#include <string>
#include <unordered_map>

class Editor;

// Output format of the minimap export.
enum class MinimapExportFormat {
	Otmm, // otclient client minimap
	Png,
	Bmp
};

// Which slice of the map is exported.
enum class MinimapExportMode {
	AllFloors,
	GroundFloor,
	SpecificFloor,
	SelectedArea
};

// OTMM (otclient minimap) on-disk constants.
enum {
	MMBLOCK_SIZE = 64,
	OTMM_SIGNATURE = 0x4D4d544F,
	OTMM_VERSION = 1
};

// otclient-compatible per-tile flags.
enum MinimapTileFlags {
	MinimapTileWasSeen = 1,
	MinimapTileNotWalkable = 4
};

#pragma pack(push, 1) // disable memory alignment
struct MinimapTile {
	uint8_t flags = 0;
	uint8_t color = INVALID_MINIMAP_COLOR;
	uint8_t speed = 10;
};
static_assert(sizeof(MinimapTile) == 3, "MinimapTile must be exactly 3 bytes");

class MinimapBlock {
public:
	void updateTile(int x, int y, const MinimapTile& tile);
	inline uint32_t getTileIndex(int x, int y) const noexcept {
		return ((y % MMBLOCK_SIZE) * MMBLOCK_SIZE) + (x % MMBLOCK_SIZE);
	}
	const std::array<MinimapTile, MMBLOCK_SIZE * MMBLOCK_SIZE>& getTiles() const noexcept {
		return m_tiles;
	}

private:
	std::array<MinimapTile, MMBLOCK_SIZE * MMBLOCK_SIZE> m_tiles;
};
#pragma pack(pop)

// Exports the current editor map as an .otmm (otclient) minimap or as .png/.bmp images.
class IOMinimap {
public:
	IOMinimap(Editor* editor, MinimapExportFormat format, MinimapExportMode mode, bool updateLoadbar);

	bool saveMinimap(const std::string& directory, const std::string& name, int floor = -1);

	const std::string& getError() const noexcept {
		return m_error;
	}

private:
	bool saveOtmm(const std::string& path);
	bool saveImage(const std::string& directory, const std::string& name);
	bool exportMinimap(const std::string& directory, const std::string& name);
	bool exportSelection(const std::string& directory, const std::string& name);
	void readBlocks();
	inline uint32_t getBlockIndex(const Position& pos) const noexcept {
		assert(pos.x >= 0 && pos.y >= 0);
		return ((pos.y / MMBLOCK_SIZE) * (65536 / MMBLOCK_SIZE)) + (pos.x / MMBLOCK_SIZE);
	}

	Editor* m_editor;
	MinimapExportFormat m_format;
	MinimapExportMode m_mode;
	bool m_updateLoadbar = false;
	int m_floor = -1;
	std::unordered_map<uint32_t, MinimapBlock> m_blocks[MAP_LAYERS];
	std::string m_error;
};

#endif
