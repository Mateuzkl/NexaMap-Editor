//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#ifndef RME_REPLACE_TOOL_VISUAL_SIMILARITY_INDEX_H_
#define RME_REPLACE_TOOL_VISUAL_SIMILARITY_INDEX_H_

#include "replace_rule.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

struct VisualSpriteSample {
	uint64_t generation = 0;
	ServerItemId serverId;
	int width = 0;
	int height = 0;
	std::vector<uint8_t> rgba;
};

struct VisualSignature {
	ServerItemId serverId;
	bool opaque = false;
	uint64_t averageHash = 0;
	int width = 0;
	int height = 0;
	uint32_t foregroundPixels = 0;
	std::vector<uint64_t> alphaMask;
	std::array<uint16_t, 512> histogram {};
	uint32_t histogramPixels = 0;
};

struct VisualSimilarityMatch {
	ServerItemId serverId;
	double score = 0.0;
	float histogramScore = 0.0f;
};

class VisualSimilarityIndex {
public:
	explicit VisualSimilarityIndex(size_t maxPendingSamples = 256);
	~VisualSimilarityIndex();

	VisualSimilarityIndex(const VisualSimilarityIndex&) = delete;
	VisualSimilarityIndex& operator=(const VisualSimilarityIndex&) = delete;

	void Reset(uint64_t generation);
	bool Submit(VisualSpriteSample sample);
	[[nodiscard]] std::vector<VisualSimilarityMatch> FindSimilar(uint64_t generation, ServerItemId sourceServerId, size_t count) const;
	[[nodiscard]] size_t GetIndexedCount(uint64_t generation) const;
	[[nodiscard]] bool HasPendingWork(uint64_t generation) const;
	bool WaitUntilIdle(uint64_t generation, std::chrono::milliseconds timeout);

	[[nodiscard]] static bool CalculateSignature(const VisualSpriteSample& sample, VisualSignature& signature);

private:
	void WorkerLoop();

	const size_t maxPendingSamples;
	mutable std::mutex mutex;
	std::condition_variable condition;
	std::condition_variable idleCondition;
	std::deque<VisualSpriteSample> pendingSamples;
	std::unordered_map<uint16_t, std::shared_ptr<const VisualSignature>> signatures;
	std::thread worker;
	uint64_t generation = 0;
	bool active = false;
	bool stopping = false;
};

#endif
