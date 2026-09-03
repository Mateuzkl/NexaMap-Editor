#include "map_chunk_render_cache.h"
#include <algorithm>

MapChunkRenderCache::MapChunkRenderCache(GLRenderer& renderer, size_t maximumChunks, size_t budgetBytes) :
	renderer(renderer), maximumChunks(std::max<size_t>(1, maximumChunks)), budgetBytes(budgetBytes) { }

MapChunkRenderCache::~MapChunkRenderCache() {
	clear();
}

void MapChunkRenderCache::clear() {
	for (auto& [key, entry] : entries) {
		renderer.releaseRetainedQuads(entry.buffer);
	}
	entries.clear();
	order.clear();
	residentBytes = 0;
	stats = {};
}

void MapChunkRenderCache::beginPass(uint64_t map, uint64_t resources, bool active) {
	if (!active || map != mapId || resources != resourceId) {
		clear();
	}
	mapId = map;
	resourceId = resources;
	enabled = active;
	++frame;
	stats = {};
}

bool MapChunkRenderCache::makeRoom(size_t additionalBytes, uint32_t protectedKey, bool adding) {
	while ((adding && entries.size() >= maximumChunks) || additionalBytes > budgetBytes - residentBytes) {
		if (order.empty()) {
			return false;
		}
		const uint32_t oldest = order.front();
		auto found = entries.find(oldest);
		if (oldest == protectedKey || found->second.lastFrame == frame) {
			return false; // Never evict geometry used by the current scene pass.
		}
		residentBytes -= found->second.capacity;
		renderer.releaseRetainedQuads(found->second.buffer);
		entries.erase(found);
		order.pop_front();
		++stats.evicted;
	}
	return true;
}

const MapChunkRenderCache::Entry* MapChunkRenderCache::prepare(uint32_t key, uint64_t revision, const MapChunkGeometry& geometry, const Resolve& resolve, const Retain& retain) {
	if (!enabled) {
		return nullptr;
	}
	++stats.visible;
	auto found = entries.find(key);
	if (found != entries.end()) {
		found->second.lastFrame = frame;
		order.splice(order.end(), order, found->second.lru);
		if (found->second.revision == revision && std::all_of(found->second.pages.begin(), found->second.pages.end(), retain)) {
			++stats.hits;
			return &found->second;
		}
	}
	++stats.misses;
	Entry prepared;
	std::vector<GLRenderer::Vertex> vertices;
	vertices.reserve(geometry.quadCount * 4);
	for (size_t slot = 0; slot < geometry.grounds.size(); ++slot) {
		const auto& quad = geometry.grounds[slot];
		if (!quad.itemId) {
			continue;
		}
		const auto sprite = resolve(quad);
		if (!retain(sprite.page)) {
			++stats.fallback; // Upload deferred/missing: retain live renderer behavior.
			return nullptr;
		}
		if (std::find(prepared.pages.begin(), prepared.pages.end(), sprite.page) == prepared.pages.end()) {
			prepared.pages.push_back(sprite.page);
		}
		prepared.quads[slot] = static_cast<int>(vertices.size() / 4);
		prepared.textures[slot] = sprite.page.texture;
		const float x = static_cast<float>((slot / 4) * 32) + quad.offsetX;
		const float y = static_cast<float>((slot % 4) * 32) + quad.offsetY;
		vertices.push_back({ x, y, sprite.u0, sprite.v0, 255, 255, 255, 255 });
		vertices.push_back({ x + 32, y, sprite.u1, sprite.v0, 255, 255, 255, 255 });
		vertices.push_back({ x + 32, y + 32, sprite.u1, sprite.v1, 255, 255, 255, 255 });
		vertices.push_back({ x, y + 32, sprite.u0, sprite.v1, 255, 255, 255, 255 });
	}
	const size_t bytes = vertices.size() * sizeof(GLRenderer::Vertex);
	const size_t oldCapacity = found == entries.end() ? 0 : found->second.capacity;
	if (!makeRoom(bytes > oldCapacity ? bytes - oldCapacity : 0, key, found == entries.end())) {
		++stats.fallback;
		return nullptr;
	}
	// Eviction may invalidate iterators through erase, but not surviving entries.
	found = entries.find(key);
	if (found == entries.end()) {
		order.push_back(key);
		found = entries.try_emplace(key).first;
		found->second.lru = std::prev(order.end());
	}
	Entry& entry = found->second;
	const bool newBuffer = entry.buffer == 0;
	residentBytes -= entry.capacity;
	if (vertices.empty()) {
		renderer.releaseRetainedQuads(entry.buffer);
		entry.capacity = 0;
	} else if (!renderer.uploadRetainedQuads(entry.buffer, entry.capacity, vertices)) {
		residentBytes += entry.capacity;
		order.erase(entry.lru);
		entries.erase(found);
		++stats.fallback;
		return nullptr;
	} else {
		stats.uploadedBytes += bytes;
		uploadedTotal += bytes;
		stats.created += newBuffer ? 1 : 0;
	}
	residentBytes += entry.capacity;
	entry.quads = prepared.quads;
	entry.textures = prepared.textures;
	entry.pages = std::move(prepared.pages);
	entry.revision = revision;
	entry.lastFrame = frame;
	++stats.rebuilt;
	return &entry;
}

bool MapChunkRenderCache::draw(const Entry& entry, size_t slot, int tileX, int tileY, const GLColor& color) {
	if (slot >= entry.quads.size() || entry.quads[slot] < 0) {
		return false;
	}
	if (!renderer.drawRetainedQuad(entry.buffer, entry.quads[slot], entry.textures[slot], static_cast<float>(tileX - static_cast<int>(slot / 4) * 32), static_cast<float>(tileY - static_cast<int>(slot % 4) * 32), color)) {
		return false;
	}
	++stats.replayedQuads;
	return true;
}
