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
#include "profiling.h"

#include "bitmap_font.h"
#include "theme.h"

#ifdef __WINDOWS__
	#include <windows.h>
	#include <psapi.h>
	#pragma comment(lib, "psapi.lib")
#else
	#include <unistd.h>
	#include <cstring>
	#include <fstream>
#endif

#include <thread>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <iterator>

#include "editor.h"
#include "autoborder_preview.h"
#include "gui.h"
#include "sprites.h"
#include "map_drawer.h"
#include "minimap_import.h"
#include "map_display.h"
#include "copybuffer.h"
#include "graphics.h"
#include "sprite_appearances.h"

#include "creature_brush.h"
#include "house_exit_brush.h"
#include "house_brush.h"
#include "raw_brush.h"
#include "light_drawer.h"

using Color = std::tuple<int, int, int>;

namespace {
	struct PreparedSpritePart {
		int screen_x;
		int screen_y;
		GameSprite::SpriteTex texture;
	};

	template <typename Loader>
	bool AppendPreparedSpriteParts(int screenX, int screenY, int width, int height, int layers, Loader loader, std::vector<PreparedSpritePart>& parts) {
		parts.reserve(parts.size() + static_cast<size_t>(width) * height * layers);
		bool complete = true;
		for (int cx = 0; cx != width; ++cx) {
			for (int cy = 0; cy != height; ++cy) {
				for (int layer = 0; layer != layers; ++layer) {
					auto texture = loader(cx, cy, layer);
					complete = texture.texture != 0 && complete;
					parts.push_back({ screenX - cx * TileSize, screenY - cy * TileSize, texture });
				}
			}
		}
		return complete;
	}
}

static std::vector<Color> colors;
void GenerateColors() {
	if (!colors.empty()) {
		return;
	}

	colors.reserve(32);
	int r = 250, g = 100, b = 100;
	const int step = 25;
	bool incrementing = true;

	while (true) {
		if (std::find(colors.begin(), colors.end(), Color({ r, g, b })) == colors.end()) {
			colors.push_back({ r, g, b });
		}

		if (g < 250 && incrementing) {
			g += step;
		} else if (r > 100 && !incrementing && g == 250) {
			r -= step;
		} else if (b < 250 && r == 100) {
			b += step;
		} else if (g > 100 && b == 250) {
			g -= step;
		} else if (r < 250 && g == 100) {
			r += step;
		} else if (b > 100 && r == 250) {
			b -= step;
		} else if (b == 100 && g == 250) {
			incrementing = false;
		}

		if (r == 250 && g == 100 && b == 100 && !incrementing) {
			break;
		}
	}
}

static Color GetZoneColor(const Tile& tile, unsigned int activeZoneId) {
	if (activeZoneId != 0 && tile.hasZone(activeZoneId)) {
		return colors.at(activeZoneId % colors.size());
	}

	uint32_t red = 0;
	uint32_t green = 0;
	uint32_t blue = 0;
	for (const unsigned int zoneId : tile.zones) {
		const Color& colour = colors.at(zoneId % colors.size());
		red += std::get<0>(colour);
		green += std::get<1>(colour);
		blue += std::get<2>(colour);
	}

	const size_t zoneCount = tile.zones.size();
	return {
		static_cast<int>(red / zoneCount),
		static_cast<int>(green / zoneCount),
		static_cast<int>(blue / zoneCount),
	};
}

DrawingOptions::DrawingOptions() {
	SetDefault();
	GenerateColors();
}

void DrawingOptions::SetDefault() {
	transparent_floors = false;
	transparent_items = false;
	show_ingame_box = false;
	show_lights = false;
	show_light_str = true;
	show_tech_items = true;
	show_waypoints = true;
	ingame = false;
	dragging = false;

	show_grid = 0;
	show_all_floors = true;
	show_creatures = true;
	show_spawns = true;
	show_houses = true;
	show_shade = true;
	show_special_tiles = true;
	show_zone_areas = true;
	show_items = true;
	active_zone_id = 0;

	highlight_items = false;
	highlight_locked_doors = true;
	show_blocking = false;
	show_tooltips = false;
	show_performance_stats = false;
	show_as_minimap = false;
	show_only_colors = false;
	show_only_modified = false;
	show_preview = false;
	show_hooks = false;
	hide_items_when_zoomed = true;
	post_process_effect = 0;
}

void DrawingOptions::SetIngame() {
	transparent_floors = false;
	transparent_items = false;
	show_ingame_box = false;
	show_lights = false;
	show_light_str = false;
	show_tech_items = false;
	show_waypoints = false;
	ingame = true;
	dragging = false;

	show_grid = 0;
	show_all_floors = true;
	show_creatures = true;
	show_spawns = false;
	show_houses = false;
	show_shade = false;
	show_special_tiles = false;
	show_zone_areas = false;
	show_items = true;
	active_zone_id = 0;

	highlight_items = false;
	highlight_locked_doors = false;
	show_blocking = false;
	show_tooltips = false;
	show_performance_stats = false;
	show_as_minimap = false;
	show_only_colors = false;
	show_only_modified = false;
	show_preview = false;
	show_hooks = false;
	hide_items_when_zoomed = false;
	post_process_effect = 0;
}

bool DrawingOptions::isDrawLight() const noexcept {
	return show_lights;
}

bool DrawingOptions::isOnlyColors() const noexcept {
	return show_as_minimap || show_only_colors;
}

bool DrawingOptions::isTooltips() const noexcept {
	return show_tooltips && !isOnlyColors();
}

MapDrawer::MapDrawer(MapCanvas* canvas) :
	canvas(canvas),
	editor(canvas->editor)
#ifdef __WINDOWS__
	,
	last_cpu_time {},
	last_sys_time {},
	last_now_time {}
#endif
{
	light_drawer = std::make_shared<LightDrawer>();
	perf_update_timer.Start();
}

MapDrawer::~MapDrawer() {
	Release();
	minimap_page_cache.releaseGL();
}

void MapDrawer::SetupVars() {
	canvas->MouseToMap(&mouse_map_x, &mouse_map_y);
	canvas->GetViewBox(&view_scroll_x, &view_scroll_y, &screensize_x, &screensize_y);

	dragging = canvas->dragging;
	dragging_draw = canvas->dragging_draw;

	zoom = (float)canvas->GetZoom();
	tile_size = int(TileSize / zoom); // after zoom
	floor = canvas->GetFloor();

	if (options.show_all_floors) {
		if (floor <= GROUND_LAYER) {
			start_z = GROUND_LAYER;
		} else {
			start_z = std::min(MAP_MAX_LAYER, floor + 2);
		}
	} else {
		start_z = floor;
	}

	end_z = floor;
	superend_z = (floor > GROUND_LAYER ? 8 : 0);

	start_x = view_scroll_x / TileSize;
	start_y = view_scroll_y / TileSize;

	if (floor > GROUND_LAYER) {
		start_x -= 2;
		start_y -= 2;
	}

	end_x = start_x + screensize_x / tile_size + 2;
	end_y = start_y + screensize_y / tile_size + 2;

	medium_zoom_mode = zoom > 3.0f && zoom < FAR_ZOOM_THRESHOLD;
	far_zoom_mode = zoom >= FAR_ZOOM_THRESHOLD;
}

void MapDrawer::SetupGL() {
	glViewport(0, 0, screensize_x, screensize_y);

	// Enable 2D mode
	int vPort[4];

	glGetIntegerv(GL_VIEWPORT, vPort);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, vPort[2] * zoom, vPort[3] * zoom, 0, -1, 1);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glTranslatef(0.375f, 0.375f, 0.0f);

	renderer->init();
	renderer->beginFrame();
	renderer->setOrtho(0.0f, static_cast<float>(vPort[2]) * zoom, static_cast<float>(vPort[3]) * zoom, 0.0f);
}

void MapDrawer::Release() {
	renderer->endFrame();

	tooltips.clear();

	if (light_drawer) {
		light_drawer->clear();
	}

	// Disable 2D mode
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}

void MapDrawer::DrawScene() {
	const auto started = std::chrono::steady_clock::now();
	DrawBackground();
	DrawMap();
	if (canvas->IsIngamePreview()) {
		DrawIngamePreviewPlayer();
	}
	if (options.isDrawLight() && !far_zoom_mode) {
		DrawLight();
	}
	if (!far_zoom_mode) {
		DrawHigherFloors();
	}
	last_scene_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}

void MapDrawer::DrawOverlays() {
	DrawMinimapImportOverlay();
	if (!far_zoom_mode) {
		DrawDraggingShadow();
	}
	if (options.dragging) {
		DrawSelectionBox();
	}
	DrawBrush();
	if (options.show_grid && !medium_zoom_mode && !far_zoom_mode) {
		DrawGrid();
	}
	if (options.show_ingame_box) {
		DrawIngameBox();
	}
	if (options.show_tooltips && !medium_zoom_mode && !far_zoom_mode && !isViewportInteractionActive()) {
		DrawTooltips();
	}
	for (int map_z = start_z; map_z >= superend_z; --map_z) {
		DrawPositionIndicator(map_z);
	}
	if (options.show_performance_stats) {
		DrawPerformanceStats();
	}
}

void MapDrawer::DrawMinimapImportOverlay() {
	if (!canvas->minimap_import_overlay) {
		return;
	}
	const auto& document = *canvas->minimap_import_overlay;
	const auto& bounds = document.getFloorInfo(floor).bounds;
	if (!bounds.valid()) {
		return;
	}
	const int firstY = std::max(start_y, bounds.minY);
	const int lastY = std::min(end_y, bounds.maxY);
	const int firstX = std::max(start_x, bounds.minX);
	const int lastX = std::min(end_x, bounds.maxX);
	for (int y = firstY; y <= lastY; ++y) {
		for (int x = firstX; x <= lastX;) {
			const MinimapTile* tile = document.getTile(x, y, floor);
			if (!tile) {
				++x;
				continue;
			}
			const uint8_t color = tile->color;
			int runEnd = x + 1;
			while (runEnd <= lastX) {
				const MinimapTile* next = document.getTile(runEnd, y, floor);
				if (!next || next->color != color) {
					break;
				}
				++runEnd;
			}
			const uint8_t red = static_cast<uint8_t>((color / 36) % 6 * 51);
			const uint8_t green = static_cast<uint8_t>((color / 6) % 6 * 51);
			const uint8_t blue = static_cast<uint8_t>(color % 6 * 51);
			drawFilledRect(x * TileSize - view_scroll_x, y * TileSize - view_scroll_y, (runEnd - x) * TileSize, TileSize, wxColor(red, green, blue, canvas->minimap_import_overlay_opacity));
			x = runEnd;
		}
	}
}

void MapDrawer::markDirty() {
	scene_dirty = true;
}

void MapDrawer::invalidateMinimapPages() {
	minimap_page_cache.invalidateAll();
}

bool MapDrawer::isSceneDirty() {
	if (!input_view_initialized) {
		input_view_initialized = true;
		last_input_zoom = zoom;
		last_input_scroll_x = view_scroll_x;
		last_input_scroll_y = view_scroll_y;
	} else if (last_input_zoom != zoom || last_input_scroll_x != view_scroll_x || last_input_scroll_y != view_scroll_y) {
		last_input_zoom = zoom;
		last_input_scroll_x = view_scroll_x;
		last_input_scroll_y = view_scroll_y;
		viewport_settle_timer.Start();
		viewport_settle_pending = true;
	}

	const bool viewport_changed = cached_scene_zoom != zoom
		|| cached_scroll_x != view_scroll_x
		|| cached_scroll_y != view_scroll_y;
	const bool hard_invalidation = !cached_scene_initialized
		|| cached_floor != floor
		|| cached_start_z != start_z
		|| cached_screensize_x != screensize_x
		|| cached_screensize_y != screensize_y
		|| viewport_changed;
	if (hard_invalidation) {
		viewport_settle_pending = false;
		return true;
	}

	if (isViewportInteractionActive()) {
		return false;
	}

	viewport_settle_pending = false;
	return scene_dirty
		|| cached_scroll_x != view_scroll_x
		|| cached_scroll_y != view_scroll_y
		|| cached_scene_zoom != zoom;
}

