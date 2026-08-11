#include "border_learning.h"

#include <iostream>
#include <stdexcept>

namespace {

	BorderLearningTile makeTile(int x, int y, BorderGroundFamilyIndex family) {
		BorderLearningTile tile;
		tile.position = Position(x, y, 7);
		tile.groundFamily = family;
		return tile;
	}

	void require(bool condition, const char* message) {
		if (!condition) {
			throw std::runtime_error(message);
		}
	}

	std::array<BorderType, 4> classifyTestMask(uint8_t mask) {
		if (mask == TILE_EAST) {
			return { EAST_HORIZONTAL, BORDER_NONE, BORDER_NONE, BORDER_NONE };
		}
		if (mask == TILE_SOUTH) {
			return { SOUTH_HORIZONTAL, BORDER_NONE, BORDER_NONE, BORDER_NONE };
		}
		return { BORDER_NONE, BORDER_NONE, BORDER_NONE, BORDER_NONE };
	}

	BorderLearningItem makeItem(uint16_t itemId, bool alwaysOnBottom = false) {
		BorderLearningItem item;
		item.itemId = itemId;
		item.alwaysOnBottom = alwaysOnBottom;
		return item;
	}

} // namespace

int main() {
	BorderLearningSnapshot snapshot;
	snapshot.floor = 7;
	snapshot.groundFamilies.resize(3);

	auto first = makeTile(100, 100, 0);
	first.neighbourFamilies[1] = 2; // Outside the selected snapshot; must not count.
	first.neighbourFamilies[4] = 1;
	first.neighbourFamilies[6] = 1;
	snapshot.tiles.push_back(first);

	auto second = makeTile(101, 100, 1);
	second.neighbourFamilies[3] = 0;
	snapshot.tiles.push_back(second);

	auto third = makeTile(100, 101, 1);
	third.neighbourFamilies[1] = 0;
	third.neighbourFamilies[4] = 2;
	snapshot.tiles.push_back(third);

	auto fourth = makeTile(101, 101, 2);
	fourth.neighbourFamilies[3] = 1;
	snapshot.tiles.push_back(fourth);

	const auto transitions = BorderLearningAnalyzer::detectTransitions(snapshot);
	require(transitions.size() == 2, "unexpected transition count");
	require(transitions[0].familyA == 0, "dominant family A mismatch");
	require(transitions[0].familyB == 1, "dominant family B mismatch");
	require(transitions[0].contacts == 2, "selected contact deduplication failed");
	require(transitions[1].familyA == 1, "secondary family A mismatch");
	require(transitions[1].familyB == 2, "secondary family B mismatch");
	require(transitions[1].contacts == 1, "outside-selection contact was counted");

	const auto repeated = BorderLearningAnalyzer::detectTransitions(snapshot);
	require(repeated.size() == transitions.size(), "repeated analysis size mismatch");
	for (size_t index = 0; index < transitions.size(); ++index) {
		require(repeated[index].familyA == transitions[index].familyA, "non-deterministic family A");
		require(repeated[index].familyB == transitions[index].familyB, "non-deterministic family B");
		require(repeated[index].contacts == transitions[index].contacts, "non-deterministic contact count");
	}

	BorderLearningSnapshot inferenceSnapshot;
	for (int index = 0; index < 5; ++index) {
		auto tile = makeTile(200, 200 + index, 0);
		tile.neighbourFamilies[4] = 1;
		tile.items.push_back(makeItem(500, true));
		inferenceSnapshot.tiles.push_back(std::move(tile));
	}
	auto weakTile = makeTile(210, 210, 0);
	weakTile.neighbourFamilies[6] = 1;
	weakTile.items.push_back(makeItem(600));
	inferenceSnapshot.tiles.push_back(std::move(weakTile));

	const BorderLearningTransition inferenceTransition { 0, 1, 6 };
	const auto inference = BorderLearningAnalyzer::inferBorder(inferenceSnapshot, inferenceTransition, classifyTestMask);
	require(inference.assignedSlotCount == 1, "weak evidence was assigned automatically");
	require(inference.slots[EAST_HORIZONTAL].itemId == 500, "strong east candidate mismatch");
	require(inference.slots[EAST_HORIZONTAL].observations == 5, "strong candidate observation count mismatch");
	require(inference.slots[EAST_HORIZONTAL].confidence >= 0.99, "strong candidate confidence too low");
	require(inference.slots[SOUTH_HORIZONTAL].itemId == 0, "ambiguous candidate should remain unassigned");
	require(inference.slots[SOUTH_HORIZONTAL].ambiguous, "weak candidate was not marked ambiguous");
	require(inference.slots[SOUTH_HORIZONTAL].alternatives.size() == 1, "weak alternative missing");
	require(inference.unclassifiedItemIds.size() == 1 && inference.unclassifiedItemIds.front() == 600, "unclassified candidate mismatch");
	require(inference.boundaryObservations.size() == 6, "boundary evidence count mismatch");

	std::cout << "border_learning_tests passed\n";
	return 0;
}
