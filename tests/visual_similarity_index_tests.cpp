#include "replace_tool/visual_similarity_index.h"

#include <chrono>
#include <iostream>
#include <string_view>
#include <utility>

namespace {
	int failures = 0;

	void check(bool condition, std::string_view name) {
		if (!condition) {
			std::cerr << "FAILED: " << name << '\n';
			++failures;
		}
	}

	VisualSpriteSample makeOpaque(uint64_t generation, uint16_t serverId, bool inverse = false) {
		VisualSpriteSample sample;
		sample.generation = generation;
		sample.serverId = ServerItemId(serverId);
		sample.width = 8;
		sample.height = 8;
		sample.rgba.resize(8 * 8 * 4);
		for (int y = 0; y < 8; ++y) {
			for (int x = 0; x < 8; ++x) {
				const bool white = (x < 4) != inverse;
				const size_t offset = (static_cast<size_t>(y) * 8 + x) * 4;
				sample.rgba[offset + 0] = white ? 255 : 0;
				sample.rgba[offset + 1] = white ? 255 : 0;
				sample.rgba[offset + 2] = white ? 255 : 0;
				sample.rgba[offset + 3] = 255;
			}
		}
		return sample;
	}

	VisualSpriteSample makeTransparent(uint64_t generation, uint16_t serverId, int offsetX) {
		VisualSpriteSample sample;
		sample.generation = generation;
		sample.serverId = ServerItemId(serverId);
		sample.width = 8;
		sample.height = 8;
		sample.rgba.resize(8 * 8 * 4);
		for (int y = 2; y < 6; ++y) {
			for (int x = 2 + offsetX; x < 6 + offsetX && x < 8; ++x) {
				const size_t pixel = (static_cast<size_t>(y) * 8 + x) * 4;
				sample.rgba[pixel + 0] = 80;
				sample.rgba[pixel + 1] = 180;
				sample.rgba[pixel + 2] = 40;
				sample.rgba[pixel + 3] = 255;
			}
		}
		return sample;
	}
}

int main() {
	VisualSimilarityIndex index;
	constexpr uint64_t Generation1 = 1;
	index.Reset(Generation1);
	check(index.Submit(makeOpaque(Generation1, 100)), "opaque source must queue");
	check(index.Submit(makeOpaque(Generation1, 101)), "identical opaque candidate must queue");
	check(index.Submit(makeOpaque(Generation1, 102, true)), "different opaque candidate must queue");
	check(index.Submit(makeTransparent(Generation1, 200, 0)), "transparent source must queue");
	check(index.Submit(makeTransparent(Generation1, 201, 0)), "identical transparent candidate must queue");
	check(index.Submit(makeTransparent(Generation1, 202, 1)), "shifted transparent candidate must queue");
	check(index.WaitUntilIdle(Generation1, std::chrono::seconds(2)), "worker must finish bounded samples");
	check(index.GetIndexedCount(Generation1) == 6, "all submitted samples must be indexed");

	const std::vector<VisualSimilarityMatch> opaqueMatches = index.FindSimilar(Generation1, ServerItemId(100), 5);
	check(!opaqueMatches.empty() && opaqueMatches.front().serverId == ServerItemId(101) && opaqueMatches.front().score == 1.0, "aHash must rank identical opaque sprites first");
	const std::vector<VisualSimilarityMatch> transparentMatches = index.FindSimilar(Generation1, ServerItemId(200), 5);
	check(transparentMatches.size() >= 2 && transparentMatches.front().serverId == ServerItemId(201) && transparentMatches.front().score == 1.0, "Dice must rank identical transparent masks first");
	check(transparentMatches[1].serverId == ServerItemId(202) && transparentMatches[1].score < 1.0, "Dice must score shifted masks below identical masks");

	constexpr uint64_t Generation2 = 2;
	index.Reset(Generation2);
	check(index.GetIndexedCount(Generation1) == 0, "old generation must become invisible after reset");
	check(!index.Submit(makeOpaque(Generation1, 300)), "stale generation submission must be rejected");
	check(index.Submit(makeOpaque(Generation2, 301)), "current generation submission must queue");
	check(index.WaitUntilIdle(Generation2, std::chrono::seconds(2)) && index.GetIndexedCount(Generation2) == 1, "new generation must index independently");

	VisualSpriteSample invalid;
	invalid.generation = Generation2;
	invalid.serverId = ServerItemId(400);
	invalid.width = 8;
	invalid.height = 8;
	check(!index.Submit(std::move(invalid)), "invalid RGBA buffer size must be rejected");

	if (failures != 0) {
		std::cerr << failures << " visual similarity test(s) failed.\n";
		return 1;
	}

	std::cout << "All visual similarity tests passed.\n";
	return 0;
}