bool MapDrawer::isViewportInteractionActive() const {
	return viewport_settle_pending
		&& cached_scene_initialized
		&& viewport_settle_timer.Time() < VIEWPORT_SETTLE_DELAY_MS;
}

void MapDrawer::Draw() {
	if (!options.use_fbo_scene_cache) {
		DrawScene();
		DrawOverlays();
		return;
	}

	renderer->ensureFBO(screensize_x, screensize_y);
	if (!renderer->hasFBO()) {
		cached_scene_initialized = false;
		DrawScene();
		DrawOverlays();
		return;
	}

	if (isSceneDirty()) {
		renderer->beginFBO();
		DrawScene();
		renderer->flush();
		renderer->endFBO();
		const bool frameComplete = g_gui.gfx.isCurrentMapRenderComplete();
		scene_dirty = !frameComplete && g_gui.gfx.hasPendingTextureWork();
		cached_scene_initialized = true;
		cached_scene_zoom = zoom;
		cached_scroll_x = view_scroll_x;
		cached_scroll_y = view_scroll_y;
		cached_floor = floor;
		cached_start_z = start_z;
		cached_screensize_x = screensize_x;
		cached_screensize_y = screensize_y;
	}

	const double scale = cached_scene_zoom / zoom;
	const double translated_x = static_cast<double>(cached_scroll_x - view_scroll_x) / zoom;
	const double translated_y = static_cast<double>(cached_scroll_y - view_scroll_y) / zoom;
	const int dst_left = static_cast<int>(std::lround(translated_x));
	const int dst_top = static_cast<int>(std::lround(translated_y));
	const int dst_right = static_cast<int>(std::lround(translated_x + screensize_x * scale));
	const int dst_bottom = static_cast<int>(std::lround(translated_y + screensize_y * scale));

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	renderer->blitFBO(
		0,
		0,
		screensize_x,
		screensize_y,
		dst_left,
		screensize_y - dst_bottom,
		dst_right,
		screensize_y - dst_top,
		(scale == 1.0 ? GL_NEAREST : GL_LINEAR),
		options.post_process_effect
	);
	DrawOverlays();
}

void MapDrawer::DrawBackground() {
	// Black Background
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glLoadIdentity();

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);

	// glAlphaFunc(GL_GEQUAL, 0.9f);
	// glEnable(GL_ALPHA_TEST);
}

inline int getFloorAdjustment(int floor) {
	if (floor > GROUND_LAYER) { // Underground
		return 0; // No adjustment
	} else {
		return TileSize * (GROUND_LAYER - floor);
	}
}

void MapDrawer::DrawMap() {
	Brush* brush = g_gui.GetCurrentBrush();
	if (!far_zoom_mode) {
		visible_tile_count = 0;
		visible_item_count = 0;
	}

	// The current house we're drawing
	current_house_id = 0;
	if (brush) {
		if (brush->isHouse()) {
			current_house_id = brush->asHouse()->getHouseID();
		} else if (brush->isHouseExit()) {
			current_house_id = brush->asHouseExit()->getHouseID();
		}
	}

	bool show_zone_tooltips = options.isTooltips() && !far_zoom_mode;
	if (far_zoom_mode) {
		DrawMapMinimapPages();
		return;
	}

	// Note: texture mode is controlled by uUseTexture in the shader

	for (int map_z = start_z; map_z >= superend_z; map_z--) {
		if (map_z == end_z && start_z != end_z && options.show_shade) {
			// Draw shade

			renderer->drawColoredQuad(0.0f, 0.0f, static_cast<float>(screensize_x) * zoom, static_cast<float>(screensize_y) * zoom, { 0, 0, 0, 128 });
			renderer->flush();
		}

		if (map_z >= end_z) {
			int nd_start_x = start_x & ~3;
			int nd_start_y = start_y & ~3;
			int nd_end_x = (end_x & ~3) + 4;
			int nd_end_y = (end_y & ~3) + 4;

			zoneTiles.clear();
			for (int nd_map_x = nd_start_x; nd_map_x <= nd_end_x; nd_map_x += 4) {
				for (int nd_map_y = nd_start_y; nd_map_y <= nd_end_y; nd_map_y += 4) {
					QTreeNode* nd = editor.map.getLeaf(nd_map_x, nd_map_y);
					if (!nd) {
						continue;
					}

					for (int map_x = 0; map_x < 4; ++map_x) {
						for (int map_y = 0; map_y < 4; ++map_y) {
							TileLocation* location = nd->getTile(map_x, map_y, map_z);
							DrawTile(location);
							// draw light, but only if not zoomed too far
							if (location && options.isDrawLight()) {
								AddLight(location);
							}
						}
					}
				}
			}

			if (show_zone_tooltips && !zoneTiles.empty()) {
				for (auto& itZonePos : zoneTiles) {
					ZoneFinder finder(itZonePos.second);
					const auto& zones = finder.findZones();

					for (const auto& itZone : zones) {
						const FinderPosition center = finder.findClosestToCenter(itZone);

						QTreeNode* nd = editor.getMap().getLeaf(center.x, center.y);
						if (!nd) {
							continue;
						}
						TileLocation* location = nd->getTile(center.x, center.y, center.z);
						if (!location) {
							continue;
						}

						const Tile* tile = location->get();
						if (!tile) {
							continue;
						}

						std::ostringstream tooltip;
						tooltip << "Zone ID: ";
						size_t zones = tile->zones.size();
						for (const auto& zoneId : tile->zones) {
							tooltip << zoneId;
							if (--zones > 0) {
								tooltip << "/";
							}
						}

						int offset;
						if (map_z <= GROUND_LAYER) {
							offset = (GROUND_LAYER - map_z) * TileSize;
						} else {
							offset = TileSize * (floor - map_z);
						}

						int draw_x = ((tile->getX() * TileSize) - view_scroll_x) - offset;
						int draw_y = ((tile->getY() * TileSize) - view_scroll_y) - offset;
						MakeTooltip(draw_x, draw_y + 8, tooltip.str());
					}
				}
			}
		}

		// Draws the doodad preview or the paste preview (or import preview)
		if (g_gui.secondary_map != nullptr && !options.ingame && !far_zoom_mode) {
			const bool autoborderPreview = g_autoborder_preview.Owns(g_gui.secondary_map);
			Position normalPos;
			Position to(mouse_map_x, mouse_map_y, floor);

			if (canvas->isPasting()) {
				normalPos = editor.copybuffer.getPosition();
			} else if (brush && brush->isDoodad()) {
				normalPos = Position(0x8000, 0x8000, 0x8);
			}

			for (int map_x = start_x; map_x <= end_x; map_x++) {
				for (int map_y = start_y; map_y <= end_y; map_y++) {
					Position final(map_x, map_y, map_z);
					Position pos = autoborderPreview ? final : normalPos + final - to;
					// Position pos = topos + copypos - Position(map_x, map_y, map_z);
					if (pos.z >= MAP_LAYERS || pos.z < 0) {
						continue;
					}

					Tile* tile = g_gui.secondary_map->getTile(pos);
					if (autoborderPreview && g_autoborder_preview.IsDeletion(final)) {
						const int offset = map_z <= GROUND_LAYER ? (GROUND_LAYER - map_z) * TileSize : TileSize * (floor - map_z);
						const int draw_x = ((map_x * TileSize) - view_scroll_x) - offset;
						const int draw_y = ((map_y * TileSize) - view_scroll_y) - offset;
						drawFilledRect(draw_x, draw_y, TileSize, TileSize, wxColour(220, 45, 45, 110));
					}
					if (tile) {
						// Compensate for underground/overground
						int offset;
						if (map_z <= GROUND_LAYER) {
							offset = (GROUND_LAYER - map_z) * TileSize;
						} else {
							offset = TileSize * (floor - map_z);
						}

						int draw_x = ((map_x * TileSize) - view_scroll_x) - offset;
						int draw_y = ((map_y * TileSize) - view_scroll_y) - offset;

						// Draw ground
						uint8_t r = 160, g = 160, b = 160;
						uint8_t ground_alpha = 160;
						if (tile->ground) {
							if (tile->isBlocking() && options.show_blocking) {
								g = g / 3 * 2;
								b = b / 3 * 2;
							}
							if (tile->isHouseTile() && options.show_houses) {
								if ((int)tile->getHouseID() == current_house_id) {
									r /= 2;
								} else {
									r /= 2;
									g /= 2;
								}
							} else if (options.show_special_tiles && tile->isPZ()) {
								r /= 2;
								b /= 2;
							}
							if (options.show_special_tiles && tile->getMapFlags() & TILESTATE_PVPZONE) {
								r = r / 3 * 2;
								b = r / 3 * 2;
							}
							if (options.show_special_tiles && tile->getMapFlags() & TILESTATE_NOLOGOUT) {
								b /= 2;
							}
							if (options.show_special_tiles && tile->getMapFlags() & TILESTATE_NOPVP) {
								g /= 2;
							}
							if (options.show_zone_areas && tile->hasZone()) {
								const Color colour = GetZoneColor(*tile, options.active_zone_id);
								r = std::get<0>(colour);
								g = std::get<1>(colour);
								b = std::get<2>(colour);
								if (options.active_zone_id != 0 && tile->hasZone(options.active_zone_id)) {
									ground_alpha = 220;
								}
							}
							BlitItem(draw_x, draw_y, tile, tile->ground, true, r, g, b, ground_alpha);
						}

						// Draw items on the tile
						if (zoom <= 10.0 || !options.hide_items_when_zoomed) {
							ItemVector::iterator it;
							for (it = tile->items.begin(); it != tile->items.end(); it++) {
								if ((*it)->isBorder()) {
									BlitItem(draw_x, draw_y, tile, *it, true, 160, r, g, b);
								} else {
									BlitItem(draw_x, draw_y, tile, *it, true, 160, 160, 160, 160);
								}
							}
							if (tile->creature && options.show_creatures) {
								BlitCreature(draw_x, draw_y, tile->creature);
							}
						}
					}
				}
			}
		}

		--start_x;
		--start_y;
		++end_x;
		++end_y;
	}
}

void MapDrawer::DrawMapMinimapPages() {
	minimap_page_cache.bindMap(&editor.map);
	minimap_page_cache.beginVisibleFrame();
	const uint64_t styleKey = static_cast<uint64_t>(options.show_only_modified)
		| (static_cast<uint64_t>(options.show_zone_areas) << 1)
		| (static_cast<uint64_t>(options.show_houses) << 2)
		| (static_cast<uint64_t>(options.show_special_tiles) << 3)
		| (static_cast<uint64_t>(options.active_zone_id) << 8)
		| (static_cast<uint64_t>(current_house_id) << 40);

	const auto resolvePixel = [this](const Tile& tile) -> GLColor {
		if (options.show_only_modified && !tile.isModified()) {
			return { 0, 0, 0, 0 };
		}
		const uint8_t color = tile.getMiniMapColor();
		uint8_t red = static_cast<uint8_t>((color / 36) % 6 * 51);
		uint8_t green = static_cast<uint8_t>((color / 6) % 6 * 51);
		uint8_t blue = static_cast<uint8_t>(color % 6 * 51);
		if (options.show_zone_areas && tile.hasZone()) {
			const Color zoneColor = GetZoneColor(tile, options.active_zone_id);
			red = static_cast<uint8_t>(std::get<0>(zoneColor));
			green = static_cast<uint8_t>(std::get<1>(zoneColor));
			blue = static_cast<uint8_t>(std::get<2>(zoneColor));
		} else if (options.show_houses && tile.isHouseTile()) {
			red = static_cast<uint8_t>(red / 2);
			if (tile.getHouseID() != current_house_id) {
				green = static_cast<uint8_t>(green / 2);
			}
		} else if (options.show_special_tiles && tile.isPZ()) {
			red = static_cast<uint8_t>(red / 2);
			blue = static_cast<uint8_t>(blue / 2);
		}
		return { red, green, blue, 255 };
	};

	for (int mapZ = start_z; mapZ >= superend_z; --mapZ) {
		if (mapZ == end_z && start_z != end_z && options.show_shade) {
			renderer->drawColoredQuad(0.0f, 0.0f, static_cast<float>(screensize_x) * zoom, static_cast<float>(screensize_y) * zoom, { 0, 0, 0, 128 });
			renderer->flush();
		}
		if (mapZ >= end_z) {
			minimap_page_cache.drawVisible(
				*renderer,
				mapZ,
				start_x,
				start_y,
				end_x,
				end_y,
				view_scroll_x,
				view_scroll_y,
				getFloorAdjustment(mapZ),
				styleKey,
				resolvePixel
			);
		}
		--start_x;
		--start_y;
		++end_x;
		++end_y;
	}
	minimap_page_cache.endVisibleFrame(*renderer);
}

