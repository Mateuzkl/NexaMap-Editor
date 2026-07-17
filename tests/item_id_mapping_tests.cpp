#include "item_id_mapping.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <string_view>

namespace {
	int failures = 0;

	void check(bool condition, std::string_view test) {
		if (!condition) {
			std::cerr << "FAILED: " << test << '\n';
			++failures;
		}
	}

	void digestByte(uint64_t& digest, uint8_t value) {
		digest = (digest ^ value) * 1099511628211ULL;
	}

	void digestU16(uint64_t& digest, uint16_t value) {
		digestByte(digest, static_cast<uint8_t>(value));
		digestByte(digest, static_cast<uint8_t>(value >> 8));
	}
}

int main() {
	using ItemIdMapping::Direction;

	check(ItemIdMapping::validateTables(), "embedded tables validate");

	const auto statistics = ItemIdMapping::statistics();
	check(statistics.mapping_count == 51793 && statistics.unique_client_count == 49871 && statistics.collision_count == 1418 && statistics.ambiguous_candidate_count == 3340, "mapping statistics match imported JavaScript");

	check(ItemIdMapping::sourceVersion() == "server:426df26fdb90e6484144fe78484b826a700fd32031c78cb9dbd959a8d202f61a;"
											"inverse:b6761668c448ac4cf9f199fd0eb74e0cae74f4773b68fdcf1196c433f6b3e63a",
		  "mapping provenance hashes are fixed");

	const auto identity = ItemIdMapping::serverToClient(103);
	check(identity.found && identity.converted == 103 && !identity.ambiguous, "identity mapping");

	check(ItemIdMapping::serverToClient(462).converted == 8709 && ItemIdMapping::serverToClient(598).converted == 21477 && ItemIdMapping::serverToClient(1094).converted == 168 && ItemIdMapping::serverToClient(59258).converted == 54267, "representative forward boundaries and exceptions");

	const auto reportedHighId = ItemIdMapping::serverToClient(56658);
	check(reportedHighId.found && reportedHighId.converted == 51667 && ItemIdMapping::clientToServer(51667).converted == 56658, "high Server ID 56658 converts without requiring a loaded item type");

	const auto unknownForward = ItemIdMapping::serverToClient(65535);
	check(!unknownForward.found && unknownForward.converted == 65535 && unknownForward.candidates.empty(), "unknown forward ID remains unchanged");

	const auto reverseUnique = ItemIdMapping::clientToServer(21477);
	check(reverseUnique.found && reverseUnique.converted == 598 && !reverseUnique.ambiguous && reverseUnique.candidates.size() == 1 && reverseUnique.candidates.front() == 598, "unique reverse mapping");

	const auto reverseCollision = ItemIdMapping::clientToServer(615);
	check(reverseCollision.found && reverseCollision.ambiguous && reverseCollision.converted == 482 && reverseCollision.candidates.size() == 2 && reverseCollision.candidates[0] == 482 && reverseCollision.candidates[1] == 489, "reverse collision keeps first JavaScript case");

	constexpr std::array<uint16_t, 24> largestCandidates = {
		4404,
		5025,
		5026,
		5027,
		5028,
		5029,
		5030,
		5031,
		5032,
		5033,
		5034,
		5035,
		5036,
		5037,
		5038,
		5039,
		5040,
		5041,
		5042,
		5043,
		5044,
		5402,
		5403,
		5404,
	};
	const auto largestCollision = ItemIdMapping::clientToServer(2117);
	check(largestCollision.converted == 4404 && largestCollision.ambiguous && std::equal(largestCollision.candidates.begin(), largestCollision.candidates.end(), largestCandidates.begin(), largestCandidates.end()), "largest 24-candidate collision is complete");

	const auto ambiguousForward = ItemIdMapping::serverToClient(489);
	check(ambiguousForward.found && ambiguousForward.converted == 615 && ambiguousForward.ambiguous && ambiguousForward.candidates.size() == 2, "forward result reports reverse ambiguity");

	const auto unknownReverse = ItemIdMapping::convert(65535, Direction::ClientToServer);
	check(!unknownReverse.found && unknownReverse.converted == 65535 && !unknownReverse.ambiguous, "unknown reverse ID remains unchanged");

	bool allRoundTripsCovered = true;
	for (uint32_t rawId = 0; rawId <= 65535; ++rawId) {
		const auto forward = ItemIdMapping::convert(static_cast<uint16_t>(rawId), Direction::ServerToClient);
		if (!forward.found) {
			continue;
		}
		const auto reverse = ItemIdMapping::convert(forward.converted, Direction::ClientToServer);
		if (!reverse.found || !std::binary_search(reverse.candidates.begin(), reverse.candidates.end(), static_cast<uint16_t>(rawId))) {
			allRoundTripsCovered = false;
			break;
		}
	}
	check(allRoundTripsCovered, "every forward pair appears among reverse candidates");

	uint64_t mappingDigest = 14695981039346656037ULL;
	for (uint32_t rawId = 0; rawId <= 65535; ++rawId) {
		const auto result = ItemIdMapping::serverToClient(static_cast<uint16_t>(rawId));
		if (!result.found) {
			continue;
		}
		digestByte(mappingDigest, 0x53);
		digestU16(mappingDigest, static_cast<uint16_t>(rawId));
		digestU16(mappingDigest, result.converted);
	}
	for (uint32_t rawId = 0; rawId <= 65535; ++rawId) {
		const auto result = ItemIdMapping::clientToServer(static_cast<uint16_t>(rawId));
		if (!result.found) {
			continue;
		}
		digestByte(mappingDigest, 0x43);
		digestU16(mappingDigest, static_cast<uint16_t>(rawId));
		digestU16(mappingDigest, result.converted);
		digestU16(mappingDigest, static_cast<uint16_t>(result.candidates.size()));
		for (uint16_t candidate : result.candidates) {
			digestU16(mappingDigest, candidate);
		}
	}
	check(mappingDigest == 0x161DC0141CB49A56ULL, "complete forward and reverse mapping digest matches source JavaScript");

	if (failures != 0) {
		std::cerr << failures << " item ID mapping test(s) failed.\n";
		return 1;
	}
	std::cout << "14 item ID mapping tests passed.\n";
	return 0;
}
