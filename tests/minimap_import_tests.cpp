#include "minimap_import.h"

#include <zlib.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

	void require(bool condition, const char* message) {
		if (!condition) {
			throw std::runtime_error(message);
		}
	}

	void appendU8(std::vector<uint8_t>& bytes, uint8_t value) {
		bytes.push_back(value);
	}

	void appendU16(std::vector<uint8_t>& bytes, uint16_t value) {
		bytes.push_back(static_cast<uint8_t>(value & 0xFF));
		bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
	}

	void appendU32(std::vector<uint8_t>& bytes, uint32_t value) {
		appendU16(bytes, static_cast<uint16_t>(value & 0xFFFF));
		appendU16(bytes, static_cast<uint16_t>((value >> 16) & 0xFFFF));
	}

	void appendString(std::vector<uint8_t>& bytes, const std::string& value) {
		appendU16(bytes, static_cast<uint16_t>(value.size()));
		bytes.insert(bytes.end(), value.begin(), value.end());
	}

	std::vector<uint8_t> makeOtmm() {
		std::array<MinimapTile, MMBLOCK_SIZE * MMBLOCK_SIZE> tiles {};
		tiles[2 * MMBLOCK_SIZE + 1] = { MinimapTileWasSeen, 86, 14 };
		tiles[2 * MMBLOCK_SIZE + 2] = { static_cast<uint8_t>(MinimapTileWasSeen | MinimapTileNotWalkable), 24, 20 };
		tiles[3 * MMBLOCK_SIZE + 1] = { MinimapTileWasSeen, 86, 14 };

		const uLong sourceSize = static_cast<uLong>(tiles.size() * sizeof(MinimapTile));
		std::vector<uint8_t> compressed(compressBound(sourceSize));
		uLongf compressedSize = static_cast<uLongf>(compressed.size());
		require(compress2(compressed.data(), &compressedSize, reinterpret_cast<const Bytef*>(tiles.data()), sourceSize, 3) == Z_OK, "test compression failed");
		compressed.resize(compressedSize);

		std::vector<uint8_t> bytes;
		appendU32(bytes, OTMM_SIGNATURE);
		appendU16(bytes, 22);
		appendU16(bytes, OTMM_VERSION);
		appendU32(bytes, 0);
		appendString(bytes, "OTMM 1.0");
		appendU16(bytes, 32000);
		appendU16(bytes, 32000);
		appendU8(bytes, 7);
		appendU16(bytes, static_cast<uint16_t>(compressed.size()));
		bytes.insert(bytes.end(), compressed.begin(), compressed.end());
		appendU16(bytes, 65535);
		appendU16(bytes, 65535);
		appendU8(bytes, 255);
		return bytes;
	}

} // namespace

int main() {
	MinimapImportDocument document;
	std::string error;
	const std::vector<uint8_t> valid = makeOtmm();
	require(document.loadFromBytes(valid, error), error.c_str());
	require(document.getVersion() == OTMM_VERSION, "version mismatch");
	require(document.getDescription() == "OTMM 1.0", "description mismatch");
	require(document.getBlockCount() == 1, "block count mismatch");
	require(document.getTileCount() == 3, "tile count mismatch");
	require(document.getAvailableFloors() == std::vector<int> { 7 }, "floor list mismatch");
	const auto& floor = document.getFloorInfo(7);
	require(floor.bounds.minX == 32001 && floor.bounds.maxX == 32002, "floor X bounds mismatch");
	require(floor.bounds.minY == 32002 && floor.bounds.maxY == 32003, "floor Y bounds mismatch");
	const MinimapTile* first = document.getTile(32001, 32002, 7);
	require(first && first->color == 86 && first->speed == 14, "first tile mismatch");
	require(document.getTile(32000, 32000, 7) == nullptr, "unseen tile was exposed");
	require(document.getColorStats()[86].count == 2, "color count mismatch");
	require(document.getColorStats()[86].dominantSpeed() == 14, "dominant speed mismatch");

	uint64_t visited = 0;
	document.forEachSeenTile([&visited](int, int, int, const MinimapTile&) { ++visited; });
	require(visited == 3, "seen tile iteration mismatch");

	std::vector<uint8_t> badSignature = valid;
	badSignature[0] = 0;
	require(!document.loadFromBytes(badSignature, error) && error.find("signature") != std::string::npos, "bad signature was accepted");
	std::vector<uint8_t> truncated(valid.begin(), valid.end() - 8);
	require(!document.loadFromBytes(truncated, error), "truncated block was accepted");
	require(document.getTileCount() == 3, "failed load changed the existing document");

	std::cout << "minimap_import_tests passed\n";
	return 0;
}