void MapDrawer::DrawIngameBox() {
	const int viewport_width = int(screensize_x * zoom);
	const int viewport_height = int(screensize_y * zoom);
	const int center_screen_x = (view_scroll_x / TileSize + int(screensize_x * zoom / (2 * TileSize))) * TileSize - view_scroll_x;
	const int center_screen_y = (view_scroll_y / TileSize + int(screensize_y * zoom / (2 * TileSize))) * TileSize - view_scroll_y;

	int box_start_x = center_screen_x - (ClientMapWidth / 2) * TileSize;
	int box_start_y = center_screen_y - (ClientMapHeight / 2) * TileSize;
	int box_end_x = box_start_x + ClientMapWidth * TileSize;
	int box_end_y = box_start_y + ClientMapHeight * TileSize;

	static wxColor side_color(0, 0, 0, 200);

	// left side
	if (box_start_x > 0) {
		drawFilledRect(0, 0, std::min(box_start_x, viewport_width), viewport_height, side_color);
	}

	// right side
	if (box_end_x < viewport_width) {
		const int clipped_end_x = std::max(0, box_end_x);
		drawFilledRect(clipped_end_x, 0, viewport_width - clipped_end_x, viewport_height, side_color);
	}

	const int clipped_start_x = std::max(0, box_start_x);
	const int clipped_end_x = std::min(viewport_width, box_end_x);
	const int clipped_width = std::max(0, clipped_end_x - clipped_start_x);

	// top side
	if (box_start_y > 0 && clipped_width > 0) {
		drawFilledRect(clipped_start_x, 0, clipped_width, std::min(box_start_y, viewport_height), side_color);
	}

	// bottom side
	if (box_end_y < viewport_height && clipped_width > 0) {
		const int clipped_end_y = std::max(0, box_end_y);
		drawFilledRect(clipped_start_x, clipped_end_y, clipped_width, viewport_height - clipped_end_y, side_color);
	}

	// hidden tiles
	drawRect(box_start_x, box_start_y, box_end_x - box_start_x, box_end_y - box_start_y, *wxRED);

	// visible tiles
	box_start_x += TileSize;
	box_start_y += TileSize;
	box_end_x -= 1 * TileSize;
	box_end_y -= 1 * TileSize;
	drawRect(box_start_x, box_start_y, box_end_x - box_start_x, box_end_y - box_start_y, *wxGREEN);

	// player position
	box_start_x += (ClientMapWidth - 3) / 2 * TileSize;
	box_start_y += (ClientMapHeight - 3) / 2 * TileSize;
	box_end_x = box_start_x + TileSize;
	box_end_y = box_start_y + TileSize;
	drawRect(box_start_x, box_start_y, box_end_x - box_start_x, box_end_y - box_start_y, *wxGREEN);
}

void MapDrawer::DrawGrid() {
	std::vector<float> lines;
	lines.reserve(static_cast<size_t>((end_y - start_y) + (end_x - start_x)) * 4);

	for (int y = start_y; y < end_y; ++y) {
		auto py = static_cast<float>(y * TileSize - view_scroll_y);
		lines.push_back(static_cast<float>(start_x * TileSize - view_scroll_x));
		lines.push_back(py);
		lines.push_back(static_cast<float>(end_x * TileSize - view_scroll_x));
		lines.push_back(py);
	}

	for (int x = start_x; x < end_x; ++x) {
		auto px = static_cast<float>(x * TileSize - view_scroll_x);
		lines.push_back(px);
		lines.push_back(static_cast<float>(start_y * TileSize - view_scroll_y));
		lines.push_back(px);
		lines.push_back(static_cast<float>(end_y * TileSize - view_scroll_y));
	}

	if (!lines.empty()) {
		renderer->drawLines(lines.data(), static_cast<int>(lines.size() / 4), 255, 255, 255, 128, 1.0f);
	}
}

void MapDrawer::DrawDraggingShadow() {

	// Draw dragging shadow
	if (!editor.selection.isBusy() && dragging && !options.ingame) {
		for (auto tit = editor.selection.begin(); tit != editor.selection.end(); tit++) {
			Tile* tile = *tit;
			Position pos = tile->getPosition();

			int move_x, move_y, move_z;
			move_x = canvas->drag_start_x - mouse_map_x;
			move_y = canvas->drag_start_y - mouse_map_y;
			move_z = canvas->drag_start_z - floor;

			pos.x -= move_x;
			pos.y -= move_y;
			pos.z -= move_z;

			if (pos.z < 0 || pos.z >= MAP_LAYERS) {
				continue;
			}

			// On screen and dragging?
			if (pos.x + 2 > start_x && pos.x < end_x && pos.y + 2 > start_y && pos.y < end_y && (move_x != 0 || move_y != 0 || move_z != 0)) {
				int offset;
				if (pos.z <= GROUND_LAYER) {
					offset = (GROUND_LAYER - pos.z) * TileSize;
				} else {
					offset = TileSize * (floor - pos.z);
				}

				int draw_x = ((pos.x * TileSize) - view_scroll_x) - offset;
				int draw_y = ((pos.y * TileSize) - view_scroll_y) - offset;

				// save performance when moving large chunks unzoomed
				ItemVector toRender = tile->getSelectedItems(zoom > 3.0);
				Tile* desttile = editor.map.getTile(pos);
				for (ItemVector::const_iterator iit = toRender.begin(); iit != toRender.end(); iit++) {
					if (desttile) {
						BlitItem(draw_x, draw_y, desttile, *iit, true, 160, 160, 160, 160);
					} else {
						BlitItem(draw_x, draw_y, pos, *iit, true, 160, 160, 160, 160);
					}
				}

				// save performance when moving large chunks unzoomed
				if (zoom <= 3.0) {
					if (tile->creature && tile->creature->isSelected() && options.show_creatures) {
						BlitCreature(draw_x, draw_y, tile->creature);
					}
					if (tile->spawn && tile->spawn->isSelected()) {
						DrawIndicator(draw_x, draw_y, EDITOR_SPRITE_SPAWNS, 160, 160, 160, 160);
					}
				}
			}
		}
	}
}

void MapDrawer::DrawHigherFloors() {

	// Draw "transparent higher floor"
	if (floor != 8 && floor != 0 && options.transparent_floors) {
		int map_z = floor - 1;
		for (int map_x = start_x; map_x <= end_x; map_x++) {
			for (int map_y = start_y; map_y <= end_y; map_y++) {
				Tile* tile = editor.map.getTile(map_x, map_y, map_z);
				if (tile) {
					int offset;
					if (map_z <= GROUND_LAYER) {
						offset = (GROUND_LAYER - map_z) * TileSize;
					} else {
						offset = TileSize * (floor - map_z);
					}

					int draw_x = ((map_x * TileSize) - view_scroll_x) - offset;
					int draw_y = ((map_y * TileSize) - view_scroll_y) - offset;

					// Position pos = tile->getPosition();

					if (tile->ground) {
						if (tile->isPZ()) {
							BlitItem(draw_x, draw_y, tile, tile->ground, false, 128, 255, 128, 96);
						} else {
							BlitItem(draw_x, draw_y, tile, tile->ground, false, 255, 255, 255, 96);
						}
					}
					if (zoom <= 10.0 || !options.hide_items_when_zoomed) {
						ItemVector::iterator it;
						for (it = tile->items.begin(); it != tile->items.end(); it++) {
							BlitItem(draw_x, draw_y, tile, *it, false, 255, 255, 255, 96);
						}
					}
				}
			}
		}
	}
}

void MapDrawer::DrawSelectionBox() {
	if (options.ingame) {
		return;
	}

	// Draw bounding box

	int last_click_rx = canvas->last_click_abs_x - view_scroll_x;
	int last_click_ry = canvas->last_click_abs_y - view_scroll_y;
	double cursor_rx = canvas->cursor_x * zoom;
	double cursor_ry = canvas->cursor_y * zoom;

	double lines[4][4];

	lines[0][0] = last_click_rx;
	lines[0][1] = last_click_ry;
	lines[0][2] = cursor_rx;
	lines[0][3] = last_click_ry;

	lines[1][0] = cursor_rx;
	lines[1][1] = last_click_ry;
	lines[1][2] = cursor_rx;
	lines[1][3] = cursor_ry;

	lines[2][0] = cursor_rx;
	lines[2][1] = cursor_ry;
	lines[2][2] = last_click_rx;
	lines[2][3] = cursor_ry;

	lines[3][0] = last_click_rx;
	lines[3][1] = cursor_ry;
	lines[3][2] = last_click_rx;
	lines[3][3] = last_click_ry;

	float stipple_verts[16];
	for (int i = 0; i < 4; i++) {
		stipple_verts[i * 4 + 0] = lines[i][0];
		stipple_verts[i * 4 + 1] = lines[i][1];
		stipple_verts[i * 4 + 2] = lines[i][2];
		stipple_verts[i * 4 + 3] = lines[i][3];
	}
	float dash_width = std::max(1.0f, zoom);
	int dash_factor = std::max(1, static_cast<int>(zoom + 0.5f));
	renderer->drawStippledLines(stipple_verts, 4, { 255, 255, 255, 255 }, dash_width, dash_factor, 0xf0f0);
	renderer->flush();
}

