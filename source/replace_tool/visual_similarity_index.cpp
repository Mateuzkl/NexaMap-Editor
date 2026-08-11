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

#include "visual_similarity_index.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>

namespace {
	constexpr uint8_t AlphaThreshold = 10;

	bool MaskAt(const VisualSignature& signature, size_t index) {
		return (signature.alphaMask[index / 64] & (uint64_t(1) << (index % 64))) != 0;
	}

	std::vector<uint8_t> ResizeMask(const VisualSignature& signature, int width, int height) {
		std::vector<uint8_t> resized(static_cast<size_t>(width) * height);
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				const int sourceX = std::min((x * signature.width) / width, signature.width - 1);
				const int sourceY = std::min((y * signature.height) / height, signature.height - 1);
				resized[static_cast<size_t>(y) * width + x] = MaskAt(signature, static_cast<size_t>(sourceY) * signature.width + sourceX);
			}
		}
		return resized;
	}

	double DiceScore(const VisualSignature& left, const VisualSignature& right) {
		if (left.width == right.width && left.height == right.height) {
			uint32_t intersection = 0;
			for (size_t index = 0; index < left.alphaMask.size(); ++index) {
				intersection += std::popcount(left.alphaMask[index] & right.alphaMask[index]);
			}
			const uint32_t denominator = left.foregroundPixels + right.foregroundPixels;
			return denominator == 0 ? 0.0 : (2.0 * intersection) / denominator;
		}
		const int width = std::max(left.width, right.width);
		const int height = std::max(left.height, right.height);
		const std::vector<uint8_t> leftMask = ResizeMask(left, width, height);
		const std::vector<uint8_t> rightMask = ResizeMask(right, width, height);
		uint32_t leftPixels = 0;
		uint32_t rightPixels = 0;
		uint32_t intersection = 0;
		for (size_t index = 0; index < leftMask.size(); ++index) {
			leftPixels += leftMask[index] != 0;
			rightPixels += rightMask[index] != 0;
			intersection += leftMask[index] != 0 && rightMask[index] != 0;
		}
		const uint32_t denominator = leftPixels + rightPixels;
		return denominator == 0 ? 0.0 : (2.0 * intersection) / denominator;
	}

	float HistogramIntersection(const VisualSignature& left, const VisualSignature& right) {
		if (left.histogramPixels == 0 || right.histogramPixels == 0) {
			return 0.0f;
		}
		float score = 0.0f;
		for (size_t index = 0; index < left.histogram.size(); ++index) {
			const float leftFrequency = static_cast<float>(left.histogram[index]) / left.histogramPixels;
			const float rightFrequency = static_cast<float>(right.histogram[index]) / right.histogramPixels;
			score += std::min(leftFrequency, rightFrequency);
		}
		return score;
	}
}

VisualSimilarityIndex::VisualSimilarityIndex(size_t maxPendingSamples) :
	maxPendingSamples(std::max<size_t>(1, maxPendingSamples)),
	worker(&VisualSimilarityIndex::WorkerLoop, this) { }

VisualSimilarityIndex::~VisualSimilarityIndex() {
	{
		std::lock_guard lock(mutex);
		stopping = true;
		pendingSamples.clear();
	}
	condition.notify_all();
	if (worker.joinable()) {
		worker.join();
	}
}

void VisualSimilarityIndex::Reset(uint64_t newGeneration) {
	{
		std::lock_guard lock(mutex);
		generation = newGeneration;
		pendingSamples.clear();
		signatures.clear();
	}
	idleCondition.notify_all();
}

bool VisualSimilarityIndex::Submit(VisualSpriteSample sample) {
	if (!sample.serverId.isValid() || sample.width <= 0 || sample.height <= 0 || sample.rgba.size() != static_cast<size_t>(sample.width) * sample.height * 4) {
		return false;
	}
	{
		std::lock_guard lock(mutex);
		if (stopping || sample.generation != generation || pendingSamples.size() >= maxPendingSamples) {
			return false;
		}
		pendingSamples.push_back(std::move(sample));
	}
	condition.notify_one();
	return true;
}

std::vector<VisualSimilarityMatch> VisualSimilarityIndex::FindSimilar(uint64_t requestedGeneration, ServerItemId sourceServerId, size_t count) const {
	std::vector<std::shared_ptr<const VisualSignature>> snapshot;
	std::shared_ptr<const VisualSignature> source;
	{
		std::lock_guard lock(mutex);
		if (requestedGeneration != generation) {
			return {};
		}
		const auto sourceIterator = signatures.find(sourceServerId.value);
		if (sourceIterator == signatures.end()) {
			return {};
		}
		source = sourceIterator->second;
		snapshot.reserve(signatures.size());
		for (const auto& [serverId, signature] : signatures) {
			if (serverId != sourceServerId.value) {
				snapshot.push_back(signature);
			}
		}
	}

	std::vector<VisualSimilarityMatch> matches;
	matches.reserve(snapshot.size());
	for (const std::shared_ptr<const VisualSignature>& candidate : snapshot) {
		if (candidate->opaque != source->opaque) {
			continue;
		}
		const double score = source->opaque ? 1.0 - static_cast<double>(std::popcount(source->averageHash ^ candidate->averageHash)) / 64.0 : DiceScore(*source, *candidate);
		if (score > 0.0) {
			matches.push_back({ candidate->serverId, score, HistogramIntersection(*source, *candidate) });
		}
	}
	std::sort(matches.begin(), matches.end(), [](const VisualSimilarityMatch& left, const VisualSimilarityMatch& right) {
		if (std::abs(left.score - right.score) > 0.000001) {
			return left.score > right.score;
		}
		if (std::abs(left.histogramScore - right.histogramScore) > 0.000001f) {
			return left.histogramScore > right.histogramScore;
		}
		return left.serverId.value < right.serverId.value;
	});
	if (matches.size() > count) {
		matches.resize(count);
	}
	return matches;
}

