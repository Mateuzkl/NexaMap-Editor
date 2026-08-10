//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#ifndef RME_LIGHDRAWER_H
#define RME_LIGHDRAWER_H

#include "graphics.h"
#include "position.h"
#include <cstddef>
#include <cstdint>

class GLRenderer;

class LightDrawer {
	struct Light {
		uint16_t map_x = 0;
		uint16_t map_y = 0;
		uint8_t color = 0;
		uint8_t intensity = 0;
	};

public:
	LightDrawer();
	virtual ~LightDrawer();

	void draw(int map_x, int map_y, int end_x, int end_y, int scroll_x, int scroll_y, bool fog, GLRenderer* renderer);

	void addLight(int map_x, int map_y, int map_z, const SpriteLight& light);
	void clear() noexcept;

private:
	void createGLTexture();
	void unloadGLTexture();

	static inline float calculateIntensity(int map_x, int map_y, const Light& light) {
		const int dx = map_x - light.map_x;
		const int dy = map_y - light.map_y;
		const float distance = std::sqrt(dx * dx + dy * dy);
		if (distance > MaxLightIntensity) {
			return 0.f;
		}
		const float intensity = (-distance + light.intensity) * 0.2f;
		if (intensity < 0.01f) {
			return 0.f;
		}
		return std::min(intensity, 1.f);
	}

	GLuint texture;
	int texture_width = 0;
	int texture_height = 0;
	std::vector<Light> lights;
	// Spatial grid for fast light lookup
	static constexpr int GRID_CELL_SIZE = 8; // MaxLightIntensity
	struct LightGrid {
		std::vector<std::vector<std::size_t>> cells; // indices into lights vector
		int grid_w = 0, grid_h = 0;
		int origin_x = 0, origin_y = 0;
		void build(const std::vector<Light>& lights, int map_x, int map_y, int end_x, int end_y);
		const std::vector<std::size_t>& getCell(int mx, int my) const;
	};
	LightGrid light_grid;
	std::vector<uint8_t> buffer;
	wxColor global_color;
};

#endif
