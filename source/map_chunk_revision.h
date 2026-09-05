#ifndef NEXAMAP_MAP_CHUNK_REVISION_H_
#define NEXAMAP_MAP_CHUNK_REVISION_H_

#include <cstddef>
#include <cstdint>
#include <unordered_map>

// CPU metadata only. These revisions never control drawing in Phase 1.
enum class MapChunkChange {
	Content,
	Presentation,
};

struct MapChunkRevision {
	uint64_t content = 0;
	uint64_t presentation = 0;

	bool operator==(const MapChunkRevision&) const = default;
};

struct MapChunkRevisionStats {
	uint64_t contentMarks = 0;
	uint64_t presentationMarks = 0;
	uint64_t contentChanges = 0;
	uint64_t presentationChanges = 0;
	uint64_t coalescedMarks = 0;
};

// One tracker per BaseMap. Floors own the revision values; no tile, brush or
// GPU pointers are retained here. All writes happen on the map's owning thread.
class MapChunkRevisionTracker {
public:
	MapChunkRevisionTracker() = default;
	MapChunkRevisionTracker(const MapChunkRevisionTracker&) = delete;
	MapChunkRevisionTracker& operator=(const MapChunkRevisionTracker&) = delete;

	class Batch {
	public:
		explicit Batch(MapChunkRevisionTracker& tracker) noexcept :
			tracker(tracker) {
			++tracker.batchDepth;
		}
		~Batch() {
			if (--tracker.batchDepth == 0) {
				tracker.batchRevision = 0;
			}
		}
		Batch(const Batch&) = delete;
		Batch& operator=(const Batch&) = delete;

	private:
		MapChunkRevisionTracker& tracker;
	};

	void mark(MapChunkRevision& revision, MapChunkChange change = MapChunkChange::Content) noexcept {
		uint64_t& value = change == MapChunkChange::Content ? revision.content : revision.presentation;
		uint64_t& marks = change == MapChunkChange::Content ? stats.contentMarks : stats.presentationMarks;
		uint64_t& changes = change == MapChunkChange::Content ? stats.contentChanges : stats.presentationChanges;
		++marks;
		if (batchDepth == 0) {
			value = ++sequence;
			++changes;
			return;
		}
		if (batchRevision == 0) {
			batchRevision = ++sequence;
		}
		if (value == batchRevision) {
			++stats.coalescedMarks;
		} else {
			value = batchRevision;
			++changes;
		}
	}

	// A reentrant paint during a long operation must not hide subsequent writes
	// in that operation. Reading a snapshot ends the current coalescing interval.
	MapChunkRevision read(const MapChunkRevision& revision) const noexcept {
		batchRevision = 0;
		return revision;
	}

	template <typename TileType>
	void markReplacement(MapChunkRevision& revision, const TileType* oldTile, const TileType* newTile, MapChunkChange change) noexcept {
		if (oldTile || newTile) {
			mark(revision, change);
		}
	}

	template <typename TileType>
	void markIfInstalled(MapChunkRevision& revision, const TileType* installed, const TileType* candidate, MapChunkChange change) noexcept {
		if (candidate && candidate == installed) {
			mark(revision, change);
		}
	}

	const MapChunkRevisionStats& getStats() const noexcept {
		return stats;
	}

private:
	uint64_t sequence = 0;
	mutable uint64_t batchRevision = 0;
	size_t batchDepth = 0;
	MapChunkRevisionStats stats;
};

// Coordinates come from a valid TileLocation (16-bit X/Y, floor 0..15).
constexpr uint32_t MakeMapChunkKey(uint16_t x, uint16_t y, uint8_t floor) noexcept {
	return (static_cast<uint32_t>(floor) << 28) | (static_cast<uint32_t>(x >> 2) << 14) | (y >> 2);
}

struct MapChunkObservationStats {
	size_t visible = 0;
	size_t firstSeen = 0;
	size_t contentChanged = 0;
	size_t presentationChanged = 0;
	size_t unchanged = 0;
};

// Diagnostic comparison with the preceding sampled map pass, NOT cache hits.
// Each view has its own observer. Only two visible sets are retained, by value.
class MapChunkRevisionObserver {
public:
	void beginPass() {
		previous.swap(current);
		current.clear();
		stats = {};
	}
	void observe(uint32_t key, MapChunkRevision revision) {
		if (!current.emplace(key, revision).second) {
			return;
		}
		++stats.visible;
		const auto found = previous.find(key);
		if (found == previous.end()) {
			++stats.firstSeen;
			return;
		}
		stats.contentChanged += found->second.content != revision.content;
		stats.presentationChanged += found->second.presentation != revision.presentation;
		stats.unchanged += found->second == revision;
	}
	void reset() {
		std::unordered_map<uint32_t, MapChunkRevision>().swap(previous);
		std::unordered_map<uint32_t, MapChunkRevision>().swap(current);
		stats = {};
	}
	const MapChunkObservationStats& getStats() const noexcept {
		return stats;
	}

private:
	std::unordered_map<uint32_t, MapChunkRevision> previous;
	std::unordered_map<uint32_t, MapChunkRevision> current;
	MapChunkObservationStats stats;
};

#endif