void MapDrawer::DrawBrush() {
	if (!g_gui.IsDrawingMode()) {
		return;
	}
	if (!g_gui.GetCurrentBrush()) {
		return;
	}
	if (options.ingame) {
		return;
	}

	Brush* brush = g_gui.GetCurrentBrush();

	BrushColor brushColor = COLOR_BLANK;
	if (brush->isTerrain() || brush->isTable() || brush->isCarpet()) {
		brushColor = COLOR_BRUSH;
	} else if (brush->isHouse()) {
		brushColor = COLOR_HOUSE_BRUSH;
	} else if (brush->isFlag()) {
		brushColor = COLOR_FLAG_BRUSH;
	} else if (brush->isSpawn()) {
		brushColor = COLOR_SPAWN_BRUSH;
	} else if (brush->isEraser()) {
		brushColor = COLOR_ERASER;
	}

	if (dragging_draw) {
		ASSERT(brush->canDrag());

		if (brush->isWall()) {
			int last_click_start_map_x = std::min(canvas->last_click_map_x, mouse_map_x);
			int last_click_start_map_y = std::min(canvas->last_click_map_y, mouse_map_y);
			int last_click_end_map_x = std::max(canvas->last_click_map_x, mouse_map_x) + 1;
			int last_click_end_map_y = std::max(canvas->last_click_map_y, mouse_map_y) + 1;

			int last_click_start_sx = last_click_start_map_x * TileSize - view_scroll_x - getFloorAdjustment(floor);
			int last_click_start_sy = last_click_start_map_y * TileSize - view_scroll_y - getFloorAdjustment(floor);
			int last_click_end_sx = last_click_end_map_x * TileSize - view_scroll_x - getFloorAdjustment(floor);
			int last_click_end_sy = last_click_end_map_y * TileSize - view_scroll_y - getFloorAdjustment(floor);

			int delta_x = last_click_end_sx - last_click_start_sx;
			int delta_y = last_click_end_sy - last_click_start_sy;

			glColor(brushColor);
			glFillQuad(last_click_start_sx, last_click_start_sy + TileSize, last_click_end_sx, last_click_start_sy + TileSize, last_click_end_sx, last_click_start_sy, last_click_start_sx, last_click_start_sy);

			if (delta_y > TileSize) {
				glFillQuad(last_click_start_sx, last_click_end_sy - TileSize, last_click_start_sx + TileSize, last_click_end_sy - TileSize, last_click_start_sx + TileSize, last_click_start_sy + TileSize, last_click_start_sx, last_click_start_sy + TileSize);
			}

			if (delta_x > TileSize && delta_y > TileSize) {
				glFillQuad(last_click_end_sx - TileSize, last_click_start_sy + TileSize, last_click_end_sx, last_click_start_sy + TileSize, last_click_end_sx, last_click_end_sy - TileSize, last_click_end_sx - TileSize, last_click_end_sy - TileSize);
			}

			if (delta_y > TileSize) {
				glFillQuad(last_click_start_sx, last_click_end_sy - TileSize, last_click_end_sx, last_click_end_sy - TileSize, last_click_end_sx, last_click_end_sy, last_click_start_sx, last_click_end_sy);
			}
		} else {
			if (brush->isRaw()) {
			}

			if (g_gui.GetBrushShape() == BRUSHSHAPE_SQUARE || brush->isSpawn() /* Spawn brush is always square */) {
				if (brush->isRaw() || brush->isOptionalBorder()) {
					int start_x, end_x;
					int start_y, end_y;

					if (mouse_map_x < canvas->last_click_map_x) {
						start_x = mouse_map_x;
						end_x = canvas->last_click_map_x;
					} else {
						start_x = canvas->last_click_map_x;
						end_x = mouse_map_x;
					}
					if (mouse_map_y < canvas->last_click_map_y) {
						start_y = mouse_map_y;
						end_y = canvas->last_click_map_y;
					} else {
						start_y = canvas->last_click_map_y;
						end_y = mouse_map_y;
					}

					RAWBrush* raw_brush = nullptr;
					if (brush->isRaw()) {
						raw_brush = brush->asRaw();
					}

					for (int y = start_y; y <= end_y; y++) {
						int cy = y * TileSize - view_scroll_y - getFloorAdjustment(floor);
						for (int x = start_x; x <= end_x; x++) {
							int cx = x * TileSize - view_scroll_x - getFloorAdjustment(floor);
							if (brush->isOptionalBorder()) {
								glColorCheck(brush, Position(x, y, floor));
							} else if (raw_brush) {
								DrawRawBrush(cx, cy, raw_brush->getItemType(), 160, 160, 160, 160);
							}
						}
					}
				} else {
					int last_click_start_map_x = std::min(canvas->last_click_map_x, mouse_map_x);
					int last_click_start_map_y = std::min(canvas->last_click_map_y, mouse_map_y);
					int last_click_end_map_x = std::max(canvas->last_click_map_x, mouse_map_x) + 1;
					int last_click_end_map_y = std::max(canvas->last_click_map_y, mouse_map_y) + 1;

					int last_click_start_sx = last_click_start_map_x * TileSize - view_scroll_x - getFloorAdjustment(floor);
					int last_click_start_sy = last_click_start_map_y * TileSize - view_scroll_y - getFloorAdjustment(floor);
					int last_click_end_sx = last_click_end_map_x * TileSize - view_scroll_x - getFloorAdjustment(floor);
					int last_click_end_sy = last_click_end_map_y * TileSize - view_scroll_y - getFloorAdjustment(floor);

					glColor(brushColor);
					glFillQuad(last_click_start_sx, last_click_start_sy, last_click_end_sx, last_click_start_sy, last_click_end_sx, last_click_end_sy, last_click_start_sx, last_click_end_sy);
				}
			} else if (g_gui.GetBrushShape() == BRUSHSHAPE_CIRCLE) {
				// Calculate drawing offsets
				int start_x, end_x;
				int start_y, end_y;
				int width = std::max(
					std::abs(std::max(mouse_map_y, canvas->last_click_map_y) - std::min(mouse_map_y, canvas->last_click_map_y)),
					std::abs(std::max(mouse_map_x, canvas->last_click_map_x) - std::min(mouse_map_x, canvas->last_click_map_x))
				);

				if (mouse_map_x < canvas->last_click_map_x) {
					start_x = canvas->last_click_map_x - width;
					end_x = canvas->last_click_map_x;
				} else {
					start_x = canvas->last_click_map_x;
					end_x = canvas->last_click_map_x + width;
				}

				if (mouse_map_y < canvas->last_click_map_y) {
					start_y = canvas->last_click_map_y - width;
					end_y = canvas->last_click_map_y;
				} else {
					start_y = canvas->last_click_map_y;
					end_y = canvas->last_click_map_y + width;
				}

				int center_x = start_x + (end_x - start_x) / 2;
				int center_y = start_y + (end_y - start_y) / 2;
				float radii = width / 2.0f + 0.005f;

				RAWBrush* raw_brush = nullptr;
				if (brush->isRaw()) {
					raw_brush = brush->asRaw();
				}

				for (int y = start_y - 1; y <= end_y + 1; y++) {
					int cy = y * TileSize - view_scroll_y - getFloorAdjustment(floor);
					float dy = center_y - y;
					for (int x = start_x - 1; x <= end_x + 1; x++) {
						int cx = x * TileSize - view_scroll_x - getFloorAdjustment(floor);

						float dx = center_x - x;
						// printf("%f;%f\n", dx, dy);
						float distance = sqrt(dx * dx + dy * dy);
						if (distance < radii) {
							if (brush->isRaw()) {
								DrawRawBrush(cx, cy, raw_brush->getItemType(), 160, 160, 160, 160);
							} else {
								glColor(brushColor);
								glFillQuad(cx, cy + TileSize, cx + TileSize, cy + TileSize, cx + TileSize, cy, cx, cy);
							}
						}
					}
				}
			}

			if (brush->isRaw()) {
			}
		}
	} else {
		if (brush->isWall()) {
			int start_map_x = mouse_map_x - g_gui.GetBrushSize();
			int start_map_y = mouse_map_y - g_gui.GetBrushSize();
			int end_map_x = mouse_map_x + g_gui.GetBrushSize() + 1;
			int end_map_y = mouse_map_y + g_gui.GetBrushSize() + 1;

			int start_sx = start_map_x * TileSize - view_scroll_x - getFloorAdjustment(floor);
			int start_sy = start_map_y * TileSize - view_scroll_y - getFloorAdjustment(floor);
			int end_sx = end_map_x * TileSize - view_scroll_x - getFloorAdjustment(floor);
			int end_sy = end_map_y * TileSize - view_scroll_y - getFloorAdjustment(floor);

			int delta_x = end_sx - start_sx;
			int delta_y = end_sy - start_sy;

			glColor(brushColor);
			glFillQuad(start_sx, start_sy + TileSize, end_sx, start_sy + TileSize, end_sx, start_sy, start_sx, start_sy);

			if (delta_y > TileSize) {
				glFillQuad(start_sx, end_sy - TileSize, start_sx + TileSize, end_sy - TileSize, start_sx + TileSize, start_sy + TileSize, start_sx, start_sy + TileSize);
			}

			if (delta_x > TileSize && delta_y > TileSize) {
				glFillQuad(end_sx - TileSize, start_sy + TileSize, end_sx, start_sy + TileSize, end_sx, end_sy - TileSize, end_sx - TileSize, end_sy - TileSize);
			}

			if (delta_y > TileSize) {
				glFillQuad(start_sx, end_sy - TileSize, end_sx, end_sy - TileSize, end_sx, end_sy, start_sx, end_sy);
			}
		} else if (brush->isDoor()) {
			int cx = (mouse_map_x)*TileSize - view_scroll_x - getFloorAdjustment(floor);
			int cy = (mouse_map_y)*TileSize - view_scroll_y - getFloorAdjustment(floor);

			glColorCheck(brush, Position(mouse_map_x, mouse_map_y, floor));
			glFillQuad(cx, cy + TileSize, cx + TileSize, cy + TileSize, cx + TileSize, cy, cx, cy);
		} else if (brush->isCreature()) {
			int cy = (mouse_map_y)*TileSize - view_scroll_y - getFloorAdjustment(floor);
			int cx = (mouse_map_x)*TileSize - view_scroll_x - getFloorAdjustment(floor);
			CreatureBrush* creature_brush = brush->asCreature();
			if (creature_brush->canDraw(&editor.map, Position(mouse_map_x, mouse_map_y, floor))) {
				BlitCreature(cx, cy, creature_brush->getType()->outfit, SOUTH, 255, 255, 255, 160);
			} else {
				BlitCreature(cx, cy, creature_brush->getType()->outfit, SOUTH, 255, 64, 64, 160);
			}
		} else if (!brush->isDoodad()) {
			RAWBrush* raw_brush = nullptr;
			if (brush->isRaw()) { // Textured brush
				raw_brush = brush->asRaw();
			}

			for (int y = -g_gui.GetBrushSize() - 1; y <= g_gui.GetBrushSize() + 1; y++) {
				int cy = (mouse_map_y + y) * TileSize - view_scroll_y - getFloorAdjustment(floor);
				for (int x = -g_gui.GetBrushSize() - 1; x <= g_gui.GetBrushSize() + 1; x++) {
					int cx = (mouse_map_x + x) * TileSize - view_scroll_x - getFloorAdjustment(floor);
					if (g_gui.GetBrushShape() == BRUSHSHAPE_SQUARE) {
						if (x >= -g_gui.GetBrushSize() && x <= g_gui.GetBrushSize() && y >= -g_gui.GetBrushSize() && y <= g_gui.GetBrushSize()) {
							if (brush->isRaw()) {
								DrawRawBrush(cx, cy, raw_brush->getItemType(), 160, 160, 160, 160);
							} else {
								if (brush->isWaypoint()) {
									uint8_t r, g, b;
									getColor(brush, Position(mouse_map_x + x, mouse_map_y + y, floor), r, g, b);
									DrawBrushIndicator(cx, cy, brush, r, g, b);
								} else {
									if (brush->isHouseExit() || brush->isOptionalBorder()) {
										glColorCheck(brush, Position(mouse_map_x + x, mouse_map_y + y, floor));
									} else {
										glColor(brushColor);
									}

									glFillQuad(cx, cy + TileSize, cx + TileSize, cy + TileSize, cx + TileSize, cy, cx, cy);
								}
							}
						}
					} else if (g_gui.GetBrushShape() == BRUSHSHAPE_CIRCLE) {
						double distance = sqrt(double(x * x) + double(y * y));
						if (distance < g_gui.GetBrushSize() + 0.005) {
							if (brush->isRaw()) {
								DrawRawBrush(cx, cy, raw_brush->getItemType(), 160, 160, 160, 160);
							} else {
								if (brush->isWaypoint()) {
									uint8_t r, g, b;
									getColor(brush, Position(mouse_map_x + x, mouse_map_y + y, floor), r, g, b);
									DrawBrushIndicator(cx, cy, brush, r, g, b);
								} else {
									if (brush->isHouseExit() || brush->isOptionalBorder()) {
										glColorCheck(brush, Position(mouse_map_x + x, mouse_map_y + y, floor));
									} else {
										glColor(brushColor);
									}

									glFillQuad(cx, cy + TileSize, cx + TileSize, cy + TileSize, cx + TileSize, cy, cx, cy);
								}
							}
						}
					}
				}
			}

			if (brush->isRaw()) { // Textured brush
			}
		}
	}
}

void MapDrawer::BlitItem(int& draw_x, int& draw_y, const Tile* tile, Item* item, bool ephemeral, int red, int green, int blue, int alpha) {
	RME_PROFILE_SCOPE("MapDrawer::BlitItem(tile)");
	const Position& pos = tile->getPosition();
	BlitItem(draw_x, draw_y, pos, item, ephemeral, red, green, blue, alpha, tile);
}

