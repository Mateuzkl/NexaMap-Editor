#include "main.h"

#include "minimap_page_cache.h"

#include "graphics.h"
#include "gui.h"
#include "map.h"
#include "map_region.h"

#include <algorithm>
#include <limits>

MinimapPageCache::~MinimapPageCache() = default;

void MinimapPageCache::bindMap(Map* newMap) {
	const int newWidth = newMap ? newMap->getWidth() : 0;
	const int newHeight = newMap ? newMap->getHeight() : 0;
	if (map == newMap && mapWidth == newWidth && mapHeight == newHeight) {
		return;
	}

	releaseGL();
	map = newMap;
	mapWidth = newWidth;
	mapHeight = newHeight;
}

void MinimapPageCache::invalidateAll() noexcept {
	for (auto& [key, page] : pages) {
		page.dirty = true;
	}
}

void MinimapPageCache::beginVisibleFrame() noexcept {
	visibleFrameActive = true;
	++visibleFrameCounter;
}

void MinimapPageCache::endVisibleFrame(GLRenderer& renderer) {
	visibleFrameActive = false;
	size_t visiblePageCount = 0;
	for (const auto& [key, page] : pages) {
		if (page.lastVisibleFrame == visibleFrameCounter) {
			++visiblePageCount;
		}
	}

	const size_t retainedPageCount = std::max(MaximumPages, visiblePageCount);
	while (pages.size() > retainedPageCount) {
		auto victim = pages.end();
		uint64_t oldestUse = std::numeric_limits<uint64_t>::max();
		for (auto candidate = pages.begin(); candidate != pages.end(); ++candidate) {
			if (candidate->second.lastVisibleFrame == visibleFrameCounter) {
				continue;
			}
			if (candidate->second.lastUse < oldestUse) {
				oldestUse = candidate->second.lastUse;
				victim = candidate;
			}
		}
		if (victim == pages.end()) {
			break;
		}

		renderer.flush();
		deletePageTexture(victim->second);
		pages.erase(victim);
	}
}

bool MinimapPageCache::drawVisible(
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
) {
	if (!map || floor < 0 || floor >= MAP_LAYERS || mapWidth <= 0 || mapHeight <= 0) {
		return true;
	}
	if (styleKey != activeStyleKey) {
		activeStyleKey = styleKey;
		invalidateAll();
	}
	if (endX < 0 || endY < 0 || startX >= mapWidth || startY >= mapHeight) {
		return true;
	}

	startX = std::clamp(startX, 0, mapWidth - 1);
	startY = std::clamp(startY, 0, mapHeight - 1);
	endX = std::clamp(endX, 0, mapWidth - 1);
	endY = std::clamp(endY, 0, mapHeight - 1);
	if (startX > endX || startY > endY) {
		return true;
	}

	const int startPageX = startX / PageSize;
	const int startPageY = startY / PageSize;
	const int endPageX = endX / PageSize;
	const int endPageY = endY / PageSize;
	bool complete = true;
	for (int pageY = startPageY; pageY <= endPageY; ++pageY) {
		for (int pageX = startPageX; pageX <= endPageX; ++pageX) {
			Page& page = getOrCreatePage(renderer, floor, pageX, pageY);
			page.lastUse = ++useCounter;
			page.lastVisibleFrame = visibleFrameCounter;
			if (page.dirty && !uploadPage(page, resolvePixel)) {
				complete = false;
				g_gui.gfx.markTextureMissing();
			}
			if (page.texture == 0) {
				continue;
			}

			const float worldX = static_cast<float>(pageX * PageSize * TileSize - viewScrollX - floorAdjustment);
			const float worldY = static_cast<float>(pageY * PageSize * TileSize - viewScrollY - floorAdjustment);
			const float worldSize = static_cast<float>(PageSize * TileSize);
			renderer.drawTexturedQuad(worldX, worldY, worldSize, worldSize, page.texture, { 255, 255, 255, 255 });
		}
	}
	return complete;
}

void MinimapPageCache::releaseGL() {
	for (auto& [key, page] : pages) {
		deletePageTexture(page);
	}
	pages.clear();
	uploadPixels.clear();
	activeStyleKey = 0;
	useCounter = 0;
	visibleFrameCounter = 0;
	visibleFrameActive = false;
	allocationFailureLogged = false;
}

