// NexaMap multiplayer. SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MULTIPLAYER_CODEC_H
#define NEXAMAP_MULTIPLAYER_CODEC_H
#include "multiplayer_protocol.h"
#include <memory>
class Map;
class Tile;
namespace Multiplayer {
Bytes encodeTile(const Tile* tile);
std::unique_ptr<Tile> decodeTile(Map& map, uint64_t key, std::span<const uint8_t> data);
Bytes encodeMetadata(Map& map);
void validateMetadata(std::span<const uint8_t> data);
void applyMetadata(Map& map, std::span<const uint8_t> data);
Digest assetSignature();
// Validate global Unique IDs and references over the entire atomic transaction.
std::string validateTiles(Map& map, const std::vector<std::unique_ptr<Tile>>& tiles, bool allowSensitive, std::span<const uint8_t> metadata = {});
}
#endif
