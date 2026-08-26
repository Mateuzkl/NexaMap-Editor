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
		server.write("data/items/items.xml", "<items/>");
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
		check(!detection.workspace.monstersDirectory.empty(), "standard TFS monsters are detected");
		check(!detection.workspace.npcsDirectory.empty(), "standard TFS NPCs are detected");
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
		check(!detection.workspace.hasRequiredResources(), "missing items.otb prevents readiness");
		check(detection.error.find("items.otb") != std::string::npos, "missing items.otb has a precise error");
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
		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(!detection.workspace.hasRequiredResources(), "build directories are excluded from fallback discovery");
	}

	{
		TemporaryDirectory server;
		server.write("config.lua", "worldType = \"open\"");
		server.write("items.otb");
		const ServerDetectionResult detection = ServerResourceDetector::Detect(server.path);
		check(detection.workspace.serverProfile == "TFS", "config.lua identifies a TFS-style root");
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
