#include "map_chunk_geometry_cache.h"
#include "map_chunk_revision.h"

#include <iostream>
#include <stdexcept>

namespace {
	void require(bool condition, const char* message) {
		if (!condition) {
			throw std::runtime_error(message);
		}
	}
	MapChunkGeometry ground(uint16_t id) {
		MapChunkGeometry result;
		result.grounds[0] = { -8, -12, 4, 31, id };
		result.quadCount = 1;
		return result;
	}
}

int main() {
	try {
		MapChunkRevisionTracker tracker;
		MapChunkRevision revision;
		MapChunkGeometryCache cache(3);
		const auto key = MakeMapChunkKey(100, 100, 7);
		int builds = 0;
		auto build = [&] { ++builds; return ground(100); };
		tracker.mark(revision);
		cache.beginPass(1, 10);
		const auto& first = cache.get(key, tracker.read(revision).content, build);
		require(first.grounds[0].offsetX == -8 && first.grounds[0].imageIndex == 31, "quad geometry preserved");
		require(cache.getStats().misses == 1 && cache.getStats().rebuilds == 1, "cold miss");
		for (int pan = 0; pan < 20; ++pan) {
			tracker.mark(revision, MapChunkChange::Presentation);
			cache.beginPass(1, 10);
			cache.get(key, tracker.read(revision).content, build);
			require(cache.getStats().hits == 1 && cache.getStats().rebuilds == 0, "presentation/camera reuse");
		}
		require(builds == 1, "no geometry generation on hits");
		{
			MapChunkRevisionTracker::Batch batch(tracker);
			for (int i = 0; i < 16; ++i) {
				tracker.mark(revision);
			}
		}
		require(builds == 1, "offscreen edit is lazy");
		cache.beginPass(1, 10);
		cache.get(key, tracker.read(revision).content, build);
		require(builds == 2, "one rebuild after coalesced edit");
		cache.get(MakeMapChunkKey(104, 100, 7), revision.content, build);
		cache.get(MakeMapChunkKey(100, 100, 8), revision.content, build);
		require(cache.getChunkCount() == 3 && cache.getQuadCount() == 3, "boundary/floor isolation");
		cache.get(key, ++revision.content, [] { return MapChunkGeometry {}; });
		require(cache.getQuadCount() == 2, "delete clears geometry");
		cache.get(key, ++revision.content, build);
		require(cache.getQuadCount() == 3, "recreate replaces deleted geometry");
		const auto previousRevision = revision.content;
		try {
			cache.get(key, revision.content + 1, []() -> MapChunkGeometry { throw std::runtime_error("build failure"); });
		} catch (const std::runtime_error&) { }
		cache.beginPass(1, 10);
		cache.get(key, previousRevision, build);
		require(cache.getStats().hits == 1, "failed build does not publish a revision");
		for (int x = 200; x < 800; x += 4) {
			cache.get(MakeMapChunkKey(x, 100, 7), revision.content, build);
		}
		require(cache.getChunkCount() == 3 && cache.getQuadCount() == 3, "bounded CPU storage");
		require(cache.getGeometryBytes() == 3 * sizeof(MapChunkGeometry), "payload accounting");
		cache.beginPass(2, 10);
		cache.get(key, revision.content, build);
		require(cache.getStats().misses == 1 && cache.getChunkCount() == 1, "different map cannot reuse same revisions");
		cache.beginPass(2, 11);
		cache.get(key, revision.content, build);
		require(cache.getStats().misses == 1, "resource reload invalidates logical image indices");
		MapChunkGeometryCache secondView;
		secondView.beginPass(2, 11);
		secondView.get(key, revision.content, build);
		require(secondView.getStats().misses == 1, "views own independent caches");
		cache.clear();
		require(cache.getGeometryBytes() == 0 && cache.getQuadCount() == 0, "cache OFF releases geometry");
		std::cout << "CPU chunk geometry cache tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
