#ifndef NEXAMAP_SPRITE_PRELOADER_H_
#define NEXAMAP_SPRITE_PRELOADER_H_

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class SpritePreloadStatus : uint8_t {
	Ready,
	Pending,
	Failed,
	Unavailable,
};

class SpritePreloader {
public:
	~SpritePreloader();

	void configure(const std::filesystem::path& file, const std::vector<uint32_t>& offsets, bool hasTransparency, size_t workerCount = 2);
	void clear();
	SpritePreloadStatus getOrRequest(uint32_t spriteId, std::vector<uint8_t>& pixels);

private:
	struct Task {
		uint32_t spriteId = 0;
		uint32_t offset = 0;
		uint64_t generation = 0;
	};

	static bool decode(const std::filesystem::path& file, uint32_t offset, bool hasTransparency, std::vector<uint8_t>& pixels);
	void workerLoop();

	static constexpr size_t MaximumPendingSprites = 256;
	static constexpr size_t MaximumReadySprites = 512;

	mutable std::mutex mutex;
	std::condition_variable condition;
	std::filesystem::path spriteFile;
	std::vector<uint32_t> spriteOffsets;
	std::deque<Task> pendingTasks;
	std::unordered_map<uint32_t, std::vector<uint8_t>> readySprites;
	std::deque<uint32_t> readyOrder;
	std::unordered_set<uint32_t> queuedSpriteIds;
	std::unordered_set<uint32_t> failedSpriteIds;
	std::vector<std::thread> workers;
	uint64_t generation = 0;
	bool transparency = false;
	bool configured = false;
	bool stopping = false;
};

extern SpritePreloader g_spritePreloader;

#endif
