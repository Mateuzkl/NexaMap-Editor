#ifndef NEXAMAP_MAP_CHUNK_GEOMETRY_CACHE_H_
#define NEXAMAP_MAP_CHUNK_GEOMETRY_CACHE_H_

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <utility>

// One tile-local 32x32 quad. Colours and camera/floor transforms are supplied
// at replay. The image index is logical: never retain a GL name or atlas UV.
struct MapChunkGroundQuad {
	int offsetX = 0;
	int offsetY = 0;
	int elevation = 0;
	uint32_t imageIndex = 0;
	uint16_t itemId = 0; // zero means this ground stays on the live path
};

struct MapChunkGeometry {
	// Same X-major/Y-minor order as Floor::locs and DrawMap's tile traversal.
	std::array<MapChunkGroundQuad, 16> grounds {};
	size_t quadCount = 0;
};

struct MapChunkGeometryStats {
	size_t visible = 0;
	size_t hits = 0;
	size_t misses = 0;
	size_t rebuilds = 0;
	size_t replayedQuads = 0;
	double buildMs = 0;
	double lookupMs = 0;
};

// Renderer-owned, bounded CPU storage. Revisions are consumed, never cleared.
// Invalidation is lazy; no offscreen work is scheduled. FIFO limits CPU memory
// without maintaining another dirty system or touching any GPU resource.
class MapChunkGeometryCache {
public:
	explicit MapChunkGeometryCache(size_t capacity = 4096) :
		capacity(capacity == 0 ? 1 : capacity) { }

	void beginPass(uint64_t mapIdentity, uint64_t resourceIdentity, bool measureTimings = true) {
		if (mapId != mapIdentity || resourceId != resourceIdentity) {
			clear();
			mapId = mapIdentity;
			resourceId = resourceIdentity;
		}
		stats = {};
		measure = measureTimings;
	}

	template <typename Builder>
	const MapChunkGeometry& get(uint32_t chunkKey, uint64_t contentRevision, Builder&& build) {
		const auto lookupStart = measure ? Clock::now() : Clock::time_point {};
		++stats.visible;
		auto found = entries.find(chunkKey);
		const bool hit = found != entries.end() && found->second.revision == contentRevision;
		if (measure) {
			stats.lookupMs += elapsed(lookupStart);
		}
		if (hit) {
			++stats.hits;
			return found->second.geometry;
		}
		++stats.misses;
		const auto buildStart = measure ? Clock::now() : Clock::time_point {};
		MapChunkGeometry geometry = build();
		if (measure) {
			stats.buildMs += elapsed(buildStart);
		}
		++stats.rebuilds;
		if (found == entries.end()) {
			if (entries.size() == capacity) {
				residentQuads -= entries.at(insertionOrder.front()).geometry.quadCount;
				entries.erase(insertionOrder.front());
				insertionOrder.pop_front();
			}
			found = entries.emplace(chunkKey, Entry {}).first;
			insertionOrder.push_back(chunkKey);
		} else {
			residentQuads -= found->second.geometry.quadCount;
		}
		found->second = { contentRevision, std::move(geometry) };
		residentQuads += found->second.geometry.quadCount;
		return found->second.geometry;
	}

	void recordReplay() noexcept {
		++stats.replayedQuads;
	}
	void clear() {
		entries.clear();
		insertionOrder.clear();
		residentQuads = 0;
		stats = {};
	}
	const MapChunkGeometryStats& getStats() const noexcept {
		return stats;
	}
	size_t getChunkCount() const noexcept {
		return entries.size();
	}
	size_t getQuadCount() const noexcept {
		return residentQuads;
	}
	// Exact payload bytes, excluding STL buckets/nodes/allocator overhead.
	size_t getGeometryBytes() const noexcept {
		return entries.size() * sizeof(MapChunkGeometry);
	}

private:
	using Clock = std::chrono::steady_clock;
	static double elapsed(Clock::time_point start) {
		return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	}
	struct Entry {
		uint64_t revision = 0;
		MapChunkGeometry geometry;
	};
	size_t capacity;
	bool measure = true;
	uint64_t mapId = 0;
	uint64_t resourceId = 0;
	size_t residentQuads = 0;
	std::unordered_map<uint32_t, Entry> entries;
	std::deque<uint32_t> insertionOrder;
	MapChunkGeometryStats stats;
};

#endif
