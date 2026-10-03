//////////////////////////////////////////////////////////////////////
// NexaMap server workspace discovery and resource metadata.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_SERVER_WORKSPACE_H_
#define NEXAMAP_SERVER_WORKSPACE_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class ItemIdMode : uint8_t {
	Unknown = 0,
	ServerId,
	ClientId,
};

enum class ItemIdModePreference : uint8_t {
	Auto = 0,
	ServerId,
	ClientId,
};

enum class ServerType : uint8_t {
	UnknownGeneric = 0,
	Tfs,
	Canary,
	Crystal,
	CanaryCrystal,
};

struct ResourceFingerprint {
	std::filesystem::path path;
	uintmax_t size = 0;
	std::filesystem::file_time_type modifiedAt {};
	bool exists = false;

	[[nodiscard]] static ResourceFingerprint Read(const std::filesystem::path& path);
	[[nodiscard]] bool MatchesCurrentFile() const;

	friend bool operator==(const ResourceFingerprint&, const ResourceFingerprint&) = default;
};

struct DetectedMap {
	std::filesystem::path path;
	ResourceFingerprint fingerprint;
	std::filesystem::path serverRootPath;
	ServerType serverType = ServerType::UnknownGeneric;
};

struct ServerWorkspace {
	std::filesystem::path rootPath;
	std::filesystem::path itemsOtbPath;
	std::filesystem::path itemsXmlPath;
	std::filesystem::path appearancesPath;
	std::filesystem::path activeDataDirectory;
	std::filesystem::path mapsDirectory;
	std::filesystem::path primaryMapPath;
	std::filesystem::path monstersDirectory;
	std::filesystem::path npcsDirectory;

	ResourceFingerprint itemsOtbFingerprint;
	ResourceFingerprint itemsXmlFingerprint;
	ResourceFingerprint appearancesFingerprint;
	std::vector<DetectedMap> maps;

	ItemIdMode itemIdMode = ItemIdMode::Unknown;
	ServerType serverType = ServerType::UnknownGeneric;
	std::string serverProfile;
	std::string protocol;
	std::vector<std::string> warnings;
	std::size_t directoriesScanned = 0;
	bool scanLimitReached = false;

	[[nodiscard]] bool hasRequiredResources() const;
	[[nodiscard]] bool hasItemsOtb() const;
	[[nodiscard]] bool hasItemsXml() const;
	[[nodiscard]] bool hasAppearances() const;
	[[nodiscard]] bool usesCanaryCrystalLoader() const;
	[[nodiscard]] bool containsMap(const std::filesystem::path& path) const;
	[[nodiscard]] const DetectedMap* findMap(const std::filesystem::path& path) const;
	[[nodiscard]] bool trackedResourcesChanged() const;
};

struct ServerDetectionOptions {
	std::size_t maximumDirectories = 2048;
	std::size_t maximumMaps = 256;
	std::size_t fallbackDepth = 4;
	std::size_t mapDepth = 3;
	bool diagnosticLogging = false;
};

struct ServerDetectionResult {
	ServerWorkspace workspace;
	bool validRoot = false;
	std::string error;
};

class ServerResourceDetector {
public:
	[[nodiscard]] static ServerDetectionResult Detect(
		const std::filesystem::path& root,
		const ServerDetectionOptions& options = {}
	);
};

[[nodiscard]] const char* ItemIdModeName(ItemIdMode mode);
[[nodiscard]] const char* ServerTypeName(ServerType type);
[[nodiscard]] bool UsesCanaryCrystalLoader(ServerType type);
[[nodiscard]] ItemIdMode ResolveEffectiveItemIdMode(
	ItemIdModePreference preference,
	ItemIdMode clientAssetMode,
	ItemIdMode serverEvidence
);

#endif // NEXAMAP_SERVER_WORKSPACE_H_
