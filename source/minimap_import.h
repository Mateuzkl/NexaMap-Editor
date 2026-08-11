#ifndef RME_MINIMAP_IMPORT_H_
#define RME_MINIMAP_IMPORT_H_

#include "definitions.h"
#include "minimap_format.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct MinimapImportBounds {
	int minX = 65536;
	int minY = 65536;
	int maxX = -1;
	int maxY = -1;

	bool valid() const noexcept {
		return minX <= maxX && minY <= maxY;
	}
	int width() const noexcept {
		return valid() ? maxX - minX + 1 : 0;
	}
	int height() const noexcept {
		return valid() ? maxY - minY + 1 : 0;
	}
	void include(int x, int y) noexcept;
};

struct MinimapImportColorStats {
	uint64_t count = 0;
	std::array<uint32_t, 256> speedCounts {};

	uint8_t dominantSpeed() const noexcept;
};

struct MinimapImportFloorInfo {
	MinimapImportBounds bounds;
	uint64_t tileCount = 0;
};

class MinimapImportDocument {
public:
	bool loadFromFile(const std::string& path, std::string& error);
	bool loadFromBytes(const std::vector<uint8_t>& bytes, std::string& error);

	const MinimapTile* getTile(int x, int y, int z) const noexcept;
	std::vector<int> getAvailableFloors() const;
	std::vector<uint8_t> getUsedColors() const;

	const MinimapImportFloorInfo& getFloorInfo(int floor) const;
	const MinimapImportBounds& getBounds() const noexcept {
		return bounds_;
	}
	const std::array<MinimapImportColorStats, 256>& getColorStats() const noexcept {
		return colorStats_;
	}
	const std::string& getDescription() const noexcept {
		return description_;
	}
	const std::string& getSourcePath() const noexcept {
		return sourcePath_;
	}
	uint16_t getVersion() const noexcept {
		return version_;
	}
	uint32_t getFlags() const noexcept {
		return flags_;
	}
	uint64_t getTileCount() const noexcept {
		return tileCount_;
	}
	size_t getBlockCount() const noexcept {
		return blockCount_;
	}

	void forEachSeenTile(const std::function<void(int, int, int, const MinimapTile&)>& visitor) const;

private:
	static uint32_t getBlockIndex(int x, int y) noexcept;

	std::array<std::unordered_map<uint32_t, MinimapBlock>, MAP_LAYERS> blocks_;
	std::array<MinimapImportFloorInfo, MAP_LAYERS> floors_;
	std::array<MinimapImportColorStats, 256> colorStats_;
	MinimapImportBounds bounds_;
	std::string description_;
	std::string sourcePath_;
	uint16_t version_ = 0;
	uint32_t flags_ = 0;
	uint64_t tileCount_ = 0;
	size_t blockCount_ = 0;
};

#endif