size_t VisualSimilarityIndex::GetIndexedCount(uint64_t requestedGeneration) const {
	std::lock_guard lock(mutex);
	return requestedGeneration == generation ? signatures.size() : 0;
}

bool VisualSimilarityIndex::HasPendingWork(uint64_t requestedGeneration) const {
	std::lock_guard lock(mutex);
	return requestedGeneration == generation && (active || !pendingSamples.empty());
}

bool VisualSimilarityIndex::WaitUntilIdle(uint64_t requestedGeneration, std::chrono::milliseconds timeout) {
	std::unique_lock lock(mutex);
	return idleCondition.wait_for(lock, timeout, [this, requestedGeneration] {
		return requestedGeneration != generation || (!active && pendingSamples.empty());
	});
}

bool VisualSimilarityIndex::CalculateSignature(const VisualSpriteSample& sample, VisualSignature& signature) {
	if (!sample.serverId.isValid() || sample.width <= 0 || sample.height <= 0 || sample.rgba.size() != static_cast<size_t>(sample.width) * sample.height * 4) {
		return false;
	}

	signature = {};
	signature.serverId = sample.serverId;
	signature.width = sample.width;
	signature.height = sample.height;
	signature.opaque = true;
	const size_t pixelCount = static_cast<size_t>(sample.width) * sample.height;
	signature.alphaMask.resize((pixelCount + 63) / 64);
	std::array<uint32_t, 512> histogramCounts {};
	uint32_t histogramPixels = 0;
	for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
		const size_t offset = pixel * 4;
		const bool foreground = sample.rgba[offset + 3] > AlphaThreshold;
		if (foreground) {
			signature.alphaMask[pixel / 64] |= uint64_t(1) << (pixel % 64);
		}
		signature.foregroundPixels += foreground;
		signature.opaque = signature.opaque && foreground;
		if (foreground) {
			const size_t red = sample.rgba[offset + 0] / 32;
			const size_t green = sample.rgba[offset + 1] / 32;
			const size_t blue = sample.rgba[offset + 2] / 32;
			++histogramCounts[red * 64 + green * 8 + blue];
			++histogramPixels;
		}
	}
	signature.histogramPixels = histogramPixels;
	for (size_t index = 0; index < histogramCounts.size(); ++index) {
		signature.histogram[index] = static_cast<uint16_t>(std::min<uint32_t>(histogramCounts[index], std::numeric_limits<uint16_t>::max()));
	}

	std::array<uint8_t, 64> gray {};
	uint64_t totalBrightness = 0;
	for (int outputY = 0; outputY < 8; ++outputY) {
		for (int outputX = 0; outputX < 8; ++outputX) {
			const int startX = (outputX * sample.width) / 8;
			const int startY = (outputY * sample.height) / 8;
			const int endX = std::max(startX + 1, ((outputX + 1) * sample.width) / 8);
			const int endY = std::max(startY + 1, ((outputY + 1) * sample.height) / 8);
			uint64_t blockBrightness = 0;
			uint32_t blockPixels = 0;
			for (int y = startY; y < std::min(endY, sample.height); ++y) {
				for (int x = startX; x < std::min(endX, sample.width); ++x) {
					const size_t offset = (static_cast<size_t>(y) * sample.width + x) * 4;
					blockBrightness += static_cast<uint64_t>(299 * sample.rgba[offset + 0] + 587 * sample.rgba[offset + 1] + 114 * sample.rgba[offset + 2]);
					++blockPixels;
				}
			}
			const uint8_t average = blockPixels == 0 ? 0 : static_cast<uint8_t>(blockBrightness / (blockPixels * 1000));
			gray[static_cast<size_t>(outputY) * 8 + outputX] = average;
			totalBrightness += average;
		}
	}
	const uint8_t averageBrightness = static_cast<uint8_t>(totalBrightness / gray.size());
	for (size_t index = 0; index < gray.size(); ++index) {
		if (gray[index] >= averageBrightness) {
			signature.averageHash |= uint64_t(1) << index;
		}
	}
	return true;
}

void VisualSimilarityIndex::WorkerLoop() {
	for (;;) {
		VisualSpriteSample sample;
		{
			std::unique_lock lock(mutex);
			condition.wait(lock, [this] {
				return stopping || !pendingSamples.empty();
			});
			if (stopping) {
				return;
			}
			sample = std::move(pendingSamples.front());
			pendingSamples.pop_front();
			active = true;
		}

		VisualSignature signature;
		const bool calculated = CalculateSignature(sample, signature);
		{
			std::lock_guard lock(mutex);
			if (calculated && sample.generation == generation) {
				signatures[sample.serverId.value] = std::make_shared<VisualSignature>(std::move(signature));
			}
			active = false;
			if (pendingSamples.empty()) {
				idleCondition.notify_all();
			}
		}
	}
}
