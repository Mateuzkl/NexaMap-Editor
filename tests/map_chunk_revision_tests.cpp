#include "map_chunk_revision.h"

#include <iostream>
#include <map>
#include <stdexcept>

namespace {
	int failures = 0;

	void check(bool condition, const char* message) {
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	}

	void testChunkBoundaries() {
		const auto origin = MakeMapChunkKey(100, 200, 7);
		for (int x = 100; x < 104; ++x) {
			for (int y = 200; y < 204; ++y) {
				check(MakeMapChunkKey(x, y, 7) == origin, "all 16 tiles share a 4x4/floor key");
			}
		}
		check(MakeMapChunkKey(104, 200, 7) != origin, "east neighbor has a different key");
		check(MakeMapChunkKey(100, 204, 7) != origin, "south neighbor has a different key");
		check(MakeMapChunkKey(99, 200, 7) != origin, "west boundary has a different key");
		check(MakeMapChunkKey(100, 199, 7) != origin, "north boundary has a different key");
		check(MakeMapChunkKey(100, 200, 8) != origin, "same XY on another floor is independent");
		check(MakeMapChunkKey(65535, 65535, 15) == MakeMapChunkKey(65532, 65532, 15), "last legal tile stays in its chunk");
		check(MakeMapChunkKey(65535, 65535, 15) != MakeMapChunkKey(0, 0, 0), "map extremes never alias");
	}

	void testBulkEdit() {
		MapChunkRevisionTracker tracker;
		std::map<uint32_t, MapChunkRevision> chunks;
		const auto neighbor = MakeMapChunkKey(100, 0, 7);
		const auto otherFloor = MakeMapChunkKey(0, 0, 8);
		chunks[neighbor] = {};
		chunks[otherFloor] = {};
		{
			MapChunkRevisionTracker::Batch transaction(tracker);
			// A 100x100 edit covers 625 chunks, even with two notifications per tile
			// (installation plus the subsequent Tile::update).
			for (int x = 0; x < 100; ++x) {
				MapChunkRevisionTracker::Batch nestedAction(tracker);
				for (int y = 0; y < 100; ++y) {
					auto& chunk = chunks[MakeMapChunkKey(x, y, 7)];
					tracker.mark(chunk);
					tracker.mark(chunk);
				}
			}
		}
		const auto stats = tracker.getStats();
		check(stats.contentMarks == 20000, "bulk operation counts all invalidation requests");
		check(stats.contentChanges == 625, "nested bulk operation advances each affected chunk once");
		check(stats.coalescedMarks == 19375, "duplicate invalidations are coalesced without a pending set");
		check(tracker.read(chunks[neighbor]).content == 0, "adjacent untouched chunk remains clean");
		check(tracker.read(chunks[otherFloor]).content == 0, "edit does not invalidate another floor");
	}

	void testSelectionAndTransactions() {
		MapChunkRevisionTracker tracker;
		MapChunkRevision chunk;
		tracker.mark(chunk);
		const auto original = tracker.read(chunk);
		{
			MapChunkRevisionTracker::Batch selection(tracker);
			tracker.mark(chunk, MapChunkChange::Presentation);
			tracker.mark(chunk, MapChunkChange::Presentation);
		}
		const auto selected = tracker.read(chunk);
		check(selected.content == original.content, "selection must not invalidate content");
		check(selected.presentation != original.presentation, "selection has its own revision");
		check(tracker.getStats().presentationChanges == 1, "selection notifications coalesce");

		// Commit, undo, redo and remote apply all create new mutation intervals;
		// reverting content must never restore an old revision number.
		auto previous = selected;
		for (int operation = 0; operation < 4; ++operation) {
			{
				MapChunkRevisionTracker::Batch transaction(tracker);
				tracker.mark(chunk);
				tracker.mark(chunk);
			}
			const auto current = tracker.read(chunk);
			check(current.content > previous.content, "each separate transaction advances content revision");
			check(current.presentation == selected.presentation, "content edits preserve independent presentation revision");
			previous = current;
		}
	}

	void testDetachedCopies() {
		MapChunkRevisionTracker tracker;
		MapChunkRevision chunk;
		int installedTile = 1;
		int detachedCopy = 2;
		tracker.markIfInstalled(chunk, &installedTile, &detachedCopy, MapChunkChange::Content);
		tracker.markIfInstalled(chunk, &installedTile, &detachedCopy, MapChunkChange::Presentation);
		tracker.markIfInstalled(chunk, &installedTile, static_cast<int*>(nullptr), MapChunkChange::Content);
		check(tracker.read(chunk) == MapChunkRevision {}, "preview and undo copies cannot dirty the installed tile");
		check(tracker.getStats().contentMarks == 0, "ignored copies do not inflate content counters");
		tracker.markIfInstalled(chunk, &installedTile, &installedTile, MapChunkChange::Content);
		check(tracker.read(chunk).content != 0, "in-place edit of the installed tile invalidates content");
	}

