#include "server_visual_catalog.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
	int failures = 0;
	int checks = 0;

	void Check(bool condition, const std::string& message) {
		++checks;
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	}

	class TemporaryDirectory {
	public:
		TemporaryDirectory() {
			path = std::filesystem::temp_directory_path() / ("nexamap-visual-catalog-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(path);
		}
		~TemporaryDirectory() {
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
		void write(const std::filesystem::path& relative, const std::string& contents) const {
			const std::filesystem::path target = path / relative;
			std::filesystem::create_directories(target.parent_path());
			std::ofstream stream(target, std::ios::binary);
			stream << contents;
		}
		std::filesystem::path path;
	};

	void TestFixture() {
		TemporaryDirectory server;
		server.write("src/const.h", R"(enum MagicEffectClasses : uint16_t {
 CONST_ME_NONE,
 CONST_ME_DRAWBLOOD = 1,
 CONST_ME_IMPLICIT,
 CONST_ME_HEX = 0x10,
 CONST_ME_ALIAS = CONST_ME_HEX,
 CONST_ME_LAST = 65535,
};
enum ShootType_t : uint16_t {
 CONST_ANI_NONE,
 CONST_ANI_SPEAR = 1,
 CONST_ANI_ARROW,
 CONST_ANI_WEAPONTYPE = 0xFE,
};)");
		server.write("data/lib/custom.lua", "CONST_ME_CUSTOM = 77\nCONST_ANI_CUSTOM = 33\n");
		ServerWorkspace workspace;
		workspace.rootPath = server.path;
		workspace.activeDataDirectory = server.path / "data";
		const ServerVisualCatalog catalog = ServerVisualCatalog::Build(workspace);
		Check(catalog.stats().filesRead == 2 && catalog.stats().filesInspected == 2, "visual catalog exposes deterministic file-read counters");
		Check(catalog.resolve(ServerVisualKind::MagicEffect, "CONST_ME_NONE") == 0, "implicit zero effect is resolved");
		Check(catalog.resolve(ServerVisualKind::MagicEffect, "const_me_implicit") == 2, "implicit enum increment is resolved case-insensitively");
		Check(catalog.resolve(ServerVisualKind::MagicEffect, "CONST_ME_ALIAS") == 16, "constant aliases are resolved");
		Check(catalog.resolve(ServerVisualKind::MagicEffect, "CONST_ME_CUSTOM") == 77, "custom Lua effect constants supplement the engine catalog");
		Check(catalog.resolve(ServerVisualKind::DistanceEffect, "CONST_ANI_ARROW") == 2, "projectile enum increment is resolved");
		Check(catalog.resolve(ServerVisualKind::DistanceEffect, "33") == 33, "numeric projectile IDs are accepted");
		Check(!catalog.resolve(ServerVisualKind::MagicEffect, "CONST_ME_UNKNOWN"), "unknown effect names do not invent IDs");
		Check(catalog.nameFor(ServerVisualKind::MagicEffect, 16) == "CONST_ME_ALIAS" || catalog.nameFor(ServerVisualKind::MagicEffect, 16) == "CONST_ME_HEX", "ID lookup returns an exact workspace constant");
	}

	void TestRealBase(const std::filesystem::path& root, const std::string& label) {
		const ServerDetectionResult detection = ServerResourceDetector::Detect(root);
		Check(detection.validRoot, label + " is a valid server workspace");
		const ServerVisualCatalog catalog = ServerVisualCatalog::Build(detection.workspace);
		const auto blood = catalog.resolve(ServerVisualKind::MagicEffect, "CONST_ME_DRAWBLOOD");
		const auto spear = catalog.resolve(ServerVisualKind::DistanceEffect, "CONST_ANI_SPEAR");
		Check(blood == 1, label + " resolves CONST_ME_DRAWBLOOD from its own source tree");
		Check(spear == 1, label + " resolves CONST_ANI_SPEAR from its own source tree");
		Check(!catalog.effects().empty() && !catalog.projectiles().empty(), label + " exposes effect and projectile catalogs");
	}
}

int main(int argc, char** argv) {
	TestFixture();
	if (argc == 4) {
		TestRealBase(argv[1], "TFS Lua");
		TestRealBase(argv[2], "TFS XML");
		TestRealBase(argv[3], "Crystal/Canary");
	} else if (argc != 1) {
		std::cerr << "Usage: server_visual_catalog_tests [tfs-lua-root tfs-xml-root crystal-root]\n";
		return 2;
	}
	if (failures == 0) {
		std::cout << checks << " server visual catalog checks passed.\n";
	}
	return failures == 0 ? 0 : 1;
}
