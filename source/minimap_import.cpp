#include "minimap_import.h"

#include <zlib.h>

#include <algorithm>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>

namespace {

	constexpr size_t minimapBlockByteSize = MMBLOCK_SIZE * MMBLOCK_SIZE * sizeof(MinimapTile);
	constexpr size_t maximumImportBlocks = 16384;
	constexpr size_t maximumImportFileSize = 512ULL * 1024ULL * 1024ULL;

	class ByteReader {
	public:
		explicit ByteReader(const std::vector<uint8_t>& bytes) :
			bytes_(bytes) { }

		bool readU8(uint8_t& value) {
			if (remaining() < 1) {
				return false;
			}
			value = bytes_[offset_++];
			return true;
		}
		bool readU16(uint16_t& value) {
			if (remaining() < 2) {
				return false;
			}
			value = static_cast<uint16_t>(bytes_[offset_])
				| static_cast<uint16_t>(bytes_[offset_ + 1] << 8);
			offset_ += 2;
			return true;
		}
		bool readU32(uint32_t& value) {
			if (remaining() < 4) {
				return false;
			}
			value = static_cast<uint32_t>(bytes_[offset_])
				| (static_cast<uint32_t>(bytes_[offset_ + 1]) << 8)
				| (static_cast<uint32_t>(bytes_[offset_ + 2]) << 16)
				| (static_cast<uint32_t>(bytes_[offset_ + 3]) << 24);
			offset_ += 4;
			return true;
		}
		bool readString(std::string& value) {
			uint16_t length = 0;
			if (!readU16(length) || remaining() < length) {
				return false;
			}
			value.assign(reinterpret_cast<const char*>(bytes_.data() + offset_), length);
			offset_ += length;
			return true;
		}
		bool readBytes(std::vector<uint8_t>& value, size_t length) {
			if (remaining() < length) {
				return false;
			}
			value.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_), bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + length));
			offset_ += length;
			return true;
		}
		bool seek(size_t offset) {
			if (offset > bytes_.size()) {
				return false;
			}
			offset_ = offset;
			return true;
		}
		size_t tell() const noexcept {
			return offset_;
		}
		size_t remaining() const noexcept {
			return bytes_.size() - offset_;
		}

	private:
		const std::vector<uint8_t>& bytes_;
		size_t offset_ = 0;
	};

	std::string blockError(size_t blockNumber, const std::string& message) {
		return "Invalid OTMM block " + std::to_string(blockNumber) + ": " + message;
	}

} // namespace

void MinimapImportBounds::include(int x, int y) noexcept {
	minX = std::min(minX, x);
	minY = std::min(minY, y);
	maxX = std::max(maxX, x);
	maxY = std::max(maxY, y);
}

uint8_t MinimapImportColorStats::dominantSpeed() const noexcept {
	return static_cast<uint8_t>(std::distance(speedCounts.begin(), std::max_element(speedCounts.begin(), speedCounts.end())));
}

bool MinimapImportDocument::loadFromFile(const std::string& path, std::string& error) {
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input) {
		error = "Unable to open minimap file '" + path + "'.";
		return false;
	}
	const std::streamoff fileSize = input.tellg();
	if (fileSize < 0 || static_cast<uint64_t>(fileSize) > maximumImportFileSize) {
		error = "The minimap file is too large to import safely.";
		return false;
	}
	input.seekg(0, std::ios::beg);
	std::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
	if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), fileSize)) {
		error = "Unable to read the complete minimap file.";
		return false;
	}
	if (!loadFromBytes(bytes, error)) {
		return false;
	}
	sourcePath_ = path;
	return true;
}

