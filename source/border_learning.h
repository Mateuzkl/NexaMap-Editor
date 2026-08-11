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

struct BorderLearningBorderDefinition {
	uint32_t borderId = 0;
	std::array<uint16_t, 13> items {};
};

struct BorderLearningExistingMatch {
	uint32_t borderId = 0;
	size_t matchingSlots = 0;
	size_t conflictingSlots = 0;
	size_t learnedSlots = 0;
	size_t existingSlots = 0;
	double similarity = 0.0;
	bool exact = false;
};

struct BorderLearningValidation {
	size_t matchedRoles = 0;
	size_t mismatchedRoles = 0;
	size_t unresolvedRoles = 0;
	double matchRate = 0.0;
	std::vector<Position> mismatchPositions;
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
	static std::vector<BorderLearningExistingMatch> matchExistingBorders(
		const LearnedBorderResult& result,
		const std::vector<BorderLearningBorderDefinition>& definitions
	);
	static BorderLearningValidation validateLearnedBorder(const LearnedBorderResult& result);
};

class BorderLearningSession {
public:
	void clear() noexcept;
	bool addSnapshot(
		const BorderLearningSnapshot& snapshot,
		const BorderLearningTransition& transition,
		std::string* error = nullptr
	);
	LearnedBorderResult infer(BorderMaskClassifier classifier) const;

	bool empty() const noexcept {
		return selectionCount_ == 0;
	}
	size_t getSelectionCount() const noexcept {
		return selectionCount_;
	}
	const BorderLearningSnapshot& getSnapshot() const noexcept {
		return snapshot_;
	}
	const BorderLearningTransition& getTransition() const noexcept {
		return transition_;
	}

private:
	BorderLearningSnapshot snapshot_;
	BorderLearningTransition transition_;
	uint64_t firstFamilyKey_ = 0;
	uint64_t secondFamilyKey_ = 0;
	size_t selectionCount_ = 0;
};

class BorderLearningScanner {
public:
	static BorderLearningSnapshot capture(const Selection& selection, const BaseMap& map, int floor);
};

#endif
