#include "png_map_import.h"

#include <algorithm>

namespace {

	constexpr uint64_t maxPixels = 25'000'000;
	constexpr int maxMapDimension = 65'000;

	int ScaledDimension(int value, int scalePercent) {
		return static_cast<int>(std::max<int64_t>(1, static_cast<int64_t>(value) * scalePercent / 100));
	}

	uint8_t QuantizeChannel(uint8_t value, uint8_t levels) {
		const int index = (static_cast<int>(value) * (levels - 1) + 127) / 255;
		return static_cast<uint8_t>((index * 255 + (levels - 1) / 2) / (levels - 1));
	}

} // namespace

bool PngMapImportDocument::setPixels(int width, int height, std::vector<PngImportPixel> pixels, std::string& error) {
	error.clear();
	if (width <= 0 || height <= 0) {
		error = "PNG dimensions must be positive.";
		return false;
	}
	const uint64_t pixelCount = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
	if (pixelCount > maxPixels) {
		error = "PNG is too large. The native importer accepts at most 25,000,000 pixels.";
		return false;
	}
	if (pixels.size() != pixelCount) {
		error = "PNG pixel buffer does not match its dimensions.";
		return false;
	}
	width_ = width;
	height_ = height;
	originalPixels_ = std::move(pixels);
	pixels_ = originalPixels_;
	colorsSimplified_ = false;
	rebuildColorStats();
	return true;
}

bool PngMapImportDocument::quantizeColors(uint8_t levelsPerChannel, std::string& error) {
	error.clear();
	if (levelsPerChannel < 2 || levelsPerChannel > 16) {
		error = "Color simplification must use between 2 and 16 levels per channel.";
		return false;
	}
	if (originalPixels_.empty()) {
		error = "No PNG is loaded.";
		return false;
	}
	pixels_ = originalPixels_;
	colorsSimplified_ = true;
	for (PngImportPixel& pixel : pixels_) {
		if (pixel.alpha < 128) {
			continue;
		}
		pixel.red = QuantizeChannel(pixel.red, levelsPerChannel);
		pixel.green = QuantizeChannel(pixel.green, levelsPerChannel);
		pixel.blue = QuantizeChannel(pixel.blue, levelsPerChannel);
	}
	rebuildColorStats();
	return true;
}

void PngMapImportDocument::restoreOriginalColors() {
	pixels_ = originalPixels_;
	colorsSimplified_ = false;
	rebuildColorStats();
}

int PngMapImportDocument::getWidth() const {
	return width_;
}

int PngMapImportDocument::getHeight() const {
	return height_;
}

uint64_t PngMapImportDocument::getPixelCount() const {
	return pixels_.size();
}

uint64_t PngMapImportDocument::getTransparentPixelCount() const {
	return transparentPixelCount_;
}

bool PngMapImportDocument::hasSimplifiedColors() const {
	return colorsSimplified_;
}

const std::vector<PngImportPixel>& PngMapImportDocument::getPixels() const {
	return pixels_;
}

const std::vector<PngImportColor>& PngMapImportDocument::getColors() const {
	return colors_;
}

std::pair<int, int> PngMapImportDocument::getOutputSize(const PngImportOptions& options) const {
	const int scaledWidth = ScaledDimension(width_, options.scalePercent);
	const int scaledHeight = ScaledDimension(height_, options.scalePercent);
	if (options.rotation == 90 || options.rotation == 270) {
		return { scaledHeight, scaledWidth };
	}
	return { scaledWidth, scaledHeight };
}

bool PngMapImportDocument::countMappedTiles(const ColorMapping& mappings, const PngImportOptions& options, uint64_t& count, std::string& error) const {
	count = 0;
	return forEachMappedTile(
		mappings, options, [&count](const PngImportTile&, uint64_t, uint64_t) {
			++count;
			return true;
		},
		error
	);
}