void MapDrawer::BlitItem(int& draw_x, int& draw_y, const Position& pos, Item* item, bool ephemeral, int red, int green, int blue, int alpha, const Tile* tile) {
	RME_PROFILE_SCOPE("MapDrawer::BlitItem(pos)");
	ItemType& it = g_items[item->getID()];

	// Locked door indicator
	if (!options.ingame && options.highlight_locked_doors && it.isDoor() && it.isLocked) {
		blue /= 2;
		green /= 2;
	}

	if (!options.ingame && !ephemeral && item->isSelected()) {
		red /= 2;
		blue /= 2;
		green /= 2;
	}

	// item sprite
	GameSprite* spr = it.sprite;

	// Display invisible and invalid items
	// Ugly hacks. :)
	if (!options.ingame && options.show_tech_items) {
		// Red invalid client id
		if (it.id == 0) {
			BlitSquare(draw_x, draw_y, red, 0, 0, alpha);
			return;
		}

		switch (it.clientID) {
			// Yellow invisible stairs tile (459)
			case 469:
				BlitSquare(draw_x, draw_y, red, green, 0, alpha / 3 * 2);
				return;

			// Red invisible walkable tile (460)
			case 470:
			case 17970:
			case 20028:
			case 34168:
				BlitSquare(draw_x, draw_y, red, 0, 0, alpha / 3 * 2);
				return;

			// Cyan invisible wall (1548)
			case 2187:
				BlitSquare(draw_x, draw_y, 0, green, blue, 80);
				return;

			default:
				break;
		}

		// primal light
		if (it.clientID >= 39092 && it.clientID <= 39100 || it.clientID == 39236 || it.clientID == 39367 || it.clientID == 39368) {
			spr = g_items[SPRITE_LIGHTSOURCE].sprite;
			red = 0;
			alpha = 180;
		}
	}

	// metaItem, sprite not found or not hidden
	if (it.isMetaItem() || spr == nullptr || !ephemeral && it.pickupable && !options.show_items) {
		return;
	}

	int screenx = draw_x - spr->getDrawOffset().first;
	int screeny = draw_y - spr->getDrawOffset().second;

	// Set the newd drawing height accordingly
	draw_x -= spr->getDrawHeight();
	draw_y -= spr->getDrawHeight();

	int subtype = -1;

	int pattern_x = pos.x % spr->pattern_x;
	int pattern_y = pos.y % spr->pattern_y;
	int pattern_z = pos.z % spr->pattern_z;

	if (it.isSplash() || it.isFluidContainer()) {
		subtype = item->getSubtype();
	} else if (it.isHangable) {
		if (tile && tile->hasProperty(HOOK_SOUTH)) {
			pattern_x = 1;
		} else if (tile && tile->hasProperty(HOOK_EAST)) {
			pattern_x = 2;
		} else {
			pattern_x = 0;
		}
	} else if (it.stackable) {
		if (item->getSubtype() <= 1) {
			subtype = 0;
		} else if (item->getSubtype() <= 2) {
			subtype = 1;
		} else if (item->getSubtype() <= 3) {
			subtype = 2;
		} else if (item->getSubtype() <= 4) {
			subtype = 3;
		} else if (item->getSubtype() < 10) {
			subtype = 4;
		} else if (item->getSubtype() < 25) {
			subtype = 5;
		} else if (item->getSubtype() < 50) {
			subtype = 6;
		} else {
			subtype = 7;
		}
	}

	if (!ephemeral && options.transparent_items && (!it.isGroundTile() || spr->width > 1 || spr->height > 1) && !it.isSplash() && (!it.isBorder || spr->width > 1 || spr->height > 1)) {
		alpha /= 2;
	}

	Podium* podium = nullptr;
	if (it.isPodium()) {
		podium = static_cast<Podium*>(item);
		if (!podium->hasShowPlatform() && !options.ingame) {
			if (options.show_tech_items) {
				alpha /= 2;
			} else {
				alpha = 0;
			}
		}
	}

	const int requestedFrame = item->getFrame();
	if (spr->width == 1 && spr->height == 1 && spr->layers == 1) {
		auto st = spr->getSpriteTex(0, 0, 0, subtype, pattern_x, pattern_y, pattern_z, requestedFrame);
		if (st.texture != 0) {
			item->setLastReadyFrame(requestedFrame);
		} else {
			const int lastReadyFrame = item->getLastReadyFrame();
			if (lastReadyFrame < 0 || lastReadyFrame == requestedFrame) {
				return;
			}
			st = spr->getSpriteTex(0, 0, 0, subtype, pattern_x, pattern_y, pattern_z, lastReadyFrame);
			if (st.texture == 0) {
				return;
			}
		}
		glBlitTexture(screenx, screeny, st.texture, red, green, blue, alpha, false, st.u0, st.v0, st.u1, st.v1);
	} else {
		std::vector<PreparedSpritePart> parts;
		auto prepareFrame = [&](int requestedFrameToPrepare) {
			parts.clear();
			return AppendPreparedSpriteParts(
				screenx,
				screeny,
				spr->width,
				spr->height,
				spr->layers,
				[&](int cx, int cy, int layer) {
					return spr->getSpriteTex(cx, cy, layer, subtype, pattern_x, pattern_y, pattern_z, requestedFrameToPrepare);
				},
				parts
			);
		};

		if (prepareFrame(requestedFrame)) {
			item->setLastReadyFrame(requestedFrame);
		} else {
			const int lastReadyFrame = item->getLastReadyFrame();
			if (lastReadyFrame < 0 || lastReadyFrame == requestedFrame || !prepareFrame(lastReadyFrame)) {
				return;
			}
		}

		for (const PreparedSpritePart& part : parts) {
			const auto& st = part.texture;
			glBlitTexture(part.screen_x, part.screen_y, st.texture, red, green, blue, alpha, false, st.u0, st.v0, st.u1, st.v1);
		}
	}

	// zoomed out very far, avoid drawing stuff barely visible
	if (zoom > 3.0) {
		return;
	}

	if (podium) {
		Outfit outfit = podium->getOutfit();
		if (!podium->hasShowOutfit()) {
			if (podium->hasShowMount()) {
				outfit.lookType = outfit.lookMount;
				outfit.lookHead = outfit.lookMountHead;
				outfit.lookBody = outfit.lookMountBody;
				outfit.lookLegs = outfit.lookMountLegs;
				outfit.lookFeet = outfit.lookMountFeet;
				outfit.lookAddon = 0;
				outfit.lookMount = 0;
			} else {
				outfit.lookType = 0;
			}
		}
		if (!podium->hasShowMount()) {
			outfit.lookMount = 0;
		}

		BlitCreature(draw_x, draw_y, outfit, static_cast<Direction>(podium->getDirection()), red, green, blue, 255);
	}

	// draw wall hook
	if (!options.ingame && options.show_hooks && (it.hookSouth || it.hookEast)) {
		DrawHookIndicator(draw_x, draw_y, it);
	}

	// draw light color indicator
	if (!options.ingame && options.show_light_str) {
		const SpriteLight& light = item->getLight();
		if (light.intensity > 0) {
			wxColor lightColor = colorFromEightBit(light.color);
			uint8_t byteR = lightColor.Red();
			uint8_t byteG = lightColor.Green();
			uint8_t byteB = lightColor.Blue();
			uint8_t byteA = 255;

			int startOffset = std::max<int>(16, 32 - light.intensity);
			int sqSize = TileSize - startOffset;
			glBlitSquare(draw_x + startOffset - 2, draw_y + startOffset - 2, 0, 0, 0, byteA, sqSize + 2);
			glBlitSquare(draw_x + startOffset - 1, draw_y + startOffset - 1, byteR, byteG, byteB, byteA, sqSize);
		}
	}
}

void MapDrawer::BlitSpriteType(int screenx, int screeny, uint32_t spriteid, int red, int green, int blue, int alpha) {
	BlitSpriteType(screenx, screeny, g_items[spriteid].sprite, red, green, blue, alpha);
}

void MapDrawer::BlitSpriteType(int screenx, int screeny, GameSprite* spr, int red, int green, int blue, int alpha) {
	if (spr == nullptr) {
		return;
	}
	screenx -= spr->getDrawOffset().first;
	screeny -= spr->getDrawOffset().second;

	const int frame = 0; // GetTime() % itype->FPA;
	if (spr->width == 1 && spr->height == 1 && spr->layers == 1) {
		const auto st = spr->getSpriteTex(0, 0, 0, -1, 0, 0, 0, frame);
		if (st.texture != 0) {
			glBlitTexture(screenx, screeny, st.texture, red, green, blue, alpha, false, st.u0, st.v0, st.u1, st.v1);
		}
		return;
	}

	std::vector<PreparedSpritePart> parts;
	const bool complete = AppendPreparedSpriteParts(
		screenx,
		screeny,
		spr->width,
		spr->height,
		spr->layers,
		[&](int cx, int cy, int layer) {
			return spr->getSpriteTex(cx, cy, layer, -1, 0, 0, 0, frame);
		},
		parts
	);
	if (!complete) {
		return;
	}
	for (const PreparedSpritePart& part : parts) {
		const auto& st = part.texture;
		glBlitTexture(part.screen_x, part.screen_y, st.texture, red, green, blue, alpha, false, st.u0, st.v0, st.u1, st.v1);
	}
}

void MapDrawer::BlitCreature(int screenx, int screeny, const Outfit& outfit, Direction dir, int red, int green, int blue, int alpha, int animationFrame) {
	if (outfit.lookItem != 0) {
		ItemType& it = g_items[outfit.lookItem];
		BlitSpriteType(screenx, screeny, it.sprite, red, green, blue, alpha);
	} else {
		// get outfit sprite
		GameSprite* spr = g_gui.gfx.getCreatureSprite(outfit.lookType);
		if (!spr || outfit.lookType == 0) {
			return;
		}

		const int frame = spr->frames == 0 ? 0 : animationFrame % spr->frames;
		std::vector<PreparedSpritePart> parts;
		bool complete = true;

		// mount and addon drawing thanks to otc code
		// mount colors by Zbizu
		int pattern_z = 0;
		if (outfit.lookMount != 0) {
			if (GameSprite* mountSpr = g_gui.gfx.getCreatureSprite(outfit.lookMount)) {
				// generate mount colors
				Outfit mountOutfit;
				mountOutfit.lookType = outfit.lookMount;
				mountOutfit.lookHead = outfit.lookMountHead;
				mountOutfit.lookBody = outfit.lookMountBody;
				mountOutfit.lookLegs = outfit.lookMountLegs;
				mountOutfit.lookFeet = outfit.lookMountFeet;

				complete = AppendPreparedSpriteParts(
							   screenx,
							   screeny,
							   mountSpr->width,
							   mountSpr->height,
							   1,
							   [&](int cx, int cy, int) {
								   const int mountFrame = mountSpr->frames == 0 ? 0 : animationFrame % mountSpr->frames;
								   return mountSpr->getSpriteTex(cx, cy, static_cast<int>(dir), 0, 0, mountOutfit, mountFrame);
							   },
							   parts
						   )
					&& complete;

				pattern_z = std::min<int>(1, spr->pattern_z - 1);
			}
		}

		// pattern_y => creature addon
		for (int pattern_y = 0; pattern_y < spr->pattern_y; pattern_y++) {

			// continue if we dont have this addon
			if (pattern_y > 0 && !(outfit.lookAddon & (1 << (pattern_y - 1)))) {
				continue;
			}

			complete = AppendPreparedSpriteParts(
						   screenx,
						   screeny,
						   spr->width,
						   spr->height,
						   1,
						   [&](int cx, int cy, int) {
							   return spr->getSpriteTex(cx, cy, static_cast<int>(dir), pattern_y, pattern_z, outfit, frame);
						   },
						   parts
					   )
				&& complete;
		}

		if (!complete) {
			return;
		}
		for (const PreparedSpritePart& part : parts) {
			const auto& st = part.texture;
			glBlitTexture(part.screen_x, part.screen_y, st.texture, red, green, blue, alpha, false, st.u0, st.v0, st.u1, st.v1);
		}
	}
}

void MapDrawer::DrawIngamePreviewPlayer() {
	const Position& position = canvas->ingamePreviewPlayerPosition;
	if (!position.isValid() || position.z != floor) {
		return;
	}

	const int floorOffset = position.z <= GROUND_LAYER ? (GROUND_LAYER - position.z) * TileSize : TileSize * (floor - position.z);
	const int drawX = position.x * TileSize - view_scroll_x - floorOffset + canvas->ingamePreviewWalkOffsetX;
	const int drawY = position.y * TileSize - view_scroll_y - floorOffset + canvas->ingamePreviewWalkOffsetY;

	drawRect(drawX + 2, drawY + 2, TileSize - 4, TileSize - 4, wxColour(70, 210, 255, 190), 1);
	BlitCreature(drawX, drawY, canvas->ingamePreviewPlayerOutfit, canvas->ingamePreviewPlayerDirection, 255, 255, 255, 255, canvas->ingamePreviewAnimationFrame);

	const int centerX = drawX + TileSize / 2;
	const int centerY = drawY + TileSize / 2;
	int tipX = centerX;
	int tipY = centerY;
	switch (canvas->ingamePreviewPlayerDirection) {
		case NORTH:
			tipY -= 12;
			break;
		case EAST:
			tipX += 12;
			break;
		case SOUTH:
			tipY += 12;
			break;
		case WEST:
			tipX -= 12;
			break;
		default:
			break;
	}
	const float directionLine[] = {
		static_cast<float>(centerX), static_cast<float>(centerY), static_cast<float>(tipX), static_cast<float>(tipY)
	};
	renderer->drawLines(directionLine, 1, 70, 210, 255, 230, 2.0f);
}

