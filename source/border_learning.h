#ifndef RME_BORDER_LEARNING_H_
#define RME_BORDER_LEARNING_H_

#include "brush_enums.h"
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
	bool doodad = false;
	bool technical = false;
	bool optionalBorder = false;
	BorderType knownAlignment = BORDER_NONE;
	uint32_t borderGroup = 0;
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

struct BorderLearningCandidate {
	uint16_t itemId = 0;
	size_t observations = 0;
	size_t totalOccurrences = 0;
	double confidence = 0.0;
	double purity = 0.0;
	double boundaryOccurrenceRate = 0.0;
	double averageStackIndex = 0.0;
	bool knownBorder = false;
	bool alwaysOnBottom = false;
	bool optionalBorder = false;
	uint32_t borderGroup = 0;
	std::vector<Position> evidence;
};

struct LearnedBorderSlot {
	BorderType edge = BORDER_NONE;
	uint16_t itemId = 0;
	size_t observations = 0;
	double confidence = 0.0;
	bool ambiguous = false;
	std::vector<Position> evidence;
	std::vector<BorderLearningCandidate> alternatives;
};

struct BorderLearningBoundaryObservation {
	Position position;
	uint8_t mask = 0;
	std::vector<BorderType> expectedEdges;
	std::vector<uint16_t> candidateItemIds;
};

struct LearnedBorderResult {
	BorderLearningTransition transition;
	std::array<LearnedBorderSlot, 13> slots;
	std::vector<uint16_t> unclassifiedItemIds;
	std::vector<Position> ambiguousTiles;
	std::vector<BorderLearningBoundaryObservation> boundaryObservations;
	double overallConfidence = 0.0;
	size_t assignedSlotCount = 0;
};

using BorderMaskClassifier = std::array<BorderType, 4> (*)(uint8_t);

class BorderLearningAnalyzer {
public:
	static std::vector<BorderLearningTransition> detectTransitions(const BorderLearningSnapshot& snapshot);
	static LearnedBorderResult inferBorder(
		const BorderLearningSnapshot& snapshot,
		const BorderLearningTransition& transition,
		BorderMaskClassifier classifier
	);
};

class BorderLearningScanner {
public:
	static BorderLearningSnapshot capture(const Selection& selection, const BaseMap& map, int floor);
};

#endif
