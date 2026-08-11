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

#ifndef RME_GRAPHICS_H_
#define RME_GRAPHICS_H_

#include "outfit.h"
#include "common.h"
#include <chrono>
#include <deque>
#include <memory>

#include "client_version.h"
#include <wx/artprov.h>

namespace rme {
	namespace protobuf {
		namespace appearances {
			class Appearance;
		}
	}
}

enum SpriteSize {
	SPRITE_SIZE_16x16,
	// SPRITE_SIZE_24x24,
	SPRITE_SIZE_32x32,
	SPRITE_SIZE_COUNT
};

enum AnimationDirection {
	ANIMATION_FORWARD = 0,
	ANIMATION_BACKWARD = 1
};

enum ItemAnimationDuration {
	ITEM_FRAME_DURATION = 500
};

class MapCanvas;
class GraphicManager;
class GLRenderer;
class FileReadHandle;
class Animator;
class ItemType;

struct SpriteLight {
	uint8_t intensity = 0;
	uint8_t color = 0;
};

class Sprite {
public:
	Sprite() { }
	virtual ~Sprite() { }

	virtual void DrawTo(wxDC* dc, SpriteSize sz, int start_x, int start_y, int width = -1, int height = -1) = 0;
	virtual void unloadDC() = 0;

private:
	Sprite(const Sprite&);
	Sprite& operator=(const Sprite&);
};

class EditorSprite : public Sprite {
public:
	EditorSprite(wxBitmap* b16x16, wxBitmap* b32x32);
	~EditorSprite() override;

	void DrawTo(wxDC* dc, SpriteSize sz, int start_x, int start_y, int width = -1, int height = -1) override;
	void unloadDC() override;

protected:
	wxBitmap* bm[SPRITE_SIZE_COUNT];
};

class GameSprite : public Sprite {
public:
	GameSprite();
	~GameSprite() override;

	int getIndex(int width, int height, int layer, int pattern_x, int pattern_y, int pattern_z, int frame) const;
	GLuint getHardwareID(int _x, int _y, int _layer, int _subtype, int _pattern_x, int _pattern_y, int _pattern_z, int _frame);
	GLuint getHardwareID(int _x, int _y, int _dir, int _addon, int _pattern_z, const Outfit& _outfit, int _frame); // CreatureDatabase

	struct SpriteTex {
		GLuint texture = 0;
		float u0 = 0.f;
		float v0 = 0.f;
		float u1 = 1.f;
		float v1 = 1.f;
	};
	SpriteTex getSpriteTex(int _x, int _y, int _layer, int _subtype, int _pattern_x, int _pattern_y, int _pattern_z, int _frame);
	SpriteTex getSpriteTex(int _x, int _y, int _dir, int _addon, int _pattern_z, const Outfit& _outfit, int _frame);
	void DrawTo(wxDC* dc, SpriteSize sz, int start_x, int start_y, int width = -1, int height = -1) override;

	void unloadDC() override;

	void clean(int time);
	bool getVisualPreviewRGBA(std::vector<uint8_t>& pixels, int& pixelWidth, int& pixelHeight, bool& pending);

	int getDrawHeight() const;
	std::pair<int, int> getDrawOffset() const;
	uint8_t getMiniMapColor() const;

	bool hasLight() const noexcept {
		return has_light;
	}
	const SpriteLight& getLight() const noexcept {
		return light;
	}
	static GameSprite* createFromBitmap(const wxArtID& bitmapId);

protected:
	class Image;
	class NormalImage;
	class EditorImage;
	class TemplateImage;

	wxMemoryDC* getDC(SpriteSize size);
	TemplateImage* getTemplateImage(int sprite_index, const Outfit& outfit);

	class Image {
	public:
		Image();
		virtual ~Image();

		bool isGLLoaded;
		int lastaccess;

		void visit();
		virtual void clean(int time);