void MapDrawer::BlitCreature(int screenx, int screeny, const Creature* c, int red, int green, int blue, int alpha) {
	if (!options.ingame && c->isSelected()) {
		red /= 2;
		green /= 2;
		blue /= 2;
	}
	BlitCreature(screenx, screeny, c->getLookType(), c->getDirection(), red, green, blue, alpha);
}

void MapDrawer::BlitSquare(int sx, int sy, int red, int green, int blue, int alpha, int size) {
	if (size == 0) {
		size = TileSize;
	}

	GameSprite* spr = g_items[SPRITE_ZONE].sprite;
	if (!spr) {
		return;
	}

	auto st = spr->getSpriteTex(0, 0, 0, -1, 0, 0, 0, 0);
	if (st.texture == 0) {
		return;
	}

	renderer->drawTexturedQuad(static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(TileSize), static_cast<float>(TileSize), st.texture, { uint8_t(red), uint8_t(green), uint8_t(blue), uint8_t(alpha) }, st.u0, st.v0, st.u1, st.v1);
}

void MapDrawer::DrawRawBrush(int screenx, int screeny, ItemType* itemType, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
	GameSprite* spr = itemType->sprite;
	uint16_t cid = itemType->clientID;

	switch (cid) {
		// Yellow invisible stairs tile
		case 469:
			b = 0;
			alpha = alpha / 3 * 2;
			spr = g_items[SPRITE_ZONE].sprite;
			break;

		// Red invisible walkable tile
		case 470:
			g = 0;
			b = 0;
			alpha = alpha / 3 * 2;
			spr = g_items[SPRITE_ZONE].sprite;
			break;

		// Cyan invisible wall
		case 2187:
			r = 0;
			alpha = alpha / 3;
			spr = g_items[SPRITE_ZONE].sprite;
			break;

		default:
			break;
	}

	// primal light
	if (cid >= 39092 && cid <= 39100 || cid == 39236 || cid == 39367 || cid == 39368) {
		spr = g_items[SPRITE_LIGHTSOURCE].sprite;
		r = 0;
		alpha = alpha / 3 * 2;
	}

	BlitSpriteType(screenx, screeny, spr, r, g, b, alpha);
}

void MapDrawer::WriteTooltip(Tile* tile, Item* item, std::ostringstream& stream, bool isHouseTile) {
	if (item == nullptr) {
		return;
	}

	const uint16_t id = item->getID();
	if (id < 100) {
		return;
	}

	const auto& zoneIds = tile->zones;
	const uint16_t unique = item->getUniqueID();
	const uint16_t action = item->getActionID();
	const std::string& text = item->getText();
	uint8_t doorId = 0;

	if (isHouseTile && item->isDoor()) {
		if (Door* door = dynamic_cast<Door*>(item)) {
			if (door->isRealDoor()) {
				doorId = door->getDoorID();
			}
		}
	}

	auto* tp = dynamic_cast<Teleport*>(item);
	if (unique == 0 && action == 0 && doorId == 0 && text.empty() && !tp && zoneIds.empty()) {
		return;
	}

	if (stream.tellp() > 0) {
		stream << "\n";
	}

	if (!zoneIds.empty()) {
		const FinderPosition position(tile->getX(), tile->getY(), tile->getZ());
		for (auto& zoneId : zoneIds) {
			auto& positions = zoneTiles[zoneId];
			if (positions.empty() || !(positions.back() == position)) {
				positions.push_back(position);
			}
		}
	} else {
		stream << "Item ID: " << id << "\n";
	}

	if (action > 0) {
		stream << "Action ID: " << action << "\n";
	}
	if (unique > 0) {
		stream << "Unique ID: " << unique << "\n";
	}
	if (doorId > 0) {
		stream << "Door ID: " << static_cast<int>(doorId) << "\n";
	}
	if (!text.empty()) {
		stream << "Text: " << text << "\n";
	}
	if (tp) {
		Position dest = tp->getDestination();
		stream << "Destination: " << dest.x << ", " << dest.y << ", " << dest.z << "\n";
	}
}

void MapDrawer::WriteTooltip(Waypoint* waypoint, std::ostringstream& stream) {
	if (stream.tellp() > 0) {
		stream << "\n";
	}
	stream << "Waypoint: " << waypoint->name << "\n";
}

void MapDrawer::DrawTile(TileLocation* location) {
	RME_PROFILE_SCOPE("MapDrawer::DrawTile");
	if (!location) {
		return;
	}
	Tile* tile = location->get();

	if (!tile) {
		return;
	}

	if (options.show_only_modified && !tile->isModified()) {
		return;
	}

	++visible_tile_count;
	visible_item_count += tile->items.size() + (tile->ground ? 1u : 0u) + (tile->creature ? 1u : 0u);

	int map_x = location->getX();
	int map_y = location->getY();
	int map_z = location->getZ();

	bool as_minimap = options.show_as_minimap;
	bool only_colors = options.isOnlyColors();
	bool show_tooltips = options.isTooltips() && !medium_zoom_mode;
	bool draw_waypoints = !only_colors && zoom < 10.0 && !options.ingame && options.show_waypoints;

	Waypoint* waypoint = nullptr;
	if ((show_tooltips && location->getWaypointCount() > 0) || draw_waypoints) {
		waypoint = canvas->editor.map.waypoints.getWaypoint(location);
	}

	if (show_tooltips && location->getWaypointCount() > 0) {
		if (waypoint) {
			WriteTooltip(waypoint, tooltip);
		}
	}

	int offset;
	if (map_z <= GROUND_LAYER) {
		offset = (GROUND_LAYER - map_z) * TileSize;
	} else {
		offset = TileSize * (floor - map_z);
	}

	int draw_x = ((map_x * TileSize) - view_scroll_x) - offset;
	int draw_y = ((map_y * TileSize) - view_scroll_y) - offset;

	uint8_t r = 255, g = 255, b = 255;

	// begin filters for ground tile
	if (!as_minimap) {
		bool showspecial = options.show_only_colors || options.show_special_tiles;

		if (options.show_blocking && tile->isBlocking() && tile->size() > 0) {
			g = g / 3 * 2;
			b = b / 3 * 2;
		}

		int item_count = tile->items.size();
		if (options.highlight_items && item_count > 0 && !tile->items.back()->isBorder()) {
			static const float factor[5] = { 0.75f, 0.6f, 0.48f, 0.40f, 0.33f };
			int idx = (item_count < 5 ? item_count : 5) - 1;
			g = int(g * factor[idx]);
			r = int(r * factor[idx]);
		}

		if (options.show_spawns && location->getSpawnCount() > 0) {
			float f = 1.0f;
			for (uint32_t i = 0; i < location->getSpawnCount(); ++i) {
				f *= 0.7f;
			}
			g = uint8_t(g * f);
			b = uint8_t(b * f);
		}

		if (options.show_houses && tile->isHouseTile()) {
			if ((int)tile->getHouseID() == current_house_id) {
				r /= 2;
			} else {
				r /= 2;
				g /= 2;
			}
		} else if (showspecial && tile->isPZ()) {
			r /= 2;
			b /= 2;
		}

		if (showspecial && tile->getMapFlags() & TILESTATE_PVPZONE) {
			g = r / 4;
			b = b / 3 * 2;
		}

		if (showspecial && tile->getMapFlags() & TILESTATE_NOLOGOUT) {
			b /= 2;
		}

		if (showspecial && tile->getMapFlags() & TILESTATE_NOPVP) {
			g /= 2;
		}

		if (options.show_zone_areas && tile->hasZone()) {
			const Color colour = GetZoneColor(*tile, options.active_zone_id);
			r = std::get<0>(colour);
			g = std::get<1>(colour);
			b = std::get<2>(colour);
		}
	}

	if (only_colors) {
		if (as_minimap) {
			uint8_t color = tile->getMiniMapColor();
			r = (uint8_t)(int(color / 36) % 6 * 51);
			g = (uint8_t)(int(color / 6) % 6 * 51);
			b = (uint8_t)(color % 6 * 51);
			BlitSquare(draw_x, draw_y, r, g, b, 255);
		} else if (r != 255 || g != 255 || b != 255) {
			BlitSquare(draw_x, draw_y, r, g, b, 128);
		}
	} else {
		if (tile->ground) {
			if (options.show_preview && zoom <= 2.0) {
				tile->ground->animate();
			}

			BlitItem(draw_x, draw_y, tile, tile->ground, false, r, g, b);
		} else if (options.always_show_zones && (r != 255 || g != 255 || b != 255)) {
			DrawRawBrush(draw_x, draw_y, &g_items[SPRITE_ZONE], r, g, b, 60);
		}
	}

	if (show_tooltips && map_z == floor && tile->ground) {
		WriteTooltip(tile, tile->ground, tooltip, tile->isHouseTile());
	}
	// end filters for ground tile

	if (!only_colors) {
		if (zoom < 10.0 || !options.hide_items_when_zoomed) {
			// items on tile
			for (auto it = tile->items.begin(); it != tile->items.end(); it++) {
				if (medium_zoom_mode && !(*it)->isBorder() && std::next(it) != tile->items.end()) {
					continue;
				}
				// item tooltip
				if (show_tooltips && map_z == floor) {
					WriteTooltip(tile, *it, tooltip, tile->isHouseTile());
				}

				// item animation
				if (options.show_preview && zoom <= 2.0) {
					(*it)->animate();
				}

				// item sprite
				if ((*it)->isBorder()) {
					BlitItem(draw_x, draw_y, tile, *it, false, r, g, b);
				} else {
					r = 255, g = 255, b = 255;

					if (options.extended_house_shader && options.show_houses && tile->isHouseTile()) {
						if ((int)tile->getHouseID() == current_house_id) {
							r /= 2;
						} else {
							r /= 2;
							g /= 2;
						}
					}
					BlitItem(draw_x, draw_y, tile, *it, false, r, g, b);
				}
			}
			// monster/npc on tile
			if (!medium_zoom_mode && tile->creature && options.show_creatures) {
				BlitCreature(draw_x, draw_y, tile->creature);
			}
		}

		if (!medium_zoom_mode && zoom < 10.0) {
			// waypoint (blue flame)
			if (draw_waypoints && waypoint) {
				BlitSpriteType(draw_x, draw_y, SPRITE_WAYPOINT, 64, 64, 255);
			}

			// house exit (blue splash)
			if (tile->isHouseExit() && options.show_houses) {
				if (tile->hasHouseExit(current_house_id)) {
					BlitSpriteType(draw_x, draw_y, SPRITE_HOUSE_EXIT, 64, 255, 255);
				} else {
					BlitSpriteType(draw_x, draw_y, SPRITE_HOUSE_EXIT, 64, 64, 255);
				}
			}

			// town temple (gray flag)
			if (options.show_towns && tile->isTownExit(editor.map)) {
				BlitSpriteType(draw_x, draw_y, SPRITE_TOWN_TEMPLE, 255, 255, 64, 170);
			}

			if (tile->spawn && options.show_spawns) {
				if (tile->spawn->isSelected()) {
					DrawIndicator(draw_x, draw_y, EDITOR_SPRITE_SPAWNS, 128, 128, 128);
				} else {
					DrawIndicator(draw_x, draw_y, EDITOR_SPRITE_SPAWNS);
				}
			}

			// tooltips
			if (show_tooltips) {
				if (location->getWaypointCount() > 0) {
					MakeTooltip(draw_x, draw_y, tooltip.str(), 0, 255, 0);
				} else {
					MakeTooltip(draw_x, draw_y, tooltip.str());
				}
			}
			tooltip.str("");
		}
	}
}