	void testReplacementAndDeletion() {
		MapChunkRevisionTracker tracker;
		MapChunkRevision chunk;
		const int* empty = nullptr;
		int firstTile = 1;
		int replacementTile = 2;
		tracker.markReplacement(chunk, empty, empty, MapChunkChange::Content);
		check(tracker.getStats().contentMarks == 0, "clearing an already-empty location does not invalidate it");
		tracker.markReplacement(chunk, empty, &firstTile, MapChunkChange::Content);
		const auto created = tracker.read(chunk);
		tracker.markReplacement(chunk, &firstTile, &replacementTile, MapChunkChange::Content);
		const auto replaced = tracker.read(chunk);
		tracker.markReplacement(chunk, &replacementTile, empty, MapChunkChange::Content);
		const auto deleted = tracker.read(chunk);
		check(created.content != 0 && replaced.content > created.content && deleted.content > replaced.content, "create, replace and delete all advance revision, including the last tile");
		tracker.markReplacement(chunk, empty, &replacementTile, MapChunkChange::Content);
		check(tracker.read(chunk).content > deleted.content, "recreating an emptied chunk does not reuse the deleted revision");
	}

	void testReentrantReadAndException() {
		MapChunkRevisionTracker tracker;
		MapChunkRevision chunk;
		{
			MapChunkRevisionTracker::Batch transaction(tracker);
			tracker.mark(chunk);
			const auto painted = tracker.read(chunk);
			tracker.mark(chunk);
			check(tracker.read(chunk).content > painted.content, "a paint during a batch cannot hide subsequent edits");
		}
		try {
			MapChunkRevisionTracker::Batch transaction(tracker);
			tracker.mark(chunk);
			throw std::runtime_error("simulated cancellation");
		} catch (const std::runtime_error&) {
		}
		const auto before = tracker.getStats().contentChanges;
		tracker.mark(chunk);
		tracker.mark(chunk);
		check(tracker.getStats().contentChanges == before + 2, "exception closes the batch; later edits remain independent");
	}

	void testIndependentMapsAndViews() {
		MapChunkRevisionTracker firstMap;
		MapChunkRevisionTracker secondMap;
		MapChunkRevision firstChunk;
		MapChunkRevision secondChunk;
		firstMap.mark(firstChunk);
		check(secondMap.read(secondChunk).content == 0, "same coordinates in another map/session remain untouched");
		check(secondMap.getStats().contentChanges == 0, "counters belong to the map, not global resources");

		MapChunkRevisionObserver viewA;
		MapChunkRevisionObserver viewB;
		const auto key = MakeMapChunkKey(100, 100, 7);
		for (auto* view : { &viewA, &viewB }) {
			view->beginPass();
			view->observe(key, firstMap.read(firstChunk));
			check(view->getStats().firstSeen == 1, "each view starts with its own observation");
		}
		firstMap.mark(firstChunk);
		for (auto* view : { &viewA, &viewB }) {
			view->beginPass();
			view->observe(key, firstMap.read(firstChunk));
			check(view->getStats().contentChanged == 1, "reading in view A does not clear dirty state for view B");
		}
		viewA.beginPass();
		viewA.observe(key, firstMap.read(firstChunk));
		viewA.observe(key, firstMap.read(firstChunk));
		check(viewA.getStats().visible == 1 && viewA.getStats().unchanged == 1, "duplicate visits count once and unchanged content is observable");
		firstMap.mark(firstChunk, MapChunkChange::Presentation);
		viewA.beginPass();
		viewA.observe(key, firstMap.read(firstChunk));
		check(viewA.getStats().contentChanged == 0 && viewA.getStats().presentationChanged == 1, "HUD distinguishes selection from content edits");

		// Panning to unrelated regions must not accumulate a whole-map history.
		for (int x = 104; x < 1000; x += 4) {
			viewA.beginPass();
			viewA.observe(MakeMapChunkKey(x, 100, 7), {});
			check(viewA.getStats().visible == 1 && viewA.getStats().firstSeen == 1, "panning reports new visibility without claiming a cache hit");
		}
		viewA.beginPass();
		viewA.observe(key, firstMap.read(firstChunk));
		check(viewA.getStats().firstSeen == 1, "an offscreen chunk is not retained indefinitely");
		viewA.reset();
		viewA.beginPass();
		viewA.observe(key, secondMap.read(secondChunk));
		check(viewA.getStats().firstSeen == 1 && viewA.getStats().contentChanged == 0, "resource-session reset discards previous observations");
	}
}

int main() {
	testChunkBoundaries();
	testBulkEdit();
	testSelectionAndTransactions();
	testDetachedCopies();
	testReplacementAndDeletion();
	testReentrantReadAndException();
	testIndependentMapsAndViews();
	if (failures != 0) {
		std::cerr << failures << " chunk revision test(s) failed.\n";
		return 1;
	}
	std::cout << "All chunk revision model tests passed.\n";
	return 0;
}
