// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_PLAYTEST_MAP_H_
#define NEXAMAP_PLAYTEST_MAP_H_
#include "playtest_controller.h"
class Map;
namespace Playtest {
	// Short-lived adapter. It never owns the map or retains Tile/Item pointers.
	class MapWorld final : public World {
	public:
		explicit MapWorld(const Map& map) :
			map(map) { }
		TileInfo read(const Position& position) const override;

	private:
		const Map& map;
	};
}
#endif
