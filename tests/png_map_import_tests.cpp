#include "png_map_import.h"

#include <iostream>
#include <vector>

#define CHECK(...)                                                                                 \
	do {                                                                                           \
		if (!(__VA_ARGS__)) {                                                                      \
			std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__ << ": " << #__VA_ARGS__ \
					  << '\n';                                                                     \
			return 1;                                                                              \
		}                                                                                          \
	} while (false)

namespace {

	PngImportPixel Pixel(uint32_t rgb, uint8_t alpha = 255) {
		return {
			static_cast<uint8_t>((rgb >> 16) & 0xFF),
			static_cast<uint8_t>((rgb >> 8) & 0xFF),
			static_cast<uint8_t>(rgb & 0xFF),
			alpha,
		};
	}

	int TestColorStatsAndTransparency() {
		PngMapImportDocument document;
		std::string error;
		CHECK(document.setPixels(2, 2, { Pixel(0x112233), Pixel(0x112233), Pixel(0xAABBCC), Pixel(0xFFFFFF, 0) }, error));
		CHECK(document.getPixelCount() == 4);
		CHECK(document.getTransparentPixelCount() == 1);
		CHECK(document.getColors().size() == 2);
		CHECK(document.getColors()[0].rgb == 0x112233);
		CHECK(document.getColors()[0].count == 2);
		return 0;
	}

	int TestMappingAndTransparentGround() {
		PngMapImportDocument document;
		std::string error;
		CHECK(document.setPixels(2, 1, { Pixel(0xFF0000), Pixel(0x000000, 0) }, error));
		PngMapImportDocument::ColorMapping mappings { { 0xFF0000, 100 } };
		PngImportOptions options;
		options.offsetX = 10;
		options.offsetY = 20;
		options.floor = 6;
		options.transparentGroundId = 200;
		std::vector<PngImportTile> tiles;
		CHECK(document.forEachMappedTile(
			mappings, options, [&tiles](const PngImportTile& tile, uint64_t, uint64_t) {
				tiles.push_back(tile);
				return true;
			},
			error
		));
		CHECK(tiles.size() == 2);
		CHECK(tiles[0].x == 10 && tiles[0].y == 20 && tiles[0].z == 6 && tiles[0].groundId == 100);
		CHECK(tiles[1].x == 11 && tiles[1].groundId == 200 && tiles[1].transparent);
		uint64_t mappedTiles = 0;
		CHECK(document.countMappedTiles(mappings, options, mappedTiles, error));
		CHECK(mappedTiles == 2);
		return 0;
	}

	int TestRotationScaleAndFlip() {
		PngMapImportDocument document;
		std::string error;
		CHECK(document.setPixels(2, 3, {
										   Pixel(0x000001),
										   Pixel(0x000002),
										   Pixel(0x000003),
										   Pixel(0x000004),
										   Pixel(0x000005),
										   Pixel(0x000006),
									   },
								 error));
		PngMapImportDocument::ColorMapping mappings;
		for (uint16_t id = 1; id <= 6; ++id) {
			mappings[id] = id;
		}
		PngImportOptions options;
		options.rotation = 90;
		options.flipHorizontal = true;
		CHECK(document.getOutputSize(options) == std::make_pair(3, 2));
		std::vector<PngImportTile> tiles;
		CHECK(document.forEachMappedTile(
			mappings, options, [&tiles](const PngImportTile& tile, uint64_t, uint64_t) {
				tiles.push_back(tile);
				return true;
			},
			error
		));
		CHECK(tiles.size() == 6);
		CHECK(tiles[0].groundId == 5 && tiles[0].x == 2 && tiles[0].y == 0);
		CHECK(tiles[2].groundId == 1 && tiles[2].x == 0 && tiles[2].y == 0);
		CHECK(tiles[3].groundId == 6 && tiles[3].x == 2 && tiles[3].y == 1);

		options = {};
		options.scalePercent = 50;
		CHECK(document.getOutputSize(options) == std::make_pair(1, 1));
		return 0;
	}

	int TestQuantizationAndValidation() {
		PngMapImportDocument document;
		std::string error;
		CHECK(document.setPixels(2, 1, { Pixel(0x101010), Pixel(0x202020) }, error));
		CHECK(!document.hasSimplifiedColors());
		CHECK(document.quantizeColors(2, error));
		CHECK(document.hasSimplifiedColors());
		CHECK(document.getColors().size() == 1);
		document.restoreOriginalColors();
		CHECK(!document.hasSimplifiedColors());
		CHECK(document.getColors().size() == 2);

		PngImportOptions options;
		options.rotation = 45;
		CHECK(!document.forEachMappedTile({}, options, {}, error));
		CHECK(!error.empty());
		uint64_t mappedTiles = 123;
		CHECK(!document.countMappedTiles({}, options, mappedTiles, error));
		CHECK(mappedTiles == 0);
		CHECK(!error.empty());
		options.rotation = 0;
		options.offsetX = 64'999;
		CHECK(!document.forEachMappedTile({}, options, {}, error));
		CHECK(error.find("65000") != std::string::npos);
		return 0;
	}

} // namespace

int main() {
	if (TestColorStatsAndTransparency() != 0 || TestMappingAndTransparentGround() != 0 || TestRotationScaleAndFlip() != 0 || TestQuantizationAndValidation() != 0) {
		return 1;
	}
	std::cout << "png_map_import_tests passed\n";
	return 0;
}
