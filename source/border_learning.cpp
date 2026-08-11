#include "border_learning.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace {

	const std::array<Position, 8> neighbourOffsets = {
		Position(-1, -1, 0),
		Position(0, -1, 0),
		Position(1, -1, 0),
		Position(-1, 0, 0),
		Position(1, 0, 0),
		Position(-1, 1, 0),
		Position(0, 1, 0),
		Position(1, 1, 0),
	};

	constexpr std::array<size_t, 4> cardinalNeighbourIndices = { 1, 3, 4, 6 };

} // namespace

std::vector<BorderLearningTransition> BorderLearningAnalyzer::detectTransitions(const BorderLearningSnapshot& snapshot) {
	std::set<Position> selectedPositions;
	for (const auto& tile : snapshot.tiles) {
		selectedPositions.insert(tile.position);
	}

	std::map<std::pair<BorderGroundFamilyIndex, BorderGroundFamilyIndex>, size_t> contactCounts;
	for (const auto& tile : snapshot.tiles) {
		if (tile.groundFamily == BORDER_GROUND_FAMILY_NONE) {
			continue;
		}

		for (const size_t neighbourIndex : cardinalNeighbourIndices) {
			const BorderGroundFamilyIndex neighbourFamily = tile.neighbourFamilies[neighbourIndex];
			if (neighbourFamily == BORDER_GROUND_FAMILY_NONE || neighbourFamily == tile.groundFamily) {
				continue;
			}

			const Position neighbourPosition = tile.position + neighbourOffsets[neighbourIndex];
			if (!selectedPositions.contains(neighbourPosition)) {
				continue;
			}
			if (neighbourPosition < tile.position) {
				continue;
			}

			const auto familyPair = std::minmax(tile.groundFamily, neighbourFamily);
			++contactCounts[{ familyPair.first, familyPair.second }];
		}
	}

	std::vector<BorderLearningTransition> transitions;
	transitions.reserve(contactCounts.size());
	for (const auto& [families, contacts] : contactCounts) {
		transitions.push_back({ families.first, families.second, contacts });
	}
	std::sort(transitions.begin(), transitions.end(), [](const BorderLearningTransition& lhs, const BorderLearningTransition& rhs) {
		if (lhs.contacts != rhs.contacts) {
			return lhs.contacts > rhs.contacts;
		}
		if (lhs.familyA != rhs.familyA) {
			return lhs.familyA < rhs.familyA;
		}
		return lhs.familyB < rhs.familyB;
	});
	return transitions;
}