bool MinimapImportDocument::loadFromBytes(const std::vector<uint8_t>& bytes, std::string& error) {
	auto parsed = std::make_unique<MinimapImportDocument>();
	ByteReader reader(bytes);
	uint32_t signature = 0;
	uint16_t dataStart = 0;
	if (!reader.readU32(signature) || !reader.readU16(dataStart) || !reader.readU16(parsed->version_) || !reader.readU32(parsed->flags_)) {
		error = "The file ends before the OTMM header is complete.";
		return false;
	}
	if (signature != OTMM_SIGNATURE) {
		error = "The file does not have the OTMM signature.";
		return false;
	}
	if (parsed->version_ != OTMM_VERSION) {
		error = "Unsupported OTMM version " + std::to_string(parsed->version_) + ".";
		return false;
	}
	if (parsed->flags_ != 0) {
		error = "This OTMM file uses unsupported whole-file compression flags.";
		return false;
	}
	if (!reader.readString(parsed->description_)) {
		error = "The OTMM description is truncated.";
		return false;
	}
	if (dataStart < reader.tell() || !reader.seek(dataStart)) {
		error = "The OTMM data offset is invalid.";
		return false;
	}

	bool foundTerminator = false;
	size_t processedBlockCount = 0;
	std::vector<uint8_t> compressed;
	std::array<MinimapTile, MMBLOCK_SIZE * MMBLOCK_SIZE> decoded {};
	for (size_t blockNumber = 0; reader.remaining() != 0; ++blockNumber) {
		uint16_t baseX = 0;
		uint16_t baseY = 0;
		uint8_t floor = 0;
		if (!reader.readU16(baseX) || !reader.readU16(baseY) || !reader.readU8(floor)) {
			error = blockError(blockNumber, "position is truncated.");
			return false;
		}
		if (baseX == 65535 && baseY == 65535 && floor == 255) {
			foundTerminator = true;
			break;
		}
		if (floor >= MAP_LAYERS) {
			error = blockError(blockNumber, "floor is outside 0-15.");
			return false;
		}
		if (baseX % MMBLOCK_SIZE != 0 || baseY % MMBLOCK_SIZE != 0) {
			error = blockError(blockNumber, "origin is not aligned to 64 tiles.");
			return false;
		}
		if (processedBlockCount >= maximumImportBlocks) {
			error = "The minimap contains too many blocks to import safely.";
			return false;
		}

		uint16_t compressedSize = 0;
		if (!reader.readU16(compressedSize) || compressedSize == 0 || !reader.readBytes(compressed, compressedSize)) {
			error = blockError(blockNumber, "compressed payload is truncated.");
			return false;
		}
		++processedBlockCount;
		uLongf decodedSize = minimapBlockByteSize;
		const int result = uncompress(reinterpret_cast<Bytef*>(decoded.data()), &decodedSize, compressed.data(), static_cast<uLong>(compressed.size()));
		if (result != Z_OK || decodedSize != minimapBlockByteSize) {
			error = blockError(blockNumber, "zlib payload is invalid.");
			return false;
		}

		const uint32_t blockIndex = getBlockIndex(baseX, baseY);
		auto& floorBlocks = parsed->blocks_[floor];
		if (floorBlocks.contains(blockIndex)) {
			error = blockError(blockNumber, "duplicates an earlier block.");
			return false;
		}
		MinimapBlock block;
		bool hasSeenTile = false;
		for (size_t tileIndex = 0; tileIndex < decoded.size(); ++tileIndex) {
			const MinimapTile& tile = decoded[tileIndex];
			block.updateTile(static_cast<int>(tileIndex % MMBLOCK_SIZE), static_cast<int>(tileIndex / MMBLOCK_SIZE), tile);
			if ((tile.flags & MinimapTileWasSeen) == 0) {
				continue;
			}
			hasSeenTile = true;
			const int x = baseX + static_cast<int>(tileIndex % MMBLOCK_SIZE);
			const int y = baseY + static_cast<int>(tileIndex / MMBLOCK_SIZE);
			parsed->bounds_.include(x, y);
			parsed->floors_[floor].bounds.include(x, y);
			++parsed->floors_[floor].tileCount;
			++parsed->tileCount_;
			auto& colorStats = parsed->colorStats_[tile.color];
			++colorStats.count;
			++colorStats.speedCounts[tile.speed];
		}
		if (hasSeenTile) {
			floorBlocks.emplace(blockIndex, std::move(block));
			++parsed->blockCount_;
		}
	}
	if (!foundTerminator) {
		error = "The OTMM file has no end marker.";
		return false;
	}
	if (parsed->tileCount_ == 0) {
		error = "The OTMM file does not contain any seen minimap tiles.";
		return false;
	}

	*this = std::move(*parsed);
	error.clear();
	return true;
}

uint32_t MinimapImportDocument::getBlockIndex(int x, int y) noexcept {
	return static_cast<uint32_t>((y / MMBLOCK_SIZE) * (65536 / MMBLOCK_SIZE) + (x / MMBLOCK_SIZE));
}

const MinimapTile* MinimapImportDocument::getTile(int x, int y, int z) const noexcept {
	if (x < 0 || x > 65535 || y < 0 || y > 65535 || z < 0 || z >= MAP_LAYERS) {
		return nullptr;
	}
	const auto& floorBlocks = blocks_[z];
	const auto found = floorBlocks.find(getBlockIndex(x, y));
	if (found == floorBlocks.end()) {
		return nullptr;
	}
	const MinimapTile& tile = found->second.getTiles()[found->second.getTileIndex(x, y)];
	return (tile.flags & MinimapTileWasSeen) != 0 ? &tile : nullptr;
}

std::vector<int> MinimapImportDocument::getAvailableFloors() const {
	std::vector<int> floors;
	for (int floor = 0; floor < MAP_LAYERS; ++floor) {
		if (floors_[floor].tileCount != 0) {
			floors.push_back(floor);
		}
	}
	return floors;
}

std::vector<uint8_t> MinimapImportDocument::getUsedColors() const {
	std::vector<uint8_t> colors;
	for (size_t color = 0; color < colorStats_.size(); ++color) {
		if (colorStats_[color].count != 0) {
			colors.push_back(static_cast<uint8_t>(color));
		}
	}
	return colors;
}

const MinimapImportFloorInfo& MinimapImportDocument::getFloorInfo(int floor) const {
	static const MinimapImportFloorInfo empty;
	return floor >= 0 && floor < MAP_LAYERS ? floors_[floor] : empty;
}

void MinimapImportDocument::forEachSeenTile(const std::function<void(int, int, int, const MinimapTile&)>& visitor) const {
	if (!visitor) {
		return;
	}
	for (int floor = 0; floor < MAP_LAYERS; ++floor) {
		for (const auto& [blockIndex, block] : blocks_[floor]) {
			const int baseX = static_cast<int>(blockIndex % (65536 / MMBLOCK_SIZE)) * MMBLOCK_SIZE;
			const int baseY = static_cast<int>(blockIndex / (65536 / MMBLOCK_SIZE)) * MMBLOCK_SIZE;
			const auto& tiles = block.getTiles();
			for (size_t tileIndex = 0; tileIndex < tiles.size(); ++tileIndex) {
				const MinimapTile& tile = tiles[tileIndex];
				if ((tile.flags & MinimapTileWasSeen) == 0) {
					continue;
				}
				visitor(baseX + static_cast<int>(tileIndex % MMBLOCK_SIZE), baseY + static_cast<int>(tileIndex / MMBLOCK_SIZE), floor, tile);
			}
		}
	}
}