		virtual GLuint getHardwareID() = 0;
		virtual uint8_t* getRGBData() = 0;
		virtual uint8_t* getRGBAData() = 0;
		virtual void getUV(float& u0, float& v0, float& u1, float& v1) {
			u0 = 0.f;
			v0 = 0.f;
			u1 = 1.f;
			v1 = 1.f;
		}

	protected:
		virtual void createGLTexture(GLuint whatid);
		virtual void unloadGLTexture(GLuint whatid);
	};

	class NormalImage : public Image {
	public:
		NormalImage();
		~NormalImage() override;

		// We use the sprite id as GL texture id
		uint32_t id;

		// This contains the pixel data
		uint16_t size;
		uint8_t* dump;
		bool fromAssets = false;
		uint8_t assetCropX = 0;
		uint8_t assetCropY = 0;

		void clean(int time) override;

		GLuint getHardwareID() override;
		uint8_t* getRGBData() override;
		uint8_t* getRGBAData() override;
		uint8_t* getRGBAData(bool* pending);
		void getUV(float& u0, float& v0, float& u1, float& v1) override;

		// Sprite atlas state (only used for atlased NormalImage sprites)
		GLuint atlas_tex = 0;
		float au0 = 0.f;
		float av0 = 0.f;
		float au1 = 1.f;
		float av1 = 1.f;
		bool atlas_loaded = false;

	protected:
		void createGLTexture(GLuint ignored = 0) override;
		void unloadGLTexture(GLuint ignored = 0) override;
	};

	class EditorImage : public NormalImage {
	public:
		EditorImage(const wxArtID& bitmapId);

		GLuint getHardwareID() override;

	protected:
		void createGLTexture(GLuint textureId) override;
		void unloadGLTexture(GLuint textureId) override;

	private:
		wxArtID bitmapId;
	};

	class TemplateImage : public Image {
	public:
		TemplateImage(GameSprite* parent, int v, const Outfit& outfit);
		~TemplateImage() override;

		GLuint getHardwareID() override;
		uint8_t* getRGBData() override;
		uint8_t* getRGBAData() override;

		GLuint gl_tid;
		GameSprite* parent;
		int sprite_index;
		uint8_t lookHead;
		uint8_t lookBody;
		uint8_t lookLegs;
		uint8_t lookFeet;

	protected:
		void colorizePixel(uint8_t color, uint8_t& r, uint8_t& b, uint8_t& g);

		void createGLTexture(GLuint ignored = 0) override;
		void unloadGLTexture(GLuint ignored = 0) override;
	};

	uint32_t id;
	wxMemoryDC* dc[SPRITE_SIZE_COUNT];

public:
	// GameSprite info
	uint8_t height;
	uint8_t width;
	uint8_t layers;
	uint8_t pattern_x;
	uint8_t pattern_y;
	uint8_t pattern_z;
	uint8_t frames;
	uint32_t numsprites;

	Animator* animator;

	uint16_t draw_height;
	uint16_t drawoffset_x;
	uint16_t drawoffset_y;

	uint16_t ground_speed;
	uint16_t minimap_color;

	bool has_light = false;
	SpriteLight light;

	std::vector<NormalImage*> spriteList;
	std::list<TemplateImage*> instanced_templates; // Templates that use this sprite

	friend class GraphicManager;
};

struct FrameDuration {
	int min;
	int max;

	FrameDuration(int min, int max) :
		min(min), max(max) {
		ASSERT(min <= max);
	}

	int getDuration() const {
		if (min == max) {
			return min;
		}
		return uniform_random(min, max);
	};

	void setValues(int min_, int max_) {
		ASSERT(min_ <= max_);
		this->min = min_;
		this->max = max_;
	}
};

class Animator {
public:
	Animator(int frames, int start_frame, int loop_count, bool async);
	~Animator();

	int getStartFrame() const;

	FrameDuration* getFrameDuration(int frame);

	int getFrame();
	void setFrame(int frame);

	void reset();

private:
	int getDuration(int frame) const;
	int getPingPongFrame();
	int getLoopFrame();
	void calculateSynchronous();

