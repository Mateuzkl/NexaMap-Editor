#ifndef RME_BORDER_LEARNING_H_
#define RME_BORDER_LEARNING_H_

#include "definitions.h"

#include <istream>

#include "position.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

class BaseMap;
class Selection;

using BorderGroundFamilyIndex = uint32_t;
constexpr BorderGroundFamilyIndex BORDER_GROUND_FAMILY_NONE = std::numeric_limits<BorderGroundFamilyIndex>::max();

struct BorderLearningGroundFamily {
	uint64_t key = 0;
	uint32_t brushId = 0;
	uint16_t representativeItemId = 0;
	bool knownBrush = false;
	std::string name;
	std::vector<uint16_t> itemIds;
};

struct BorderLearningItem {
	uint16_t itemId = 0;
	uint16_t clientId = 0;
	uint16_t stackIndex = 0;
	bool knownBorder = false;
	bool alwaysOnBottom = false;
	bool wall = false;
};

struct BorderLearningTile {
	Position position;
	uint16_t groundItemId = 0;
	BorderGroundFamilyIndex groundFamily = BORDER_GROUND_FAMILY_NONE;
	std::vector<BorderLearningItem> items;
	std::array<BorderGroundFamilyIndex, 8> neighbourFamilies {
		BORDER_GROUND_FAMILY_NONE,
		BORDER_GROUND_FAMILY_NONE,
		BORDER_GROUND_FAMILY_NONE,
		BORDER_GROUND_FAMILY_NONE,
		BORDER_GROUND_FAMILY_NONE,
		BORDER_GROUND_FAMILY_NONE,
		BORDER_GROUND_FAMILY_NONE,
		BORDER_GROUND_FAMILY_NONE,
	};
};

struct BorderLearningTransition {
	BorderGroundFamilyIndex familyA = BORDER_GROUND_FAMILY_NONE;
	BorderGroundFamilyIndex familyB = BORDER_GROUND_FAMILY_NONE;
	size_t contacts = 0;
};

struct BorderLearningSnapshot {
	int floor = 0;
	size_t selectedTileCount = 0;
	size_t ignoredOtherFloorTiles = 0;
	std::vector<BorderLearningGroundFamily> groundFamilies;
	std::vector<BorderLearningTile> tiles;
};

class BorderLearningAnalyzer {
public:
	static std::vector<BorderLearningTransition> detectTransitions(const BorderLearningSnapshot& snapshot);
};

class BorderLearningScanner {
public:
	static BorderLearningSnapshot capture(const Selection& selection, const BaseMap& map, int floor);
};

#endif
