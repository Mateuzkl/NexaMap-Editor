#include "png_map_import.h"

#include <cassert>
#include <iostream>
#include <vector>

namespace {

	PngImportPixel Pixel(uint32_t rgb, uint8_t alpha = 255) {
		return {
			static_cast<uint8_t>((rgb >> 16) & 0xFF),
			static_cast<uint8_t>((rgb >> 8) & 0xFF),
			static_cast<uint8_t>(rgb & 0xFF),
			alpha,
		};
	}

	void TestColorStatsAndTransparency() {
		PngMapImportDocument document;
		std::string error;
		assert(document.setPixels(2, 2, { Pixel(0x112233), Pixel(0x112233), Pixel(0xAABBCC), Pixel(0xFFFFFF, 0) }, error));
		assert(document.getPixelCount() == 4);
		assert(document.getTransparentPixelCount() == 1);
		assert(document.getColors().size() == 2);
		assert(document.getColors()[0].rgb == 0x112233);
		assert(document.getColors()[0].count == 2);
	}

	void TestMappingAndTransparentGround() {
		PngMapImportDocument document;
		std::string error;
		assert(document.setPixels(2, 1, { Pixel(0xFF0000), Pixel(0x000000, 0) }, error));
		PngMapImportDocument::ColorMapping mappings { { 0xFF0000, 100 } };
		PngImportOptions options;
		options.offsetX = 10;
		options.offsetY = 20;
		options.floor = 6;
		options.transparentGroundId = 200;
		std::vector<PngImportTile> tiles;
		assert(document.forEachMappedTile(
			mappings, options, [&tiles](const PngImportTile& tile, uint64_t, uint64_t) {
				tiles.push_back(tile);
				return true;
			},
			error
		));
		assert(tiles.size() == 2);
		assert(tiles[0].x == 10 && tiles[0].y == 20 && tiles[0].z == 6 && tiles[0].groundId == 100);
		assert(tiles[1].x == 11 && tiles[1].groundId == 200 && tiles[1].transparent);
	}

	void TestRotationScaleAndFlip() {
		PngMapImportDocument document;
		std::string error;
		assert(document.setPixels(2, 3, {
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
		assert(document.getOutputSize(options) == std::make_pair(3, 2));
		std::vector<PngImportTile> tiles;
		assert(document.forEachMappedTile(
			mappings, options, [&tiles](const PngImportTile& tile, uint64_t, uint64_t) {
				tiles.push_back(tile);
				return true;
			},
			error
		));
		assert(tiles.size() == 6);
		assert(tiles[0].groundId == 5 && tiles[0].x == 2 && tiles[0].y == 0);
		assert(tiles[2].groundId == 1 && tiles[2].x == 0 && tiles[2].y == 0);
		assert(tiles[3].groundId == 6 && tiles[3].x == 2 && tiles[3].y == 1);

		options = {};
		options.scalePercent = 50;
		assert(document.getOutputSize(options) == std::make_pair(1, 1));
	}

	void TestQuantizationAndValidation() {
		PngMapImportDocument document;
		std::string error;
		assert(document.setPixels(2, 1, { Pixel(0x101010), Pixel(0x202020) }, error));
		assert(!document.hasSimplifiedColors());
		assert(document.quantizeColors(2, error));
		assert(document.hasSimplifiedColors());
		assert(document.getColors().size() == 1);
		document.restoreOriginalColors();
		assert(!document.hasSimplifiedColors());
		assert(document.getColors().size() == 2);

		PngImportOptions options;
		options.rotation = 45;
		assert(!document.forEachMappedTile({}, options, {}, error));
		assert(!error.empty());
		options.rotation = 0;
		options.offsetX = 64'999;
		assert(!document.forEachMappedTile({}, options, {}, error));
		assert(error.find("65000") != std::string::npos);
	}

} // namespace

int main() {
	TestColorStatsAndTransparency();
	TestMappingAndTransparentGround();
	TestRotationScaleAndFlip();
	TestQuantizationAndValidation();
	std::cout << "png_map_import_tests passed\n";
	return 0;
}
