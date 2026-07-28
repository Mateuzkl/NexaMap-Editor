#ifndef NEXAMAP_SPRITE_APPEARANCES_H_
#define NEXAMAP_SPRITE_APPEARANCES_H_

#include "client_assets_manifest.h"

#include <cstdint>
#include <filesystem>
#include <memory>
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
};

using ClientSpriteSheetPtr = std::shared_ptr<ClientSpriteSheet>;

class SpriteAppearances {
public:
	void init();
	void unload();
	bool loadCatalog(const ClientAssetsManifest& manifest, wxString& error, wxArrayString& warnings);
	bool getSpritePixels(uint32_t spriteId, std::vector<uint8_t>& pixels, ClientSpriteSize& size, wxString& error);
	ClientSpriteSheetPtr getSheetBySpriteId(uint32_t spriteId) const;
	size_t getSheetCount() const noexcept {
		return sheets.size();
	}

private:
	bool loadSpriteSheet(const ClientSpriteSheetPtr& sheet, wxString& error);
	void trimLoadedSheets(const ClientSpriteSheetPtr& keep);

	std::vector<ClientSpriteSheetPtr> sheets;
	uint64_t accessCounter = 0;
};

extern SpriteAppearances g_spriteAppearances;

#endif
