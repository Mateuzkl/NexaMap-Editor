#ifndef NEXAMAP_CLIENT_ASSETS_MANIFEST_H_
#define NEXAMAP_CLIENT_ASSETS_MANIFEST_H_

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class ClientAssetsLayout : uint8_t {
	CipSoft,
	OtClient,
};

struct ClientSpriteSheetManifest {
	std::filesystem::path file;
	uint32_t firstSpriteId = 0;
	uint32_t lastSpriteId = 0;
	uint8_t spriteType = 0;
};

struct ClientAssetsManifest {
	ClientAssetsLayout layout = ClientAssetsLayout::CipSoft;
	std::filesystem::path root;
	std::filesystem::path assetsDirectory;
	std::filesystem::path packageFile;
	std::filesystem::path assetsIndexFile;
	std::filesystem::path catalogFile;
	std::filesystem::path appearancesFile;
	std::string version;
	std::vector<ClientSpriteSheetManifest> spriteSheets;
	std::vector<std::string> warnings;
};

struct ClientAssetsValidationResult {
	bool valid = false;
	ClientAssetsManifest manifest;
	std::string error;
};

class ClientAssetsManifestLoader {
public:
	static ClientAssetsValidationResult Validate(const std::filesystem::path& root);
};

#endif
