//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "../main.h"
#include "quick_replace_selection_model.h"

#include "../item.h"
#include "../items.h"
#include "../tile.h"

#include <algorithm>
#include <unordered_map>

namespace {
	int CategoryOrder(QuickReplaceCategory category) {
		return static_cast<int>(category);
	}

	struct CandidateKeyHash {
		size_t operator()(const QuickReplaceCandidateKey& key) const noexcept {
			return (static_cast<size_t>(key.mapItemId) << 8U) ^ static_cast<size_t>(key.category);
		}
	};
}

QuickReplaceCategory ClassifyPlacedItem(const Tile& tile, const Item& item) {
	if (tile.ground == &item) {
		return QuickReplaceCategory::Ground;
	}
	if (item.isBorder()) {
		return QuickReplaceCategory::Border;
	}
	if (item.isDoor() || item.isBrushDoor()) {
		return QuickReplaceCategory::Door;
	}
	if (item.isCarpet()) {
		return QuickReplaceCategory::Carpet;
	}
	if (item.isTable()) {
		return QuickReplaceCategory::Table;
	}
	if (item.isWall()) {
		return QuickReplaceCategory::Wall;
	}
	if (item.getDoodadBrush() != nullptr) {
		return QuickReplaceCategory::Doodad;
	}
	return QuickReplaceCategory::Item;
}

QuickReplaceCategory ClassifyItemType(const ItemType& type) {
	if (type.isGroundTile()) {
		return QuickReplaceCategory::Ground;
	}
	if (type.isBorder) {
		return QuickReplaceCategory::Border;
	}
	if (type.isDoor() || type.isBrushDoor) {
		return QuickReplaceCategory::Door;
	}
	if (type.isCarpet) {
		return QuickReplaceCategory::Carpet;
	}
	if (type.isTable) {
		return QuickReplaceCategory::Table;
	}
	if (type.isWall) {
		return QuickReplaceCategory::Wall;
	}
	if (type.doodad_brush != nullptr) {
		return QuickReplaceCategory::Doodad;
	}
	return QuickReplaceCategory::Item;
}

const char* QuickReplaceCategoryName(QuickReplaceCategory category) {
	switch (category) {
		case QuickReplaceCategory::Ground:
			return "Floor";
		case QuickReplaceCategory::Border:
			return "Border";
		case QuickReplaceCategory::Wall:
			return "Wall";
		case QuickReplaceCategory::Door:
			return "Door";
		case QuickReplaceCategory::Carpet:
			return "Carpet";
		case QuickReplaceCategory::Table:
			return "Table";
		case QuickReplaceCategory::Doodad:
			return "Doodad";
		case QuickReplaceCategory::Item:
			return "Item";
	}
	return "Item";
}

std::vector<QuickReplaceCandidate> CollectQuickReplaceCandidates(const std::vector<Tile*>& tiles) {
	std::unordered_map<QuickReplaceCandidateKey, QuickReplaceCandidate, CandidateKeyHash> candidatesByKey;
	const auto add = [&candidatesByKey](const Tile& tile, const Item* item) {
		if (!item || item->getID() == 0 || item->isMetaItem()) {
			return;
		}
		const uint16_t id = item->getID();
		const QuickReplaceCategory category = ClassifyPlacedItem(tile, *item);
		const QuickReplaceCandidateKey key { id, category };
		auto [iterator, inserted] = candidatesByKey.try_emplace(key);
		QuickReplaceCandidate& candidate = iterator->second;
		if (inserted) {
			const ItemType& type = g_items.getItemType(id);
			candidate.mapItemId = id;
			candidate.spriteClientId = type.clientID;
			candidate.category = category;
			candidate.name = type.name.empty() ? "Unnamed item" : type.name;
		}
		++candidate.count;
	};

	for (const Tile* tile : tiles) {
		if (!tile) {
			continue;
		}
		add(*tile, tile->ground);
		for (const Item* item : tile->items) {
			add(*tile, item);
		}
	}

	std::vector<QuickReplaceCandidate> candidates;
	candidates.reserve(candidatesByKey.size());
	for (auto& entry : candidatesByKey) {
		candidates.push_back(std::move(entry.second));
	}
	std::sort(candidates.begin(), candidates.end(), [](const QuickReplaceCandidate& left, const QuickReplaceCandidate& right) {
		const int leftCategory = CategoryOrder(left.category);
		const int rightCategory = CategoryOrder(right.category);
		if (leftCategory != rightCategory) {
			return leftCategory < rightCategory;
		}
		if (left.count != right.count) {
			return left.count > right.count;
		}
		return left.mapItemId < right.mapItemId;
	});
	return candidates;
}
