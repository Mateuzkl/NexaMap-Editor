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

#include "sprites.h"
#include "graphics.h"
#include "gl_renderer.h"
#include "artprovider.h"
#include "filehandle.h"
#include "settings.h"
#include "gui.h"
#include "otml.h"
#include "sprite_appearances.h"
#include "sprite_preloader.h"

#include <appearances.pb.h>
#include <iterator>
#include <limits>
#include <wx/mstream.h>
#include <wx/stopwatch.h>
#include <wx/dir.h>
#include <wx/rawbmp.h>
#include "pngfiles.h"

#include "../brushes/door_normal.xpm"
#include "../brushes/door_normal_small.xpm"
#include "../brushes/door_locked.xpm"
#include "../brushes/door_locked_small.xpm"
#include "../brushes/door_magic.xpm"
#include "../brushes/door_magic_small.xpm"
#include "../brushes/door_quest.xpm"
#include "../brushes/door_quest_small.xpm"
#include "../brushes/door_normal_alt.xpm"
#include "../brushes/door_normal_alt_small.xpm"
#include "../brushes/door_archway.xpm"
#include "../brushes/door_archway_small.xpm"

// All 133 template colors
static uint32_t TemplateOutfitLookupTable[] = {
	0xFFFFFF,
	0xFFD4BF,
	0xFFE9BF,
	0xFFFFBF,
	0xE9FFBF,
	0xD4FFBF,
	0xBFFFBF,
	0xBFFFD4,
	0xBFFFE9,
	0xBFFFFF,
	0xBFE9FF,
	0xBFD4FF,
	0xBFBFFF,
	0xD4BFFF,
	0xE9BFFF,
	0xFFBFFF,
	0xFFBFE9,
	0xFFBFD4,
	0xFFBFBF,
	0xDADADA,
	0xBF9F8F,
	0xBFAF8F,
	0xBFBF8F,
	0xAFBF8F,
	0x9FBF8F,
	0x8FBF8F,
	0x8FBF9F,
	0x8FBFAF,
	0x8FBFBF,
	0x8FAFBF,
	0x8F9FBF,
	0x8F8FBF,
	0x9F8FBF,
	0xAF8FBF,
	0xBF8FBF,
	0xBF8FAF,
	0xBF8F9F,
	0xBF8F8F,
	0xB6B6B6,
	0xBF7F5F,
	0xBFAF8F,
	0xBFBF5F,
	0x9FBF5F,
	0x7FBF5F,
	0x5FBF5F,
	0x5FBF7F,
	0x5FBF9F,
	0x5FBFBF,
	0x5F9FBF,
	0x5F7FBF,
	0x5F5FBF,
	0x7F5FBF,
	0x9F5FBF,
	0xBF5FBF,
	0xBF5F9F,
	0xBF5F7F,
	0xBF5F5F,
	0x919191,
	0xBF6A3F,
	0xBF943F,
	0xBFBF3F,
	0x94BF3F,
	0x6ABF3F,
	0x3FBF3F,
	0x3FBF6A,
	0x3FBF94,
	0x3FBFBF,
	0x3F94BF,
	0x3F6ABF,
	0x3F3FBF,
	0x6A3FBF,
	0x943FBF,
	0xBF3FBF,
	0xBF3F94,
	0xBF3F6A,
	0xBF3F3F,
	0x6D6D6D,
	0xFF5500,
	0xFFAA00,
	0xFFFF00,
	0xAAFF00,
	0x54FF00,
	0x00FF00,
	0x00FF54,
	0x00FFAA,
	0x00FFFF,
	0x00A9FF,
	0x0055FF,
	0x0000FF,
	0x5500FF,
	0xA900FF,
	0xFE00FF,
	0xFF00AA,
	0xFF0055,
	0xFF0000,
	0x484848,
	0xBF3F00,
	0xBF7F00,
	0xBFBF00,
	0x7FBF00,
	0x3FBF00,
	0x00BF00,
	0x00BF3F,
	0x00BF7F,
	0x00BFBF,
	0x007FBF,
	0x003FBF,
	0x0000BF,
	0x3F00BF,
	0x7F00BF,
	0xBF00BF,
	0xBF007F,
	0xBF003F,
	0xBF0000,
	0x242424,
	0x7F2A00,
	0x7F5500,
	0x7F7F00,
	0x557F00,
	0x2A7F00,
	0x007F00,
	0x007F2A,
	0x007F55,
	0x007F7F,
	0x00547F,
	0x002A7F,
	0x00007F,
	0x2A007F,
	0x54007F,
	0x7F007F,
	0x7F0055,
	0x7F002A,
	0x7F0000,
};

GraphicManager::GraphicManager() :
	client_version(nullptr),
	unloaded(true),
	dat_format(DAT_FORMAT_UNKNOWN),
	item_count(0),
	creature_count(0),
	otfi_found(false),
	is_extended(false),
	has_transparency(false),
	has_frame_durations(false),
	has_frame_groups(false),
	loaded_textures(0),
	lastclean(0) {
	animation_timer = newd wxStopWatch();
	animation_timer->Start();
}

GraphicManager::~GraphicManager() {
	for (auto iter = sprite_space.begin(); iter != sprite_space.end(); ++iter) {
		delete iter->second;
	}

	for (auto iter = image_space.begin(); iter != image_space.end(); ++iter) {
		delete iter->second;
	}

	delete animation_timer;
}

bool GraphicManager::hasTransparency() const {
	return has_transparency;
}

bool GraphicManager::isUnloaded() const {
	return unloaded;
}

GLuint GraphicManager::getFreeTextureID() {
	static GLuint id_counter = 0x10000000;
	return id_counter++; // This should (hopefully) never run out
}

bool GraphicManager::allocAtlasSlot(GLuint& outTex, int& outX, int& outY) {
	constexpr int PAD = 1;
	constexpr int CELL = SPRITE_PIXELS + 2 * PAD;

	if (atlas_size == 0) {
		GLint maxTex = 2048;
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
		atlas_size = std::min(4096, static_cast<int>(maxTex));
		if (atlas_size < CELL) {
			atlas_size = CELL;
		}
	}

	const int perRow = atlas_size / CELL;
	const int perPage = perRow * perRow;
	constexpr size_t MAX_ATLAS_MEMORY_BYTES = 256ull * 1024ull * 1024ull;
	constexpr size_t MAX_ATLAS_PAGES = 64;
	const size_t pageBytes = static_cast<size_t>(atlas_size) * static_cast<size_t>(atlas_size) * 4;
	const size_t maxAtlasPages = std::min(MAX_ATLAS_PAGES, std::max<size_t>(1, MAX_ATLAS_MEMORY_BYTES / pageBytes));

	if (atlas_textures.empty() || atlas_count >= perPage) {
		if (atlas_textures.size() >= maxAtlasPages) {
			if (!recycleAtlasPage()) {
				deferTextureUpload();
				return false;
			}
		} else {
			GLuint tex = 0;
			glGenTextures(1, &tex);
			if (tex == 0) {
				if (!atlas_allocation_failure_logged) {
					wxLogError("GraphicManager::allocAtlasSlot - OpenGL could not allocate an atlas texture page.");
					atlas_allocation_failure_logged = true;
				}
				return false;
			}
			atlas_allocation_failure_logged = false;
			glBindTexture(GL_TEXTURE_2D, tex);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F); // GL_CLAMP_TO_EDGE
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas_size, atlas_size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			atlas_textures.push_back(tex);
			atlas_page_last_use.push_back(++atlas_access_counter);
			atlas_page_last_frame.push_back(atlas_frame_active ? atlas_frame_counter : 0);
			atlas_count = 0;
		}
	}

	const int slot = atlas_count++;
	const int col = slot % perRow;
	const int row = slot / perRow;
	outTex = atlas_textures.back();
	touchAtlasPage(outTex);
	outX = col * CELL;
	outY = row * CELL;
	return true;
}

