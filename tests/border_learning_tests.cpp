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

	BorderLearningItem makeItem(uint16_t itemId, bool alwaysOnBottom = false, bool doodad = false) {
		BorderLearningItem item;
		item.itemId = itemId;
		item.alwaysOnBottom = alwaysOnBottom;
		item.doodad = doodad;
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
		tile.items.push_back(makeItem(500, true, true));
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

	auto makeSessionSample = [](int originX, bool reverseFamilies) {
		BorderLearningSnapshot sample;
		sample.floor = 7;
		sample.groundFamilies.resize(2);
		sample.groundFamilies[reverseFamilies ? 1 : 0].key = 10;
		sample.groundFamilies[reverseFamilies ? 0 : 1].key = 20;
		const BorderGroundFamilyIndex borderSide = reverseFamilies ? 1 : 0;
		const BorderGroundFamilyIndex otherSide = reverseFamilies ? 0 : 1;
		for (int y = 0; y < 3; ++y) {
			auto borderTile = makeTile(originX, 300 + y, borderSide);
			borderTile.neighbourFamilies[4] = otherSide;
			borderTile.items.push_back(makeItem(700, true));
			sample.tiles.push_back(std::move(borderTile));
			auto otherTile = makeTile(originX + 1, 300 + y, otherSide);
			otherTile.neighbourFamilies[3] = borderSide;
			sample.tiles.push_back(std::move(otherTile));
		}
		sample.selectedTileCount = sample.tiles.size();
		return sample;
	};

	BorderLearningSession session;
	const auto firstSample = makeSessionSample(300, false);
	const auto secondSample = makeSessionSample(400, true);
	std::string sessionError;
	require(session.addSnapshot(firstSample, { 0, 1, 3 }, &sessionError), "first session sample was rejected");
	require(session.addSnapshot(secondSample, { 0, 1, 3 }, &sessionError), "remapped session sample was rejected");
	require(session.getSelectionCount() == 2, "session selection count mismatch");
	require(session.getSnapshot().tiles.size() == 12, "session tile merge mismatch");
	require(session.getTransition().contacts == 6, "session transition contacts mismatch");
	require(!session.addSnapshot(secondSample, { 0, 1, 3 }, &sessionError), "duplicate session evidence was accepted");
	const auto accumulated = session.infer(classifyTestMask);
	require(accumulated.slots[EAST_HORIZONTAL].itemId == 700, "accumulated candidate mismatch");
	require(accumulated.slots[EAST_HORIZONTAL].observations == 6, "accumulated observation count mismatch");

	LearnedBorderResult matchResult;
	matchResult.slots[NORTH_HORIZONTAL].itemId = 701;
	matchResult.slots[EAST_HORIZONTAL].itemId = 702;
	matchResult.slots[SOUTH_HORIZONTAL].itemId = 703;
	std::vector<BorderLearningBorderDefinition> definitions(3);
	definitions[0].borderId = 50;
	definitions[0].items[NORTH_HORIZONTAL] = 701;
	definitions[0].items[EAST_HORIZONTAL] = 702;
	definitions[0].items[SOUTH_HORIZONTAL] = 703;
	definitions[1].borderId = 51;
	definitions[1].items[NORTH_HORIZONTAL] = 701;
	definitions[1].items[EAST_HORIZONTAL] = 702;
	definitions[1].items[SOUTH_HORIZONTAL] = 999;
	definitions[2].borderId = 52;
	definitions[2].items[WEST_HORIZONTAL] = 800;
	const auto existingMatches = BorderLearningAnalyzer::matchExistingBorders(matchResult, definitions);
	require(existingMatches.size() == 2, "existing border match count mismatch");
	require(existingMatches[0].borderId == 50 && existingMatches[0].exact, "exact existing border was not ranked first");
	require(existingMatches[1].borderId == 51 && !existingMatches[1].exact, "near existing border mismatch");
	require(existingMatches[1].matchingSlots == 2 && existingMatches[1].conflictingSlots == 1, "near border slot counts mismatch");

	BorderLearningBoundaryObservation matchingObservation;
	matchingObservation.position = Position(500, 500, 7);
	matchingObservation.expectedEdges = { NORTH_HORIZONTAL };
	matchingObservation.candidateItemIds = { 701 };
	matchResult.boundaryObservations.push_back(matchingObservation);
	BorderLearningBoundaryObservation mismatchingObservation;
	mismatchingObservation.position = Position(501, 500, 7);
	mismatchingObservation.expectedEdges = { SOUTH_HORIZONTAL };
	mismatchingObservation.candidateItemIds = { 999 };
	matchResult.boundaryObservations.push_back(mismatchingObservation);
	BorderLearningBoundaryObservation unresolvedObservation;
	unresolvedObservation.position = Position(502, 500, 7);
	unresolvedObservation.expectedEdges = { WEST_HORIZONTAL };
	matchResult.boundaryObservations.push_back(unresolvedObservation);
	const auto validation = BorderLearningAnalyzer::validateLearnedBorder(matchResult);
	require(validation.matchedRoles == 1, "learned border validation match count mismatch");
	require(validation.mismatchedRoles == 1, "learned border validation mismatch count mismatch");
	require(validation.unresolvedRoles == 1, "learned border validation unresolved count mismatch");
	require(validation.matchRate == 0.5, "learned border validation rate mismatch");
	require(validation.mismatchPositions.size() == 1 && validation.mismatchPositions.front() == Position(501, 500, 7), "learned border mismatch position missing");

	BorderLearningSnapshot wrongFloor = firstSample;
	wrongFloor.floor = 8;
	require(!session.addSnapshot(wrongFloor, { 0, 1, 3 }, &sessionError), "cross-floor evidence was accepted");
	require(session.getSelectionCount() == 2, "rejected evidence changed the session");

	std::cout << "border_learning_tests passed\n";
	return 0;
}
