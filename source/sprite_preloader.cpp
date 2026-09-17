#include "sprite_preloader.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace {
	constexpr size_t SpriteWidth = 32;
	constexpr size_t SpritePixels = SpriteWidth * SpriteWidth;
	constexpr size_t SpriteBytes = SpritePixels * 4;
}

SpritePreloader g_spritePreloader;

SpritePreloader::~SpritePreloader() {
	stopWorkers();
	clear();
}

void SpritePreloader::configure(const std::filesystem::path& file, const std::vector<uint32_t>& offsets, bool hasTransparency, size_t workerCount) {
	clear();
	if (file.empty() || offsets.empty() || workerCount == 0) {
		return;
	}

	workerCount = std::clamp<size_t>(workerCount, 1, 2);

	{
		std::lock_guard lock(mutex);
		spriteFile = file;
		spriteOffsets = offsets;
		transparency = hasTransparency;
		configured = true;
		stopping = false;
		++generation;
	}

	if (workers.size() != workerCount) {
		stopWorkers();
		{
			std::lock_guard lock(mutex);
			configured = true;
			stopping = false;
		}
		workers.reserve(workerCount);
		for (size_t index = 0; index < workerCount; ++index) {
			workers.emplace_back(&SpritePreloader::workerLoop, this);
		}
	} else {
		condition.notify_all();
	}
}

void SpritePreloader::stopWorkers() {
	{
		std::lock_guard lock(mutex);
		stopping = true;
		configured = false;
		++generation;
	}
	condition.notify_all();
	for (std::thread& worker : workers) {
		if (worker.joinable()) {
			worker.join();
		}
	}
	workers.clear();
	stopping = false;
}

void SpritePreloader::clear() {
	std::lock_guard lock(mutex);
	spriteFile.clear();
	spriteOffsets.clear();
	pendingTasks.clear();
	readySprites.clear();
	readyOrder.clear();
	queuedSpriteIds.clear();
	failedSpriteIds.clear();
	configured = false;
	++generation;
}

SpritePreloadStatus SpritePreloader::getOrRequest(uint32_t spriteId, std::vector<uint8_t>& pixels) {
	std::lock_guard lock(mutex);
	if (!configured || spriteId == 0 || spriteId >= spriteOffsets.size()) {
		return SpritePreloadStatus::Unavailable;
	}

	const auto ready = readySprites.find(spriteId);
	if (ready != readySprites.end()) {
		pixels = std::move(ready->second.pixels);
		readyOrder.erase(ready->second.orderIt);
		readySprites.erase(ready);
		return SpritePreloadStatus::Ready;
	}
	if (failedSpriteIds.contains(spriteId)) {
		return SpritePreloadStatus::Failed;
	}
	if (queuedSpriteIds.contains(spriteId)) {
		return SpritePreloadStatus::Pending;
	}

	const uint32_t offset = spriteOffsets[spriteId];
	if (offset == 0) {
		pixels.assign(SpriteBytes, 0);
		return SpritePreloadStatus::Ready;
	}
	if (pendingTasks.size() >= MaximumPendingSprites) {
		return SpritePreloadStatus::Pending;
	}

	queuedSpriteIds.insert(spriteId);
	pendingTasks.push_back({ spriteId, offset, generation });
	condition.notify_one();
	return SpritePreloadStatus::Pending;
}

bool SpritePreloader::decode(std::ifstream& stream, uint32_t offset, bool hasTransparency, std::vector<uint8_t>& pixels) {
	if (!stream.is_open()) {
		return false;
	}

	stream.clear();
	stream.seekg(static_cast<std::streamoff>(offset) + 3, std::ios::beg);
	if (!stream) {
		return false;
	}

	uint8_t sizeBytes[2] = {};
	if (!stream.read(reinterpret_cast<char*>(sizeBytes), sizeof(sizeBytes))) {
		return false;
	}
	const uint16_t size = static_cast<uint16_t>(sizeBytes[0] | (static_cast<uint16_t>(sizeBytes[1]) << 8));
	thread_local static std::vector<uint8_t> dump;
	dump.resize(size);
	if (size != 0 && !stream.read(reinterpret_cast<char*>(dump.data()), size)) {
		return false;
	}

	pixels.assign(SpriteBytes, 0);
	const size_t bytesPerPixel = hasTransparency ? 4 : 3;
	size_t read = 0;
	size_t writePixel = 0;
	while (read + 2 <= dump.size() && writePixel < SpritePixels) {
		const size_t transparent = dump[read] | (static_cast<size_t>(dump[read + 1]) << 8);
		read += 2;
		writePixel = std::min(SpritePixels, writePixel + transparent);
		if (writePixel >= SpritePixels || read + 2 > dump.size()) {
			break;
		}

		const size_t colored = dump[read] | (static_cast<size_t>(dump[read + 1]) << 8);
		read += 2;
		for (size_t index = 0; index < colored && writePixel < SpritePixels && read + bytesPerPixel <= dump.size(); ++index) {
			uint8_t* destination = pixels.data() + writePixel * 4;
			destination[0] = dump[read + 0];
			destination[1] = dump[read + 1];
			destination[2] = dump[read + 2];
			destination[3] = hasTransparency ? dump[read + 3] : 0xFF;
			++writePixel;
			read += bytesPerPixel;
		}
	}
	return true;
}

void SpritePreloader::workerLoop() {
	std::filesystem::path currentOpenPath;
	std::ifstream stream;

	for (;;) {
		Task task;
		std::filesystem::path file;
		bool hasTransparency = false;
		{
			std::unique_lock lock(mutex);
			condition.wait(lock, [this] { return stopping || !pendingTasks.empty(); });
			if (stopping) {
				return;
			}
			task = pendingTasks.front();
			pendingTasks.pop_front();
			file = spriteFile;
			hasTransparency = transparency;
		}

		if (currentOpenPath != file) {
			if (stream.is_open()) {
				stream.close();
			}
			stream.clear();
			if (!file.empty()) {
				stream.open(file, std::ios::binary);
			}
			currentOpenPath = file;
		}

		std::vector<uint8_t> pixels;
		const bool loaded = decode(stream, task.offset, hasTransparency, pixels);

		std::lock_guard lock(mutex);
		queuedSpriteIds.erase(task.spriteId);
		if (stopping || task.generation != generation) {
			continue;
		}
		if (!loaded) {
			failedSpriteIds.insert(task.spriteId);
			continue;
		}
		while (readySprites.size() >= MaximumReadySprites && !readyOrder.empty()) {
			readySprites.erase(readyOrder.front());
			readyOrder.pop_front();
		}
		readyOrder.push_back(task.spriteId);
		readySprites[task.spriteId] = { std::move(pixels), std::prev(readyOrder.end()) };
	}
}
