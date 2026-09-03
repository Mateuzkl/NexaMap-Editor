#ifndef NEXAMAP_ATLAS_PAGE_EPOCHS_H_
#define NEXAMAP_ATLAS_PAGE_EPOCHS_H_

#include <cstdint>
#include <unordered_map>

struct AtlasPageToken {
	uint32_t texture = 0;
	uint64_t epoch = 0;
	bool operator==(const AtlasPageToken&) const = default;
};

// Scoped to one graphic-resource lifetime. Reusing a GL name never reuses its
// epoch, even after clear(). Move/swap this registry with the resource storage.
class AtlasPageEpochs {
public:
	AtlasPageToken replace(uint32_t texture) {
		const auto epoch = ++sequence;
		pages[texture] = epoch;
		return { texture, epoch };
	}
	AtlasPageToken get(uint32_t texture) const {
		const auto found = pages.find(texture);
		return { texture, found == pages.end() ? 0 : found->second };
	}
	bool contains(AtlasPageToken token) const {
		return token.texture != 0 && token.epoch != 0 && get(token.texture) == token;
	}
	void clear() {
		pages.clear();
	}

private:
	uint64_t sequence = 0;
	std::unordered_map<uint32_t, uint64_t> pages;
};

#endif