	int frame_count;
	int start_frame;
	int loop_count;
	bool async;
	std::vector<FrameDuration*> durations;
	int current_frame;
	int current_loop;
	int current_duration;
	int total_duration;
	AnimationDirection direction;
	long last_time;
	bool is_complete;
};

class GraphicManager {
public:
	GraphicManager();
	~GraphicManager();

	GraphicManager(const GraphicManager&) = delete;
	GraphicManager& operator=(const GraphicManager&) = delete;

	void clear();
	void cleanSoftwareSprites();

	Sprite* getSprite(int id);
	GameSprite* getCreatureSprite(int id);
	GameSprite* getEditorSprite(int id);

	long getElapsedTime() const {
		return (animation_timer->TimeInMicro() / 1000).ToLong();
	}

	uint16_t getItemSpriteMaxID() const;

	// Get an unused texture id (this is acquired by simply increasing a value starting from 0x10000000)
	GLuint getFreeTextureID();

	// This is part of the binary
	bool loadEditorSprites();
	// Metadata should be loaded first
	// This fills the item / creature adress space
	bool loadOTFI(const FileName& filename, wxString& error, wxArrayString& warnings);
	bool loadSpriteMetadata(const FileName& datafile, wxString& error, wxArrayString& warnings);
	bool loadSpriteMetadataFlags(FileReadHandle& file, GameSprite* sType, wxString& error, wxArrayString& warnings);
	uint8_t normalizeSpriteFlag(uint8_t flag) const;
	void readSpriteFlagData(uint8_t flag, uint8_t prev_flag, FileReadHandle& file, GameSprite* sType, wxArrayString& warnings);
	bool loadSpriteData(const FileName& datafile, wxString& error, wxArrayString& warnings);
	bool loadAppearanceItem(const rme::protobuf::appearances::Appearance& appearance, ItemType* item, wxString& error, wxArrayString& warnings);
	bool loadAppearanceOutfit(const rme::protobuf::appearances::Appearance& appearance, wxString& error, wxArrayString& warnings);

	// Cleans old & unused textures according to config settings
	void garbageCollection();
	void addSpriteToCleanup(GameSprite* spr);
	void beginMapRenderTextureBudget(GLRenderer* activeRenderer, bool limitUploads = true);
	bool endMapRenderTextureBudget();
	bool canPrepareTextureUpload();
	void recordTextureUploadAttempt() noexcept;
	void cancelTextureUploadAttempt() noexcept;
	void recordTextureUpload() noexcept;
	void deferTextureUpload() noexcept;
	void markTextureMissing() noexcept;
	bool isCurrentMapRenderComplete() const noexcept {
		return !frame_had_missing_texture && !texture_upload_deferred;
	}
	bool hasPendingTextureWork() const noexcept {
		return texture_upload_deferred;
	}
	bool isMapRenderTextureBudgetActive() const noexcept {
		return texture_upload_budget_active;
	}
	int getLastFrameTextureUploads() const noexcept {
		return last_frame_texture_uploads;
	}
	int getLastFrameTextureAttempts() const noexcept {
		return last_frame_texture_attempts;
	}
	double getLastFrameTextureUploadTimeMs() const noexcept {
		return last_frame_texture_upload_time_ms;
	}

	// Sprite atlas: packs 32x32 sprites into large pages so they can be batched.
	bool allocAtlasSlot(GLuint& outTex, int& outX, int& outY);
	int getAtlasSize() const {
		return atlas_size;
	}
	size_t getAtlasPageCount() const noexcept {
		return atlas_textures.size();
	}
	size_t getAtlasMemoryBytes() const noexcept {
		return atlas_textures.size() * static_cast<size_t>(atlas_size) * static_cast<size_t>(atlas_size) * 4;
	}

	wxFileName getMetadataFileName() const {
		return metadata_file;
	}
	wxFileName getSpritesFileName() const {
		return sprites_file;
	}

	bool hasTransparency() const;
	bool isUnloaded() const;