uint64_t MinimapPageCache::makeKey(int floor, int pageX, int pageY) noexcept {
	return (static_cast<uint64_t>(static_cast<uint8_t>(floor)) << 56)
		| (static_cast<uint64_t>(static_cast<uint32_t>(pageY)) << 28)
		| static_cast<uint32_t>(pageX);
}

MinimapPageCache::Page& MinimapPageCache::getOrCreatePage(GLRenderer& renderer, int floor, int pageX, int pageY) {
	const uint64_t key = makeKey(floor, pageX, pageY);
	const auto existing = pages.find(key);
	if (existing != pages.end()) {
		return existing->second;
	}

	if (!visibleFrameActive && pages.size() >= MaximumPages) {
		auto victim = pages.end();
		uint64_t oldestUse = std::numeric_limits<uint64_t>::max();
		for (auto candidate = pages.begin(); candidate != pages.end(); ++candidate) {
			if (candidate->second.lastUse < oldestUse) {
				oldestUse = candidate->second.lastUse;
				victim = candidate;
			}
		}
		if (victim != pages.end()) {
			renderer.flush();
			deletePageTexture(victim->second);
			pages.erase(victim);
		}
	}

	Page page;
	page.floor = floor;
	page.pageX = pageX;
	page.pageY = pageY;
	return pages.emplace(key, std::move(page)).first->second;
}

bool MinimapPageCache::uploadPage(Page& page, const PixelResolver& resolvePixel) {
	if (!g_gui.gfx.canPrepareTextureUpload()) {
		return false;
	}

	uploadPixels.assign(static_cast<size_t>(PageSize) * PageSize * 4, 0);
	const int originX = page.pageX * PageSize;
	const int originY = page.pageY * PageSize;
	const int pageEndX = std::min(mapWidth, originX + PageSize);
	const int pageEndY = std::min(mapHeight, originY + PageSize);
	const int nodeStartX = originX & ~3;
	const int nodeStartY = originY & ~3;
	for (int nodeX = nodeStartX; nodeX < pageEndX; nodeX += 4) {
		for (int nodeY = nodeStartY; nodeY < pageEndY; nodeY += 4) {
			QTreeNode* node = map->getLeaf(nodeX, nodeY);
			if (!node || !node->getFloor(page.floor)) {
				continue;
			}
			for (int localX = 0; localX < 4; ++localX) {
				const int mapX = nodeX + localX;
				if (mapX < originX || mapX >= pageEndX) {
					continue;
				}
				for (int localY = 0; localY < 4; ++localY) {
					const int mapY = nodeY + localY;
					if (mapY < originY || mapY >= pageEndY) {
						continue;
					}
					TileLocation* location = node->getTile(localX, localY, page.floor);
					const Tile* tile = location ? location->get() : nullptr;
					if (!tile) {
						continue;
					}
					const GLColor color = resolvePixel(*tile);
					const size_t pixel = (static_cast<size_t>(mapY - originY) * PageSize + (mapX - originX)) * 4;
					uploadPixels[pixel + 0] = color.r;
					uploadPixels[pixel + 1] = color.g;
					uploadPixels[pixel + 2] = color.b;
					uploadPixels[pixel + 3] = color.a;
				}
			}
		}
	}

	if (page.texture == 0) {
		glGenTextures(1, &page.texture);
	}
	if (page.texture == 0) {
		if (!allocationFailureLogged) {
			wxLogError("MinimapPageCache::uploadPage - OpenGL could not allocate a minimap page texture.");
			allocationFailureLogged = true;
		}
		g_gui.gfx.recordTextureUploadAttempt();
		return false;
	}
	allocationFailureLogged = false;
	glBindTexture(GL_TEXTURE_2D, page.texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, PageSize, PageSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, uploadPixels.data());
	page.dirty = false;
	g_gui.gfx.recordTextureUploadAttempt();
	g_gui.gfx.recordTextureUpload();
	return true;
}

void MinimapPageCache::deletePageTexture(Page& page) noexcept {
	if (page.texture == 0) {
		return;
	}
	GLRenderer::invalidateTexture(page.texture);
	glDeleteTextures(1, &page.texture);
	page.texture = 0;
}
