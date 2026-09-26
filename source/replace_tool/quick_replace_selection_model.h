//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_REPLACE_TOOL_QUICK_REPLACE_SELECTION_MODEL_H_
#define RME_REPLACE_TOOL_QUICK_REPLACE_SELECTION_MODEL_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Item;
class ItemType;
class Tile;

enum class QuickReplaceCategory : uint8_t {
	Ground,
	Border,
	Wall,
	Door,
	Carpet,
	Table,
	Doodad,
	Item,
};

struct QuickReplaceCandidateKey {
	uint16_t mapItemId = 0;
	QuickReplaceCategory category = QuickReplaceCategory::Item;

	bool operator==(const QuickReplaceCandidateKey&) const = default;
};

struct QuickReplaceCandidate {
	uint16_t mapItemId = 0;
	uint16_t spriteClientId = 0;
	size_t count = 0;
	QuickReplaceCategory category = QuickReplaceCategory::Item;
	std::string name;
};

[[nodiscard]] QuickReplaceCategory ClassifyPlacedItem(const Tile& tile, const Item& item);
[[nodiscard]] QuickReplaceCategory ClassifyItemType(const ItemType& type);
[[nodiscard]] const char* QuickReplaceCategoryName(QuickReplaceCategory category);
[[nodiscard]] std::vector<QuickReplaceCandidate> CollectQuickReplaceCandidates(const std::vector<Tile*>& tiles);

#endif