	ClientVersion* client_version;

private:
	bool unloaded;
	// This is used if memcaching is NOT on
	std::string spritefile;
	std::unique_ptr<FileReadHandle> sprite_file_handle;
	std::vector<uint32_t> sprite_offsets;
	bool loadSpriteDump(uint8_t*& target, uint16_t& size, int sprite_id);
	GameSprite::NormalImage* getOrCreateAssetImage(uint32_t spriteId, uint8_t cropX, uint8_t cropY);
	bool loadAppearanceSprite(
		const rme::protobuf::appearances::Appearance& appearance,
		int spriteSpaceId,
		wxString& error,
		wxArrayString& warnings
	);

	typedef std::map<int, Sprite*> SpriteMap;
	SpriteMap sprite_space;
	typedef std::map<uint64_t, GameSprite::Image*> ImageMap;
	ImageMap image_space;
	std::deque<GameSprite*> cleanup_list;

	DatFormat dat_format;
	uint16_t item_count;
	uint16_t creature_count;
	bool otfi_found;
	bool is_extended;
	bool has_transparency;
	bool has_frame_durations;
	bool has_frame_groups;
	wxFileName metadata_file;
	wxFileName sprites_file;

	int loaded_textures;
	int lastclean;
	bool texture_upload_budget_active = false;
	bool texture_upload_deferred = false;
	bool frame_had_missing_texture = false;
	int frame_texture_attempts = 0;
	int frame_texture_uploads = 0;
	int last_frame_texture_attempts = 0;
	int last_frame_texture_uploads = 0;
	std::chrono::steady_clock::time_point texture_upload_attempt_started;
	std::chrono::steady_clock::duration frame_texture_upload_time {};
	double last_frame_texture_upload_time_ms = 0.0;
	bool texture_upload_attempt_active = false;

	std::vector<GLuint> atlas_textures;
	std::vector<uint64_t> atlas_page_last_use;
	std::vector<uint64_t> atlas_page_last_frame;
	uint64_t atlas_access_counter = 0;
	uint64_t atlas_frame_counter = 0;
	bool atlas_frame_active = false;
	bool atlas_allocation_failure_logged = false;
	GLRenderer* active_map_renderer = nullptr;
	int atlas_size = 0;
	int atlas_count = 0;
	bool recycleAtlasPage();
	void touchAtlasPage(GLuint texture) noexcept;

	wxStopWatch* animation_timer;

	friend class GameSprite::Image;
	friend class GameSprite::NormalImage;
	friend class GameSprite::EditorImage;
	friend class GameSprite::TemplateImage;
};

struct RGBQuad {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t reserved;

	RGBQuad(uint8_t r, uint8_t g, uint8_t b) :
		red(r), green(g), blue(b), reserved(0) { }

	operator uint32_t() {
		return (blue << 0) | (green << 8) | (red << 16);
	}

	operator bool() {
		// std::cout << "RGBQuad operator bool " << int(red) << " || " << int(blue) << " || " << int(green) << std::endl;
		return blue != 0 || red != 0 || green != 0;
	}
};

