#ifndef RME_PNG_MAP_IMPORT_H_
#define RME_PNG_MAP_IMPORT_H_

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct PngImportPixel {
	uint8_t red = 0;
	uint8_t green = 0;
	uint8_t blue = 0;
	uint8_t alpha = 255;

	uint32_t rgb() const {
		return (static_cast<uint32_t>(red) << 16) | (static_cast<uint32_t>(green) << 8) | blue;
	}
};

struct PngImportColor {
	uint32_t rgb = 0;
	uint64_t count = 0;
};

struct PngImportOptions {
	int offsetX = 0;
	int offsetY = 0;
	int floor = 7;
	int scalePercent = 100;
	int rotation = 0;
	bool flipHorizontal = false;
	bool flipVertical = false;
	uint16_t transparentGroundId = 0;
};

struct PngImportTile {
	int x = 0;
	int y = 0;
	int z = 7;
	uint16_t groundId = 0;
	uint32_t sourceRgb = 0;
	bool transparent = false;
};

class PngMapImportDocument {
public:
	using ColorMapping = std::unordered_map<uint32_t, uint16_t>;
	using TileCallback = std::function<bool(const PngImportTile&, uint64_t processed, uint64_t total)>;

	bool setPixels(int width, int height, std::vector<PngImportPixel> pixels, std::string& error);
	bool quantizeColors(uint8_t levelsPerChannel, std::string& error);
	void restoreOriginalColors();

	int getWidth() const;
	int getHeight() const;
	uint64_t getPixelCount() const;
	uint64_t getTransparentPixelCount() const;
	bool hasSimplifiedColors() const;
	const std::vector<PngImportPixel>& getPixels() const;
	const std::vector<PngImportColor>& getColors() const;

	std::pair<int, int> getOutputSize(const PngImportOptions& options) const;
	bool countMappedTiles(const ColorMapping& mappings, const PngImportOptions& options, uint64_t& count, std::string& error) const;
	bool forEachMappedTile(const ColorMapping& mappings, const PngImportOptions& options, const TileCallback& callback, std::string& error) const;

private:
	bool validateOptions(const PngImportOptions& options, std::string& error) const;
	void rebuildColorStats();

	int width_ = 0;
	int height_ = 0;
	uint64_t transparentPixelCount_ = 0;
	bool colorsSimplified_ = false;
	std::vector<PngImportPixel> originalPixels_;
	std::vector<PngImportPixel> pixels_;
	std::vector<PngImportColor> colors_;
};

#endif // RME_PNG_MAP_IMPORT_H_
