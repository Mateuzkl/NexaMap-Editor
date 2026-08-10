#ifndef NEXAMAP_SPRITE_APPEARANCES_H_
#define NEXAMAP_SPRITE_APPEARANCES_H_

#include "client_assets_manifest.h"

#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>
#include <wx/arrstr.h>
#include <wx/string.h>

enum class ClientSpriteLayout : uint8_t {
	OneByOne = 0,
	OneByTwo = 1,
	TwoByOne = 2,
	TwoByTwo = 3,
	ThreeByThree = 11,
	FourByFour = 16,
	FiveByFive = 22,
};

struct ClientSpriteSize {
	int width = 32;
	int height = 32;
};

class ClientSpriteSheet {
public:
	ClientSpriteSheet(const ClientSpriteSheetManifest& manifest);

	ClientSpriteSize getSpriteSize() const noexcept;
	uint32_t firstSpriteId = 0;
	uint32_t lastSpriteId = 0;
	ClientSpriteLayout layout = ClientSpriteLayout::OneByOne;
	std::filesystem::path file;
	std::unique_ptr<uint8_t[]> pixels;
	uint64_t lastAccess = 0;
	bool asyncFailed = false;
	bool synchronousFailureLogged = false;
};

using ClientSpriteSheetPtr = std::shared_ptr<ClientSpriteSheet>;

class SpriteAppearances {
public:
	~SpriteAppearances();

	void init();
	void unload();
	bool loadCatalog(const ClientAssetsManifest& manifest, wxString& error, wxArrayString& warnings);
	bool getSpritePixels(uint32_t spriteId, std::vector<uint8_t>& pixels, ClientSpriteSize& size, wxString& error);
	bool getSpritePixelsIfLoaded(uint32_t spriteId, std::vector<uint8_t>& pixels, ClientSpriteSize& size, bool& pending);
	ClientSpriteSheetPtr getSheetBySpriteId(uint32_t spriteId) const;
	size_t getSheetCount() const noexcept {
		return sheets.size();
	}
	size_t getPendingSheetCount() const;
	size_t getReadySheetCount() const noexcept {
		return readyCount.load(std::memory_order_acquire);
	}

private:
	struct DecodedSheet {
		ClientSpriteSheetPtr sheet;
		std::unique_ptr<uint8_t[]> pixels;
		wxString error;
	};

	bool decodeSpriteSheet(const ClientSpriteSheetPtr& sheet, std::unique_ptr<uint8_t[]>& output, wxString& error) const;
	bool loadSpriteSheet(const ClientSpriteSheetPtr& sheet, wxString& error);
	bool copySpritePixels(const ClientSpriteSheetPtr& sheet, uint32_t spriteId, std::vector<uint8_t>& output, ClientSpriteSize& size, wxString& error);
	void trimLoadedSheets(const ClientSpriteSheetPtr& keep);
	void startWorker();
	void stopWorker();
	void workerLoop();
	void requestSpriteSheet(const ClientSpriteSheetPtr& sheet);
	void processReadySheets();

	std::vector<ClientSpriteSheetPtr> sheets;
	uint64_t accessCounter = 0;
	mutable std::mutex workerMutex;
	std::condition_variable workerCondition;
	std::deque<ClientSpriteSheetPtr> pendingSheets;
	std::deque<DecodedSheet> readySheets;
	std::atomic_size_t readyCount { 0 };
	std::unordered_set<uint32_t> queuedSheetIds;
	ClientSpriteSheetPtr activeSheet;
	std::thread worker;
	bool workerStopping = false;
};

extern SpriteAppearances g_spriteAppearances;

#endif