static RGBQuad minimap_color[256] = {
	RGBQuad(0, 0, 0), RGBQuad(0, 0, 51), RGBQuad(0, 0, 102), RGBQuad(0, 0, 153), // 0
	RGBQuad(0, 0, 204), RGBQuad(0, 0, 255), RGBQuad(0, 51, 0), RGBQuad(0, 51, 51), // 4
	RGBQuad(0, 51, 102), RGBQuad(0, 51, 153), RGBQuad(0, 51, 204), RGBQuad(0, 51, 255), // 8
	RGBQuad(0, 102, 0), RGBQuad(0, 102, 51), RGBQuad(0, 102, 102), RGBQuad(0, 102, 153), // 12
	RGBQuad(0, 102, 204), RGBQuad(0, 102, 255), RGBQuad(0, 153, 0), RGBQuad(0, 153, 51), // 16
	RGBQuad(0, 153, 102), RGBQuad(0, 153, 153), RGBQuad(0, 153, 204), RGBQuad(0, 153, 255), // 20
	RGBQuad(0, 204, 0), RGBQuad(0, 204, 51), RGBQuad(0, 204, 102), RGBQuad(0, 204, 153), // 24
	RGBQuad(0, 204, 204), RGBQuad(0, 204, 255), RGBQuad(0, 255, 0), RGBQuad(0, 255, 51), // 28
	RGBQuad(0, 255, 102), RGBQuad(0, 255, 153), RGBQuad(0, 255, 204), RGBQuad(0, 255, 255), // 32
	RGBQuad(51, 0, 0), RGBQuad(51, 0, 51), RGBQuad(51, 0, 102), RGBQuad(51, 0, 153), // 36
	RGBQuad(51, 0, 204), RGBQuad(51, 0, 255), RGBQuad(51, 51, 0), RGBQuad(51, 51, 51), // 40
	RGBQuad(51, 51, 102), RGBQuad(51, 51, 153), RGBQuad(51, 51, 204), RGBQuad(51, 51, 255), // 44
	RGBQuad(51, 102, 0), RGBQuad(51, 102, 51), RGBQuad(51, 102, 102), RGBQuad(51, 102, 153), // 48
	RGBQuad(51, 102, 204), RGBQuad(51, 102, 255), RGBQuad(51, 153, 0), RGBQuad(51, 153, 51), // 52
	RGBQuad(51, 153, 102), RGBQuad(51, 153, 153), RGBQuad(51, 153, 204), RGBQuad(51, 153, 255), // 56
	RGBQuad(51, 204, 0), RGBQuad(51, 204, 51), RGBQuad(51, 204, 102), RGBQuad(51, 204, 153), // 60
	RGBQuad(51, 204, 204), RGBQuad(51, 204, 255), RGBQuad(51, 255, 0), RGBQuad(51, 255, 51), // 64
	RGBQuad(51, 255, 102), RGBQuad(51, 255, 153), RGBQuad(51, 255, 204), RGBQuad(51, 255, 255), // 68
	RGBQuad(102, 0, 0), RGBQuad(102, 0, 51), RGBQuad(102, 0, 102), RGBQuad(102, 0, 153), // 72
	RGBQuad(102, 0, 204), RGBQuad(102, 0, 255), RGBQuad(102, 51, 0), RGBQuad(102, 51, 51), // 76
	RGBQuad(102, 51, 102), RGBQuad(102, 51, 153), RGBQuad(102, 51, 204), RGBQuad(102, 51, 255), // 80
	RGBQuad(102, 102, 0), RGBQuad(102, 102, 51), RGBQuad(102, 102, 102), RGBQuad(102, 102, 153), // 84
	RGBQuad(102, 102, 204), RGBQuad(102, 102, 255), RGBQuad(102, 153, 0), RGBQuad(102, 153, 51), // 88
	RGBQuad(102, 153, 102), RGBQuad(102, 153, 153), RGBQuad(102, 153, 204), RGBQuad(102, 153, 255), // 92
	RGBQuad(102, 204, 0), RGBQuad(102, 204, 51), RGBQuad(102, 204, 102), RGBQuad(102, 204, 153), // 96
	RGBQuad(102, 204, 204), RGBQuad(102, 204, 255), RGBQuad(102, 255, 0), RGBQuad(102, 255, 51), // 100
	RGBQuad(102, 255, 102), RGBQuad(102, 255, 153), RGBQuad(102, 255, 204), RGBQuad(102, 255, 255), // 104
	RGBQuad(153, 0, 0), RGBQuad(153, 0, 51), RGBQuad(153, 0, 102), RGBQuad(153, 0, 153), // 108
	RGBQuad(153, 0, 204), RGBQuad(153, 0, 255), RGBQuad(153, 51, 0), RGBQuad(153, 51, 51), // 112
	RGBQuad(153, 51, 102), RGBQuad(153, 51, 153), RGBQuad(153, 51, 204), RGBQuad(153, 51, 255), // 116
	RGBQuad(153, 102, 0), RGBQuad(153, 102, 51), RGBQuad(153, 102, 102), RGBQuad(153, 102, 153), // 120
	RGBQuad(153, 102, 204), RGBQuad(153, 102, 255), RGBQuad(153, 153, 0), RGBQuad(153, 153, 51), // 124
	RGBQuad(153, 153, 102), RGBQuad(153, 153, 153), RGBQuad(153, 153, 204), RGBQuad(153, 153, 255), // 128
	RGBQuad(153, 204, 0), RGBQuad(153, 204, 51), RGBQuad(153, 204, 102), RGBQuad(153, 204, 153), // 132
	RGBQuad(153, 204, 204), RGBQuad(153, 204, 255), RGBQuad(153, 255, 0), RGBQuad(153, 255, 51), // 136
	RGBQuad(153, 255, 102), RGBQuad(153, 255, 153), RGBQuad(153, 255, 204), RGBQuad(153, 255, 255), // 140
	RGBQuad(204, 0, 0), RGBQuad(204, 0, 51), RGBQuad(204, 0, 102), RGBQuad(204, 0, 153), // 144
	RGBQuad(204, 0, 204), RGBQuad(204, 0, 255), RGBQuad(204, 51, 0), RGBQuad(204, 51, 51), // 148
	RGBQuad(204, 51, 102), RGBQuad(204, 51, 153), RGBQuad(204, 51, 204), RGBQuad(204, 51, 255), // 152
	RGBQuad(204, 102, 0), RGBQuad(204, 102, 51), RGBQuad(204, 102, 102), RGBQuad(204, 102, 153), // 156
	RGBQuad(204, 102, 204), RGBQuad(204, 102, 255), RGBQuad(204, 153, 0), RGBQuad(204, 153, 51), // 160
	RGBQuad(204, 153, 102), RGBQuad(204, 153, 153), RGBQuad(204, 153, 204), RGBQuad(204, 153, 255), // 164
	RGBQuad(204, 204, 0), RGBQuad(204, 204, 51), RGBQuad(204, 204, 102), RGBQuad(204, 204, 153), // 168
	RGBQuad(204, 204, 204), RGBQuad(204, 204, 255), RGBQuad(204, 255, 0), RGBQuad(204, 255, 51), // 172
	RGBQuad(204, 255, 102), RGBQuad(204, 255, 153), RGBQuad(204, 255, 204), RGBQuad(204, 255, 255), // 176
	RGBQuad(255, 0, 0), RGBQuad(255, 0, 51), RGBQuad(255, 0, 102), RGBQuad(255, 0, 153), // 180
	RGBQuad(255, 0, 204), RGBQuad(255, 0, 255), RGBQuad(255, 51, 0), RGBQuad(255, 51, 51), // 184
	RGBQuad(255, 51, 102), RGBQuad(255, 51, 153), RGBQuad(255, 51, 204), RGBQuad(255, 51, 255), // 188
	RGBQuad(255, 102, 0), RGBQuad(255, 102, 51), RGBQuad(255, 102, 102), RGBQuad(255, 102, 153), // 192
	RGBQuad(255, 102, 204), RGBQuad(255, 102, 255), RGBQuad(255, 153, 0), RGBQuad(255, 153, 51), // 196
	RGBQuad(255, 153, 102), RGBQuad(255, 153, 153), RGBQuad(255, 153, 204), RGBQuad(255, 153, 255), // 200
	RGBQuad(255, 204, 0), RGBQuad(255, 204, 51), RGBQuad(255, 204, 102), RGBQuad(255, 204, 153), // 204
	RGBQuad(255, 204, 204), RGBQuad(255, 204, 255), RGBQuad(255, 255, 0), RGBQuad(255, 255, 51), // 208
	RGBQuad(255, 255, 102), RGBQuad(255, 255, 153), RGBQuad(255, 255, 204), RGBQuad(255, 255, 255), // 212
	RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), // 216
	RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), // 220
	RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), // 224
	RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), // 228
	RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), // 232
	RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), // 236
	RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), // 240
	RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), // 244
	RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), // 248
	RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0), RGBQuad(0, 0, 0) // 252
};

#endif
