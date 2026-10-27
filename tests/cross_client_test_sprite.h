#pragma once

#include "graphics.h"
#include <algorithm>
#include <array>
#include <memory>

// CPU-only fixture: every pattern/frame has real, independently editable pixels.
class CrossClientTestSprite final : public GameSprite {
	class Pixels final : public NormalImage {
	public:
		std::array<uint8_t, SPRITE_PIXELS_SIZE * 4> rgba {};
		uint8_t* getRGBAData(bool* pending) override {
			if (pending) {
				*pending = false;
			}
			auto* result = new uint8_t[rgba.size()];
			std::copy(rgba.begin(), rgba.end(), result);
			return result;
		}
	};
	std::vector<std::unique_ptr<Pixels>> images;

public:
	explicit CrossClientTestSprite(uint8_t colour, uint8_t patterns = 1, uint8_t frameCount = 1) {
		width = height = layers = pattern_y = pattern_z = 1;
		pattern_x = patterns;
		frames = frameCount;
		for (size_t i = 0; i < size_t(patterns) * frameCount; ++i) {
			auto image = std::make_unique<Pixels>();
			for (size_t pixel = 0; pixel < SPRITE_PIXELS_SIZE; ++pixel) {
				image->rgba[pixel * 4] = colour;
				image->rgba[pixel * 4 + 1] = static_cast<uint8_t>(i);
				image->rgba[pixel * 4 + 3] = 255;
			}
			spriteList.push_back(image.get());
			images.push_back(std::move(image));
		}
		numsprites = static_cast<uint32_t>(spriteList.size());
	}
	void change(size_t image, size_t pixel, uint8_t value) {
		images.at(image)->rgba.at(pixel * 4) = value;
	}
};
