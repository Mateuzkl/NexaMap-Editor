#ifndef NEXAMAP_MINIMAP_PAGE_CACHE_H_
#define NEXAMAP_MINIMAP_PAGE_CACHE_H_

#include "gl_renderer.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

class Map;
class Tile;

class MinimapPageCache {
public:
	static constexpr int PageSize = 128;
	static constexpr size_t MaximumPages = 64;
	using PixelResolver = std::function<GLColor(const Tile&)>;

	~MinimapPageCache();

	void bindMap(Map* map);
	void invalidateAll() noexcept;
	void beginVisibleFrame() noexcept;
	void endVisibleFrame(GLRenderer& renderer);
	bool drawVisible(
		GLRenderer& renderer,
		int floor,
		int startX,
		int startY,
		int endX,
		int endY,
		int viewScrollX,
		int viewScrollY,
		int floorAdjustment,
		uint64_t styleKey,
		const PixelResolver& resolvePixel
	);
	void releaseGL();

	size_t getPageCount() const noexcept {
		return pages.size();
	}
	size_t getMemoryBytes() const noexcept {
		return pages.size() * static_cast<size_t>(PageSize) * PageSize * 4;
	}

private:
	struct Page {
		GLuint texture = 0;
		int floor = 0;
		int pageX = 0;
		int pageY = 0;
		uint64_t lastUse = 0;
		uint64_t lastVisibleFrame = 0;
		bool dirty = true;
	};

	static uint64_t makeKey(int floor, int pageX, int pageY) noexcept;
	Page& getOrCreatePage(GLRenderer& renderer, int floor, int pageX, int pageY);
	bool uploadPage(Page& page, const PixelResolver& resolvePixel);
	void deletePageTexture(Page& page) noexcept;

	Map* map = nullptr;
	int mapWidth = 0;
	int mapHeight = 0;
	uint64_t activeStyleKey = 0;
	uint64_t useCounter = 0;
	uint64_t visibleFrameCounter = 0;
	bool visibleFrameActive = false;
	bool allocationFailureLogged = false;
	std::unordered_map<uint64_t, Page> pages;
	std::vector<uint8_t> uploadPixels;
};

#endif
