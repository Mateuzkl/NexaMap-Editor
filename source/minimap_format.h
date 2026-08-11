#ifndef RME_MINIMAP_FORMAT_H_
#define RME_MINIMAP_FORMAT_H_

#include <array>
#include <cassert>
#include <cstdint>

enum {
	MMBLOCK_SIZE = 64,
	OTMM_SIGNATURE = 0x4D4d544F,
	OTMM_VERSION = 1
};

enum MinimapTileFlags {
	MinimapTileWasSeen = 1,
	MinimapTileNotWalkable = 4
};

#pragma pack(push, 1)
struct MinimapTile {
	uint8_t flags = 0;
	uint8_t color = 255;
	uint8_t speed = 10;
};
static_assert(sizeof(MinimapTile) == 3, "MinimapTile must be exactly 3 bytes");

class MinimapBlock {
public:
	void updateTile(int x, int y, const MinimapTile& tile) {
		m_tiles[getTileIndex(x, y)] = tile;
	}
	uint32_t getTileIndex(int x, int y) const noexcept {
		assert(x >= 0 && y >= 0);
		return ((y % MMBLOCK_SIZE) * MMBLOCK_SIZE) + (x % MMBLOCK_SIZE);
	}
	const std::array<MinimapTile, MMBLOCK_SIZE * MMBLOCK_SIZE>& getTiles() const noexcept {
		return m_tiles;
	}

private:
	std::array<MinimapTile, MMBLOCK_SIZE * MMBLOCK_SIZE> m_tiles {};
};
#pragma pack(pop)

#endif
