#include "server_workspace.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
	int failures = 0;
	int checks = 0;

	void check(bool condition, const std::string& message) {
		++checks;
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	}

	class TemporaryDirectory {
	public:
		TemporaryDirectory() {
			const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
			path = std::filesystem::temp_directory_path() / ("nexamap-workspace-test-" + std::to_string(stamp));
			std::filesystem::create_directories(path);
		}

		~TemporaryDirectory() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}

		void write(const std::filesystem::path& relative, const std::string& contents = "test") const {
			const std::filesystem::path target = path / relative;
			std::filesystem::create_directories(target.parent_path());
			std::ofstream stream(target, std::ios::binary);
			stream << contents;
		}

		std::filesystem::path path;
	};
}

int main() {
	{
		TemporaryDirectory server;
		server.write("data/items/items.otb");
		server.write("data/world/world.otbm");
		// Reproduce the unrelated U+F05C directory in the reported TFS root.
		server.write(u8"\uF05C/notes.txt");
		server.write(u8"readme-\u5730\u56FE.txt");

		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(detection.validRoot && detection.workspace.hasItemsOtb(), "Unicode directory entries do not interrupt server discovery");
		check(detection.workspace.maps.size() == 1, "unrelated Unicode entries do not hide the world map");
		check(detection.workspace.serverType == ServerType::Tfs, "Unicode directory entries preserve TFS detection");
	}

	{
		TemporaryDirectory server;
		const std::filesystem::path root = u8"servidor-\u5730\u56FE-\U0001F30D";
		const std::filesystem::path world = root / u8"data/world/cidade-\u5730\u56FE-client.OTBM";
		server.write(root / "data/items/items.otb");
		server.write(world);
		server.write(root / u8"data/world/cidade-\u5730\u56FE-client.houses.otbm");

		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path / root);
		check(detection.workspace.hasItemsOtb(), "item resources can be discovered under a Unicode server root");
		check(detection.workspace.maps.size() == 2, "Unicode map filenames and uppercase OTBM extensions are recognized");
		check(detection.workspace.primaryMapPath == std::filesystem::weakly_canonical(server.path / world), "Unicode world names remain ahead of generated houses maps");
		check(detection.workspace.itemIdMode == ItemIdMode::ClientId, "ASCII client markers are detected without changing Unicode filename bytes");
	}

	{
		TemporaryDirectory server;
		server.write("data/items/items.otb");
		server.write("data/items/items.xml", "<items/>");
		server.write("config.lua", "mapName = \"world\"\n");
		server.write("data/world/world.otbm");
		server.write("data/cache/maps/world.houses.otbm");
		server.write("data/monster/rat.lua");
		server.write("data/npc/guide.lua");

		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(detection.validRoot, "standard TFS root is valid");
		check(detection.workspace.hasRequiredResources(), "standard TFS items.otb is required and detected");
		check(detection.workspace.hasItemsXml(), "standard TFS items.xml is detected");
		check(detection.workspace.maps.size() == 2, "maps from separate standard server directories are detected");
		check(detection.workspace.containsMap(server.path / "data/cache/maps/world.houses.otbm"), "detected map membership uses normalized paths");
		check(detection.workspace.primaryMapPath == std::filesystem::weakly_canonical(server.path / "data/world/world.otbm"), "classic TFS mapName selects the original world map");
		check(detection.workspace.maps.front().path == detection.workspace.primaryMapPath, "generated houses map never precedes the original world map");
		check(detection.workspace.serverType == ServerType::Tfs, "TFS 1.4 structure selects the normal loader");
		check(detection.workspace.maps.front().serverType == ServerType::Tfs, "world.otbm inherits the detected TFS project type");
		check(!detection.workspace.usesCanaryCrystalLoader(), "TFS 1.4 never enables the dedicated Canary/Crystal loader");
		check(!detection.workspace.monstersDirectory.empty(), "standard TFS monsters are detected");
		check(!detection.workspace.npcsDirectory.empty(), "standard TFS NPCs are detected");
	}

	{
		TemporaryDirectory server;
		server.write("data/items/items.otb");
		server.write("data/items/items.xml", "<items/>");
		server.write("data/world/world.otbm");
		server.write("data/monsters/rat.lua");
		server.write("data/npc/guide.lua");

		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(detection.workspace.serverType == ServerType::Tfs, "TFS 1.8 structure is detected as TFS");
		check(detection.workspace.serverProfile == "TFS", "TFS 1.8 has the stable display label");
	}

	{
		TemporaryDirectory server;
		server.write("data/items/items.otb");
		server.write("data/items/items.xml", "<items/>");
		server.write("data/items/appearances.dat", "unrelated client metadata");
		server.write("data/world/world.otbm");
		server.write("data/monster/rat.lua");
		server.write("data/npc/guide.lua");

		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(detection.workspace.serverType == ServerType::Tfs, "appearances.dat alone cannot override a complete TFS structure");
		check(!detection.workspace.usesCanaryCrystalLoader(), "stray appearances.dat does not start dedicated loading");
	}

	{
		TemporaryDirectory server;
		server.write("data/items/items.otb");
		server.write("data/world/forgotten.otbm");
		server.write("data/cache/maps/forgotten.houses.otbm");

		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(detection.workspace.primaryMapPath == std::filesystem::weakly_canonical(server.path / "data/world/forgotten.otbm"), "fallback selection prefers an original map over a generated houses map");
		check(detection.workspace.maps.front().path == detection.workspace.primaryMapPath, "fallback primary map is ordered first");
	}

	{
		TemporaryDirectory server;
		server.write("data/items.otb");
		server.write("data/items.xml", "<items/>");
		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(detection.workspace.itemsOtbPath.parent_path().filename() == "data", "data/items.otb known layout is preferred");
		check(detection.workspace.hasItemsXml(), "data/items.xml known layout is detected");
	}

	{
		TemporaryDirectory server;
		server.write("items/items.otb");
		server.write("items/items.xml", "<items/>");
		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(detection.workspace.itemsOtbPath.parent_path().filename() == "items", "items/items.otb known layout is detected");
		check(detection.workspace.hasItemsXml(), "items/items.xml known layout is detected");
	}

	{
		TemporaryDirectory server;
		server.write("custom/resources/deep/items.otb");
		server.write("custom/resources/deep/items.xml", "<items/>");
		server.write("custom/maps/arena-client.otbm");

		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(detection.workspace.hasRequiredResources(), "bounded fallback detects a custom items.otb");
		check(detection.workspace.hasItemsXml(), "bounded fallback detects a custom items.xml");
		check(detection.workspace.itemIdMode == ItemIdMode::ClientId, "explicit client map naming is usable ID-mode evidence");
	}

	{
		TemporaryDirectory server;
		server.write("items.otb");
		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(detection.workspace.hasRequiredResources(), "root-level items.otb is detected");
		check(!detection.workspace.hasItemsXml(), "items.xml remains optional");
		check(!detection.workspace.warnings.empty(), "missing optional items.xml produces a useful warning");
	}

	{
		TemporaryDirectory server;
		server.write("data/items/items.xml", "<items/>");
		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(!detection.workspace.hasRequiredResources(), "missing item database prevents readiness");
		check(detection.error.find("appearances.dat") != std::string::npos, "missing item database names both supported formats");
	}

	{
		TemporaryDirectory server;
		server.write("data/items/appearances.dat", "modern appearances");
		server.write("data/items/items.xml", "<items/>");
		server.write("config.lua", "dataPackDirectory = \"data-global\"\nmapName = \"world\"\n");
		server.write("data-global/world/world.otbm", "configured world");
		server.write("data-crystal/world/world.otbm", "inactive world");

		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(detection.workspace.hasRequiredResources(), "appearances.dat is a valid Canary/Crystal item database");
		check(detection.workspace.hasAppearances(), "appearances.dat is detected in data/items");
		check(!detection.workspace.hasItemsOtb(), "Canary/Crystal does not require a synthetic items.otb");
		check(detection.workspace.itemIdMode == ItemIdMode::ClientId, "appearances.dat selects the ClientID item model");
		check(detection.workspace.serverType == ServerType::Canary, "configured data-global datapack identifies Canary");
		check(detection.workspace.serverProfile == "Canary", "Canary has a specific display label");
		check(detection.workspace.maps.front().serverType == ServerType::Canary, "configured Canary map carries its own type");
		check(detection.workspace.usesCanaryCrystalLoader(), "Canary structure enables dedicated loading");
		check(detection.workspace.activeDataDirectory.filename() == "data-global", "config.lua selects the active data pack");
		check(detection.workspace.primaryMapPath == std::filesystem::weakly_canonical(server.path / "data-global/world/world.otbm"), "mapName selects the active map automatically");
		check(!detection.workspace.maps.empty() && detection.workspace.maps.front().path == detection.workspace.primaryMapPath, "configured map is the first detected map");
		check(!detection.workspace.containsMap(server.path / "data-crystal/world/world.otbm"), "inactive Crystal data pack maps are not mixed into the active workspace");
	}

	{
		TemporaryDirectory server;
		server.write("data/items/appearances.dat", "modern appearances");
		server.write("data/items/items.xml", "<items/>");
		server.write("config.lua", "dataPackDirectory = \"data-crystal\"\nmapName = \"world\"\n");
		server.write("data-crystal/world/world.otbm");

		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(detection.workspace.serverType == ServerType::Crystal, "configured data-crystal datapack identifies Crystal");
		check(detection.workspace.maps.front().serverType == ServerType::Crystal, "Crystal map carries the dedicated type");
		check(detection.workspace.usesCanaryCrystalLoader(), "Crystal structure enables dedicated loading");
	}

	{
		TemporaryDirectory folder;
		folder.write("Crystal-Server-devv/data/items/appearances.dat", "modern appearances");
		folder.write("Crystal-Server-devv/data/items/items.xml", "<items/>");
		folder.write("Crystal-Server-devv/config.lua", "dataPackDirectory = \"data-global\"\nmapName = \"world\"\n");
		folder.write("Crystal-Server-devv/data-global/world/world.otbm");
		folder.write("Crystal-Server-devv/data-crystal/world/world.otbm");

		const ServerDetectionResult detection = ServerResourceDetector::Detect(folder.path / "Crystal-Server-devv");
		check(detection.workspace.serverType == ServerType::Crystal, "Crystal server root remains Crystal when its active datapack is data-global");
		check(detection.workspace.usesCanaryCrystalLoader(), "Crystal data-global configuration still uses the dedicated loader");
	}

	{
		TemporaryDirectory collection;
		collection.write("tfs/data/items/items.otb");
		collection.write("tfs/data/items/items.xml", "<items/>");
		collection.write("tfs/data/world/world.otbm");
		collection.write("tfs/data/monsters/rat.lua");
		collection.write("tfs/data/npc/guide.lua");
		collection.write("canary/data/items/appearances.dat", "modern appearances");
		collection.write("canary/data/items/items.xml", "<items/>");
		collection.write("canary/config.lua", "dataPackDirectory = \"data-global\"\nmapName = \"world\"\n");
		collection.write("canary/data-global/world/world.otbm");

		const ServerDetectionResult detection = ServerResourceDetector::Detect(collection.path);
		const DetectedMap* tfsMap = detection.workspace.findMap(collection.path / "tfs/data/world/world.otbm");
		const DetectedMap* canaryMap = detection.workspace.findMap(collection.path / "canary/data-global/world/world.otbm");
		check(tfsMap != nullptr && tfsMap->serverType == ServerType::Tfs, "same-named TFS world keeps its per-map type");
		check(canaryMap != nullptr && canaryMap->serverType == ServerType::Canary, "same-named Canary world keeps its per-map type");
		check(tfsMap != nullptr && tfsMap->serverRootPath == std::filesystem::weakly_canonical(collection.path / "tfs"), "TFS map resolves its nearest project root");
		check(canaryMap != nullptr && canaryMap->serverRootPath == std::filesystem::weakly_canonical(collection.path / "canary"), "Canary map resolves its nearest project root");
	}

	{
		TemporaryDirectory folder;
		folder.write("maps/world.otbm");

		const ServerDetectionResult detection = ServerResourceDetector::Detect(folder.path);
		check(!detection.workspace.maps.empty(), "unknown folder still lists its map");
		check(detection.workspace.maps.front().serverType == ServerType::UnknownGeneric, "an otbm file alone remains Unknown/Generic");
		check(std::string(ServerTypeName(detection.workspace.maps.front().serverType)) == "Unknown/Generic", "generic maps have a stable display label");
		check(!UsesCanaryCrystalLoader(detection.workspace.maps.front().serverType), "an otbm file alone never enables dedicated loading");
	}

	{
		TemporaryDirectory server;
		server.write("data/items/appearances.dat", "old appearances");
		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(!detection.workspace.trackedResourcesChanged(), "fresh appearances.dat fingerprint is unchanged");
		server.write("data/items/appearances.dat", "changed modern appearances and larger");
		check(detection.workspace.trackedResourcesChanged(), "changed appearances.dat invalidates the workspace cache");
	}

	{
		TemporaryDirectory server;
		server.write("data/items/items.otb", "old");
		server.write("data/items/items.xml", "<items/>");
		server.write("data/world/world.otbm", "map");
		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(!detection.workspace.trackedResourcesChanged(), "fresh fingerprints are unchanged");
		server.write("data/items/items.otb", "changed and larger");
		check(detection.workspace.trackedResourcesChanged(), "changed items.otb invalidates the workspace cache");
	}

	{
		TemporaryDirectory server;
		server.write("data/items/items.otb", "otb");
		server.write("data/items/items.xml", "<items/>");
		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		server.write("data/items/items.xml", "<items><item id=\"100\"/></items>");
		check(detection.workspace.trackedResourcesChanged(), "changed items.xml invalidates the workspace cache");
	}

	{
		TemporaryDirectory server;
		server.write("data/items/items.otb", "otb");
		server.write("data/world/world.otbm", "old map");
		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		server.write("data/world/world.otbm", "changed map and larger");
		check(detection.workspace.trackedResourcesChanged(), "changed map invalidates the workspace cache");
	}

	{
		TemporaryDirectory server;
		server.write("a/b/c/items.otb");
		ServerDetectionOptions options;
		options.fallbackDepth = 2;
		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path, options);
		check(!detection.workspace.hasRequiredResources(), "bounded fallback does not scan beyond its depth");
	}

	{
		TemporaryDirectory server;
		server.write("custom/items.otb");
		ServerDetectionOptions options;
		options.maximumDirectories = 1;
		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path, options);
		check(detection.workspace.scanLimitReached, "directory limit is reported");
		check(!detection.workspace.warnings.empty(), "directory limit provides a warning");
	}

	{
		TemporaryDirectory server;
		server.write("data/items/items.otb");
		server.write("data/world/realm-server.otbm");
		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(detection.workspace.itemIdMode == ItemIdMode::ServerId, "server map naming selects ServerID mode");
		check(std::string(ItemIdModeName(detection.workspace.itemIdMode)) == "ServerID", "ServerID mode has a stable label");
	}

	{
		TemporaryDirectory server;
		server.write("data/items/items.otb");
		server.write("data/world/realm-client.otbm");
		server.write("data/world/realm-server.otbm");
		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(detection.workspace.itemIdMode == ItemIdMode::Unknown, "conflicting map evidence keeps ID mode unresolved");
	}

	{
		TemporaryDirectory server;
		server.write("build/generated/items.otb");
		server.write("build-native/generated/appearances.dat");
		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(!detection.workspace.hasRequiredResources(), "build and build-* directories are excluded from fallback discovery");
	}

	{
		TemporaryDirectory server;
		server.write("config.lua", "worldType = \"open\"");
		server.write("items.otb");
		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(detection.workspace.serverProfile == "Unknown/Generic", "config.lua and a loose items.otb are not enough to assume TFS");
	}

	{
		const ServerDetectionResult detection = ServerResourceDetector::Detect({});
		check(!detection.validRoot, "empty server root is rejected");
		check(!detection.error.empty(), "empty server root has a clear error");
	}

	check(
		ResolveEffectiveItemIdMode(ItemIdModePreference::Auto, ItemIdMode::ServerId, ItemIdMode::ClientId) == ItemIdMode::ServerId,
		"classic ServerID client model overrides conflicting map-name evidence"
	);
	check(
		ResolveEffectiveItemIdMode(ItemIdModePreference::Auto, ItemIdMode::ClientId, ItemIdMode::ServerId) == ItemIdMode::ClientId,
		"appearances ClientID model overrides conflicting map-name evidence"
	);
	check(
		ResolveEffectiveItemIdMode(ItemIdModePreference::ClientId, ItemIdMode::Unknown, ItemIdMode::ServerId) == ItemIdMode::ClientId,
		"manual ClientID preference resolves a workspace before client selection"
	);
	check(
		ResolveEffectiveItemIdMode(ItemIdModePreference::Auto, ItemIdMode::Unknown, ItemIdMode::ServerId) == ItemIdMode::ServerId,
		"automatic mode uses server evidence when client assets are not selected"
	);

	if (failures == 0) {
		std::cout << checks << " server workspace checks passed.\n";
	}
	return failures == 0 ? 0 : 1;
}
