#ifndef NEXAMAP_MAP_CHUNK_RENDER_CACHE_H_
#define NEXAMAP_MAP_CHUNK_RENDER_CACHE_H_

#include "atlas_page_epochs.h"
#include "gl_renderer.h"
#include "map_chunk_geometry_cache.h"
#include <functional>
#include <list>

struct ChunkAtlasSprite {
	AtlasPageToken page;
	float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
};

struct MapChunkGpuStats {
	size_t visible = 0, hits = 0, misses = 0, created = 0, rebuilt = 0;
	size_t evicted = 0, fallback = 0, uploadedBytes = 0, replayedQuads = 0;
};

// One bounded GPU cache per MapDrawer. No map/resource pointers or GL calls
// from mutation paths. The owner must keep its GL context current on release.
class MapChunkRenderCache {
public:
	struct Entry {
		GLuint buffer = 0;
		size_t capacity = 0;
		uint64_t revision = 0, lastFrame = 0;
		std::array<int, 16> quads;
		std::array<GLuint, 16> textures {};
		std::vector<AtlasPageToken> pages;
		std::list<uint32_t>::iterator lru;
		Entry() {
			quads.fill(-1);
		}
	};
	using Resolve = std::function<ChunkAtlasSprite(const MapChunkGroundQuad&)>;
	using Retain = std::function<bool(AtlasPageToken)>;
	explicit MapChunkRenderCache(GLRenderer& renderer, size_t maximumChunks = 4096, size_t budgetBytes = 8 * 1024 * 1024);
	~MapChunkRenderCache();
	MapChunkRenderCache(const MapChunkRenderCache&) = delete;
	MapChunkRenderCache& operator=(const MapChunkRenderCache&) = delete;
	void beginPass(uint64_t map, uint64_t resources, bool enabled);
	const Entry* prepare(uint32_t key, uint64_t revision, const MapChunkGeometry& geometry, const Resolve& resolve, const Retain& retain);
	bool draw(const Entry& entry, size_t slot, int tileX, int tileY, const GLColor& color);
	void clear();
	const MapChunkGpuStats& getStats() const {
		return stats;
	}
	size_t getMemoryBytes() const {
		return residentBytes;
	}
	size_t getChunkCount() const {
		return entries.size();
	}
	uint64_t getUploadedTotal() const {
		return uploadedTotal;
	}

private:
	bool makeRoom(size_t additionalBytes, uint32_t protectedKey, bool adding);
	GLRenderer& renderer;
	size_t maximumChunks, budgetBytes, residentBytes = 0;
	uint64_t mapId = 0, resourceId = 0, frame = 0, uploadedTotal = 0;
	bool enabled = false;
	std::unordered_map<uint32_t, Entry> entries;
	std::list<uint32_t> order;
	MapChunkGpuStats stats;
};

#endif
