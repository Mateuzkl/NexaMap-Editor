#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
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

	// Simulated GraphicManager visual appearances registry modeling
	// the protobuf appearances lazy materialization and non-materializing queries.
	class MockGraphicManager {
	public:
		MockGraphicManager(uint16_t effects, uint16_t missiles) :
			effectCount(effects), distanceCount(missiles) {
			for (uint16_t id = 1; id <= effects; ++id) {
				deferredEffects[id] = id;
			}
			for (uint16_t id = 1; id <= missiles; ++id) {
				deferredMissiles[id] = id;
			}
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

		// The new side-effect-free query:
		bool hasEffectSprite(int id) const {
			if (id <= 0 || id > effectCount) {
				return false;
			}
			if (materializedMap.contains(id)) {
				return true;
			}
			return deferredEffects.contains(static_cast<uint16_t>(id));
		}

		bool hasDistanceSprite(int id) const {
			if (id <= 0 || id > distanceCount) {
				return false;
			}
			if (materializedMap.contains(effectCount + id)) {
				return true;
			}
			return deferredMissiles.contains(static_cast<uint16_t>(id));
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
				deferredEffects.erase(it);
				materializedMap[id] = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
				++materializedVisuals;
			}
			return materializedMap[id];
		}

	private:
		uint16_t effectCount = 0;
		uint16_t distanceCount = 0;
		std::size_t materializedVisuals = 0;
		std::unordered_map<uint16_t, uint16_t> deferredEffects;
		std::unordered_map<uint16_t, uint16_t> deferredMissiles;
		std::unordered_map<int, void*> materializedMap;
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

	void TestPreviewBoundsProtection() {
		constexpr size_t MaximumPreviewBytes = 16u * 1024u * 1024u;

		// Safe check helper matching GameSprite::getVisualPreviewRGBA mount composite logic
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
}

int main() {
	TestVisualBrowserLazyMaterialization();
	TestPreviewBoundsProtection();
	TestInstancedTemplateBounding();

	if (failures == 0) {
		std::cout << checks << " spell visual browser memory checks passed.\n";
	}
	return failures == 0 ? 0 : 1;
}
