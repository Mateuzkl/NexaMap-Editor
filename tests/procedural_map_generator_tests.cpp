#include "procedural_map_generator.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <set>
#include <stop_token>
#include <tuple>

// Always-on assertion for tests
#define TEST_ASSERT(condition) \
	do { \
		if (!(condition)) { \
			std::cerr << "Test assertion failed: " << #condition << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
			std::abort(); \
		} \
	} while (false)

namespace {

ProceduralMap::GenerationRequest MakeRequest(ProceduralMap::Preset preset, uint64_t seed = 123456) {
	ProceduralMap::GenerationRequest request;
	request.minX = 100;
	request.minY = 200;
	request.minZ = 7;
	request.maxX = 147;
	request.maxY = 239;
	request.maxZ = 7;
	request.allowedCells.assign(request.cellCount(), 1);
	request.seed = seed;
	request.preset = preset;
	request.parameters.roomCount = 10;
	request.parameters.roomMinSize = 4;
	request.parameters.roomMaxSize = 9;
	return request;
}

void TestEveryPresetIsDeterministicAndMasked() {
	for (int value = 0; value < static_cast<int>(ProceduralMap::Preset::Count); ++value) {
		auto request = MakeRequest(static_cast<ProceduralMap::Preset>(value));
		const auto first = ProceduralMap::Generate(request);
		const auto second = ProceduralMap::Generate(request);
		TEST_ASSERT(!first.hasErrors());
		TEST_ASSERT(first.stableHash == second.stableHash);
		TEST_ASSERT(first.cells.size() == request.cellCount());
		for (const auto& cell : first.cells) {
			TEST_ASSERT(cell.x >= request.minX && cell.x <= request.maxX);
			TEST_ASSERT(cell.y >= request.minY && cell.y <= request.maxY);
			TEST_ASSERT(cell.z >= request.minZ && cell.z <= request.maxZ);
		}
	}
}

void TestDifferentSeedsChangeNaturalPlan() {
	auto firstRequest = MakeRequest(ProceduralMap::Preset::NaturalLandscape, 1);
	auto secondRequest = firstRequest;
	secondRequest.seed = 2;
	TEST_ASSERT(ProceduralMap::Generate(firstRequest).stableHash != ProceduralMap::Generate(secondRequest).stableHash);
}

void TestIrregularSelectionRemainsExact() {
	auto request = MakeRequest(ProceduralMap::Preset::Forest);
	std::fill(request.allowedCells.begin(), request.allowedCells.end(), uint8_t { 0 });
	const auto setAllowed = [&](int x, int y) {
		const size_t index = static_cast<size_t>(y - request.minY) * request.width() + static_cast<size_t>(x - request.minX);
		request.allowedCells[index] = 1;
	};
	for (int y = 205; y < 220; ++y) {
		for (int x = 108; x < 124; ++x) {
			if ((x + y) % 3 != 0) setAllowed(x, y);
		}
	}
	const auto plan = ProceduralMap::Generate(request);
	for (const auto& cell : plan.cells) {
		if (!request.isAllowed(cell.x, cell.y, cell.z)) TEST_ASSERT(cell.material == ProceduralMap::MaterialRole::None);
	}
}

void TestProtectedContentBecomesAPlanningHole() {
	auto request = MakeRequest(ProceduralMap::Preset::Dungeon, 77);
	request.protectedCells.assign(request.cellCount(), uint8_t { 0 });
	const int protectedX = 120;
	const int protectedY = 220;
	const size_t index = static_cast<size_t>(protectedY - request.minY) * request.width() + static_cast<size_t>(protectedX - request.minX);
	request.protectedCells[index] = 1;
	const auto protectedPlan = ProceduralMap::Generate(request);
	TEST_ASSERT(!request.isAllowed(protectedX, protectedY, request.minZ));
	TEST_ASSERT(protectedPlan.cells[index].material == ProceduralMap::MaterialRole::None);
	TEST_ASSERT(protectedPlan.statistics.protectedTiles == 1);

	request.protectedCells.clear();
	const auto unprotectedPlan = ProceduralMap::Generate(request);
	TEST_ASSERT(protectedPlan.stableHash != unprotectedPlan.stableHash);
}

void TestStructuredPresetsProduceUsableLandmarks() {
	const auto dungeon = ProceduralMap::Generate(MakeRequest(ProceduralMap::Preset::Dungeon, 901));
	TEST_ASSERT(dungeon.statistics.roomsPlaced >= 2);
	TEST_ASSERT(dungeon.statistics.pointOfInterestTiles >= 2);
	TEST_ASSERT(dungeon.statistics.walkableTiles > 0);

	const auto hunt = ProceduralMap::Generate(MakeRequest(ProceduralMap::Preset::HuntingArea, 902));
	TEST_ASSERT(hunt.statistics.roomsPlaced >= 2);
	TEST_ASSERT(hunt.statistics.pointOfInterestTiles >= 1);
	TEST_ASSERT(hunt.statistics.walkableTiles > 0);

	const auto city = ProceduralMap::Generate(MakeRequest(ProceduralMap::Preset::City, 903));
	TEST_ASSERT(city.statistics.wallTiles > 0);
	TEST_ASSERT(city.statistics.pointOfInterestTiles >= 1);
	TEST_ASSERT(city.statistics.walkableTiles > city.statistics.wallTiles);
}

void TestMultiFloorCoordinatesAndTransitions() {
	auto request = MakeRequest(ProceduralMap::Preset::Mountain);
	request.minZ = 5;
	request.maxZ = 7;
	request.allowedCells.assign(request.cellCount(), 1);
	const auto plan = ProceduralMap::Generate(request);
	TEST_ASSERT(!plan.cells.empty());
	for (const auto& cell : plan.cells) TEST_ASSERT(cell.z >= 5 && cell.z <= 7);
	for (int lowerZ = request.maxZ; lowerZ > request.minZ; --lowerZ) {
		bool aligned = false;
		for (int y = request.minY; y <= request.maxY && !aligned; ++y) {
			for (int x = request.minX; x <= request.maxX; ++x) {
				const auto* lower = plan.find(x, y, lowerZ);
				const auto* upper = plan.find(x, y, lowerZ - 1);
				if (lower && upper && (lower->features & ProceduralMap::FeatureTransition) != 0 && (upper->features & ProceduralMap::FeatureTransition) != 0) {
					aligned = true;
					break;
				}
			}
		}
		TEST_ASSERT(aligned);
	}
}

void TestOversizedRequestIsRejectedBeforeGeneration() {
	auto request = MakeRequest(ProceduralMap::Preset::NaturalLandscape);
	request.minX = 0;
	request.minY = 0;
	request.maxX = 2047;
	request.maxY = 2047;
	request.minZ = 6;
	request.maxZ = 7;
	// Keep allowedCells valid for mask-size validation to trigger memory-limit rejection
	request.allowedCells.assign(request.cellCount(), 1);
	std::vector<ProceduralMap::ValidationIssue> issues;
	TEST_ASSERT(!ProceduralMap::ValidateRequest(request, issues));
	// Verify that the memory-limit issue is specifically reported
	bool foundMaxCellsIssue = false;
	for (const auto& issue : issues) {
		if (issue.message.find("MaximumPlanCells") != std::string::npos || 
		    issue.message.find("memory") != std::string::npos ||
		    issue.message.find("too large") != std::string::npos) {
			foundMaxCellsIssue = true;
			break;
		}
	}
	TEST_ASSERT(foundMaxCellsIssue);
}

void TestInvalidPoliciesAndParametersAreRejected() {
	auto request = MakeRequest(ProceduralMap::Preset::NaturalLandscape);
	request.replacementMode = static_cast<ProceduralMap::ReplacementMode>(255);
	request.parameters.noiseScale = std::numeric_limits<double>::infinity();
	std::vector<ProceduralMap::ValidationIssue> issues;
	TEST_ASSERT(!ProceduralMap::ValidateRequest(request, issues));
}

void TestBriefInterpreterDoesNotPretendUnknownTerms() {
	const auto result = ProceduralMap::InterpretBrief("mountain hunt with wide paths and dragons");
	TEST_ASSERT(result.hasSupportedTerms);
	TEST_ASSERT(result.preset == ProceduralMap::Preset::HuntingArea);
	TEST_ASSERT(result.parameters.pathWidth == 3);
	TEST_ASSERT(std::find(result.unknown.begin(), result.unknown.end(), "dragons") != result.unknown.end());
	const auto portuguese = ProceduralMap::InterpretBrief("cidade com estradas largas e vegetacao densa");
	TEST_ASSERT(portuguese.hasSupportedTerms);
	TEST_ASSERT(portuguese.preset == ProceduralMap::Preset::City);
	TEST_ASSERT(portuguese.parameters.pathWidth == 3);
	TEST_ASSERT(portuguese.parameters.decorationDensity == 42);
}

void TestLocksPreserveApprovedParts() {
	auto request = MakeRequest(ProceduralMap::Preset::Dungeon, 90);
	auto approved = ProceduralMap::Generate(request);
	request.seed = 91;
	auto regenerated = ProceduralMap::Generate(request);
	ProceduralMap::MergeLockedParts(regenerated, approved, true, true, true);
	TEST_ASSERT(regenerated.cells.size() == approved.cells.size());
	for (size_t index = 0; index < approved.cells.size(); ++index) {
		TEST_ASSERT(regenerated.cells[index].material == approved.cells[index].material);
		TEST_ASSERT(regenerated.cells[index].features == approved.cells[index].features);
		TEST_ASSERT(regenerated.cells[index].walkable == approved.cells[index].walkable);
	}
}

void TestCancellationIsControlled() {
	std::stop_source source;
	source.request_stop();
	bool cancelled = false;
	try {
		static_cast<void>(ProceduralMap::Generate(MakeRequest(ProceduralMap::Preset::Cave), source.get_token()));
	} catch (const std::runtime_error&) {
		cancelled = true;
	}
	TEST_ASSERT(cancelled);
}

} // namespace

int main() {
	TestEveryPresetIsDeterministicAndMasked();
	TestDifferentSeedsChangeNaturalPlan();
	TestIrregularSelectionRemainsExact();
	TestProtectedContentBecomesAPlanningHole();
	TestStructuredPresetsProduceUsableLandmarks();
	TestMultiFloorCoordinatesAndTransitions();
	TestOversizedRequestIsRejectedBeforeGeneration();
	TestInvalidPoliciesAndParametersAreRejected();
	TestBriefInterpreterDoesNotPretendUnknownTerms();
	TestLocksPreserveApprovedParts();
	TestCancellationIsControlled();
	std::cout << "procedural_map_generator_tests passed\n";
	return 0;
}
