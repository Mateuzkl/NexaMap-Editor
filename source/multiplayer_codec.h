// NexaMap multiplayer. SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MULTIPLAYER_CODEC_H
#define NEXAMAP_MULTIPLAYER_CODEC_H
#include "multiplayer_protocol.h"
#include <memory>
class Map;
class Tile;
namespace Multiplayer {
	Bytes encodeTile(const Tile* tile);
	// The result has no TileLocation and allocates nothing on the map, so a rejected
	// edit leaves no trace. Attach a location only after validateTiles accepts it.
	std::unique_ptr<Tile> decodeTile(Map& map, uint64_t key, std::span<const uint8_t> data, int maxSpawnRadius = MaxSpawnRadius);
	struct DecodedTile {
		uint64_t key = 0;
		std::unique_ptr<Tile> tile;
	};
	Bytes encodeMetadata(const Map& map);
	void validateMetadata(std::span<const uint8_t> data);
	void applyMetadata(Map& map, std::span<const uint8_t> data);
	// One line for the host's approval list, naming what the request adds, removes or changes.
	std::string describeMetadataChange(std::span<const uint8_t> before, std::span<const uint8_t> after);
	Digest assetSignature();
	// Spawn cells Map::addSpawn would touch: only new spawns or spawns with a new radius.
	uint64_t newSpawnArea(Map& map, const std::vector<DecodedTile>& tiles);
	// Validate global Unique IDs and references over the entire atomic transaction.
	// A snapshot is the host's existing map, so its duplicate Unique IDs are not an error.
	std::string validateTiles(Map& map, const std::vector<DecodedTile>& tiles, bool allowSensitive, std::span<const uint8_t> metadata = {}, bool snapshot = false);
}
#endif
