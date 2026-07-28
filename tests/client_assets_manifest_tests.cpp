#include "client_assets_manifest.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {
	int failures = 0;

	void check(bool condition, std::string_view name) {
		if (!condition) {
			std::cerr << "FAILED: " << name << '\n';
			++failures;
		}
	}

	void write(const std::filesystem::path& file, std::string_view content) {
		std::ofstream stream(file, std::ios::binary | std::ios::trunc);
		stream << content;
	}
}

int main() {
	const std::filesystem::path root = std::filesystem::temp_directory_path() / "nexamap-client-assets-manifest-tests";
	std::error_code error;
	std::filesystem::remove_all(root, error);

	check(!ClientAssetsManifestLoader::Validate(root).valid, "missing root is rejected");
	std::filesystem::create_directories(root / "assets");
	check(!ClientAssetsManifestLoader::Validate(root).valid, "missing package and catalog are rejected");

	write(root / "package.json", R"({"version":"14.12.0"})");
	write(root / "assets.json", R"({"files":[]})");
	write(root / "assets" / "catalog-content.json", R"([
		{"type":"appearances","file":"appearances.dat"},
		{"type":"sprite","file":"sheet.lzma","firstspriteid":0,"lastspriteid":143,"spritetype":0}
	])");
	write(root / "assets" / "appearances.dat", "");
	write(root / "assets" / "sheet.lzma", "");
	const ClientAssetsValidationResult valid = ClientAssetsManifestLoader::Validate(root);
	check(valid.valid, "complete reference-compatible root is accepted");
	check(valid.manifest.version == "14.12.0", "package version is retained");
	check(valid.manifest.spriteSheets.size() == 1, "sprite sheet entry is retained");
	check(valid.manifest.spriteSheets.front().firstSpriteId == 0, "official sprite range beginning at zero is accepted");
	check(valid.manifest.warnings.size() == 1, "optional hash absence is a warning");

	const std::filesystem::path otcRoot = root / "otc";
	const std::filesystem::path otcAssets = otcRoot / "data" / "things" / "1525";
	std::filesystem::create_directories(otcAssets);
	write(otcAssets / "catalog-content.json", R"([
		{"type":"appearances","file":"appearances.dat"},
		{"type":"sprite","file":"sheet.lzma","firstspriteid":10,"lastspriteid":25,"spritetype":11}
	])");
	write(otcAssets / "appearances.dat", "");
	write(otcAssets / "sheet.lzma", "");
	const ClientAssetsValidationResult otc = ClientAssetsManifestLoader::Validate(otcRoot);
	check(otc.valid, "OTC root with data/things/version is accepted");
	check(otc.manifest.layout == ClientAssetsLayout::OtClient, "OTC layout is detected");
	check(otc.manifest.assetsDirectory == otcAssets, "OTC version assets directory is resolved");
	check(otc.manifest.version == "15.25 (OTC)", "OTC directory version is formatted");
	check(ClientAssetsManifestLoader::Validate(otcAssets).valid, "OTC version directory can be selected directly");

	const std::filesystem::path directOtcRoot = root / "otc-direct";
	const std::filesystem::path directOtcAssets = directOtcRoot / "data" / "things";
	std::filesystem::create_directories(directOtcAssets);
	write(directOtcAssets / "catalog-content.json", R"([
		{"type":"appearances","file":"appearances.dat"},
		{"type":"sprite","file":"sheet.lzma","firstspriteid":30,"lastspriteid":45,"spritetype":11}
	])");
	write(directOtcAssets / "appearances.dat", "");
	write(directOtcAssets / "sheet.lzma", "");
	const ClientAssetsValidationResult directOtc = ClientAssetsManifestLoader::Validate(directOtcRoot);
	check(directOtc.valid, "OTC root with direct data/things catalog is accepted");
	check(directOtc.manifest.layout == ClientAssetsLayout::OtClient, "direct OTC layout is detected");
	check(directOtc.manifest.assetsDirectory == directOtcAssets, "direct OTC assets directory is resolved");

	const std::filesystem::path olderOtcAssets = directOtcAssets / "1500";
	const std::filesystem::path newerOtcAssets = directOtcAssets / "1526";
	for (const std::filesystem::path& versionAssets : { olderOtcAssets, newerOtcAssets }) {
		std::filesystem::create_directories(versionAssets);
		write(versionAssets / "catalog-content.json", R"([
			{"type":"appearances","file":"appearances.dat"},
			{"type":"sprite","file":"sheet.lzma","firstspriteid":50,"lastspriteid":65,"spritetype":11}
		])");
		write(versionAssets / "appearances.dat", "");
		write(versionAssets / "sheet.lzma", "");
	}
	const ClientAssetsValidationResult preferredOtc = ClientAssetsManifestLoader::Validate(directOtcRoot);
	check(preferredOtc.valid, "OTC root with direct and versioned catalogs is accepted");
	check(preferredOtc.manifest.assetsDirectory == newerOtcAssets, "newest OTC version is preferred over direct catalog");
	check(preferredOtc.manifest.version == "15.26 (OTC)", "preferred OTC version is retained");

	write(root / "assets" / "catalog-content.json", R"([
		{"type":"appearances","file":"appearances.dat"},
		{"type":"sprite","file":"sheet.lzma","firstspriteid":1,"lastspriteid":144,"spritetype":4}
	])");
	check(!ClientAssetsManifestLoader::Validate(root).valid, "unsupported sprite layout is rejected");

	write(root / "assets" / "catalog-content.json", R"([
		{"type":"appearances","file":"../outside.dat"},
		{"type":"sprite","file":"sheet.lzma","firstspriteid":1,"lastspriteid":144,"spritetype":0}
	])");
	write(root / "outside.dat", "");
	check(!ClientAssetsManifestLoader::Validate(root).valid, "catalog traversal outside assets is rejected");

	std::filesystem::remove_all(root, error);
	if (failures != 0) {
		std::cerr << failures << " client Assets manifest test(s) failed.\n";
		return 1;
	}
	std::cout << "Client Assets manifest tests passed.\n";
	return 0;
}
