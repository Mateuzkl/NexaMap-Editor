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

#include "main.h"
#include "light_drawer.h"
#include "gl_renderer.h"

LightDrawer::LightDrawer() :
	global_color(wxColor(50, 50, 50, 255)) {
	texture = 0;
}

LightDrawer::~LightDrawer() {
	unloadGLTexture();

	lights.clear();
}

void LightDrawer::draw(int map_x, int map_y, int end_x, int end_y, int scroll_x, int scroll_y, bool fog, GLRenderer* renderer) {
	if (texture == 0) {
		createGLTexture();
	}

	const int w = end_x - map_x;
	const int h = end_y - map_y;
	if (w <= 0 || h <= 0) {
		return;
	}

	const auto bufferSize = static_cast<size_t>(w) * static_cast<size_t>(h) * PixelFormatRGBA;
	if (buffer.size() != bufferSize) {
		buffer.resize(bufferSize);
	}

	light_grid.build(lights, map_x, map_y, end_x, end_y);

	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			const int mx = (map_x + x);
			const int my = (map_y + y);
			const int index = (y * w + x);
			const int color_index = index * PixelFormatRGBA;

			buffer[color_index] = global_color.Red();
			buffer[color_index + 1] = global_color.Green();
			buffer[color_index + 2] = global_color.Blue();
			buffer[color_index + 3] = 140; // global_color.Alpha();

			const auto& nearby = light_grid.getCell(mx, my);
			for (std::size_t li : nearby) {
				const Light& light = lights[li];
				const float intensity = calculateIntensity(mx, my, light);
				if (intensity == 0.f) {
					continue;
				}
				const wxColor light_color = colorFromEightBit(light.color);
				const auto red = static_cast<uint8_t>(light_color.Red() * intensity);
				const auto green = static_cast<uint8_t>(light_color.Green() * intensity);
				const auto blue = static_cast<uint8_t>(light_color.Blue() * intensity);
				buffer[color_index] = std::max(buffer[color_index], red);
				buffer[color_index + 1] = std::max(buffer[color_index + 1], green);
				buffer[color_index + 2] = std::max(buffer[color_index + 2], blue);
			}
		}
	}

	const int draw_x = map_x * TileSize - scroll_x;
	const int draw_y = map_y * TileSize - scroll_y;
	const int draw_width = w * TileSize;
	const int draw_height = h * TileSize;

	glBindTexture(GL_TEXTURE_2D, texture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
	if (texture_width != w || texture_height != h) {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());
		texture_width = w;
		texture_height = h;
	} else {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());
	}

	if (renderer) {
		renderer->flush();
	}

	if (!fog) {
		glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
	}

	if (renderer) {
		renderer->drawTexturedQuad(static_cast<float>(draw_x), static_cast<float>(draw_y), static_cast<float>(draw_width), static_cast<float>(draw_height), texture, { 255, 255, 255, 255 });
		renderer->flush();
	}

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	if (fog && renderer) {
		renderer->drawColoredQuad(static_cast<float>(draw_x), static_cast<float>(draw_y), static_cast<float>(draw_width), static_cast<float>(draw_height), { 10, 10, 10, 80 });
		renderer->flush();
	}
}

void LightDrawer::addLight(int map_x, int map_y, int map_z, const SpriteLight& light) {
	if (map_z <= GROUND_LAYER) {
		map_x -= (GROUND_LAYER - map_z);
		map_y -= (GROUND_LAYER - map_z);
	}

	if (map_x <= 0 || map_x >= MAP_MAX_WIDTH || map_y <= 0 || map_y >= MAP_MAX_HEIGHT) {
		return;
	}

	const uint8_t intensity = std::min(light.intensity, static_cast<uint8_t>(MaxLightIntensity));

	if (!lights.empty()) {
		Light& previous = lights.back();
		if (previous.map_x == map_x && previous.map_y == map_y && previous.color == light.color) {
			previous.intensity = std::max(previous.intensity, intensity);
			return;
		}
	}

	lights.push_back(Light { static_cast<uint16_t>(map_x), static_cast<uint16_t>(map_y), light.color, intensity });
}

void LightDrawer::clear() noexcept {
	lights.clear();
}

void LightDrawer::createGLTexture() {
	glGenTextures(1, &texture);
	ASSERT(texture != 0);
}

void LightDrawer::unloadGLTexture() {
	if (texture != 0) {
		GLRenderer::invalidateTexture(texture);
		glDeleteTextures(1, &texture);
		texture = 0;
		texture_width = 0;
		texture_height = 0;
	}
}

void LightDrawer::LightGrid::build(const std::vector<Light>& lights, int map_x, int map_y, int end_x, int end_y) {
	origin_x = map_x;
	origin_y = map_y;
	grid_w = (end_x - map_x + GRID_CELL_SIZE - 1) / GRID_CELL_SIZE + 2; // +2 for border cells
	grid_h = (end_y - map_y + GRID_CELL_SIZE - 1) / GRID_CELL_SIZE + 2;
	if (grid_w <= 0 || grid_h <= 0) {
		return;
	}
	cells.resize(static_cast<size_t>(grid_w) * grid_h);
	for (auto& cell : cells) {
		cell.clear();
	}
	for (std::size_t i = 0; i < lights.size(); ++i) {
		const Light& light = lights[i];
		int radius = light.intensity; // MaxLightIntensity is 8
		int min_gx = (light.map_x - radius - origin_x) / GRID_CELL_SIZE;
		int max_gx = (light.map_x + radius - origin_x) / GRID_CELL_SIZE;
		int min_gy = (light.map_y - radius - origin_y) / GRID_CELL_SIZE;
		int max_gy = (light.map_y + radius - origin_y) / GRID_CELL_SIZE;
		min_gx = std::max(0, min_gx);
		max_gx = std::min(grid_w - 1, max_gx);
		min_gy = std::max(0, min_gy);
		max_gy = std::min(grid_h - 1, max_gy);
		for (int gy = min_gy; gy <= max_gy; ++gy) {
			for (int gx = min_gx; gx <= max_gx; ++gx) {
				cells[static_cast<size_t>(gy) * grid_w + gx].push_back(i);
			}
		}
	}
}

static const std::vector<std::size_t> s_emptyCell;

const std::vector<std::size_t>& LightDrawer::LightGrid::getCell(int mx, int my) const {
	int gx = (mx - origin_x) / GRID_CELL_SIZE;
	int gy = (my - origin_y) / GRID_CELL_SIZE;
	if (gx < 0 || gx >= grid_w || gy < 0 || gy >= grid_h) {
		return s_emptyCell;
	}
	return cells[static_cast<size_t>(gy) * grid_w + gx];
}