void MapDrawer::DrawBrushIndicator(int x, int y, Brush* brush, uint8_t r, uint8_t g, uint8_t b) {
	x += (TileSize / 2);
	y += (TileSize / 2);

	// 7----0----1
	// |         |
	// 6--5  3--2
	//     \/
	//     4
	static int vertexes[9][2] = {
		{ -15, -20 }, // 0
		{ 15, -20 }, // 1
		{ 15, -5 }, // 2
		{ 5, -5 }, // 3
		{ 0, 0 }, // 4
		{ -5, -5 }, // 5
		{ -15, -5 }, // 6
		{ -15, -20 }, // 7
		{ -15, -20 }, // 0
	};

	// circle
	{
		std::vector<float> fan;
		fan.reserve((1 + 31) * 2);
		fan.push_back(static_cast<float>(x));
		fan.push_back(static_cast<float>(y));
		for (int i = 0; i <= 30; i++) {
			float angle = i * 2.0f * PI / 30;
			fan.push_back(cos(angle) * (TileSize / 2) + x);
			fan.push_back(sin(angle) * (TileSize / 2) + y);
		}
		renderer->drawTriangleFan(fan.data(), static_cast<int>(fan.size() / 2), 0x00, 0x00, 0x00, 0x50);
	}

	// background
	{
		std::vector<float> poly;
		poly.reserve(8 * 2);
		for (int i = 0; i < 8; ++i) {
			poly.push_back(static_cast<float>(vertexes[i][0] + x));
			poly.push_back(static_cast<float>(vertexes[i][1] + y));
		}
		renderer->drawPolygon(poly.data(), 8, r, g, b, 0xB4);
	}

	// borders
	{
		std::vector<float> seg;
		seg.reserve(8 * 4);
		for (int i = 0; i < 8; ++i) {
			seg.push_back(static_cast<float>(vertexes[i][0] + x));
			seg.push_back(static_cast<float>(vertexes[i][1] + y));
			seg.push_back(static_cast<float>(vertexes[i + 1][0] + x));
			seg.push_back(static_cast<float>(vertexes[i + 1][1] + y));
		}
		renderer->drawLines(seg.data(), 8, 0x00, 0x00, 0x00, 0xB4, 1.0f);
	}
}

void MapDrawer::DrawHookIndicator(int x, int y, const ItemType& type) {
	std::vector<float> v;
	if (type.hookSouth) {
		x -= 10;
		y += 10;
		v = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(x + 10), static_cast<float>(y), static_cast<float>(x + 20), static_cast<float>(y + 10), static_cast<float>(x + 10), static_cast<float>(y + 10) };
	} else if (type.hookEast) {
		x += 10;
		y -= 10;
		v = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(x + 10), static_cast<float>(y + 10), static_cast<float>(x + 10), static_cast<float>(y + 20), static_cast<float>(x), static_cast<float>(y + 10) };
	}
	if (!v.empty()) {
		renderer->drawPolygon(v.data(), 4, 0, 0, 255, 200);
	}
}

void MapDrawer::DrawIndicator(int x, int y, int indicator, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	GameSprite* sprite = g_gui.gfx.getEditorSprite(indicator);
	if (sprite == nullptr) {
		return;
	}

	int textureId = sprite->getHardwareID(0, 0, 0, -1, 0, 0, 0, 0);
	glBlitTexture(x, y, textureId, r, g, b, a, true);
}

void MapDrawer::DrawPositionIndicator(int z) {
	if (z != pos_indicator.z || pos_indicator.x < start_x || pos_indicator.x > end_x || pos_indicator.y < start_y || pos_indicator.y > end_y) {
		return;
	}

	const long time = GetPositionIndicatorTime();
	if (time == 0) {
		return;
	}

	int offset;
	if (pos_indicator.z <= GROUND_LAYER) {
		offset = (GROUND_LAYER - pos_indicator.z) * TileSize;
	} else {
		offset = TileSize * (floor - pos_indicator.z);
	}

	const int x = ((pos_indicator.x * TileSize) - view_scroll_x) - offset;
	const int y = ((pos_indicator.y * TileSize) - view_scroll_y) - offset;
	const int size = static_cast<int>(TileSize * (0.3f + std::abs(500 - time % 1000) / 1000.f));
	const int borderOffset = (TileSize - size) / 2;

	drawRect(x + borderOffset + 2, y + borderOffset + 2, size - 4, size - 4, *wxWHITE, 2);
	drawRect(x + borderOffset + 1, y + borderOffset + 1, size - 2, size - 2, *wxBLACK, 2);
}

void MapDrawer::DrawTooltips() {
	for (auto it = tooltips.cbegin(); it != tooltips.cend(); ++it) {
		const MapTooltip& tip = *it;
		const char* text = tip.text.c_str();
		float line_width = 0.0f;
		float width = 2.0f;
		float height = 14.0f;
		int char_count = 0;
		int line_char_count = 0;

		for (const char* c = text; *c != '\0'; c++) {
			if (*c == '\n' || (line_char_count >= MapTooltip::MAX_CHARS_PER_LINE && *c == ' ')) {
				height += 14.0f;
				line_width = 0.0f;
				line_char_count = 0;
			} else {
				line_width += bitmapCharWidth(rme_bitmap_helvetica_12, *c);
			}
			width = std::max<float>(width, line_width);
			char_count++;
			line_char_count++;

			if (tip.ellipsis && char_count > (MapTooltip::MAX_CHARS + 3)) {
				break;
			}
		}

		float scale = zoom < 1.0f ? zoom : 1.0f;

		width = (width + 8.0f) * scale;
		height = (height + 4.0f) * scale;

		float x = tip.x + (TileSize / 2.0f);
		float y = tip.y;
		float center = width / 2.0f;
		float space = (7.0f * scale);
		float startx = x - center;
		float endx = x + center;
		float starty = y - (height + space);
		float endy = y - space;

		// 7----0----1
		// |         |
		// 6--5  3--2
		//     \/
		//     4
		float vertexes[9][2] = {
			{ x, starty }, // 0
			{ endx, starty }, // 1
			{ endx, endy }, // 2
			{ x + space, endy }, // 3
			{ x, y }, // 4
			{ x - space, endy }, // 5
			{ startx, endy }, // 6
			{ startx, starty }, // 7
			{ x, starty }, // 0
		};

		// background
		{
			const wxColour background = Theme::Get(Theme::Role::TooltipBackground);
			float poly[16];
			for (int i = 0; i < 8; ++i) {
				poly[i * 2] = vertexes[i][0];
				poly[i * 2 + 1] = vertexes[i][1];
			}
			renderer->drawPolygon(poly, 8, background.Red(), background.Green(), background.Blue(), 245);
		}

		// borders
		{
			const wxColour themedBorder = Theme::Get(Theme::Role::TooltipBorder);
			const bool defaultBorder = tip.r == 255 && tip.g == 255 && tip.b == 255;
			const uint8_t borderR = defaultBorder ? themedBorder.Red() : tip.r;
			const uint8_t borderG = defaultBorder ? themedBorder.Green() : tip.g;
			const uint8_t borderB = defaultBorder ? themedBorder.Blue() : tip.b;
			float seg[32];
			for (int i = 0; i < 8; ++i) {
				seg[i * 4] = vertexes[i][0];
				seg[i * 4 + 1] = vertexes[i][1];
				seg[i * 4 + 2] = vertexes[i + 1][0];
				seg[i * 4 + 3] = vertexes[i + 1][1];
			}
			renderer->drawLines(seg, 8, borderR, borderG, borderB, 255, 1.5f);
		}

		// text
		if (zoom <= 1.0) {
			const wxColour labelColour = Theme::Get(Theme::Role::TooltipLabel);
			const wxColour valueColour = Theme::Get(Theme::Role::TooltipValue);
			renderer->flushAndUnbind();
			startx += (3.0f * scale);
			starty += (14.0f * scale);
			glColor4ub(labelColour.Red(), labelColour.Green(), labelColour.Blue(), 255);
			glRasterPos2f(startx, starty);
			char_count = 0;
			line_char_count = 0;
			bool valuePart = false;
			for (const char* c = text; *c != '\0'; c++) {
				const bool explicitLineBreak = *c == '\n';
				if (explicitLineBreak || (line_char_count >= MapTooltip::MAX_CHARS_PER_LINE && *c == ' ')) {
					starty += (14.0f * scale);
					glRasterPos2f(startx, starty);
					line_char_count = 0;
					if (explicitLineBreak) {
						valuePart = false;
					}
				}
				char_count++;
				line_char_count++;

				if (tip.ellipsis && char_count >= MapTooltip::MAX_CHARS) {
					drawBitmapChar(rme_bitmap_helvetica_18, '.');
					if (char_count >= (MapTooltip::MAX_CHARS + 2)) {
						break;
					}
				} else if (!iscntrl(*c)) {
					const wxColour& colour = valuePart ? valueColour : labelColour;
					glColor4ub(colour.Red(), colour.Green(), colour.Blue(), 255);
					drawBitmapChar(rme_bitmap_helvetica_12, *c);
					if (*c == ':') {
						valuePart = true;
					}
				}
			}
		}
	}
}

void MapDrawer::DrawLight() {
	// draw in-game light
	light_drawer->draw(start_x, start_y, end_x, end_y, view_scroll_x, view_scroll_y, options.experimental_fog, renderer.get());
}

void MapDrawer::MakeTooltip(int screenx, int screeny, const std::string& text, uint8_t r, uint8_t g, uint8_t b) {
	if (text.empty()) {
		return;
	}

	tooltips.emplace_back(screenx, screeny, text, r, g, b);
	tooltips.back().checkLineEnding();
}

void MapDrawer::AddLight(TileLocation* location) {
	if (!options.isDrawLight() || !location) {
		return;
	}

	auto tile = location->get();
	if (!tile) {
		return;
	}

	Position position = location->getPosition();

	if (tile->ground) {
		if (tile->ground->hasLight()) {
			light_drawer->addLight(position.x, position.y, position.z, tile->ground->getLight());
		}
	}

	bool hidden = options.hide_items_when_zoomed && zoom > 10.f;
	if (!hidden && !tile->items.empty()) {
		for (auto item : tile->items) {
			if (item->hasLight()) {
				light_drawer->addLight(position.x, position.y, position.z, item->getLight());
			}
		}
	}
}

void MapDrawer::getColor(Brush* brush, const Position& position, uint8_t& r, uint8_t& g, uint8_t& b) {
	if (brush->canDraw(&editor.map, position)) {
		if (brush->isWaypoint()) {
			r = 0x00;
			g = 0xff, b = 0x00;
		} else {
			r = 0x00;
			g = 0x00, b = 0xff;
		}
	} else {
		r = 0xff;
		g = 0x00, b = 0x00;
	}
}

void MapDrawer::TakeScreenshot(uint8_t* screenshot_buffer) {
	glFinish(); // Wait for the operation to finish

	glPixelStorei(GL_PACK_ALIGNMENT, 1); // 1 byte alignment

	for (int i = 0; i < screensize_y; ++i) {
		glReadPixels(0, screensize_y - i - 1, screensize_x, 1, GL_RGB, GL_UNSIGNED_BYTE, (GLubyte*)(screenshot_buffer) + 3 * screensize_x * i);
	}
}

void MapDrawer::ShowPositionIndicator(const Position& position) {
	pos_indicator = position;
	pos_indicator_timer.Start();
}

void MapDrawer::glBlitTexture(int sx, int sy, int texture_number, int red, int green, int blue, int alpha, bool adjustZoom, float u0, float v0, float u1, float v1) {
	if (texture_number != 0) {
		float size = TileSize;
		if (adjustZoom) {
			if (zoom < 1.0f) {
				float offset = 10 / (10 * zoom);
				size = std::max<float>(16, TileSize * zoom);
				sx += offset;
				sy += offset;
			} else if (zoom > 1.f) {
				float offset = (10 * zoom);
				size = TileSize + offset;
				sx -= offset;
				sy -= offset;
			}
		}
		renderer->drawTexturedQuad(static_cast<float>(sx), static_cast<float>(sy), size, size, static_cast<GLuint>(texture_number), { uint8_t(red), uint8_t(green), uint8_t(blue), uint8_t(alpha) }, u0, v0, u1, v1);
	}
}

void MapDrawer::glBlitSquare(int sx, int sy, int red, int green, int blue, int alpha, int size) {
	if (size == 0) {
		size = TileSize;
	}

	renderer->drawColoredQuad(static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(size), static_cast<float>(size), { uint8_t(red), uint8_t(green), uint8_t(blue), uint8_t(alpha) });
}

