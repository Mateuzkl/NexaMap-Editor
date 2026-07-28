#include "main.h"

#include "item_id_codec.h"

#include "complexitem.h"
#include "map.h"

#include <array>

bool MappingItemIdCodec::Decode(uint16_t storedId, uint16_t& decodedId) const {
	decodedId = ItemIdMapping::convert(storedId, direction).converted;
	return true;
}

bool MappingItemIdCodec::Encode(uint16_t memoryId, uint16_t& storedId) const {
	storedId = ItemIdMapping::convert(memoryId, direction).converted;
	return true;
}

ItemIdConversionPreview AnalyzeItemIdConversion(const Map& map, ItemIdMapping::Direction direction) {
	ItemIdConversionPreview preview;
	std::array<bool, 1u << 16> missingSeen {};
	std::array<bool, 1u << 16> ambiguousSeen {};
	std::vector<const Item*> pending;
	pending.reserve(64);

	const auto inspect = [&](const Item* root) {
		pending.push_back(root);
		while (!pending.empty()) {
			const Item* item = pending.back();
			pending.pop_back();
			if (!item || item->isMetaItem()) {
				continue;
			}

			const ItemIdMapping::Result result = ItemIdMapping::convert(item->getID(), direction);
			++preview.totalItems;
			preview.changedItems += result.converted != result.original ? 1 : 0;
			preview.missingItems += result.found ? 0 : 1;
			preview.ambiguousItems += result.ambiguous ? 1 : 0;
			if (!result.found && !missingSeen[result.original]) {
				missingSeen[result.original] = true;
				preview.missingIds.push_back(result.original);
			}
			if (result.ambiguous && !ambiguousSeen[result.original]) {
				ambiguousSeen[result.original] = true;
				preview.ambiguousIds.push_back(result.original);
			}

			const auto* container = dynamic_cast<const Container*>(item);
			if (container) {
				for (size_t index = 0; index < container->getItemCount(); ++index) {
					pending.push_back(container->getItem(index));
				}
			}
		}
	};

	for (MapIterator iterator = const_cast<Map&>(map).begin(); iterator != const_cast<Map&>(map).end(); ++iterator) {
		const Tile* tile = (*iterator)->get();
		if (!tile) {
			continue;
		}
		inspect(tile->ground);
		for (const Item* item : tile->items) {
			inspect(item);
		}
	}
	return preview;
}
