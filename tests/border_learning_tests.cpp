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

	std::cout << "border_learning_tests passed\n";
	return 0;
}