bool GraphicManager::recycleAtlasPage() {
	if (atlas_textures.empty() || atlas_textures.size() != atlas_page_last_use.size() || atlas_textures.size() != atlas_page_last_frame.size()) {
		if (!atlas_allocation_failure_logged) {
			wxLogError(
				"GraphicManager::recycleAtlasPage - invalid atlas state (%zu textures, %zu usage entries, %zu frame entries).",
				atlas_textures.size(),
				atlas_page_last_use.size(),
				atlas_page_last_frame.size()
			);
			atlas_allocation_failure_logged = true;
		}
		return false;
	}

	size_t victimIndex = atlas_textures.size();
	uint64_t oldestUse = std::numeric_limits<uint64_t>::max();
	for (size_t pageIndex = 0; pageIndex < atlas_textures.size(); ++pageIndex) {
		if (atlas_frame_active && atlas_page_last_frame[pageIndex] == atlas_frame_counter) {
			continue;
		}
		if (atlas_page_last_use[pageIndex] < oldestUse) {
			oldestUse = atlas_page_last_use[pageIndex];
			victimIndex = pageIndex;
		}
	}
	if (victimIndex == atlas_textures.size()) {
		deferTextureUpload();
		return false;
	}
	if (atlas_frame_active && !active_map_renderer) {
		if (!atlas_allocation_failure_logged) {
			wxLogError("GraphicManager::recycleAtlasPage - no active renderer available to flush the atlas batch.");
			atlas_allocation_failure_logged = true;
		}
		deferTextureUpload();
		return false;
	}
	const GLuint victimTexture = atlas_textures[victimIndex];
	if (active_map_renderer) {
		active_map_renderer->flush();
	}
	int invalidated = 0;
	for (const auto& entry : image_space) {
		auto* image = dynamic_cast<GameSprite::NormalImage*>(entry.second);
		if (!image || !image->atlas_loaded || image->atlas_tex != victimTexture) {
			continue;
		}
		image->atlas_loaded = false;
		image->isGLLoaded = false;
		image->atlas_tex = 0;
		++invalidated;
	}
	loaded_textures = std::max(0, loaded_textures - invalidated);

	GLRenderer::invalidateTexture(victimTexture);
	glBindTexture(GL_TEXTURE_2D, victimTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas_size, atlas_size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	if (victimIndex + 1 != atlas_textures.size()) {
		atlas_textures.erase(atlas_textures.begin() + victimIndex);
		atlas_textures.push_back(victimTexture);
		atlas_page_last_use.erase(atlas_page_last_use.begin() + victimIndex);
		atlas_page_last_use.push_back(++atlas_access_counter);
		atlas_page_last_frame.erase(atlas_page_last_frame.begin() + victimIndex);
		atlas_page_last_frame.push_back(atlas_frame_active ? atlas_frame_counter : 0);
	} else {
		atlas_page_last_use.back() = ++atlas_access_counter;
		atlas_page_last_frame.back() = atlas_frame_active ? atlas_frame_counter : 0;
	}
	atlas_count = 0;
	atlas_allocation_failure_logged = false;
	return true;
}

void GraphicManager::touchAtlasPage(GLuint texture) noexcept {
	const auto page = std::find(atlas_textures.begin(), atlas_textures.end(), texture);
	if (page == atlas_textures.end()) {
		return;
	}
	const size_t pageIndex = static_cast<size_t>(std::distance(atlas_textures.begin(), page));
	atlas_page_last_use[pageIndex] = ++atlas_access_counter;
	if (atlas_frame_active) {
		atlas_page_last_frame[pageIndex] = atlas_frame_counter;
	}
}

void GraphicManager::beginMapRenderTextureBudget(GLRenderer* activeRenderer, bool limitUploads) {
	texture_upload_budget_active = limitUploads;
	texture_upload_deferred = false;
	frame_had_missing_texture = false;
	frame_texture_attempts = 0;
	frame_texture_uploads = 0;
	frame_texture_upload_time = std::chrono::steady_clock::duration::zero();
	texture_upload_attempt_active = false;
	atlas_frame_active = true;
	++atlas_frame_counter;
	active_map_renderer = activeRenderer;
}

bool GraphicManager::endMapRenderTextureBudget() {
	last_frame_texture_attempts = frame_texture_attempts;
	last_frame_texture_uploads = frame_texture_uploads;
	last_frame_texture_upload_time_ms = std::chrono::duration<double, std::milli>(frame_texture_upload_time).count();
	texture_upload_attempt_active = false;
	texture_upload_budget_active = false;
	atlas_frame_active = false;
	active_map_renderer = nullptr;
	return texture_upload_deferred;
}

bool GraphicManager::canPrepareTextureUpload() {
	if (!texture_upload_budget_active) {
		return true;
	}

	constexpr int MAX_UPLOADS_PER_FRAME = 32;
	constexpr auto MAX_UPLOAD_TIME = std::chrono::milliseconds(3);
	if (frame_texture_attempts >= MAX_UPLOADS_PER_FRAME || frame_texture_upload_time >= MAX_UPLOAD_TIME) {
		texture_upload_deferred = true;
		return false;
	}

	texture_upload_attempt_started = std::chrono::steady_clock::now();
	texture_upload_attempt_active = true;
	return true;
}

void GraphicManager::recordTextureUploadAttempt() noexcept {
	if (texture_upload_budget_active) {
		if (texture_upload_attempt_active) {
			frame_texture_upload_time += std::chrono::steady_clock::now() - texture_upload_attempt_started;
			texture_upload_attempt_active = false;
		}
		++frame_texture_attempts;
	}
}

void GraphicManager::cancelTextureUploadAttempt() noexcept {
	if (!texture_upload_attempt_active) {
		return;
	}
	if (texture_upload_budget_active) {
		frame_texture_upload_time += std::chrono::steady_clock::now() - texture_upload_attempt_started;
		++frame_texture_attempts;
	}
	texture_upload_attempt_active = false;
}

void GraphicManager::recordTextureUpload() noexcept {
	if (texture_upload_budget_active) {
		++frame_texture_uploads;
	}
}

void GraphicManager::deferTextureUpload() noexcept {
	if (texture_upload_budget_active) {
		texture_upload_deferred = true;
	}
}

void GraphicManager::markTextureMissing() noexcept {
	if (atlas_frame_active) {
		frame_had_missing_texture = true;
	}
}

void GraphicManager::clear() {
	g_spritePreloader.clear();

	SpriteMap new_sprite_space;
	for (auto iter = sprite_space.begin(); iter != sprite_space.end(); ++iter) {
		if (iter->first >= 0) { // Don't clean internal sprites
			delete iter->second;
		} else {
			new_sprite_space.insert(std::make_pair(iter->first, iter->second));
		}
	}

	for (auto iter = image_space.begin(); iter != image_space.end(); ++iter) {
		delete iter->second;
	}

	sprite_space.swap(new_sprite_space);
	image_space.clear();
	cleanup_list.clear();

	item_count = 0;
	creature_count = 0;
	loaded_textures = 0;
	lastclean = time(nullptr);
	spritefile = "";
	sprite_file_handle.reset();
	sprite_offsets.clear();

	for (GLuint tex : atlas_textures) {
		if (tex) {
			GLRenderer::invalidateTexture(tex);
			glDeleteTextures(1, &tex);
		}
	}
	atlas_textures.clear();
	atlas_page_last_use.clear();
	atlas_page_last_frame.clear();
	atlas_size = 0;
	atlas_count = 0;
	atlas_access_counter = 0;
	atlas_frame_counter = 0;
	atlas_frame_active = false;
	atlas_allocation_failure_logged = false;
	active_map_renderer = nullptr;
	frame_had_missing_texture = false;

	unloaded = true;
}

void GraphicManager::cleanSoftwareSprites() {
	for (auto iter = sprite_space.begin(); iter != sprite_space.end(); ++iter) {
		if (iter->first >= 0) { // Don't clean internal sprites
			iter->second->unloadDC();
		}
	}
}

Sprite* GraphicManager::getSprite(int id) {
	auto it = sprite_space.find(id);
	if (it != sprite_space.end()) {
		return it->second;
	}
	return nullptr;
}

GameSprite* GraphicManager::getCreatureSprite(int id) {
	if (id < 0) {
		return nullptr;
	}

	auto it = sprite_space.find(id + item_count);
	if (it != sprite_space.end()) {
		return static_cast<GameSprite*>(it->second);
	}
	return nullptr;
}

GameSprite* GraphicManager::getEditorSprite(int id) {
	if (id >= 0) {
		return nullptr;
	}

	auto it = sprite_space.find(id);
	if (it != sprite_space.end()) {
		return dynamic_cast<GameSprite*>(it->second);
	}
	return nullptr;
}

uint16_t GraphicManager::getItemSpriteMaxID() const {
	return item_count;
}

#define loadPNGFile(name) _wxGetBitmapFromMemory(name, sizeof(name))
inline wxBitmap* _wxGetBitmapFromMemory(const unsigned char* data, int length) {
	wxMemoryInputStream is(data, length);
	wxImage img(is, "image/png");
	if (!img.IsOk()) {
		return nullptr;
	}
	return newd wxBitmap(img, -1);
}

bool GraphicManager::loadEditorSprites() {
	// Unused graphics MIGHT be loaded here, but it's a neglectable loss
	sprite_space[EDITOR_SPRITE_SELECTION_MARKER] = newd EditorSprite(
		newd wxBitmap(selection_marker_xpm16x16),
		newd wxBitmap(selection_marker_xpm32x32)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_1x1] = newd EditorSprite(
		loadPNGFile(circular_1_small_png),
		loadPNGFile(circular_1_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_3x3] = newd EditorSprite(
		loadPNGFile(circular_2_small_png),
		loadPNGFile(circular_2_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_5x5] = newd EditorSprite(
		loadPNGFile(circular_3_small_png),
		loadPNGFile(circular_3_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_7x7] = newd EditorSprite(
		loadPNGFile(circular_4_small_png),
		loadPNGFile(circular_4_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_9x9] = newd EditorSprite(
		loadPNGFile(circular_5_small_png),
		loadPNGFile(circular_5_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_15x15] = newd EditorSprite(
		loadPNGFile(circular_6_small_png),
		loadPNGFile(circular_6_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_19x19] = newd EditorSprite(
		loadPNGFile(circular_7_small_png),
		loadPNGFile(circular_7_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_1x1] = newd EditorSprite(
		loadPNGFile(rectangular_1_small_png),
		loadPNGFile(rectangular_1_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_3x3] = newd EditorSprite(
		loadPNGFile(rectangular_2_small_png),
		loadPNGFile(rectangular_2_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_5x5] = newd EditorSprite(
		loadPNGFile(rectangular_3_small_png),
		loadPNGFile(rectangular_3_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_7x7] = newd EditorSprite(
		loadPNGFile(rectangular_4_small_png),
		loadPNGFile(rectangular_4_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_9x9] = newd EditorSprite(
		loadPNGFile(rectangular_5_small_png),
		loadPNGFile(rectangular_5_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_15x15] = newd EditorSprite(
		loadPNGFile(rectangular_6_small_png),
		loadPNGFile(rectangular_6_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_19x19] = newd EditorSprite(
		loadPNGFile(rectangular_7_small_png),
		loadPNGFile(rectangular_7_png)
	);

	sprite_space[EDITOR_SPRITE_OPTIONAL_BORDER_TOOL] = newd EditorSprite(
		loadPNGFile(optional_border_small_png),
		loadPNGFile(optional_border_png)
	);
	sprite_space[EDITOR_SPRITE_ERASER] = newd EditorSprite(
		loadPNGFile(eraser_small_png),
		loadPNGFile(eraser_png)
	);
	sprite_space[EDITOR_SPRITE_PZ_TOOL] = newd EditorSprite(
		loadPNGFile(protection_zone_small_png),
		loadPNGFile(protection_zone_png)
	);
	sprite_space[EDITOR_SPRITE_PVPZ_TOOL] = newd EditorSprite(
		loadPNGFile(pvp_zone_small_png),
		loadPNGFile(pvp_zone_png)
	);
	sprite_space[EDITOR_SPRITE_ZONE_TOOL] = newd EditorSprite(
		loadPNGFile(zone_brush_small_png),
		loadPNGFile(zone_brush_zone_png)
	);
	sprite_space[EDITOR_SPRITE_NOLOG_TOOL] = newd EditorSprite(
		loadPNGFile(no_logout_small_png),
		loadPNGFile(no_logout_png)
	);
	sprite_space[EDITOR_SPRITE_NOPVP_TOOL] = newd EditorSprite(
		loadPNGFile(no_pvp_small_png),
		loadPNGFile(no_pvp_png)
	);

	sprite_space[EDITOR_SPRITE_DOOR_NORMAL] = newd EditorSprite(
		newd wxBitmap(door_normal_small_xpm),
		newd wxBitmap(door_normal_xpm)
	);
	sprite_space[EDITOR_SPRITE_DOOR_LOCKED] = newd EditorSprite(
		newd wxBitmap(door_locked_small_xpm),
		newd wxBitmap(door_locked_xpm)
	);
	sprite_space[EDITOR_SPRITE_DOOR_MAGIC] = newd EditorSprite(
		newd wxBitmap(door_magic_small_xpm),
		newd wxBitmap(door_magic_xpm)
	);
	sprite_space[EDITOR_SPRITE_DOOR_QUEST] = newd EditorSprite(
		newd wxBitmap(door_quest_small_xpm),
		newd wxBitmap(door_quest_xpm)
	);
	sprite_space[EDITOR_SPRITE_DOOR_NORMAL_ALT] = newd EditorSprite(
		newd wxBitmap(door_normal_alt_small_xpm),
		newd wxBitmap(door_normal_alt_xpm)
	);
	sprite_space[EDITOR_SPRITE_DOOR_ARCHWAY] = newd EditorSprite(
		newd wxBitmap(door_archway_small_xpm),
		newd wxBitmap(door_archway_xpm)
	);
	sprite_space[EDITOR_SPRITE_WINDOW_NORMAL] = newd EditorSprite(
		loadPNGFile(window_normal_small_png),
		loadPNGFile(window_normal_png)
	);
	sprite_space[EDITOR_SPRITE_WINDOW_HATCH] = newd EditorSprite(
		loadPNGFile(window_hatch_small_png),
		loadPNGFile(window_hatch_png)
	);

	sprite_space[EDITOR_SPRITE_SELECTION_GEM] = newd EditorSprite(
		loadPNGFile(gem_edit_png),
		nullptr
	);
	sprite_space[EDITOR_SPRITE_DRAWING_GEM] = newd EditorSprite(
		loadPNGFile(gem_move_png),
		nullptr
	);

	sprite_space[EDITOR_SPRITE_SPAWNS] = GameSprite::createFromBitmap(ART_SPAWNS);

	return true;
}

bool GraphicManager::loadOTFI(const FileName& filename, wxString& error, wxArrayString& warnings) {
	wxDir dir(filename.GetFullPath());
	wxString otfi_file;

	otfi_found = false;

	if (dir.GetFirst(&otfi_file, "*.otfi", wxDIR_FILES)) {
		wxFileName otfi(filename.GetFullPath(), otfi_file);
		OTMLDocumentPtr doc = OTMLDocument::parse(otfi.GetFullPath().ToStdString());
		if (doc->size() == 0 || !doc->hasChildAt("DatSpr")) {
			error += "'DatSpr' tag not found";
			return false;
		}

		OTMLNodePtr node = doc->get("DatSpr");
		is_extended = node->valueAt<bool>("extended");
		has_transparency = node->valueAt<bool>("transparency");
		has_frame_durations = node->valueAt<bool>("frame-durations");
		has_frame_groups = node->valueAt<bool>("frame-groups");
		auto metadata = node->valueAt<std::string>("metadata-file", std::string(ASSETS_NAME) + ".dat");
		auto sprites = node->valueAt<std::string>("sprites-file", std::string(ASSETS_NAME) + ".spr");
		metadata_file = wxFileName(filename.GetFullPath(), wxString(metadata));
		sprites_file = wxFileName(filename.GetFullPath(), wxString(sprites));
		otfi_found = true;
	}

	if (!otfi_found) {
		is_extended = false;
		has_transparency = false;
		has_frame_durations = false;
		has_frame_groups = false;
		metadata_file = wxFileName(filename.GetFullPath(), wxString(ASSETS_NAME) + ".dat");
		sprites_file = wxFileName(filename.GetFullPath(), wxString(ASSETS_NAME) + ".spr");
	}

	return true;
}

bool GraphicManager::loadSpriteMetadata(const FileName& datafile, wxString& error, wxArrayString& warnings) {
	// items.otb has most of the info we need. This only loads the GameSprite metadata
	FileReadHandle file(nstr(datafile.GetFullPath()));

	if (!file.isOk()) {
		error += "Failed to open " + datafile.GetFullPath() + " for reading\nThe error reported was:" + wxstr(file.getErrorMessage());
		return false;
	}

	uint16_t effect_count, distance_count;

	uint32_t datSignature;
	file.getU32(datSignature);
	// get max id
	file.getU16(item_count);
	file.getU16(creature_count);
	file.getU16(effect_count);
	file.getU16(distance_count);

	uint32_t minID = 100; // items start with id 100
	// We don't load distance/effects, if we would, just add effect_count & distance_count here
	uint32_t maxID = item_count + creature_count;

	dat_format = client_version->getDatFormatForSignature(datSignature);

	if (!otfi_found) {
		is_extended = dat_format >= DAT_FORMAT_96;
		has_frame_durations = dat_format >= DAT_FORMAT_1050;
		has_frame_groups = dat_format >= DAT_FORMAT_1057;
	}

	uint16_t id = minID;
	// loop through all ItemDatabase until we reach the end of file
	while (id <= maxID) {
		auto* sType = newd GameSprite();
		sprite_space[id] = sType;

		sType->id = id;

		// Load the sprite flags
		if (!loadSpriteMetadataFlags(file, sType, error, warnings)) {
			wxString msg;
			msg << "Failed to load flags for sprite " << sType->id;
			warnings.push_back(msg);
		}

		// Reads the group count
		uint8_t group_count = 1;
		if (has_frame_groups && id > item_count) {
			file.getU8(group_count);
		}

		for (uint32_t k = 0; k < group_count; ++k) {
			// Skipping the group type
			if (has_frame_groups && id > item_count) {
				file.skip(1);
			}

			// Size and GameSprite data
			file.getByte(sType->width);
			file.getByte(sType->height);

			// Skipping the exact size
			if ((sType->width > 1) || (sType->height > 1)) {
				file.skip(1);
			}

			file.getU8(sType->layers); // Number of blendframes (some sprites consist of several merged sprites)
			file.getU8(sType->pattern_x);
			file.getU8(sType->pattern_y);
			if (dat_format <= DAT_FORMAT_74) {
				sType->pattern_z = 1;
			} else {
				file.getU8(sType->pattern_z);
			}
			file.getU8(sType->frames); // Length of animation

			if (sType->frames > 1) {
				uint8_t async = 0;
				int loop_count = 0;
				int8_t start_frame = 0;
				if (has_frame_durations) {
					file.getByte(async);
					file.get32(loop_count);
					file.getSByte(start_frame);
				}
				sType->animator = newd Animator(sType->frames, start_frame, loop_count, async == 1);
				if (has_frame_durations) {
					for (int i = 0; i < sType->frames; i++) {
						uint32_t min;
						uint32_t max;
						file.getU32(min);
						file.getU32(max);
						FrameDuration* frame_duration = sType->animator->getFrameDuration(i);
						frame_duration->setValues(int(min), int(max));
					}
					sType->animator->reset();
				}
			}

			sType->numsprites = (int)sType->width * (int)sType->height * (int)sType->layers * (int)sType->pattern_x * (int)sType->pattern_y * sType->pattern_z * (int)sType->frames;

			// Read the sprite ids
			for (uint32_t i = 0; i < sType->numsprites; ++i) {
				uint32_t sprite_id;
				if (is_extended) {
					file.getU32(sprite_id);
				} else {
					uint16_t u16 = 0;
					file.getU16(u16);
					sprite_id = u16;
				}

				if (image_space[sprite_id] == nullptr) {
					auto* img = newd GameSprite::NormalImage();
					img->id = sprite_id;
					image_space[sprite_id] = img;
				}
				sType->spriteList.push_back(static_cast<GameSprite::NormalImage*>(image_space[sprite_id]));
			}
		}
		++id;
	}

	return true;
}

GameSprite::NormalImage* GraphicManager::getOrCreateAssetImage(uint32_t spriteId, uint8_t cropX, uint8_t cropY) {
	const uint64_t key = 0x8000000000000000ULL | (static_cast<uint64_t>(spriteId) << 16) | (static_cast<uint64_t>(cropY) << 8) | static_cast<uint64_t>(cropX);
	const auto iterator = image_space.find(key);
	if (iterator != image_space.end()) {
		return static_cast<GameSprite::NormalImage*>(iterator->second);
	}

	auto* image = newd GameSprite::NormalImage();
	image->id = spriteId;
	image->fromAssets = true;
	image->assetCropX = cropX;
	image->assetCropY = cropY;
	image_space[key] = image;
	return image;
}

bool GraphicManager::loadAppearanceSprite(
	const rme::protobuf::appearances::Appearance& appearance,
	int spriteSpaceId,
	wxString& error,
	wxArrayString& warnings
) {
	using namespace rme::protobuf::appearances;

	const FrameGroup* frameGroup = nullptr;
	for (const FrameGroup& candidate : appearance.frame_group()) {
		if (candidate.has_sprite_info() && candidate.sprite_info().sprite_id_size() > 0) {
			frameGroup = &candidate;
			break;
		}
	}
	if (!frameGroup) {
		warnings.push_back(wxString::Format("Appearance %u has no drawable frame group.", appearance.id()));
		return true;
	}

	const SpriteInfo& spriteInfo = frameGroup->sprite_info();
	const ClientSpriteSheetPtr firstSheet = g_spriteAppearances.getSheetBySpriteId(spriteInfo.sprite_id(0));
	if (!firstSheet) {
		error = wxString::Format(
			"Appearance %u references sprite ID %u, which is absent from the client catalog.",
			appearance.id(),
			spriteInfo.sprite_id(0)
		);
		return false;
	}

	const ClientSpriteSize sourceSize = firstSheet->getSpriteSize();
	const uint8_t tileWidth = static_cast<uint8_t>(std::max(1, sourceSize.width / SPRITE_PIXELS));
	const uint8_t tileHeight = static_cast<uint8_t>(std::max(1, sourceSize.height / SPRITE_PIXELS));

	auto* sprite = newd GameSprite();
	sprite->id = static_cast<uint32_t>(spriteSpaceId);
	sprite->width = tileWidth;
	sprite->height = tileHeight;
	sprite->layers = static_cast<uint8_t>(std::clamp<uint32_t>(spriteInfo.layers(), 1, 255));
	sprite->pattern_x = static_cast<uint8_t>(std::clamp<uint32_t>(spriteInfo.pattern_width(), 1, 255));
	sprite->pattern_y = static_cast<uint8_t>(std::clamp<uint32_t>(spriteInfo.pattern_height(), 1, 255));
	sprite->pattern_z = static_cast<uint8_t>(std::clamp<uint32_t>(spriteInfo.pattern_depth(), 1, 255));

	const uint64_t sourceSpritesPerFrame = static_cast<uint64_t>(sprite->layers) * sprite->pattern_x * sprite->pattern_y * sprite->pattern_z;
	uint32_t frameCount = 1;
	if (spriteInfo.has_animation() && spriteInfo.animation().sprite_phase_size() > 0) {
		frameCount = static_cast<uint32_t>(spriteInfo.animation().sprite_phase_size());
	} else if (sourceSpritesPerFrame > 0) {
		frameCount = std::max<uint32_t>(
			1,
			static_cast<uint32_t>(spriteInfo.sprite_id_size() / sourceSpritesPerFrame)
		);
	}
	sprite->frames = static_cast<uint8_t>(std::clamp<uint32_t>(frameCount, 1, 255));

	if (sprite->frames > 1) {
		int startFrame = 0;
		int loopCount = 0;
		bool asynchronous = false;
		if (spriteInfo.has_animation()) {
			const SpriteAnimation& animation = spriteInfo.animation();
			startFrame = static_cast<int>(std::min<uint32_t>(animation.default_start_phase(), sprite->frames - 1));
			loopCount = static_cast<int>(animation.loop_count());
			asynchronous = !animation.synchronized();
		}
		sprite->animator = newd Animator(sprite->frames, startFrame, loopCount, asynchronous);
		if (spriteInfo.has_animation()) {
			const SpriteAnimation& animation = spriteInfo.animation();
			for (int phaseIndex = 0; phaseIndex < std::min<int>(animation.sprite_phase_size(), sprite->frames); ++phaseIndex) {
				const SpritePhase& phase = animation.sprite_phase(phaseIndex);
				const int minimum = static_cast<int>(std::max<uint32_t>(1, phase.duration_min()));
				const int maximum = static_cast<int>(std::max<uint32_t>(minimum, phase.duration_max()));
				sprite->animator->getFrameDuration(phaseIndex)->setValues(minimum, maximum);
			}
			sprite->animator->reset();
		}
	}

	const uint64_t expectedSourceCount = sourceSpritesPerFrame * sprite->frames;
	if (expectedSourceCount != static_cast<uint64_t>(spriteInfo.sprite_id_size())) {
		warnings.push_back(wxString::Format("Appearance %u declares %llu logical sprites but contains %d sprite IDs; available IDs will be reused safely.", appearance.id(), static_cast<unsigned long long>(expectedSourceCount), spriteInfo.sprite_id_size()));
	}

	const auto appendSourceSprite = [&](uint32_t spriteId) {
		const ClientSpriteSheetPtr sheet = g_spriteAppearances.getSheetBySpriteId(spriteId);
		ClientSpriteSize actualSize = sourceSize;
		if (!sheet) {
			warnings.push_back(wxString::Format("Appearance %u references missing sprite ID %u; an empty sprite will be used.", appearance.id(), spriteId));
			spriteId = 0;
		} else {
			actualSize = sheet->getSpriteSize();
			if (actualSize.width != sourceSize.width || actualSize.height != sourceSize.height) {
				warnings.push_back(wxString::Format("Appearance %u mixes sprite layouts; sprite %u will be cropped to the first layout.", appearance.id(), spriteId));
			}
		}

		for (uint8_t y = 0; y < tileHeight; ++y) {
			for (uint8_t x = 0; x < tileWidth; ++x) {
				// RME's tile coordinate zero is the bottom-right tile of a composite.
				const uint8_t cropX = static_cast<uint8_t>(tileWidth - x - 1);
				const uint8_t cropY = static_cast<uint8_t>(tileHeight - y - 1);
				sprite->spriteList.push_back(getOrCreateAssetImage(spriteId, cropX, cropY));
			}
		}
	};

	const uint64_t logicalCount = std::max<uint64_t>(1, expectedSourceCount);
	for (uint64_t index = 0; index < logicalCount; ++index) {
		const int sourceIndex = static_cast<int>(index % std::max(1, spriteInfo.sprite_id_size()));
		appendSourceSprite(spriteInfo.sprite_id(sourceIndex));
	}
	sprite->numsprites = static_cast<uint32_t>(sprite->spriteList.size());

	if (appearance.has_flags()) {
		const AppearanceFlags& flags = appearance.flags();
		if (flags.has_automap()) {
			sprite->minimap_color = static_cast<uint16_t>(flags.automap().color());
		}
		if (flags.has_height()) {
			sprite->draw_height = static_cast<uint16_t>(flags.height().elevation());
		}
		if (flags.has_shift()) {
			sprite->drawoffset_x = static_cast<uint16_t>(std::max<int32_t>(0, flags.shift().x()));
			sprite->drawoffset_y = static_cast<uint16_t>(std::max<int32_t>(0, flags.shift().y()));
		}
		if (flags.has_light()) {
			sprite->has_light = true;
			sprite->light.intensity = static_cast<uint8_t>(std::min<uint32_t>(255, flags.light().brightness()));
			sprite->light.color = static_cast<uint8_t>(std::min<uint32_t>(255, flags.light().color()));
		}
	}

	const auto existing = sprite_space.find(spriteSpaceId);
	if (existing != sprite_space.end()) {
		delete existing->second;
		existing->second = sprite;
	} else {
		sprite_space[spriteSpaceId] = sprite;
	}
	return true;
}

bool GraphicManager::loadAppearanceItem(
	const rme::protobuf::appearances::Appearance& appearance,
	ItemType* item,
	wxString& error,
	wxArrayString& warnings
) {
	(void)item;
	if (!loadAppearanceSprite(appearance, static_cast<int>(appearance.id()), error, warnings)) {
		return false;
	}
	item_count = std::max<uint16_t>(item_count, static_cast<uint16_t>(appearance.id()));
	unloaded = false;
	has_transparency = true;
	has_frame_durations = true;
	return true;
}

bool GraphicManager::loadAppearanceOutfit(
	const rme::protobuf::appearances::Appearance& appearance,
	wxString& error,
	wxArrayString& warnings
) {
	if (appearance.id() > std::numeric_limits<uint16_t>::max()) {
		warnings.push_back(wxString::Format("Ignored outfit appearance with unsupported ID %u.", appearance.id()));
		return true;
	}
	const int spriteSpaceId = static_cast<int>(appearance.id()) + item_count;
	if (!loadAppearanceSprite(appearance, spriteSpaceId, error, warnings)) {
		return false;
	}
	creature_count = std::max<uint16_t>(creature_count, static_cast<uint16_t>(appearance.id()));
	unloaded = false;
	has_transparency = true;
	has_frame_durations = true;
	return true;
}

uint8_t GraphicManager::normalizeSpriteFlag(uint8_t flag) const {
	if (dat_format >= DAT_FORMAT_1010) {
		/* In 10.10+ all attributes from 16 and up were
		 * incremented by 1 to make space for 16 as
		 * "No Movement Animation" flag.
		 */
		if (flag == 16) {
			flag = DatFlagNoMoveAnimation;
		} else if (flag > 16) {
			flag -= 1;
		}
	} else if (dat_format >= DAT_FORMAT_86) {
		/* Default attribute values follow
		 * the format of 8.6-9.86.
		 * Therefore no changes here.
		 */
	} else if (dat_format >= DAT_FORMAT_78) {
		/* In 7.80-8.54 all attributes from 8 and higher were
		 * incremented by 1 to make space for 8 as
		 * "Item Charges" flag.
		 */
		if (flag == 8) {
			flag = DatFlagChargeable;
		} else if (flag > 8) {
			flag -= 1;
		}
	} else if (dat_format >= DAT_FORMAT_755) {
		/* In 7.55-7.72 attributes 23 is "Floor Change". */
		if (flag == 23) {
			flag = DatFlagFloorChange;
		}
	} else if (dat_format >= DAT_FORMAT_74) {
		/* In 7.4-7.5 attribute "Ground Border" did not exist
		 * attributes 1-15 have to be adjusted.
		 * Several other changes in the format.
		 */
		if (flag > 0 && flag <= 15) {
			flag += 1;
		} else if (flag == 16) {
			flag = DatFlagLight;
		} else if (flag == 17) {
			flag = DatFlagFloorChange;
		} else if (flag == 18) {
			flag = DatFlagFullGround;
		} else if (flag == 19) {
			flag = DatFlagElevation;
		} else if (flag == 20) {
			flag = DatFlagDisplacement;
		} else if (flag == 22) {
			flag = DatFlagMinimapColor;
		} else if (flag == 23) {
			flag = DatFlagRotateable;
		} else if (flag == 24) {
			flag = DatFlagLyingCorpse;
		} else if (flag == 25) {
			flag = DatFlagHangable;
		} else if (flag == 26) {
			flag = DatFlagHookSouth;
		} else if (flag == 27) {
			flag = DatFlagHookEast;
		} else if (flag == 28) {
			flag = DatFlagAnimateAlways;
		}

		/* "Multi Use" and "Force Use" are swapped */
		if (flag == DatFlagMultiUse) {
			flag = DatFlagForceUse;
		} else if (flag == DatFlagForceUse) {
			flag = DatFlagMultiUse;
		}
	}
	return flag;
}

void GraphicManager::readSpriteFlagData(uint8_t flag, uint8_t prev_flag, FileReadHandle& file, GameSprite* sType, wxArrayString& warnings) {
	switch (flag) {
		case DatFlagGroundBorder:
		case DatFlagOnBottom:
		case DatFlagOnTop:
		case DatFlagContainer:
		case DatFlagStackable:
		case DatFlagForceUse:
		case DatFlagMultiUse:
		case DatFlagFluidContainer:
		case DatFlagSplash:
		case DatFlagNotWalkable:
		case DatFlagNotMoveable:
		case DatFlagBlockProjectile:
		case DatFlagNotPathable:
		case DatFlagPickupable:
		case DatFlagHangable:
		case DatFlagHookSouth:
		case DatFlagHookEast:
		case DatFlagRotateable:
		case DatFlagDontHide:
		case DatFlagTranslucent:
		case DatFlagLyingCorpse:
		case DatFlagAnimateAlways:
		case DatFlagFullGround:
		case DatFlagLook:
		case DatFlagWrappable:
		case DatFlagUnwrappable:
		case DatFlagTopEffect:
		case DatFlagFloorChange:
		case DatFlagNoMoveAnimation:
		case DatFlagChargeable:
			break;

		case DatFlagGround: {
			uint16_t speed;
			file.getU16(speed);
			sType->ground_speed = speed;
			break;
		}

		case DatFlagWritable:
		case DatFlagWritableOnce:
		case DatFlagCloth:
		case DatFlagLensHelp:
		case DatFlagUsable:
			file.skip(2);
			break;

		case DatFlagLight: {
			uint16_t intensity;
			uint16_t color;
			file.getU16(intensity);
			file.getU16(color);
			sType->has_light = true;
			sType->light = SpriteLight { static_cast<uint8_t>(intensity), static_cast<uint8_t>(color) };
			break;
		}

		case DatFlagDisplacement: {
			if (dat_format >= DAT_FORMAT_755) {
				uint16_t offset_x;
				uint16_t offset_y;
				file.getU16(offset_x);
				file.getU16(offset_y);

				sType->drawoffset_x = offset_x;
				sType->drawoffset_y = offset_y;
			} else {
				sType->drawoffset_x = 8;
				sType->drawoffset_y = 8;
			}
			break;
		}

		case DatFlagElevation: {
			uint16_t draw_height;
			file.getU16(draw_height);
			sType->draw_height = draw_height;
			break;
		}

		case DatFlagMinimapColor: {
			uint16_t minimap_color;
			file.getU16(minimap_color);
			sType->minimap_color = minimap_color;
			break;
		}

		case DatFlagMarket: {
			file.skip(6);
			std::string marketName;
			file.getString(marketName);
			file.skip(4);
			break;
		}

		default: {
			wxString err;
			err << "Metadata: Unknown flag: " << i2ws(flag) << ". Previous flag: " << i2ws(prev_flag) << ".";
			warnings.push_back(err);
			break;
		}
	}
}

bool GraphicManager::loadSpriteMetadataFlags(FileReadHandle& file, GameSprite* sType, wxString& error, wxArrayString& warnings) {
	uint8_t prev_flag = 0;
	uint8_t flag = DatFlagLast;

	for (int i = 0; i < DatFlagLast; ++i) {
		prev_flag = flag;
		file.getU8(flag);

		if (flag == DatFlagLast) {
			return true;
		}
		flag = normalizeSpriteFlag(flag);

		readSpriteFlagData(flag, prev_flag, file, sType, warnings);
	}

	return true;
}

bool GraphicManager::loadSpriteData(const FileName& datafile, wxString& error, wxArrayString& warnings) {
	FileReadHandle fh(nstr(datafile.GetFullPath()));

	if (!fh.isOk()) {
		error = "Failed to open file for reading";
		return false;
	}

#define safe_get(func, ...)                      \
	do {                                         \
		if (!fh.get##func(__VA_ARGS__)) {        \
			error = wxstr(fh.getErrorMessage()); \
			return false;                        \
		}                                        \
	} while (false)

	uint32_t sprSignature;
	safe_get(U32, sprSignature);

	uint32_t total_pics = 0;
	if (is_extended) {
		safe_get(U32, total_pics);
	} else {
		uint16_t u16 = 0;
		safe_get(U16, u16);
		total_pics = u16;
	}

	if (!g_settings.getInteger(Config::USE_MEMCACHED_SPRITES)) {
		spritefile = nstr(datafile.GetFullPath());
		sprite_offsets.assign(static_cast<size_t>(total_pics) + 1, 0);
		for (uint32_t sprite_id = 1; sprite_id <= total_pics; ++sprite_id) {
			safe_get(U32, sprite_offsets[sprite_id]);
		}
		sprite_file_handle = std::make_unique<FileReadHandle>(spritefile);
		if (!sprite_file_handle->isOk()) {
			error = "Failed to keep the sprite file open for progressive loading";
			sprite_file_handle.reset();
			sprite_offsets.clear();
			return false;
		}
		g_spritePreloader.configure(spritefile, sprite_offsets, has_transparency);
		unloaded = false;
		return true;
	}

	std::vector<uint32_t> sprite_indexes;
	for (uint32_t i = 0; i < total_pics; ++i) {
		uint32_t index;
		safe_get(U32, index);
		sprite_indexes.push_back(index);
	}

	// Now read individual sprites
	int id = 1;
	for (auto sprite_iter = sprite_indexes.begin(); sprite_iter != sprite_indexes.end(); ++sprite_iter, ++id) {
		uint32_t index = *sprite_iter + 3;
		fh.seek(index);
		uint16_t size;
		safe_get(U16, size);

		auto it = image_space.find(id);
		if (it != image_space.end()) {
			auto* spr = dynamic_cast<GameSprite::NormalImage*>(it->second);
			if (spr && size > 0) {
				if (spr->size > 0) {
					wxString ss;
					ss << "items.spr: Duplicate GameSprite id " << id;
					warnings.push_back(ss);
					fh.seekRelative(size);
				} else {
					spr->id = id;
					spr->size = size;
					spr->dump = newd uint8_t[size];
					if (!fh.getRAW(spr->dump, size)) {
						error = wxstr(fh.getErrorMessage());
						return false;
					}
				}
			}
		} else {
			fh.seekRelative(size);
		}
	}
#undef safe_get
	unloaded = false;
	return true;
}

bool GraphicManager::loadSpriteDump(uint8_t*& target, uint16_t& size, int sprite_id) {
	if (g_settings.getInteger(Config::USE_MEMCACHED_SPRITES)) {
		return false;
	}

	if (sprite_id == 0) {
		// Empty GameSprite
		size = 0;
		target = nullptr;
		return true;
	}

	if (!sprite_file_handle || !sprite_file_handle->isOk()) {
		return false;
	}
	unloaded = false;

	if (sprite_id < 0 || static_cast<size_t>(sprite_id) >= sprite_offsets.size()) {
		return false;
	}

	const uint32_t sprite_offset = sprite_offsets[static_cast<size_t>(sprite_id)];
	if (sprite_offset == 0) {
		size = 0;
		target = nullptr;
		return true;
	}

	if (sprite_file_handle->seek(static_cast<size_t>(sprite_offset) + 3)) {
		uint16_t sprite_size = 0;
		if (sprite_file_handle->getU16(sprite_size)) {
			target = newd uint8_t[sprite_size];
			if (sprite_file_handle->getRAW(target, sprite_size)) {
				size = sprite_size;
				return true;
			}
			delete[] target;
			target = nullptr;
		}
	}
	return false;
}

void GraphicManager::addSpriteToCleanup(GameSprite* spr) {
	cleanup_list.push_back(spr);
	// Clean if needed
	if (cleanup_list.size() > std::max<uint32_t>(100, g_settings.getInteger(Config::SOFTWARE_CLEAN_THRESHOLD))) {
		for (int i = 0; i < g_settings.getInteger(Config::SOFTWARE_CLEAN_SIZE) && static_cast<uint32_t>(i) < cleanup_list.size(); ++i) {
			cleanup_list.front()->unloadDC();
			cleanup_list.pop_front();
		}
	}
}

void GraphicManager::garbageCollection() {
	if (g_settings.getInteger(Config::TEXTURE_MANAGEMENT)) {
		int t = time(nullptr);
		if (loaded_textures > g_settings.getInteger(Config::TEXTURE_CLEAN_THRESHOLD) && t - lastclean > g_settings.getInteger(Config::TEXTURE_CLEAN_PULSE)) {
			auto iit = image_space.begin();
			while (iit != image_space.end()) {
				iit->second->clean(t);
				++iit;
			}
			auto sit = sprite_space.begin();
			while (sit != sprite_space.end()) {
				auto* gs = dynamic_cast<GameSprite*>(sit->second);
				if (gs) {
					gs->clean(t);
				}
				++sit;
			}
			lastclean = t;
		}
	}
}

EditorSprite::EditorSprite(wxBitmap* b16x16, wxBitmap* b32x32) {
	bm[SPRITE_SIZE_16x16] = b16x16;
	bm[SPRITE_SIZE_32x32] = b32x32;
}

EditorSprite::~EditorSprite() {
	unloadDC();
}

void EditorSprite::DrawTo(wxDC* dc, SpriteSize sz, int start_x, int start_y, int width, int height) {
	wxBitmap* sp = bm[sz];
	if (sp) {
		dc->DrawBitmap(*sp, start_x, start_y, true);
	}
}

void EditorSprite::unloadDC() {
	delete bm[SPRITE_SIZE_16x16];
	delete bm[SPRITE_SIZE_32x32];
	bm[SPRITE_SIZE_16x16] = nullptr;
	bm[SPRITE_SIZE_32x32] = nullptr;
}

GameSprite::GameSprite() :
	id(0),
	height(0),
	width(0),
	layers(0),
	pattern_x(0),
	pattern_y(0),
	pattern_z(0),
	frames(0),
	numsprites(0),
	animator(nullptr),
	draw_height(0),
	drawoffset_x(0),
	drawoffset_y(0),
	ground_speed(0),
	minimap_color(0) {
	dc[SPRITE_SIZE_16x16] = nullptr;
	dc[SPRITE_SIZE_32x32] = nullptr;
}

GameSprite::~GameSprite() {
	unloadDC();
	for (auto iter = instanced_templates.begin(); iter != instanced_templates.end(); ++iter) {
		delete *iter;
	}

	delete animator;
}

void GameSprite::clean(int time) {
	for (auto iter = instanced_templates.begin();
		 iter != instanced_templates.end();
		 ++iter) {
		(*iter)->clean(time);
	}
}

void GameSprite::unloadDC() {
	delete dc[SPRITE_SIZE_16x16];
	delete dc[SPRITE_SIZE_32x32];
	dc[SPRITE_SIZE_16x16] = nullptr;
	dc[SPRITE_SIZE_32x32] = nullptr;
}

bool GameSprite::getVisualPreviewRGBA(std::vector<uint8_t>& pixels, int& pixelWidth, int& pixelHeight, bool& pending) {
	pending = false;
	pixelWidth = static_cast<int>(width) * SPRITE_PIXELS;
	pixelHeight = static_cast<int>(height) * SPRITE_PIXELS;
	if (pixelWidth <= 0 || pixelHeight <= 0 || layers == 0) {
		pixels.clear();
		return false;
	}

	pixels.assign(static_cast<size_t>(pixelWidth) * pixelHeight * 4, 0);
	std::vector<uint8_t> tilePixels(SPRITE_PIXELS_SIZE * 4);
	for (uint8_t layer = 0; layer < layers; ++layer) {
		for (uint8_t tileX = 0; tileX < width; ++tileX) {
			for (uint8_t tileY = 0; tileY < height; ++tileY) {
				const int index = getIndex(tileX, tileY, layer, 0, 0, 0, 0);
				if (index < 0 || static_cast<size_t>(index) >= spriteList.size() || !spriteList[index]) {
					continue;
				}

				NormalImage* image = spriteList[index];
				if (image->fromAssets) {
					std::vector<uint8_t> sourcePixels;
					ClientSpriteSize sourceSize;
					bool imagePending = false;
					if (!g_spriteAppearances.getSpritePixelsIfLoaded(image->id, sourcePixels, sourceSize, imagePending)) {
						pending = pending || imagePending;
						pixels.clear();
						return false;
					}
					const int originX = image->assetCropX * SPRITE_PIXELS;
					const int originY = image->assetCropY * SPRITE_PIXELS;
					for (int y = 0; y < SPRITE_PIXELS; ++y) {
						for (int x = 0; x < SPRITE_PIXELS; ++x) {
							const size_t destination = (static_cast<size_t>(y) * SPRITE_PIXELS + x) * 4;
							if (originX + x >= sourceSize.width || originY + y >= sourceSize.height) {
								std::fill_n(tilePixels.data() + destination, 4, 0);
								continue;
							}
							const size_t source = (static_cast<size_t>(originY + y) * sourceSize.width + originX + x) * 4;
							tilePixels[destination + 0] = sourcePixels[source + 2];
							tilePixels[destination + 1] = sourcePixels[source + 1];
							tilePixels[destination + 2] = sourcePixels[source + 0];
							tilePixels[destination + 3] = sourcePixels[source + 3];
						}
					}
				} else {
					bool imagePending = false;
					uint8_t* rgba = image->getRGBAData(&imagePending);
					if (!rgba) {
						pending = pending || imagePending;
						pixels.clear();
						return false;
					}
					std::copy_n(rgba, tilePixels.size(), tilePixels.begin());
					delete[] rgba;
				}

				const int destinationX = (static_cast<int>(width) - tileX - 1) * SPRITE_PIXELS;
				const int destinationY = (static_cast<int>(height) - tileY - 1) * SPRITE_PIXELS;
				for (int y = 0; y < SPRITE_PIXELS; ++y) {
					for (int x = 0; x < SPRITE_PIXELS; ++x) {
						const size_t source = (static_cast<size_t>(y) * SPRITE_PIXELS + x) * 4;
						if (tilePixels[source + 3] == 0) {
							continue;
						}
						const size_t destination = (static_cast<size_t>(destinationY + y) * pixelWidth + destinationX + x) * 4;
						std::copy_n(tilePixels.data() + source, 4, pixels.data() + destination);
					}
				}
			}
		}
	}
	return true;
}

int GameSprite::getDrawHeight() const {
	return draw_height;
}

std::pair<int, int> GameSprite::getDrawOffset() const {
	return std::make_pair(drawoffset_x, drawoffset_y);
}

uint8_t GameSprite::getMiniMapColor() const {
	return minimap_color;
}

int GameSprite::getIndex(int width, int height, int layer, int pattern_x, int pattern_y, int pattern_z, int frame) const {
	return ((((((frame % this->frames) * this->pattern_z + pattern_z) * this->pattern_y + pattern_y) * this->pattern_x + pattern_x) * this->layers + layer) * this->height + height) * this->width + width;
}

GLuint GameSprite::getHardwareID(int _x, int _y, int _layer, int _count, int _pattern_x, int _pattern_y, int _pattern_z, int _frame) {
	uint32_t v;
	if (_count >= 0 && height <= 1 && width <= 1) {
		v = _count;
	} else {
		v = ((((((_frame)*pattern_y + _pattern_y) * pattern_x + _pattern_x) * layers + _layer) * height + _y) * width + _x);
	}
	if (v >= numsprites) {
		if (numsprites == 1) {
			v = 0;
		} else {
			v %= numsprites;
		}
	}
	return spriteList[v]->getHardwareID();
}

GameSprite::SpriteTex GameSprite::getSpriteTex(int _x, int _y, int _layer, int _count, int _pattern_x, int _pattern_y, int _pattern_z, int _frame) {
	uint32_t v;
	if (_count >= 0 && height <= 1 && width <= 1) {
		v = _count;
	} else {
		v = ((((((_frame)*pattern_y + _pattern_y) * pattern_x + _pattern_x) * layers + _layer) * height + _y) * width + _x);
	}
	if (v >= numsprites) {
		v = (numsprites == 1) ? 0 : (v % numsprites);
	}
	SpriteTex st;
	st.texture = spriteList[v]->getHardwareID();
	if (st.texture == 0) {
		g_gui.gfx.markTextureMissing();
	}
	spriteList[v]->getUV(st.u0, st.v0, st.u1, st.v1);
	return st;
}

GameSprite::TemplateImage* GameSprite::getTemplateImage(int sprite_index, const Outfit& outfit) {
	if (instanced_templates.empty()) {
		auto* img = newd TemplateImage(this, sprite_index, outfit);
		instanced_templates.push_back(img);
		return img;
	}
	// While this is linear lookup, it is very rare for the list to contain more than 4-8 entries, so it's faster than a hashmap anyways.
	for (auto iter = instanced_templates.begin(); iter != instanced_templates.end(); ++iter) {
		TemplateImage* img = *iter;
		if (img->sprite_index == sprite_index) {
			uint32_t lookHash = img->lookHead << 24 | img->lookBody << 16 | img->lookLegs << 8 | img->lookFeet;
			if (outfit.getColorHash() == lookHash) {
				return img;
			}
		}
	}
	auto* img = newd TemplateImage(this, sprite_index, outfit);
	instanced_templates.push_back(img);
	return img;
}

GLuint GameSprite::getHardwareID(int _x, int _y, int _dir, int _addon, int _pattern_z, const Outfit& _outfit, int _frame) {
	uint32_t v = getIndex(_x, _y, 0, _dir, _addon, _pattern_z, _frame);
	if (v >= numsprites) {
		if (numsprites == 1) {
			v = 0;
		} else {
			v %= numsprites;
		}
	}
	if (layers > 1) { // Template
		TemplateImage* img = getTemplateImage(v, _outfit);
		return img->getHardwareID();
	}
	return spriteList[v]->getHardwareID();
}

GameSprite::SpriteTex GameSprite::getSpriteTex(int _x, int _y, int _dir, int _addon, int _pattern_z, const Outfit& _outfit, int _frame) {
	uint32_t v = getIndex(_x, _y, 0, _dir, _addon, _pattern_z, _frame);
	if (v >= numsprites) {
		v = (numsprites == 1) ? 0 : (v % numsprites);
	}
	SpriteTex st;
	if (layers > 1) {
		TemplateImage* img = getTemplateImage(v, _outfit);
		st.texture = img->getHardwareID();
		img->getUV(st.u0, st.v0, st.u1, st.v1);
	} else {
		st.texture = spriteList[v]->getHardwareID();
		spriteList[v]->getUV(st.u0, st.v0, st.u1, st.v1);
	}
	if (st.texture == 0) {
		g_gui.gfx.markTextureMissing();
	}
	return st;
}

wxMemoryDC* GameSprite::getDC(SpriteSize size) {
	ASSERT(size == SPRITE_SIZE_16x16 || size == SPRITE_SIZE_32x32);

	if (!dc[size]) {
		ASSERT(width >= 1 && height >= 1);

		const int bgshade = g_settings.getInteger(Config::ICON_BACKGROUND);

		int image_size = std::max<int>(width, height) * SPRITE_PIXELS;
		wxImage image(image_size, image_size);
		image.Clear(bgshade);

		for (uint8_t l = 0; l < layers; l++) {
			for (uint8_t w = 0; w < width; w++) {
				for (uint8_t h = 0; h < height; h++) {
					const int i = getIndex(w, h, l, 0, 0, 0, 0);
					uint8_t* data = spriteList[i]->getRGBData();
					if (data) {
						{
							wxImage img(SPRITE_PIXELS, SPRITE_PIXELS, data, true);
							img.SetMaskColour(0xFF, 0x00, 0xFF);
							image.Paste(img, (width - w - 1) * SPRITE_PIXELS, (height - h - 1) * SPRITE_PIXELS);
						}
						delete[] data;
					}
				}
			}
		}

		// Now comes the resizing / antialiasing
		if (size == SPRITE_SIZE_16x16 || image.GetWidth() > SPRITE_PIXELS || image.GetHeight() > SPRITE_PIXELS) {
			int new_size = SPRITE_SIZE_16x16 ? 16 : 32;
			image.Rescale(new_size, new_size);
		}

		wxBitmap bmp(image);
		dc[size] = newd wxMemoryDC(bmp);
		g_gui.gfx.addSpriteToCleanup(this);
		image.Destroy();
	}
	return dc[size];
}

void GameSprite::DrawTo(wxDC* dc, SpriteSize sz, int start_x, int start_y, int width, int height) {
	if (width == -1) {
		width = sz == SPRITE_SIZE_32x32 ? 32 : 16;
	}
	if (height == -1) {
		height = sz == SPRITE_SIZE_32x32 ? 32 : 16;
	}
	wxDC* sdc = getDC(sz);
	if (sdc) {
		dc->Blit(start_x, start_y, width, height, sdc, 0, 0, wxCOPY, true);
	} else {
		const wxBrush& b = dc->GetBrush();
		dc->SetBrush(*wxRED_BRUSH);
		dc->DrawRectangle(start_x, start_y, width, height);
		dc->SetBrush(b);
	}
}

GameSprite::Image::Image() :
	isGLLoaded(false),
	lastaccess(0) {
	////
}

GameSprite::Image::~Image() {
	unloadGLTexture(0);
}

void GameSprite::Image::createGLTexture(GLuint whatid) {
	ASSERT(!isGLLoaded);

	uint8_t* rgba = getRGBAData();
	if (!rgba) {
		return;
	}

	isGLLoaded = true;
	g_gui.gfx.loaded_textures += 1;

	glBindTexture(GL_TEXTURE_2D, whatid);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // Linear Filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); // Linear Filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F); // GL_CLAMP_TO_EDGE
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F); // GL_CLAMP_TO_EDGE
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SPRITE_PIXELS, SPRITE_PIXELS, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

	delete[] rgba;
#undef SPRITE_SIZE
}

void GameSprite::Image::unloadGLTexture(GLuint whatid) {
	if (!isGLLoaded) {
		return;
	}
	isGLLoaded = false;
	g_gui.gfx.loaded_textures = std::max(0, g_gui.gfx.loaded_textures - 1);
	if (whatid != 0) {
		GLRenderer::invalidateTexture(whatid);
		glDeleteTextures(1, &whatid);
	}
}

void GameSprite::Image::visit() {
	lastaccess = time(nullptr);
}

void GameSprite::Image::clean(int time) {
	if (isGLLoaded && time - lastaccess > g_settings.getInteger(Config::TEXTURE_LONGEVITY)) {
		unloadGLTexture(0);
	}
}

GameSprite::NormalImage::NormalImage() :
	id(0),
	size(0),
	dump(nullptr) {
	////
}

GameSprite::NormalImage::~NormalImage() {
	delete[] dump;
}

void GameSprite::NormalImage::clean(int time) {
	if (!atlas_loaded) {
		Image::clean(time);
	}
	if (time - lastaccess > 5 && !g_settings.getInteger(Config::USE_MEMCACHED_SPRITES)) { // We keep dumps around for 5 seconds.
		delete[] dump;
		dump = nullptr;
	}
}

uint8_t* GameSprite::NormalImage::getRGBData() {
	if (fromAssets) {
		std::vector<uint8_t> pixels;
		ClientSpriteSize sourceSize;
		wxString error;
		bool loaded = false;
		if (id != 0 && g_gui.gfx.isMapRenderTextureBudgetActive()) {
			bool pending = false;
			loaded = g_spriteAppearances.getSpritePixelsIfLoaded(id, pixels, sourceSize, pending);
			if (pending) {
				g_gui.gfx.deferTextureUpload();
			} else if (!loaded) {
				loaded = g_spriteAppearances.getSpritePixels(id, pixels, sourceSize, error);
			}
		} else if (id != 0) {
			loaded = g_spriteAppearances.getSpritePixels(id, pixels, sourceSize, error);
		}
		if (!loaded) {
			return nullptr;
		}

		const int pixelsDataSize = SPRITE_PIXELS * SPRITE_PIXELS * 3;
		auto* data = newd uint8_t[pixelsDataSize];
		const int originX = assetCropX * SPRITE_PIXELS;
		const int originY = assetCropY * SPRITE_PIXELS;
		for (int y = 0; y < SPRITE_PIXELS; ++y) {
			for (int x = 0; x < SPRITE_PIXELS; ++x) {
				const size_t destination = (static_cast<size_t>(y) * SPRITE_PIXELS + x) * 3;
				if (originX + x >= sourceSize.width || originY + y >= sourceSize.height) {
					data[destination + 0] = 0xFF;
					data[destination + 1] = 0x00;
					data[destination + 2] = 0xFF;
					continue;
				}
				const size_t source = (static_cast<size_t>(originY + y) * sourceSize.width + originX + x) * 4;
				const uint8_t alpha = pixels[source + 3];
				data[destination + 0] = alpha == 0 ? 0xFF : pixels[source + 2];
				data[destination + 1] = alpha == 0 ? 0x00 : pixels[source + 1];
				data[destination + 2] = alpha == 0 ? 0xFF : pixels[source + 0];
			}
		}
		return data;
	}

	if (!dump) {
		if (g_settings.getInteger(Config::USE_MEMCACHED_SPRITES)) {
			return nullptr;
		}

		if (id != 0 && g_gui.gfx.isMapRenderTextureBudgetActive()) {
			std::vector<uint8_t> pixels;
			const SpritePreloadStatus status = g_spritePreloader.getOrRequest(static_cast<uint32_t>(id), pixels);
			if (status == SpritePreloadStatus::Ready) {
				const int pixelsDataSize = SPRITE_PIXELS_SIZE * 3;
				auto* data = newd uint8_t[pixelsDataSize];
				for (int pixel = 0; pixel < SPRITE_PIXELS_SIZE; ++pixel) {
					const uint8_t alpha = pixels[static_cast<size_t>(pixel) * 4 + 3];
					data[pixel * 3 + 0] = alpha == 0 ? 0xFF : pixels[static_cast<size_t>(pixel) * 4 + 0];
					data[pixel * 3 + 1] = alpha == 0 ? 0x00 : pixels[static_cast<size_t>(pixel) * 4 + 1];
					data[pixel * 3 + 2] = alpha == 0 ? 0xFF : pixels[static_cast<size_t>(pixel) * 4 + 2];
				}
				return data;
			}
			if (status == SpritePreloadStatus::Pending) {
				g_gui.gfx.deferTextureUpload();
				return nullptr;
			}
		}

		if (!g_gui.gfx.loadSpriteDump(dump, size, id)) {
			return nullptr;
		}
	}

	const int pixels_data_size = SPRITE_PIXELS * SPRITE_PIXELS * 3;
	auto* data = newd uint8_t[pixels_data_size];
	uint8_t bpp = g_gui.gfx.hasTransparency() ? 4 : 3;
	int write = 0;
	int read = 0;

	// decompress pixels
	while (read + 2 <= size && write < pixels_data_size) {
		int transparent = dump[read] | dump[read + 1] << 8;
		read += 2;
		for (int i = 0; i < transparent && write < pixels_data_size; i++) {
			data[write + 0] = 0xFF; // red
			data[write + 1] = 0x00; // green
			data[write + 2] = 0xFF; // blue
			write += 3;
		}

		if (write >= pixels_data_size || read + 2 > size) {
			break;
		}
		int colored = dump[read] | dump[read + 1] << 8;
		read += 2;
		for (int i = 0; i < colored && write < pixels_data_size && read + bpp <= size; i++) {
			data[write + 0] = dump[read + 0]; // red
			data[write + 1] = dump[read + 1]; // green
			data[write + 2] = dump[read + 2]; // blue
			write += 3;
			read += bpp;
		}
	}

	// fill remaining pixels
	while (write < pixels_data_size) {
		data[write + 0] = 0xFF; // red
		data[write + 1] = 0x00; // green
		data[write + 2] = 0xFF; // blue
		write += 3;
	}
	return data;
}

uint8_t* GameSprite::NormalImage::getRGBAData() {
	return getRGBAData(nullptr);
}

uint8_t* GameSprite::NormalImage::getRGBAData(bool* pending) {
	if (pending) {
		*pending = false;
	}
	if (fromAssets) {
		std::vector<uint8_t> pixels;
		ClientSpriteSize sourceSize;
		wxString error;
		bool loaded = false;
		if (id != 0 && g_gui.gfx.isMapRenderTextureBudgetActive()) {
			// This local used to be called `pending` as well, shadowing the out
			// parameter, so the "still loading" state never reached the caller:
			// an unloaded sprite looked like a permanent failure and was never
			// retried.
			bool loadPending = false;
			loaded = g_spriteAppearances.getSpritePixelsIfLoaded(id, pixels, sourceSize, loadPending);
			if (loadPending) {
				g_gui.gfx.deferTextureUpload();
				if (pending) {
					*pending = true;
				}
			} else if (!loaded) {
				loaded = g_spriteAppearances.getSpritePixels(id, pixels, sourceSize, error);
			}
		} else if (id != 0) {
			loaded = g_spriteAppearances.getSpritePixels(id, pixels, sourceSize, error);
		}
		if (!loaded) {
			return nullptr;
		}

		const int pixelsDataSize = SPRITE_PIXELS_SIZE * 4;
		auto* data = newd uint8_t[pixelsDataSize];
		const int originX = assetCropX * SPRITE_PIXELS;
		const int originY = assetCropY * SPRITE_PIXELS;
		for (int y = 0; y < SPRITE_PIXELS; ++y) {
			for (int x = 0; x < SPRITE_PIXELS; ++x) {
				const size_t destination = (static_cast<size_t>(y) * SPRITE_PIXELS + x) * 4;
				if (originX + x >= sourceSize.width || originY + y >= sourceSize.height) {
					data[destination + 0] = 0;
					data[destination + 1] = 0;
					data[destination + 2] = 0;
					data[destination + 3] = 0;
					continue;
				}
				const size_t source = (static_cast<size_t>(originY + y) * sourceSize.width + originX + x) * 4;
				data[destination + 0] = pixels[source + 2];
				data[destination + 1] = pixels[source + 1];
				data[destination + 2] = pixels[source + 0];
				data[destination + 3] = pixels[source + 3];
			}
		}
		return data;
	}

	if (!dump) {
		if (g_settings.getInteger(Config::USE_MEMCACHED_SPRITES)) {
			return nullptr;
		}

		if (id != 0 && (pending || g_gui.gfx.isMapRenderTextureBudgetActive())) {
			std::vector<uint8_t> pixels;
			const SpritePreloadStatus status = g_spritePreloader.getOrRequest(static_cast<uint32_t>(id), pixels);
			if (status == SpritePreloadStatus::Ready) {
				auto* data = newd uint8_t[pixels.size()];
				std::memcpy(data, pixels.data(), pixels.size());
				return data;
			}
			if (status == SpritePreloadStatus::Pending) {
				if (pending) {
					*pending = true;
				}
				if (g_gui.gfx.isMapRenderTextureBudgetActive()) {
					g_gui.gfx.deferTextureUpload();
				}
				return nullptr;
			}
		}

		if (!g_gui.gfx.loadSpriteDump(dump, size, id)) {
			return nullptr;
		}
	}

	const int pixels_data_size = SPRITE_PIXELS_SIZE * 4;
	auto* data = newd uint8_t[pixels_data_size];
	bool use_alpha = g_gui.gfx.hasTransparency();
	uint8_t bpp = use_alpha ? 4 : 3;
	int write = 0;
	int read = 0;

	// decompress pixels
	while (read + 2 <= size && write < pixels_data_size) {
		int transparent = dump[read] | dump[read + 1] << 8;
		if (use_alpha && transparent >= SPRITE_PIXELS_SIZE) { // Corrupted sprite?
			break;
		}
		read += 2;
		for (int i = 0; i < transparent && write < pixels_data_size; i++) {
			data[write + 0] = 0x00; // red
			data[write + 1] = 0x00; // green
			data[write + 2] = 0x00; // blue
			data[write + 3] = 0x00; // alpha
			write += 4;
		}

		if (write >= pixels_data_size || read + 2 > size) {
			break;
		}
		int colored = dump[read] | dump[read + 1] << 8;
		read += 2;
		for (int i = 0; i < colored && write < pixels_data_size && read + bpp <= size; i++) {
			data[write + 0] = dump[read + 0]; // red
			data[write + 1] = dump[read + 1]; // green
			data[write + 2] = dump[read + 2]; // blue
			data[write + 3] = use_alpha ? dump[read + 3] : 0xFF; // alpha
			write += 4;
			read += bpp;
		}
	}

	// fill remaining pixels
	while (write < pixels_data_size) {
		data[write + 0] = 0x00; // red
		data[write + 1] = 0x00; // green
		data[write + 2] = 0x00; // blue
		data[write + 3] = 0x00; // alpha
		write += 4;
	}
	return data;
}

GLuint GameSprite::NormalImage::getHardwareID() {
	if (!atlas_loaded) {
		if (!g_gui.gfx.canPrepareTextureUpload()) {
			return 0;
		}
		uint8_t* rgba = getRGBAData();
		if (!rgba) {
			g_gui.gfx.cancelTextureUploadAttempt();
			return 0;
		}

		GLuint tex = 0;
		int px = 0;
		int py = 0;
		if (g_gui.gfx.allocAtlasSlot(tex, px, py)) {
			constexpr int PAD = 1;
			constexpr int S = SPRITE_PIXELS;
			constexpr int CW = S + 2 * PAD;

			thread_local static std::vector<uint8_t> padded;
			padded.resize(static_cast<size_t>(CW) * CW * 4);
			for (int y = 0; y < CW; ++y) {
				int sy = std::clamp(y - PAD, 0, S - 1);
				for (int x = 0; x < CW; ++x) {
					int sx = std::clamp(x - PAD, 0, S - 1);
					const uint8_t* src = rgba + (static_cast<size_t>(sy) * S + sx) * 4;
					uint8_t* dst = padded.data() + (static_cast<size_t>(y) * CW + x) * 4;
					dst[0] = src[0];
					dst[1] = src[1];
					dst[2] = src[2];
					dst[3] = src[3];
				}
			}

			glBindTexture(GL_TEXTURE_2D, tex);
			glTexSubImage2D(GL_TEXTURE_2D, 0, px, py, CW, CW, GL_RGBA, GL_UNSIGNED_BYTE, padded.data());

			auto fs = static_cast<float>(g_gui.gfx.getAtlasSize());
			atlas_tex = tex;
			au0 = (px + PAD) / fs;
			av0 = (py + PAD) / fs;
			au1 = (px + PAD + S) / fs;
			av1 = (py + PAD + S) / fs;
			atlas_loaded = true;
			isGLLoaded = true;
			g_gui.gfx.loaded_textures += 1;
			g_gui.gfx.recordTextureUpload();
		}

		delete[] rgba;
		g_gui.gfx.recordTextureUploadAttempt();
	}
	visit();
	if (atlas_loaded) {
		g_gui.gfx.touchAtlasPage(atlas_tex);
	}
	return atlas_tex;
}

void GameSprite::NormalImage::getUV(float& u0, float& v0, float& u1, float& v1) {
	if (atlas_loaded) {
		u0 = au0;
		v0 = av0;
		u1 = au1;
		v1 = av1;
	} else {
		u0 = 0.f;
		v0 = 0.f;
		u1 = 1.f;
		v1 = 1.f;
	}
}

void GameSprite::NormalImage::createGLTexture(GLuint ignored) {
	Image::createGLTexture(id);
}

void GameSprite::NormalImage::unloadGLTexture(GLuint ignored) {
	Image::unloadGLTexture(id);
}

GameSprite::EditorImage::EditorImage(const wxArtID& bitmapId) :
	NormalImage(),
	bitmapId(bitmapId) {
}

GLuint GameSprite::EditorImage::getHardwareID() {
	if (!isGLLoaded) {
		createGLTexture(id);
	}
	visit();
	return id;
}

void GameSprite::EditorImage::createGLTexture(GLuint textureId) {
	ASSERT(!isGLLoaded);

	wxSize size(SPRITE_PIXELS, SPRITE_PIXELS);
	wxBitmap bitmap = wxArtProvider::GetBitmap(bitmapId, wxART_OTHER, size);

	wxNativePixelData data(bitmap);
	if (!data) {
		return;
	}

	const int imageSize = SPRITE_PIXELS_SIZE * 4;
	auto* imageData = new GLubyte[imageSize];
	int write = 0;

	wxNativePixelData::Iterator it(data);
	it.Offset(data, 0, 0);

	for (size_t y = 0; y < SPRITE_PIXELS; ++y) {
		wxNativePixelData::Iterator row_start = it;

		for (size_t x = 0; x < SPRITE_PIXELS; ++x, ++it) {
			uint8_t red = it.Red();
			uint8_t green = it.Green();
			uint8_t blue = it.Blue();
			bool transparent = red == 0xFF && green == 0x00 && blue == 0xFF;

			imageData[write + 0] = red;
			imageData[write + 1] = green;
			imageData[write + 2] = blue;
			imageData[write + 3] = transparent ? 0x00 : 0xFF;
			write += 4;
		}

		it = row_start;
		it.OffsetY(data, 1);
	}

	isGLLoaded = true;
	id = g_gui.gfx.getFreeTextureID();
	g_gui.gfx.loaded_textures += 1;

	glBindTexture(GL_TEXTURE_2D, id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SPRITE_PIXELS, SPRITE_PIXELS, 0, GL_RGBA, GL_UNSIGNED_BYTE, imageData);

	delete[] imageData;
}

void GameSprite::EditorImage::unloadGLTexture(GLuint textureId) {
	Image::unloadGLTexture(id);
}

GameSprite::TemplateImage::TemplateImage(GameSprite* parent, int v, const Outfit& outfit) :
	gl_tid(0),
	parent(parent),
	sprite_index(v),
	lookHead(outfit.lookHead),
	lookBody(outfit.lookBody),
	lookLegs(outfit.lookLegs),
	lookFeet(outfit.lookFeet) {
	////
}

GameSprite::TemplateImage::~TemplateImage() {
	////
}

void GameSprite::TemplateImage::colorizePixel(uint8_t color, uint8_t& red, uint8_t& green, uint8_t& blue) {
	// Thanks! Khaos, or was it mips? Hmmm... =)
	uint8_t ro = (TemplateOutfitLookupTable[color] & 0xFF0000) >> 16; // rgb outfit
	uint8_t go = (TemplateOutfitLookupTable[color] & 0xFF00) >> 8;
	uint8_t bo = (TemplateOutfitLookupTable[color] & 0xFF);
	red = (uint8_t)(red * (ro / 255.f));
	green = (uint8_t)(green * (go / 255.f));
	blue = (uint8_t)(blue * (bo / 255.f));
}

uint8_t* GameSprite::TemplateImage::getRGBData() {
	uint8_t* rgbdata = parent->spriteList[sprite_index]->getRGBData();
	uint8_t* template_rgbdata = parent->spriteList[sprite_index + parent->height * parent->width]->getRGBData();

	if (!rgbdata) {
		delete[] template_rgbdata;
		return nullptr;
	}
	if (!template_rgbdata) {
		delete[] rgbdata;
		return nullptr;
	}

	if (lookHead >= (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		lookHead = 0;
	}
	if (lookBody >= (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		lookBody = 0;
	}
	if (lookLegs >= (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		lookLegs = 0;
	}
	if (lookFeet >= (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		lookFeet = 0;
	}

	for (int y = 0; y < SPRITE_PIXELS; ++y) {
		for (int x = 0; x < SPRITE_PIXELS; ++x) {
			uint8_t& red = rgbdata[y * SPRITE_PIXELS * 3 + x * 3 + 0];
			uint8_t& green = rgbdata[y * SPRITE_PIXELS * 3 + x * 3 + 1];
			uint8_t& blue = rgbdata[y * SPRITE_PIXELS * 3 + x * 3 + 2];

			uint8_t& tred = template_rgbdata[y * SPRITE_PIXELS * 3 + x * 3 + 0];
			uint8_t& tgreen = template_rgbdata[y * SPRITE_PIXELS * 3 + x * 3 + 1];
			uint8_t& tblue = template_rgbdata[y * SPRITE_PIXELS * 3 + x * 3 + 2];

			if (tred && tgreen && !tblue) { // yellow => head
				colorizePixel(lookHead, red, green, blue);
			} else if (tred && !tgreen && !tblue) { // red => body
				colorizePixel(lookBody, red, green, blue);
			} else if (!tred && tgreen && !tblue) { // green => legs
				colorizePixel(lookLegs, red, green, blue);
			} else if (!tred && !tgreen && tblue) { // blue => feet
				colorizePixel(lookFeet, red, green, blue);
			}
		}
	}
	delete[] template_rgbdata;
	return rgbdata;
}

uint8_t* GameSprite::TemplateImage::getRGBAData() {
	uint8_t* rgbadata = parent->spriteList[sprite_index]->getRGBAData();
	uint8_t* template_rgbdata = parent->spriteList[sprite_index + parent->height * parent->width]->getRGBData();

	if (!rgbadata) {
		delete[] template_rgbdata;
		return nullptr;
	}
	if (!template_rgbdata) {
		delete[] rgbadata;
		return nullptr;
	}

	if (lookHead >= (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		lookHead = 0;
	}
	if (lookBody >= (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		lookBody = 0;
	}
	if (lookLegs >= (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		lookLegs = 0;
	}
	if (lookFeet >= (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		lookFeet = 0;
	}

	for (int y = 0; y < SPRITE_PIXELS; ++y) {
		for (int x = 0; x < SPRITE_PIXELS; ++x) {
			uint8_t& red = rgbadata[y * SPRITE_PIXELS * 4 + x * 4 + 0];
			uint8_t& green = rgbadata[y * SPRITE_PIXELS * 4 + x * 4 + 1];
			uint8_t& blue = rgbadata[y * SPRITE_PIXELS * 4 + x * 4 + 2];

			uint8_t& tred = template_rgbdata[y * SPRITE_PIXELS * 3 + x * 3 + 0];
			uint8_t& tgreen = template_rgbdata[y * SPRITE_PIXELS * 3 + x * 3 + 1];
			uint8_t& tblue = template_rgbdata[y * SPRITE_PIXELS * 3 + x * 3 + 2];

			if (tred && tgreen && !tblue) { // yellow => head
				colorizePixel(lookHead, red, green, blue);
			} else if (tred && !tgreen && !tblue) { // red => body
				colorizePixel(lookBody, red, green, blue);
			} else if (!tred && tgreen && !tblue) { // green => legs
				colorizePixel(lookLegs, red, green, blue);
			} else if (!tred && !tgreen && tblue) { // blue => feet
				colorizePixel(lookFeet, red, green, blue);
			}
		}
	}
	delete[] template_rgbdata;
	return rgbadata;
}

GLuint GameSprite::TemplateImage::getHardwareID() {
	if (!isGLLoaded) {
		if (!g_gui.gfx.canPrepareTextureUpload()) {
			return 0;
		}
		if (gl_tid == 0) {
			gl_tid = g_gui.gfx.getFreeTextureID();
		}
		createGLTexture(gl_tid);
		if (!isGLLoaded) {
			g_gui.gfx.cancelTextureUploadAttempt();
			return 0;
		}
		g_gui.gfx.recordTextureUploadAttempt();
		g_gui.gfx.recordTextureUpload();
	}
	visit();
	return gl_tid;
}

void GameSprite::TemplateImage::createGLTexture(GLuint unused) {
	Image::createGLTexture(gl_tid);
}

void GameSprite::TemplateImage::unloadGLTexture(GLuint unused) {
	Image::unloadGLTexture(gl_tid);
}

GameSprite* GameSprite::createFromBitmap(const wxArtID& bitmapId) {
	auto* image = new GameSprite::EditorImage(bitmapId);

	auto* sprite = new GameSprite();
	sprite->width = 1;
	sprite->height = 1;
	sprite->layers = 1;
	sprite->pattern_x = 1;
	sprite->pattern_y = 1;
	sprite->pattern_z = 1;
	sprite->frames = 1;
	sprite->numsprites = 1;
	sprite->spriteList.push_back(image);
	return sprite;
}

// ============================================================================
// Animator

Animator::Animator(int frame_count, int start_frame, int loop_count, bool async) :
	frame_count(frame_count),
	start_frame(start_frame),
	loop_count(loop_count),
	async(async),
	current_frame(0),
	current_loop(0),
	current_duration(0),
	total_duration(0),
	direction(ANIMATION_FORWARD),
	last_time(0),
	is_complete(false) {
	ASSERT(start_frame >= -1 && start_frame < frame_count);

	for (int i = 0; i < frame_count; i++) {
		durations.push_back(newd FrameDuration(ITEM_FRAME_DURATION, ITEM_FRAME_DURATION));
	}

	reset();
}

Animator::~Animator() {
	for (int i = 0; i < frame_count; i++) {
		delete durations[i];
	}
	durations.clear();
}

int Animator::getStartFrame() const {
	if (start_frame > -1) {
		return start_frame;
	}
	return uniform_random(0, frame_count - 1);
}

FrameDuration* Animator::getFrameDuration(int frame) {
	ASSERT(frame >= 0 && frame < frame_count);
	return durations[frame];
}

int Animator::getFrame() {
	long time = g_gui.gfx.getElapsedTime();
	if (time != last_time && !is_complete) {
		long elapsed = time - last_time;
		if (elapsed >= current_duration) {
			int frame = 0;
			if (loop_count < 0) {
				frame = getPingPongFrame();
			} else {
				frame = getLoopFrame();
			}

			if (current_frame != frame) {
				int duration = getDuration(frame) - (elapsed - current_duration);
				if (duration < 0 && !async) {
					calculateSynchronous();
				} else {
					current_frame = frame;
					current_duration = std::max<int>(0, duration);
				}
			} else {
				is_complete = true;
			}
		} else {
			current_duration -= elapsed;
		}

		last_time = time;
	}
	return current_frame;
}

void Animator::setFrame(int frame) {
	ASSERT(frame == -1 || frame == 255 || frame == 254 || (frame >= 0 && frame < frame_count));

	if (current_frame == frame) {
		return;
	}

	if (async) {
		if (frame == 255) { // Async mode
			current_frame = 0;
		} else if (frame == 254) { // Random mode
			current_frame = uniform_random(0, frame_count - 1);
		} else if (frame >= 0 && frame < frame_count) {
			current_frame = frame;
		} else {
			current_frame = getStartFrame();
		}

		is_complete = false;
		last_time = g_gui.gfx.getElapsedTime();
		current_duration = getDuration(current_frame);
		current_loop = 0;
	} else {
		calculateSynchronous();
	}
}

void Animator::reset() {
	total_duration = 0;
	for (int i = 0; i < frame_count; i++) {
		total_duration += durations[i]->max;
	}

	is_complete = false;
	direction = ANIMATION_FORWARD;
	current_loop = 0;
	async = false;
	setFrame(-1);
}

int Animator::getDuration(int frame) const {
	ASSERT(frame >= 0 && frame < frame_count);
	return durations[frame]->getDuration();
}

int Animator::getPingPongFrame() {
	int count = direction == ANIMATION_FORWARD ? 1 : -1;
	int next_frame = current_frame + count;
	if (next_frame < 0 || next_frame >= frame_count) {
		direction = direction == ANIMATION_FORWARD ? ANIMATION_BACKWARD : ANIMATION_FORWARD;
		count *= -1;
	}
	return current_frame + count;
}

int Animator::getLoopFrame() {
	int next_phase = current_frame + 1;
	if (next_phase < frame_count) {
		return next_phase;
	}

	if (loop_count == 0) {
		return 0;
	}

	if (current_loop < (loop_count - 1)) {
		current_loop++;
		return 0;
	}
	return current_frame;
}

void Animator::calculateSynchronous() {
	long time = g_gui.gfx.getElapsedTime();
	if (time > 0 && total_duration > 0) {
		long elapsed = time % total_duration;
		int total_time = 0;
		for (int i = 0; i < frame_count; i++) {
			int duration = getDuration(i);
			if (elapsed >= total_time && elapsed < total_time + duration) {
				current_frame = i;
				current_duration = duration - (elapsed - total_time);
				break;
			}
			total_time += duration;
		}
		last_time = time;
	}
}
