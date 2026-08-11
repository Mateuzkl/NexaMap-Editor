#include "border_learning.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace {

	const std::array<Position, 8> analysisNeighbourOffsets = {
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
	constexpr size_t maxEvidencePerCandidate = 16;

	bool isCanonicalBorderType(BorderType edge) {
		return edge >= NORTH_HORIZONTAL && edge <= SOUTHWEST_DIAGONAL;
	}

	bool isCandidateItem(const BorderLearningItem& item) {
		return !item.wall && !item.technical && (!item.doodad || item.knownBorder);
	}

	struct CandidateStats {
		std::array<size_t, 13> edgeCounts {};
		std::array<std::vector<Position>, 13> evidence;
		size_t boundaryOccurrences = 0;
		size_t totalOccurrences = 0;
		size_t alwaysOnBottomOccurrences = 0;
		size_t optionalOccurrences = 0;
		double stackIndexTotal = 0.0;
		bool knownBorder = false;
		BorderType knownAlignment = BORDER_NONE;
		uint32_t borderGroup = 0;
		bool conflictingBorderGroups = false;
	};

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

			const Position neighbourPosition = tile.position + analysisNeighbourOffsets[neighbourIndex];
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

LearnedBorderResult BorderLearningAnalyzer::inferBorder(
	const BorderLearningSnapshot& snapshot,
	const BorderLearningTransition& transition,
	BorderMaskClassifier classifier
) {
	LearnedBorderResult result;
	result.transition = transition;
	for (size_t edge = 0; edge < result.slots.size(); ++edge) {
		result.slots[edge].edge = static_cast<BorderType>(edge);
	}
	if (!classifier || transition.familyA == BORDER_GROUND_FAMILY_NONE || transition.familyB == BORDER_GROUND_FAMILY_NONE) {
		return result;
	}

	std::map<uint16_t, CandidateStats> candidateStats;
	for (const auto& tile : snapshot.tiles) {
		for (const auto& item : tile.items) {
			++candidateStats[item.itemId].totalOccurrences;
		}
	}

	std::set<Position> ambiguousPositions;
	for (const auto& tile : snapshot.tiles) {
		BorderGroundFamilyIndex otherFamily = BORDER_GROUND_FAMILY_NONE;
		if (tile.groundFamily == transition.familyA) {
			otherFamily = transition.familyB;
		} else if (tile.groundFamily == transition.familyB) {
			otherFamily = transition.familyA;
		} else {
			continue;
		}

		uint8_t mask = 0;
		for (size_t neighbourIndex = 0; neighbourIndex < tile.neighbourFamilies.size(); ++neighbourIndex) {
			if (tile.neighbourFamilies[neighbourIndex] == otherFamily) {
				mask |= static_cast<uint8_t>(1u << neighbourIndex);
			}
		}
		if (mask == 0) {
			continue;
		}

		BorderLearningBoundaryObservation boundaryObservation;
		boundaryObservation.position = tile.position;
		boundaryObservation.mask = mask;
		for (const BorderType edge : classifier(mask)) {
			if (!isCanonicalBorderType(edge)) {
				break;
			}
			if (std::find(boundaryObservation.expectedEdges.begin(), boundaryObservation.expectedEdges.end(), edge) == boundaryObservation.expectedEdges.end()) {
				boundaryObservation.expectedEdges.push_back(edge);
			}
		}
		if (boundaryObservation.expectedEdges.empty()) {
			continue;
		}

		for (const auto& item : tile.items) {
			if (!isCandidateItem(item)) {
				continue;
			}

			boundaryObservation.candidateItemIds.push_back(item.itemId);
			auto& stats = candidateStats[item.itemId];
			++stats.boundaryOccurrences;
			stats.stackIndexTotal += item.stackIndex;
			stats.knownBorder = stats.knownBorder || item.knownBorder;
			stats.knownAlignment = item.knownAlignment != BORDER_NONE ? item.knownAlignment : stats.knownAlignment;
			stats.alwaysOnBottomOccurrences += item.alwaysOnBottom ? 1 : 0;
			stats.optionalOccurrences += item.optionalBorder ? 1 : 0;
			if (item.borderGroup != 0) {
				if (stats.borderGroup == 0) {
					stats.borderGroup = item.borderGroup;
				} else if (stats.borderGroup != item.borderGroup) {
					stats.conflictingBorderGroups = true;
				}
			}

			const bool knownAlignmentMatches = item.knownBorder && isCanonicalBorderType(item.knownAlignment) && std::find(boundaryObservation.expectedEdges.begin(), boundaryObservation.expectedEdges.end(), item.knownAlignment) != boundaryObservation.expectedEdges.end();
			for (const BorderType edge : boundaryObservation.expectedEdges) {
				if (knownAlignmentMatches && edge != item.knownAlignment) {
					continue;
				}
				++stats.edgeCounts[edge];
				auto& evidence = stats.evidence[edge];
				if (evidence.size() < maxEvidencePerCandidate) {
					evidence.push_back(tile.position);
				}
			}
		}

		std::sort(boundaryObservation.candidateItemIds.begin(), boundaryObservation.candidateItemIds.end());
		boundaryObservation.candidateItemIds.erase(std::unique(boundaryObservation.candidateItemIds.begin(), boundaryObservation.candidateItemIds.end()), boundaryObservation.candidateItemIds.end());
		result.boundaryObservations.push_back(std::move(boundaryObservation));
	}

	std::set<uint16_t> unclassifiedItems;
	for (auto& [itemId, stats] : candidateStats) {
		if (stats.boundaryOccurrences == 0) {
			continue;
		}

		size_t roleObservationTotal = 0;
		size_t bestCount = 0;
		std::vector<BorderType> bestEdges;
		for (size_t edgeIndex = NORTH_HORIZONTAL; edgeIndex <= SOUTHWEST_DIAGONAL; ++edgeIndex) {
			const size_t count = stats.edgeCounts[edgeIndex];
			roleObservationTotal += count;
			if (count > bestCount) {
				bestCount = count;
				bestEdges.assign(1, static_cast<BorderType>(edgeIndex));
			} else if (count != 0 && count == bestCount) {
				bestEdges.push_back(static_cast<BorderType>(edgeIndex));
			}
		}

		if (bestCount == 0 || bestEdges.size() != 1) {
			unclassifiedItems.insert(itemId);
			for (const auto& evidenceByEdge : stats.evidence) {
				ambiguousPositions.insert(evidenceByEdge.begin(), evidenceByEdge.end());
			}
			continue;
		}

		const BorderType bestEdge = bestEdges.front();
		BorderLearningCandidate candidate;
		candidate.itemId = itemId;
		candidate.observations = bestCount;
		candidate.totalOccurrences = stats.totalOccurrences;
		candidate.purity = roleObservationTotal == 0 ? 0.0 : static_cast<double>(bestCount) / roleObservationTotal;
		candidate.boundaryOccurrenceRate = stats.totalOccurrences == 0 ? 0.0 : static_cast<double>(stats.boundaryOccurrences) / stats.totalOccurrences;
		const double sampleScore = std::min(1.0, static_cast<double>(bestCount) / 5.0);
		candidate.confidence = candidate.purity * (0.60 + 0.40 * sampleScore) * candidate.boundaryOccurrenceRate;
		if (stats.knownBorder && stats.knownAlignment == bestEdge) {
			candidate.confidence += 0.08;
		}
		if (stats.alwaysOnBottomOccurrences * 5 >= stats.boundaryOccurrences * 4) {
			candidate.confidence += 0.03;
		}
		candidate.confidence = std::clamp(candidate.confidence, 0.0, 1.0);
		candidate.averageStackIndex = stats.boundaryOccurrences == 0 ? 0.0 : stats.stackIndexTotal / stats.boundaryOccurrences;
		candidate.knownBorder = stats.knownBorder;
		candidate.alwaysOnBottom = stats.alwaysOnBottomOccurrences * 2 >= stats.boundaryOccurrences;
		candidate.optionalBorder = stats.optionalOccurrences == stats.boundaryOccurrences;
		candidate.borderGroup = stats.conflictingBorderGroups ? 0 : stats.borderGroup;
		candidate.evidence = stats.evidence[bestEdge];
		result.slots[bestEdge].alternatives.push_back(std::move(candidate));
	}

	double assignedConfidenceTotal = 0.0;
	std::set<uint16_t> assignedItems;
	for (size_t edgeIndex = NORTH_HORIZONTAL; edgeIndex <= SOUTHWEST_DIAGONAL; ++edgeIndex) {
		auto& slot = result.slots[edgeIndex];
		std::sort(slot.alternatives.begin(), slot.alternatives.end(), [](const BorderLearningCandidate& lhs, const BorderLearningCandidate& rhs) {
			if (std::abs(lhs.confidence - rhs.confidence) > 0.000001) {
				return lhs.confidence > rhs.confidence;
			}
			if (lhs.observations != rhs.observations) {
				return lhs.observations > rhs.observations;
			}
			return lhs.itemId < rhs.itemId;
		});
		if (slot.alternatives.empty()) {
			continue;
		}

		const auto& best = slot.alternatives.front();
		const bool closeCompetitor = slot.alternatives.size() > 1 && std::abs(best.confidence - slot.alternatives[1].confidence) < 0.05;
		slot.ambiguous = best.confidence < 0.75 || closeCompetitor;
		slot.observations = best.observations;
		slot.confidence = best.confidence;
		slot.evidence = best.evidence;
		if (slot.ambiguous) {
			unclassifiedItems.insert(best.itemId);
			ambiguousPositions.insert(best.evidence.begin(), best.evidence.end());
			continue;
		}

		slot.itemId = best.itemId;
		assignedItems.insert(best.itemId);
		assignedConfidenceTotal += best.confidence;
		++result.assignedSlotCount;
	}

	for (const auto& [itemId, stats] : candidateStats) {
		if (stats.boundaryOccurrences != 0 && !assignedItems.contains(itemId)) {
			unclassifiedItems.insert(itemId);
		}
	}
	result.unclassifiedItemIds.assign(unclassifiedItems.begin(), unclassifiedItems.end());
	result.ambiguousTiles.assign(ambiguousPositions.begin(), ambiguousPositions.end());
	result.overallConfidence = result.assignedSlotCount == 0 ? 0.0 : assignedConfidenceTotal / result.assignedSlotCount;
	return result;
}
