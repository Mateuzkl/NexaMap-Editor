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
		return !item.wall && !item.technical;
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

std::vector<BorderLearningExistingMatch> BorderLearningAnalyzer::matchExistingBorders(
	const LearnedBorderResult& result,
	const std::vector<BorderLearningBorderDefinition>& definitions
) {
	std::vector<BorderLearningExistingMatch> matches;
	for (const auto& definition : definitions) {
		BorderLearningExistingMatch match;
		match.borderId = definition.borderId;
		for (size_t edgeIndex = NORTH_HORIZONTAL; edgeIndex <= SOUTHWEST_DIAGONAL; ++edgeIndex) {
			const uint16_t learnedItem = result.slots[edgeIndex].itemId;
			const uint16_t existingItem = definition.items[edgeIndex];
			match.learnedSlots += learnedItem != 0 ? 1 : 0;
			match.existingSlots += existingItem != 0 ? 1 : 0;
			if (learnedItem == 0) {
				continue;
			}
			if (learnedItem == existingItem) {
				++match.matchingSlots;
			} else {
				++match.conflictingSlots;
			}
		}
		if (match.learnedSlots == 0 || match.matchingSlots == 0) {
			continue;
		}
		match.similarity = static_cast<double>(match.matchingSlots) / match.learnedSlots;
		match.exact = match.conflictingSlots == 0 && match.learnedSlots == match.existingSlots;
		matches.push_back(match);
	}

	std::sort(matches.begin(), matches.end(), [](const BorderLearningExistingMatch& left, const BorderLearningExistingMatch& right) {
		if (left.exact != right.exact) {
			return left.exact;
		}
		if (std::abs(left.similarity - right.similarity) > 0.000001) {
			return left.similarity > right.similarity;
		}
		if (left.matchingSlots != right.matchingSlots) {
			return left.matchingSlots > right.matchingSlots;
		}
		return left.borderId < right.borderId;
	});
	return matches;
}

BorderLearningValidation BorderLearningAnalyzer::validateLearnedBorder(const LearnedBorderResult& result) {
	BorderLearningValidation validation;
	std::set<Position> mismatchPositions;
	for (const auto& observation : result.boundaryObservations) {
		for (const BorderType edge : observation.expectedEdges) {
			if (!isCanonicalBorderType(edge) || result.slots[edge].itemId == 0 || observation.candidateItemIds.empty()) {
				++validation.unresolvedRoles;
				continue;
			}
			const uint16_t learnedItem = result.slots[edge].itemId;
			if (std::find(observation.candidateItemIds.begin(), observation.candidateItemIds.end(), learnedItem) != observation.candidateItemIds.end()) {
				++validation.matchedRoles;
			} else {
				++validation.mismatchedRoles;
				mismatchPositions.insert(observation.position);
			}
		}
	}
	const size_t comparableRoles = validation.matchedRoles + validation.mismatchedRoles;
	validation.matchRate = comparableRoles == 0 ? 0.0 : static_cast<double>(validation.matchedRoles) / comparableRoles;
	validation.mismatchPositions.assign(mismatchPositions.begin(), mismatchPositions.end());
	return validation;
}

void BorderLearningSession::clear() noexcept {
	snapshot_ = BorderLearningSnapshot {};
	transition_ = BorderLearningTransition {};
	firstFamilyKey_ = 0;
	secondFamilyKey_ = 0;
	selectionCount_ = 0;
}

