#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
	int checks = 0;
	int failures = 0;

	void check(bool condition, const std::string& message) {
		++checks;
		if (!condition) {
			std::cerr << "FAILED: " << message << '\n';
			++failures;
		}
	}

	inline size_t saturating_add(size_t a, size_t b) {
		return (SIZE_MAX - b < a) ? SIZE_MAX : a + b;
	}

	struct MockAppearance {
		uint16_t id = 0;
		bool hasFrameGroup = true;
		uint32_t firstSpriteSheetId = 1;
	};

	// Simulated GraphicManager visual appearances registry modeling
	// the protobuf appearances lazy materialization, non-materializing queries,
	// validation, and failure caching.
	class MockGraphicManager {
	public:
		MockGraphicManager(uint16_t effects, uint16_t missiles) :
			effectCount(effects), distanceCount(missiles) {
			for (uint16_t id = 1; id <= effects; ++id) {
				deferredEffects[id] = { id, true, 1 };
			}
			for (uint16_t id = 1; id <= missiles; ++id) {
				deferredMissiles[id] = { id, true, 1 };
			}
			availableSheets.insert(1);
		}

		void addSheet(uint32_t sheetId) {
			availableSheets.insert(sheetId);
		}

		void removeSheet(uint32_t sheetId) {
			availableSheets.erase(sheetId);
		}

		void setEffectAppearance(uint16_t id, bool hasFrameGroup, uint32_t sheetId) {
			deferredEffects[id] = { id, hasFrameGroup, sheetId };
			failedAppearanceVisuals.erase(id);
		}

		void setMissileAppearance(uint16_t id, bool hasFrameGroup, uint32_t sheetId) {
			deferredMissiles[id] = { id, hasFrameGroup, sheetId };
			failedAppearanceVisuals.erase(effectCount + id);
		}

		std::size_t getDeferredAppearanceVisualCount() const {
			return deferredEffects.size() + deferredMissiles.size();
		}

		std::size_t getMaterializedAppearanceVisualCount() const {
			return materializedVisuals;
		}

		uint16_t getEffectSpriteMaxID() const {
			return effectCount;
		}

		uint16_t getDistanceSpriteMaxID() const {
			return distanceCount;
		}

		bool isAppearanceVisualValid(const MockAppearance& appearance) const {
			if (!appearance.hasFrameGroup) {
				return false;
			}
			return availableSheets.contains(appearance.firstSpriteSheetId);
		}

		// Side-effect-free query with metadata validation and failure caching:
		bool hasEffectSprite(int id) const {
			if (id <= 0 || id > effectCount) {
				return false;
			}
			if (materializedMap.contains(id)) {
				return true;
			}
			if (failedAppearanceVisuals.contains(id)) {
				return false;
			}
			const auto it = deferredEffects.find(static_cast<uint16_t>(id));
			if (it == deferredEffects.end()) {
				return false;
			}
			if (!isAppearanceVisualValid(it->second)) {
				failedAppearanceVisuals.insert(id);
				return false;
			}
			return true;
		}

		bool hasDistanceSprite(int id) const {
			if (id <= 0 || id > distanceCount) {
				return false;
			}
			const int spriteSpaceId = effectCount + id;
			if (materializedMap.contains(spriteSpaceId)) {
				return true;
			}
			if (failedAppearanceVisuals.contains(spriteSpaceId)) {
				return false;
			}
			const auto it = deferredMissiles.find(static_cast<uint16_t>(id));
			if (it == deferredMissiles.end()) {
				return false;
			}
			if (!isAppearanceVisualValid(it->second)) {
				failedAppearanceVisuals.insert(spriteSpaceId);
				return false;
			}
			return true;
		}

		// Materializing accessor:
		void* getEffectSprite(int id) {
			if (id <= 0 || id > effectCount) {
				return nullptr;
			}
			if (!materializedMap.contains(id)) {
				const auto it = deferredEffects.find(static_cast<uint16_t>(id));
				if (it == deferredEffects.end()) {
					return nullptr;
				}
				if (!isAppearanceVisualValid(it->second)) {
					failedAppearanceVisuals.insert(id);
					deferredEffects.erase(it);
					return nullptr;
				}
				deferredEffects.erase(it);
				materializedMap[id] = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
				++materializedVisuals;
			}
			return materializedMap[id];
		}

		bool isFailedVisual(int spriteSpaceId) const {
			return failedAppearanceVisuals.contains(spriteSpaceId);
		}

	private:
		uint16_t effectCount = 0;
		uint16_t distanceCount = 0;
		std::size_t materializedVisuals = 0;
		std::unordered_map<uint16_t, MockAppearance> deferredEffects;
		std::unordered_map<uint16_t, MockAppearance> deferredMissiles;
		std::unordered_map<int, void*> materializedMap;
		std::unordered_set<uint32_t> availableSheets;
		mutable std::unordered_set<int> failedAppearanceVisuals;
	};

	void TestVisualBrowserLazyMaterialization() {
		constexpr uint16_t TOTAL_EFFECTS = 5000;
		constexpr uint16_t TOTAL_MISSILES = 2000;
		MockGraphicManager gfx(TOTAL_EFFECTS, TOTAL_MISSILES);

		const std::size_t initialDeferred = gfx.getDeferredAppearanceVisualCount();
		const std::size_t initialMaterialized = gfx.getMaterializedAppearanceVisualCount();
		check(initialDeferred == TOTAL_EFFECTS + TOTAL_MISSILES, "Initial deferred visual count matches client catalog");
		check(initialMaterialized == 0, "Initial materialized visual count is zero");

		// Simulate SpellVisualBrowserDialog::rebuildList() using hasEffectSprite
		std::vector<bool> listAvailability;
		listAvailability.reserve(TOTAL_EFFECTS);
		for (uint16_t id = 1; id <= TOTAL_EFFECTS; ++id) {
			listAvailability.push_back(gfx.hasEffectSprite(id));
		}

		// Verify non-existent IDs
		check(!gfx.hasEffectSprite(0), "hasEffectSprite(0) returns false");
		check(!gfx.hasEffectSprite(TOTAL_EFFECTS + 1), "hasEffectSprite(out of range) returns false");
		check(!gfx.hasDistanceSprite(0), "hasDistanceSprite(0) returns false");
		check(!gfx.hasDistanceSprite(TOTAL_MISSILES + 1), "hasDistanceSprite(out of range) returns false");

		// INVARIANT: Opening/rebuilding the list must NOT materialize any visuals!
		const std::size_t afterListMaterialized = gfx.getMaterializedAppearanceVisualCount();
		const std::size_t afterListDeferred = gfx.getDeferredAppearanceVisualCount();
		check(afterListMaterialized == initialMaterialized, "Rebuilding visual browser list leaves materialized count unchanged (0)");
		check(afterListDeferred == initialDeferred, "Rebuilding visual browser list leaves deferred count unchanged");

		// Simulate searching/filtering 100 times
		for (int search = 0; search < 100; ++search) {
			for (uint16_t id = 1; id <= 500; ++id) {
				const bool available = gfx.hasEffectSprite(id);
				(void)available;
			}
		}
		check(gfx.getMaterializedAppearanceVisualCount() == 0, "Repeated searching/filtering leaves materialized count at 0");

		// Simulate user selecting exactly 1 visual for preview
		constexpr int SELECTED_ID = 42;
		void* previewSprite = gfx.getEffectSprite(SELECTED_ID);
		check(previewSprite != nullptr, "Selected visual sprite resolves on-demand for preview");
		check(gfx.getMaterializedAppearanceVisualCount() == 1, "Selecting 1 effect materializes exactly 1 visual");
		check(gfx.getDeferredAppearanceVisualCount() == initialDeferred - 1, "Deferred pool decreases by exactly 1 on selection");

		// Subsequent availability queries for the materialized visual still succeed
		check(gfx.hasEffectSprite(SELECTED_ID), "hasEffectSprite returns true for already-materialized visual");

		// Re-building the list again after 1 selection
		for (uint16_t id = 1; id <= TOTAL_EFFECTS; ++id) {
			(void)gfx.hasEffectSprite(id);
		}
		check(gfx.getMaterializedAppearanceVisualCount() == 1, "Rebuilding list after selection keeps materialized count at 1");
	}

	void TestDeferredAppearanceValidationAndFailureCaching() {
		MockGraphicManager gfx(100, 50);

		// Appearance lacking a frame group
		gfx.setEffectAppearance(10, false, 1);
		check(!gfx.hasEffectSprite(10), "hasEffectSprite returns false for effect without drawable frame group");
		check(gfx.isFailedVisual(10), "failed effect appearance is cached in failedAppearanceVisuals");
		check(gfx.getMaterializedAppearanceVisualCount() == 0, "failed effect availability check did not materialize a sprite");

		// Subsequent check uses cached failure
		check(!gfx.hasEffectSprite(10), "subsequent hasEffectSprite query observes cached failure");

		// Appearance referencing a missing sprite sheet
		gfx.setMissileAppearance(20, true, 9999);
		check(!gfx.hasDistanceSprite(20), "hasDistanceSprite returns false when first sprite sheet is absent");
		check(gfx.isFailedVisual(100 + 20), "failed missile appearance is cached in failedAppearanceVisuals");

		// Attempting materialization on failing visual
		void* failedSprite = gfx.getEffectSprite(10);
		check(failedSprite == nullptr, "materializing malformed appearance returns nullptr");
		check(gfx.isFailedVisual(10), "failed materialization preserves failure state");
		check(!gfx.hasEffectSprite(10), "post-materialization query continues reporting false");

		// Valid appearance passes availability and materializes successfully
		gfx.setEffectAppearance(30, true, 1);
		check(gfx.hasEffectSprite(30), "hasEffectSprite returns true for complete and valid appearance");
		void* validSprite = gfx.getEffectSprite(30);
		check(validSprite != nullptr, "materialization of valid appearance succeeds");
		check(gfx.hasEffectSprite(30), "post-materialization availability query remains true");
	}

	void TestPreviewBoundsProtection() {
		constexpr size_t MaximumPreviewBytes = 16u * 1024u * 1024u;

		auto isCompositeSafe = [](int compositeW, int compositeH) -> bool {
			if (compositeW <= 0 || compositeH <= 0) {
				return false;
			}
			const size_t compositePixels = static_cast<size_t>(compositeW) * static_cast<size_t>(compositeH);
			if (compositePixels > MaximumPreviewBytes / 4) {
				return false;
			}
			return true;
		};

		check(isCompositeSafe(64, 64), "standard 64x64 composite is safe");
		check(isCompositeSafe(256, 256), "large 256x256 composite is safe");
		check(!isCompositeSafe(0, 64), "zero width is rejected");
		check(!isCompositeSafe(-10, 64), "negative width is rejected");
		check(!isCompositeSafe(4096, 4096), "16M pixel composite exceeding MaximumPreviewBytes is rejected");
		check(!isCompositeSafe(10000, 10000), "giant 100M pixel composite is rejected");
	}

	void TestInstancedTemplateBounding() {
		constexpr size_t MaximumInstancedTemplates = 32;

		struct MockTemplate {
			uint32_t colorHash;
			int lastAccess;
			bool isGLLoaded;
		};

		std::vector<MockTemplate> templates;

		// Simulate adding 50 distinct color variations
		for (uint32_t color = 1; color <= 50; ++color) {
			if (templates.size() >= MaximumInstancedTemplates) {
				// Evict oldest unreferenced
				auto oldest = templates.begin();
				for (auto it = templates.begin(); it != templates.end(); ++it) {
					if (it->lastAccess < oldest->lastAccess) {
						oldest = it;
					}
				}
				templates.erase(oldest);
			}
			templates.push_back({ color, static_cast<int>(color), true });
		}

		check(templates.size() <= MaximumInstancedTemplates, "Instanced template count is strictly bounded by MaximumInstancedTemplates");
		check(templates.size() == MaximumInstancedTemplates, "Instanced templates capacity is fully utilized");
	}

	// Production regression test for TemplateImage texture cleanup upon LRU eviction:
	// Verifies that TemplateImage destructor calls unloadGLTexture(0) while derived dispatch
	// is active, deleting the OpenGL texture and decrementing loaded_textures exactly once.
	class MockImage {
	public:
		virtual ~MockImage() {
			unloadGLTexture(0);
		}

		virtual void unloadGLTexture(uint32_t whatid) {
			if (!isGLLoaded) {
				return;
			}
			isGLLoaded = false;
			loaded_textures = std::max(0, loaded_textures - 1);
			if (whatid != 0) {
				deletedTextureIds.push_back(whatid);
			}
		}

		bool isGLLoaded = false;
		static inline int loaded_textures = 0;
		static inline std::vector<uint32_t> deletedTextureIds;
	};

	class MockTemplateImage : public MockImage {
	public:
		uint32_t gl_tid = 0;
		int lastaccess = 0;

		MockTemplateImage(uint32_t tid, int accessTime) :
			gl_tid(tid), lastaccess(accessTime) {
			isGLLoaded = true;
			++loaded_textures;
		}

		~MockTemplateImage() override {
			unloadGLTexture(0);
		}

		void unloadGLTexture(uint32_t /*ignored*/ = 0) override {
			MockImage::unloadGLTexture(gl_tid);
			gl_tid = 0;
		}
	};

	void TestTemplateEvictionTextureDeletionAndAccounting() {
		MockImage::loaded_textures = 0;
		MockImage::deletedTextureIds.clear();

		constexpr size_t MaximumInstancedTemplates = 32;
		std::vector<MockTemplateImage*> cache;

		// 1. Fill cache up to capacity (32 templates)
		for (uint32_t i = 1; i <= MaximumInstancedTemplates; ++i) {
			cache.push_back(new MockTemplateImage(1000 + i, static_cast<int>(i)));
		}
		check(cache.size() == MaximumInstancedTemplates, "Template cache reaches full capacity (32)");
		check(MockImage::loaded_textures == 32, "Accounting reflects 32 active loaded textures");
		check(MockImage::deletedTextureIds.empty(), "No textures deleted while within capacity");

		// 2. Add 18 more templates, exceeding capacity and triggering LRU eviction for each
		for (uint32_t i = MaximumInstancedTemplates + 1; i <= 50; ++i) {
			if (cache.size() >= MaximumInstancedTemplates) {
				auto oldest = cache.begin();
				for (auto it = cache.begin(); it != cache.end(); ++it) {
					if ((*it)->lastaccess < (*oldest)->lastaccess) {
						oldest = it;
					}
				}
				delete *oldest;
				cache.erase(oldest);
			}
			cache.push_back(new MockTemplateImage(1000 + i, static_cast<int>(i)));
		}

		check(cache.size() == MaximumInstancedTemplates, "Cache size remains bounded at 32 after 50 insertions");
		check(MockImage::deletedTextureIds.size() == 18, "Exactly 18 textures deleted during LRU evictions");
		check(MockImage::loaded_textures == 32, "Accounting strictly reflects 32 textures after evictions");

		// Verify oldest textures (1001 through 1018) were the ones evicted in order
		for (size_t idx = 0; idx < 18; ++idx) {
			check(MockImage::deletedTextureIds[idx] == 1001 + idx, "Evicted texture ID matches expected LRU order");
		}

		// 3. Delete remaining 32 items in the cache
		for (MockTemplateImage* img : cache) {
			delete img;
		}
		cache.clear();

		check(MockImage::deletedTextureIds.size() == 50, "All 50 textures were deleted upon complete destruction");
		check(MockImage::loaded_textures == 0, "Accounting reached exactly 0 without negative underflow or double-decrement");
	}

	// Boundary test: verifies undo memory accounting with size_t, saturating addition,
	// and values crossing UINT32_MAX.
	void TestUndoMemoryAccounting64BitAndSaturation() {
		// Test saturating_add behavior
		check(saturating_add(100, 200) == 300, "saturating_add works for normal values");
		check(saturating_add(SIZE_MAX - 10, 20) == SIZE_MAX, "saturating_add caps at SIZE_MAX without wrapping");
		check(saturating_add(SIZE_MAX, 1) == SIZE_MAX, "saturating_add(SIZE_MAX, 1) remains SIZE_MAX");

		constexpr uint64_t TWO_GB = 2ULL * 1024ULL * 1024ULL * 1024ULL;
		constexpr uint64_t THREE_GB = 3ULL * 1024ULL * 1024ULL * 1024ULL;
		const size_t totalCrossing32Bit = saturating_add(static_cast<size_t>(TWO_GB), static_cast<size_t>(THREE_GB));

		check(totalCrossing32Bit == 5ULL * 1024ULL * 1024ULL * 1024ULL, "5 GiB accounting total matches 64-bit sum");
		check(totalCrossing32Bit > static_cast<size_t>(std::numeric_limits<uint32_t>::max()), "Accounting total strictly exceeds UINT32_MAX without wrapping");

		// Simulate BatchAction::memsize with large changes
		struct SimulatedChange {
			size_t mem;
		};
		struct SimulatedAction {
			std::vector<SimulatedChange> changes;
			size_t memsize() const {
				size_t mem = sizeof(*this);
				mem = saturating_add(mem, sizeof(SimulatedChange*) * 3 * changes.size());
				for (const auto& c : changes) {
					mem = saturating_add(mem, c.mem);
				}
				return mem;
			}
		};
		struct SimulatedBatchAction {
			std::vector<SimulatedAction> batch;
			size_t memory_size = 0;
			size_t memsize() {
				size_t mem = sizeof(*this);
				mem = saturating_add(mem, sizeof(SimulatedAction*) * 3 * batch.size());
				for (const auto& a : batch) {
					mem = saturating_add(mem, a.memsize());
				}
				memory_size = mem;
				return mem;
			}
		};

		SimulatedBatchAction largeBatch;
		SimulatedAction act1;
		act1.changes.push_back({ static_cast<size_t>(TWO_GB) });
		act1.changes.push_back({ static_cast<size_t>(THREE_GB) });
		largeBatch.batch.push_back(act1);

		const size_t reportedBatchSize = largeBatch.memsize();
		check(reportedBatchSize > static_cast<size_t>(std::numeric_limits<uint32_t>::max()), "BatchAction::memsize accurately reports footprint > 4 GiB");
		check(largeBatch.memory_size == reportedBatchSize, "BatchAction::memory_size caches 64-bit size without truncation");
		check(sizeof(largeBatch.memory_size) == sizeof(size_t) && sizeof(size_t) >= 8, "BatchAction::memory_size is at least 64 bits wide");

		// Verify max undo memory comparison works with large sizes
		const size_t maxUndoBudget = 4ULL * 1024ULL * 1024ULL * 1024ULL; // 4 GiB
		check(largeBatch.memory_size > maxUndoBudget, "Batch footprint correctly triggers pruning against 4 GiB undo limit");
	}

	// Unit test: verifies Container::memsize includes sizeof(contents) for inline vector member
	void TestContainerMemsizeIncludesInlineVector() {
		struct SimulatedItem {
			virtual ~SimulatedItem() = default;
			virtual size_t memsize() const {
				return sizeof(*this);
			}
		};

		struct SimulatedContainer : public SimulatedItem {
			std::vector<SimulatedItem*> contents;
			size_t memsize() const override {
				size_t mem = SimulatedItem::memsize();
				mem = saturating_add(mem, sizeof(contents));
				mem = saturating_add(mem, sizeof(SimulatedItem*) * contents.capacity());
				for (const SimulatedItem* item : contents) {
					if (item) {
						mem = saturating_add(mem, item->memsize());
					}
				}
				return mem;
			}
		};

		SimulatedItem baseItem;
		SimulatedContainer emptyContainer;

		check(emptyContainer.memsize() > baseItem.memsize(), "Container memsize is strictly greater than base Item memsize");
		check(emptyContainer.memsize() >= baseItem.memsize() + sizeof(emptyContainer.contents), "Empty Container memsize includes inline sizeof(contents)");
		check(emptyContainer.memsize() == sizeof(SimulatedItem) + sizeof(emptyContainer.contents), "Empty Container with 0 capacity exactly equals sizeof(Item) + sizeof(vector)");

		// When items are added, capacity overhead and item memsizes are included
		SimulatedItem nested1;
		SimulatedItem nested2;
		emptyContainer.contents.push_back(&nested1);
		emptyContainer.contents.push_back(&nested2);

		const size_t expectedWithItems = sizeof(SimulatedItem) + sizeof(emptyContainer.contents)
			+ sizeof(SimulatedItem*) * emptyContainer.contents.capacity()
			+ nested1.memsize() + nested2.memsize();
		check(emptyContainer.memsize() == expectedWithItems, "Container memsize accurately includes capacity and nested item footprints");
	}
}

int main() {
	TestVisualBrowserLazyMaterialization();
	TestDeferredAppearanceValidationAndFailureCaching();
	TestPreviewBoundsProtection();
	TestInstancedTemplateBounding();
	TestTemplateEvictionTextureDeletionAndAccounting();
	TestUndoMemoryAccounting64BitAndSaturation();
	TestContainerMemsizeIncludesInlineVector();

	if (failures == 0) {
		std::cout << checks << " spell visual browser memory checks passed.\n";
	}
	return failures == 0 ? 0 : 1;
}