bool PngMapImportDocument::forEachMappedTile(const ColorMapping& mappings, const PngImportOptions& options, const TileCallback& callback, std::string& error) const {
	if (!validateOptions(options, error)) {
		return false;
	}
	const int scaledWidth = ScaledDimension(width_, options.scalePercent);
	const int scaledHeight = ScaledDimension(height_, options.scalePercent);
	const auto [outputWidth, outputHeight] = getOutputSize(options);
	const uint64_t total = static_cast<uint64_t>(outputWidth) * outputHeight;
	uint64_t processed = 0;
	for (int outputY = 0; outputY < outputHeight; ++outputY) {
		for (int outputX = 0; outputX < outputWidth; ++outputX) {
			int logicalX = outputX;
			int logicalY = outputY;
			switch (options.rotation) {
				case 90:
					logicalX = outputY;
					logicalY = scaledHeight - 1 - outputX;
					break;
				case 180:
					logicalX = scaledWidth - 1 - outputX;
					logicalY = scaledHeight - 1 - outputY;
					break;
				case 270:
					logicalX = scaledWidth - 1 - outputY;
					logicalY = outputX;
					break;
				default:
					break;
			}
			const int sourceX = std::min(width_ - 1, logicalX * width_ / scaledWidth);
			const int sourceY = std::min(height_ - 1, logicalY * height_ / scaledHeight);
			const PngImportPixel& pixel = pixels_[static_cast<size_t>(sourceY) * width_ + sourceX];
			uint16_t groundId = 0;
			const bool transparent = pixel.alpha < 128;
			if (transparent) {
				groundId = options.transparentGroundId;
			} else if (const auto found = mappings.find(pixel.rgb()); found != mappings.end()) {
				groundId = found->second;
			}
			++processed;
			if (groundId == 0) {
				continue;
			}
			PngImportTile tile;
			tile.x = options.offsetX + (options.flipHorizontal ? outputWidth - 1 - outputX : outputX);
			tile.y = options.offsetY + (options.flipVertical ? outputHeight - 1 - outputY : outputY);
			tile.z = options.floor;
			tile.groundId = groundId;
			tile.sourceRgb = pixel.rgb();
			tile.transparent = transparent;
			if (callback && !callback(tile, processed, total)) {
				error = "PNG import was cancelled.";
				return false;
			}
		}
	}
	return true;
}

bool PngMapImportDocument::validateOptions(const PngImportOptions& options, std::string& error) const {
	error.clear();
	if (pixels_.empty()) {
		error = "No PNG is loaded.";
		return false;
	}
	if (options.floor < 0 || options.floor > 15) {
		error = "Floor must be between 0 and 15.";
		return false;
	}
	if (options.scalePercent < 1 || options.scalePercent > 100) {
		error = "Scale must be between 1% and 100%.";
		return false;
	}
	if (options.rotation != 0 && options.rotation != 90 && options.rotation != 180 && options.rotation != 270) {
		error = "Rotation must be 0, 90, 180 or 270 degrees.";
		return false;
	}
	if (options.offsetX < 0 || options.offsetY < 0) {
		error = "Map offsets cannot be negative.";
		return false;
	}
	const auto [outputWidth, outputHeight] = getOutputSize(options);
	if (static_cast<int64_t>(options.offsetX) + outputWidth > maxMapDimension || static_cast<int64_t>(options.offsetY) + outputHeight > maxMapDimension) {
		error = "PNG output exceeds the editor's maximum map size (65000).";
		return false;
	}
	return true;
}

void PngMapImportDocument::rebuildColorStats() {
	std::unordered_map<uint32_t, uint64_t> counts;
	transparentPixelCount_ = 0;
	for (const PngImportPixel& pixel : pixels_) {
		if (pixel.alpha < 128) {
			++transparentPixelCount_;
		} else {
			++counts[pixel.rgb()];
		}
	}
	colors_.clear();
	colors_.reserve(counts.size());
	for (const auto& [rgb, count] : counts) {
		colors_.push_back({ rgb, count });
	}
	std::sort(colors_.begin(), colors_.end(), [](const PngImportColor& left, const PngImportColor& right) {
		return left.count != right.count ? left.count > right.count : left.rgb < right.rgb;
	});
}