bool BorderLearningSession::addSnapshot(
	const BorderLearningSnapshot& snapshot,
	const BorderLearningTransition& transition,
	std::string* error
) {
	if (transition.familyA >= snapshot.groundFamilies.size() || transition.familyB >= snapshot.groundFamilies.size()) {
		if (error) {
			*error = "The selected transition is not valid for this map snapshot.";
		}
		return false;
	}
	if (!empty() && snapshot.floor != snapshot_.floor) {
		if (error) {
			*error = "Evidence from different floors cannot be combined in one learning session.";
		}
		return false;
	}

	uint64_t familyKeyA = snapshot.groundFamilies[transition.familyA].key;
	uint64_t familyKeyB = snapshot.groundFamilies[transition.familyB].key;
	if (familyKeyB < familyKeyA) {
		std::swap(familyKeyA, familyKeyB);
	}
	if (!empty() && (familyKeyA != firstFamilyKey_ || familyKeyB != secondFamilyKey_)) {
		if (error) {
			*error = "The selection does not contain the same terrain transition as the current learning session.";
		}
		return false;
	}
	if (!empty()) {
		std::set<Position> existingPositions;
		for (const auto& tile : snapshot_.tiles) {
			existingPositions.insert(tile.position);
		}
		const bool hasNewTile = std::any_of(snapshot.tiles.begin(), snapshot.tiles.end(), [&existingPositions](const BorderLearningTile& tile) {
			return !existingPositions.contains(tile.position);
		});
		if (!hasNewTile) {
			if (error) {
				*error = "This selection does not add any new tile evidence to the learning session.";
			}
			return false;
		}
	}

	if (empty()) {
		snapshot_.floor = snapshot.floor;
		firstFamilyKey_ = familyKeyA;
		secondFamilyKey_ = familyKeyB;
	}

	std::map<uint64_t, BorderGroundFamilyIndex> familyIndices;
	for (BorderGroundFamilyIndex index = 0; index < snapshot_.groundFamilies.size(); ++index) {
		familyIndices.emplace(snapshot_.groundFamilies[index].key, index);
	}
	std::vector<BorderGroundFamilyIndex> remappedFamilies(snapshot.groundFamilies.size(), BORDER_GROUND_FAMILY_NONE);
	for (size_t sourceIndex = 0; sourceIndex < snapshot.groundFamilies.size(); ++sourceIndex) {
		const auto& sourceFamily = snapshot.groundFamilies[sourceIndex];
		auto found = familyIndices.find(sourceFamily.key);
		if (found == familyIndices.end()) {
			snapshot_.groundFamilies.push_back(sourceFamily);
			const auto targetIndex = static_cast<BorderGroundFamilyIndex>(snapshot_.groundFamilies.size() - 1);
			found = familyIndices.emplace(sourceFamily.key, targetIndex).first;
		} else {
			auto& targetFamily = snapshot_.groundFamilies[found->second];
			targetFamily.itemIds.insert(targetFamily.itemIds.end(), sourceFamily.itemIds.begin(), sourceFamily.itemIds.end());
			std::sort(targetFamily.itemIds.begin(), targetFamily.itemIds.end());
			targetFamily.itemIds.erase(std::unique(targetFamily.itemIds.begin(), targetFamily.itemIds.end()), targetFamily.itemIds.end());
		}
		remappedFamilies[sourceIndex] = found->second;
	}

	std::set<Position> capturedPositions;
	for (const auto& tile : snapshot_.tiles) {
		capturedPositions.insert(tile.position);
	}
	for (const auto& sourceTile : snapshot.tiles) {
		if (!capturedPositions.insert(sourceTile.position).second) {
			continue;
		}
		BorderLearningTile tile = sourceTile;
		tile.groundFamily = sourceTile.groundFamily < remappedFamilies.size() ? remappedFamilies[sourceTile.groundFamily] : BORDER_GROUND_FAMILY_NONE;
		for (size_t neighbourIndex = 0; neighbourIndex < sourceTile.neighbourFamilies.size(); ++neighbourIndex) {
			const auto sourceFamily = sourceTile.neighbourFamilies[neighbourIndex];
			tile.neighbourFamilies[neighbourIndex] = sourceFamily < remappedFamilies.size() ? remappedFamilies[sourceFamily] : BORDER_GROUND_FAMILY_NONE;
		}
		snapshot_.tiles.push_back(std::move(tile));
	}
	std::sort(snapshot_.tiles.begin(), snapshot_.tiles.end(), [](const BorderLearningTile& left, const BorderLearningTile& right) {
		return left.position < right.position;
	});
	snapshot_.selectedTileCount = snapshot_.tiles.size();
	snapshot_.ignoredOtherFloorTiles += snapshot.ignoredOtherFloorTiles;
	++selectionCount_;

	transition_.familyA = familyIndices[firstFamilyKey_];
	transition_.familyB = familyIndices[secondFamilyKey_];
	transition_.contacts = 0;
	for (const auto& detected : BorderLearningAnalyzer::detectTransitions(snapshot_)) {
		const uint64_t detectedA = snapshot_.groundFamilies[detected.familyA].key;
		const uint64_t detectedB = snapshot_.groundFamilies[detected.familyB].key;
		if ((detectedA == firstFamilyKey_ && detectedB == secondFamilyKey_)
			|| (detectedA == secondFamilyKey_ && detectedB == firstFamilyKey_)) {
			transition_.contacts = detected.contacts;
			break;
		}
	}
	return true;
}

LearnedBorderResult BorderLearningSession::infer(BorderMaskClassifier classifier) const {
	if (empty()) {
		return LearnedBorderResult {};
	}
	return BorderLearningAnalyzer::inferBorder(snapshot_, transition_, classifier);
}
