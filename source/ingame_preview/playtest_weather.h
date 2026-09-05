// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_PLAYTEST_WEATHER_H_
#define NEXAMAP_PLAYTEST_WEATHER_H_
#include <array>
#include <cstddef>
#include "../gl_renderer.h"
namespace Playtest {
	enum class Weather { Off,
						 Rain,
						 Storm,
						 Fog,
						 Snow,
						 DesertHeat };
	struct WeatherQuad {
		float x, y, w, h;
		GLColor color;
	};
	struct WeatherFrame {
		// Fixed work and storage, independent of map size. No random frame jitter.
		std::array<WeatherQuad, 128> quads;
		size_t count = 0;
	};
	WeatherFrame BuildWeather(Weather weather, float width, float height, double seconds, float scale);
	inline void DrawWeather(GLRenderer& renderer, const WeatherFrame& frame) {
		for (size_t i = 0; i < frame.count; ++i) {
			const auto& q = frame.quads[i];
			renderer.drawColoredQuad(q.x, q.y, q.w, q.h, q.color);
		}
	}
}
#endif
