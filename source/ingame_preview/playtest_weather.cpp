// SPDX-License-Identifier: GPL-3.0-or-later
#include "playtest_weather.h"
#include <algorithm>
#include <cmath>
namespace Playtest {
	WeatherFrame BuildWeather(Weather weather, float width, float height, double seconds, float scale) {
		WeatherFrame frame {};
		if (weather == Weather::Off || width <= 0 || height <= 0 || !std::isfinite(seconds)) {
			return frame;
		}
		scale = std::clamp(scale, 0.5f, 4.0f);
		const float t = static_cast<float>(std::fmod(std::max(0.0, seconds), 3600.0));
		auto quad = [&](float x, float y, float w, float h, GLColor color) { frame.quads[frame.count++] = { x, y, w, h, color }; };
		if (weather == Weather::Fog) {
			quad(0, 0, width, height, { 172, 190, 203, 38 });
			for (int i = 0; i < 6; ++i) {
				const float x = std::fmod(i * width / 3 + t * 9 * scale, width * 1.6f) - width * 0.6f;
				quad(x, 0, width * 0.6f, height, { 195, 211, 218, 12 });
			}
		} else if (weather == Weather::DesertHeat) {
			quad(0, 0, width, height, { 232, 173, 87, static_cast<uint8_t>(18 + 7 * (1 + std::sin(t * 0.7f))) });
			for (int i = 0; i < 10; ++i) {
				const float y = std::fmod(i * height / 10 + t * 5 * scale, height);
				quad(0, y, width, 2 * scale, { 255, 218, 139, 12 });
			}
		} else if (weather == Weather::Rain || weather == Weather::Storm || weather == Weather::Snow) {
			const bool snow = weather == Weather::Snow;
			const bool storm = weather == Weather::Storm;
			if (storm) {
				quad(0, 0, width, height, { 14, 24, 46, 72 });
			}
			const int particles = storm ? 96 : 64;
			for (int i = 0; i < particles; ++i) {
				const float vx = snow ? 11.0f : 37.0f;
				const float vy = snow ? 29.0f : (storm ? 330.0f : 230.0f);
				float x = std::fmod((i * 193 % 997) / 997.0f * width + t * vx * scale, width);
				const float y = std::fmod((i * 317 % 991) / 991.0f * height + t * vy * scale, height);
				if (snow) {
					x += std::sin(t + i) * 4 * scale;
				}
				const float size = (1.0f + (i % 3) * 0.5f) * scale;
				quad(x, y, snow ? size : std::max(1.0f, scale * 1.3f), snow ? size : (storm ? 15 : 10) * scale, snow ? GLColor { 235, 245, 252, 195 } : GLColor { 178, 217, 246, 160 });
			}
			// A short, soft lightning pulse every twelve seconds, with no rapid
			// strobing. The bolt is geometry in the same bounded weather batch.
			const float phase = std::fmod(t, 12.0f) - 6.0f;
			if (storm && phase >= 0 && phase < 0.45f) {
				const float intensity = std::sin(phase / 0.45f * 3.14159265f);
				quad(0, 0, width, height, { 192, 214, 255, static_cast<uint8_t>(28 * intensity) });
				const int cycle = static_cast<int>(t / 12);
				float x = width * (0.2f + (cycle * 37 % 60) / 100.0f);
				const float segment = height * 0.045f;
				for (int i = 0; i < 12; ++i) {
					const float nextX = x + ((i % 3 == 1) ? -6 : 4) * scale;
					quad(std::min(x, nextX), i * segment, std::abs(nextX - x) + 2 * scale, segment + 1, { 216, 234, 255, static_cast<uint8_t>(215 * intensity) });
					x = nextX;
				}
			}
		}
		return frame;
	}
}