void MapDrawer::glColor(wxColor color) {
	m_brushColor = { color.Red(), color.Green(), color.Blue(), color.Alpha() };
}

void MapDrawer::glColor(MapDrawer::BrushColor color) {
	switch (color) {
		case COLOR_BRUSH:
			m_brushColor = {
				(uint8_t)g_settings.getInteger(Config::CURSOR_RED),
				(uint8_t)g_settings.getInteger(Config::CURSOR_GREEN),
				(uint8_t)g_settings.getInteger(Config::CURSOR_BLUE),
				(uint8_t)g_settings.getInteger(Config::CURSOR_ALPHA)
			};
			break;

		case COLOR_FLAG_BRUSH:
		case COLOR_HOUSE_BRUSH:
			m_brushColor = {
				(uint8_t)g_settings.getInteger(Config::CURSOR_ALT_RED),
				(uint8_t)g_settings.getInteger(Config::CURSOR_ALT_GREEN),
				(uint8_t)g_settings.getInteger(Config::CURSOR_ALT_BLUE),
				(uint8_t)g_settings.getInteger(Config::CURSOR_ALT_ALPHA)
			};
			break;

		case COLOR_SPAWN_BRUSH:
			m_brushColor = { 166, 0, 0, 128 };
			break;

		case COLOR_ERASER:
			m_brushColor = { 166, 0, 0, 128 };
			break;

		case COLOR_VALID:
			m_brushColor = { 0, 166, 0, 128 };
			break;

		case COLOR_INVALID:
			m_brushColor = { 166, 0, 0, 128 };
			break;

		default:
			m_brushColor = { 255, 255, 255, 128 };
			break;
	}
}

void MapDrawer::glColorCheck(Brush* brush, const Position& pos) {
	if (brush->canDraw(&editor.map, pos)) {
		glColor(COLOR_VALID);
	} else {
		glColor(COLOR_INVALID);
	}
}

void MapDrawer::drawRect(int x, int y, int w, int h, const wxColor& color, int width) {
	renderer->drawRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), { color.Red(), color.Green(), color.Blue(), color.Alpha() }, static_cast<float>(width));
}

void MapDrawer::drawFilledRect(int x, int y, int w, int h, const wxColor& color) {
	renderer->drawColoredQuad(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), { color.Red(), color.Green(), color.Blue(), color.Alpha() });
}

void MapDrawer::glFillQuad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) {
	float minx = std::min({ x0, x1, x2, x3 });
	float miny = std::min({ y0, y1, y2, y3 });
	float maxx = std::max({ x0, x1, x2, x3 });
	float maxy = std::max({ y0, y1, y2, y3 });
	renderer->drawColoredQuad(minx, miny, maxx - minx, maxy - miny, m_brushColor);
}

// Performance Monitor
void MapDrawer::UpdateRAMUsage() {
#ifdef __WINDOWS__
	PROCESS_MEMORY_COUNTERS pmc;
	if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
		current_ram = pmc.WorkingSetSize / (1024 * 1024);
	}
#else
	std::ifstream file("/proc/self/statm");
	if (file.is_open()) {
		unsigned long size;
		unsigned long rss;
		file >> size >> rss;
		current_ram = (rss * static_cast<unsigned long>(sysconf(_SC_PAGESIZE))) / (1024 * 1024);
	}
#endif
}

void MapDrawer::UpdateCPUUsage() {
#ifdef __WINDOWS__
	FILETIME ftime, fsys, fuser;
	ULARGE_INTEGER now, sys, user;

	GetSystemTimeAsFileTime(&ftime);
	memcpy(&now, &ftime, sizeof(FILETIME));

	GetProcessTimes(GetCurrentProcess(), &ftime, &ftime, &fsys, &fuser);
	memcpy(&sys, &fsys, sizeof(FILETIME));
	memcpy(&user, &fuser, sizeof(FILETIME));

	if (last_now_time.QuadPart != 0) {
		auto process_diff = static_cast<double>(
			(sys.QuadPart - last_sys_time.QuadPart) + (user.QuadPart - last_cpu_time.QuadPart)
		);
		auto system_diff = static_cast<double>(now.QuadPart - last_now_time.QuadPart);

		if (system_diff > 0) {
			current_cpu = (process_diff / system_diff) * 100.0;
			unsigned int num_cores = std::thread::hardware_concurrency();
			if (num_cores > 0) {
				current_cpu = current_cpu / num_cores;
			}
			if (current_cpu > 100.0) {
				current_cpu = 100.0;
			}
		}
	}

	last_cpu_time = user;
	last_sys_time = sys;
	last_now_time = now;
#else
	std::ifstream file("/proc/self/stat");
	if (!file.is_open()) {
		return;
	}

	std::string buffer;
	if (!std::getline(file, buffer)) {
		return;
	}

	// strrchr handles process names with parentheses
	const char* ptr = std::strrchr(buffer.c_str(), ')');
	if (!ptr) {
		return;
	}

	unsigned long long utime;
	unsigned long long stime;
	int fields = sscanf(ptr + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &utime, &stime);

	if (fields != 2) {
		return;
	}

	unsigned long long process_time = utime + stime;

	std::ifstream stat_file("/proc/stat");
	if (!stat_file.is_open()) {
		return;
	}

	unsigned long long user_t, nice, system, idle, iowait, irq, softirq, steal;
	std::string cpu_label;
	stat_file >> cpu_label >> user_t >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

	if (cpu_label == "cpu") {
		unsigned long long total_time = user_t + nice + system + idle + iowait + irq + softirq + steal;
		if (last_total_time != 0) {
			unsigned long long total_diff = total_time - last_total_time;
			unsigned long long process_diff = process_time - last_process_time;
			if (total_diff > 0) {
				current_cpu = (100.0 * process_diff) / total_diff;
			}
		}
		last_total_time = total_time;
		last_process_time = process_time;
	}
#endif
}

void MapDrawer::DrawPerformanceStats() {
	frame_count++;

	long elapsed = perf_update_timer.Time();
	if (elapsed >= 500) {
		current_fps = (frame_count * 1000.0) / elapsed;
		frame_count = 0;
		UpdateRAMUsage();
		UpdateCPUUsage();
		perf_update_timer.Start();

		if (fps_history_size < fps_history.size()) {
			fps_history[fps_history_index] = current_fps;
			fps_history_sum += current_fps;
			++fps_history_size;
		} else {
			fps_history_sum -= fps_history[fps_history_index];
			fps_history[fps_history_index] = current_fps;
			fps_history_sum += current_fps;
		}
		fps_history_index = (fps_history_index + 1) % fps_history.size();
		average_fps = fps_history_size == 0 ? 0.0 : fps_history_sum / static_cast<double>(fps_history_size);
	}

	// Save current matrices and switch to screen-space projection
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, screensize_x, screensize_y, 0, -1, 1);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	int width = 240;
	int height = 232;
	int margin = 10;
	int x = std::max(margin, screensize_x - width - margin);
	int y = margin;

	renderer->flush();
	renderer->setOrtho(0.0f, static_cast<float>(screensize_x), static_cast<float>(screensize_y), 0.0f);
	renderer->drawColoredQuad(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height), { 20, 20, 20, 180 });
	renderer->drawRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height), { 60, 60, 60, 200 }, 1.0f);
	renderer->flushAndUnbind();

	int padding = 8;
	int text_x = x + padding;
	int text_y = y + padding + 12;

	auto drawText = [&](int dx, int dy, float r, float g, float b, const char* text) {
		glColor3f(r, g, b);
		glRasterPos2i(dx, dy);
		for (int i = 0; text[i] != '\0'; ++i) {
			drawBitmapChar(rme_bitmap_fixed_9x15, text[i]);
		}
	};

	char buf[64];

	// FPS
	float r = 0.0f, g = 1.0f, b = 0.4f;
	if (current_fps < 15) {
		r = 1.0f;
		g = 0.3f;
		b = 0.3f;
	} else if (current_fps < 30) {
		r = 1.0f;
		g = 0.8f;
		b = 0.0f;
	}
	snprintf(buf, sizeof(buf), "FPS: %.1f", current_fps);
	drawText(text_x, text_y, r, g, b, buf);

	// Avg
	float ar = 0.7f, ag = 0.7f, ab = 0.7f;
	if (average_fps < 15) {
		ar = 1.0f;
		ag = 0.3f;
		ab = 0.3f;
	} else if (average_fps < 30) {
		ar = 1.0f;
		ag = 0.8f;
		ab = 0.0f;
	}
	snprintf(buf, sizeof(buf), "Avg: %.1f", average_fps);
	drawText(text_x, text_y + 16, ar, ag, ab, buf);

	// Frame time
	double frameTimeMs = current_fps > 0 ? (1000.0 / current_fps) : 0.0;
	snprintf(buf, sizeof(buf), "%.1fms", frameTimeMs);
	drawText(text_x, text_y + 32, 0.6f, 0.6f, 0.6f, buf);

	// CPU
	snprintf(buf, sizeof(buf), "CPU: %.1f%%", current_cpu);
	drawText(text_x, text_y + 48, 0.8f, 0.8f, 0.8f, buf);

	// RAM
	snprintf(buf, sizeof(buf), "RAM: %zu MB", current_ram);
	drawText(text_x, text_y + 64, 0.8f, 0.8f, 0.8f, buf);

	// Last exact scene rebuild and scene complexity
	snprintf(buf, sizeof(buf), "Last exact: %.1fms", last_scene_ms);
	drawText(text_x, text_y + 80, 0.7f, 0.7f, 0.7f, buf);
	snprintf(buf, sizeof(buf), "Last exact T/I: %zu/%zu", visible_tile_count, visible_item_count);
	drawText(text_x, text_y + 96, 0.7f, 0.7f, 0.7f, buf);
	const char* render_mode = isViewportInteractionActive() ? "Scene: cached" : (far_zoom_mode ? "LOD: minimap" : (medium_zoom_mode ? "LOD: medium" : "Scene: exact"));
	drawText(text_x, text_y + 112, 0.55f, 0.75f, 1.0f, render_mode);
	snprintf(
		buf,
		sizeof(buf),
		"GPU upload: %d/%d %.2fms",
		g_gui.gfx.getLastFrameTextureUploads(),
		g_gui.gfx.getLastFrameTextureAttempts(),
		g_gui.gfx.getLastFrameTextureUploadTimeMs()
	);
	drawText(text_x, text_y + 128, 0.7f, 0.7f, 0.7f, buf);
	snprintf(buf, sizeof(buf), "Sheets Q/R: %zu/%zu", g_spriteAppearances.getPendingSheetCount(), g_spriteAppearances.getReadySheetCount());
	drawText(text_x, text_y + 144, 0.7f, 0.7f, 0.7f, buf);
	snprintf(buf, sizeof(buf), "Atlas: %zu pages / %zu MB", g_gui.gfx.getAtlasPageCount(), g_gui.gfx.getAtlasMemoryBytes() / (1024 * 1024));
	drawText(text_x, text_y + 160, 0.7f, 0.7f, 0.7f, buf);
	snprintf(buf, sizeof(buf), "Minimap: %zu pages / %zu KB", minimap_page_cache.getPageCount(), minimap_page_cache.getMemoryBytes() / 1024);
	drawText(text_x, text_y + 176, 0.7f, 0.7f, 0.7f, buf);
	const GLRenderBatchStats& batchStats = renderer->getFrameStats();
	snprintf(buf, sizeof(buf), "Batch C/B/Q: %zu/%zu/%zu", batchStats.drawCalls, batchStats.textureBindings, batchStats.quads);
	drawText(text_x, text_y + 192, 0.7f, 0.7f, 0.7f, buf);
	snprintf(buf, sizeof(buf), "Stream: %zu KB O/F:%zu/%zu", batchStats.streamBytes / 1024, batchStats.bufferOrphans, batchStats.mappingFallbacks);
	drawText(text_x, text_y + 208, 0.7f, 0.7f, 0.7f, buf);

	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	renderer->setOrtho(0.0f, static_cast<float>(screensize_x) * zoom, static_cast<float>(screensize_y) * zoom, 0.0f);
}
