#ifndef NEXAMAP_ITEM_ID_CODEC_H_
#define NEXAMAP_ITEM_ID_CODEC_H_

#include "iomap.h"
#include "item_id_mapping.h"

#include <cstdint>
#include <vector>

class Map;

class MappingItemIdCodec final : public ItemIdCodec {
public:
	explicit MappingItemIdCodec(ItemIdMapping::Direction direction) :
		direction(direction) {
	}

	bool Decode(uint16_t storedId, uint16_t& decodedId) const override;
	bool Encode(uint16_t memoryId, uint16_t& storedId) const override;

private:
	ItemIdMapping::Direction direction;
};

struct ItemIdConversionPreview {
	uint64_t totalItems = 0;
	uint64_t changedItems = 0;
	uint64_t missingItems = 0;
	uint64_t ambiguousItems = 0;
	std::vector<uint16_t> missingIds;
	std::vector<uint16_t> ambiguousIds;
};

ItemIdConversionPreview AnalyzeItemIdConversion(const Map& map, ItemIdMapping::Direction direction);

#endif
