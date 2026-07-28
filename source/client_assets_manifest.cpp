#include "client_assets_manifest.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <system_error>

namespace {
	using json = nlohmann::json;

	struct DetectedAssetsLayout {
		ClientAssetsLayout layout = ClientAssetsLayout::CipSoft;
		std::filesystem::path assetsDirectory;
		std::filesystem::path packageFile;
		std::filesystem::path assetsIndexFile;
		std::string otcVersion;
		std::vector<std::string> warnings;
	};

	ClientAssetsValidationResult Failure(std::string error) {
		ClientAssetsValidationResult result;
		result.error = std::move(error);
		return result;
	}

	bool IsPathInside(const std::filesystem::path& parent, const std::filesystem::path& child) {
		std::error_code error;
		const std::filesystem::path normalizedParent = std::filesystem::weakly_canonical(parent, error);
		if (error) {
			return false;
		}
		const std::filesystem::path normalizedChild = std::filesystem::weakly_canonical(child, error);
		if (error) {
			return false;
		}

		auto parentIt = normalizedParent.begin();
		auto childIt = normalizedChild.begin();
		for (; parentIt != normalizedParent.end(); ++parentIt, ++childIt) {
			if (childIt == normalizedChild.end() || *parentIt != *childIt) {
				return false;
			}
		}
		return true;
	}

	bool ReadJson(const std::filesystem::path& file, json& document, std::string& error) {
		std::ifstream stream(file, std::ios::in | std::ios::binary);
		if (!stream.is_open()) {
			error = "Could not open " + file.string() + ".";
			return false;
		}
		document = json::parse(stream, nullptr, false);
		if (document.is_discarded()) {
			error = "Invalid JSON in " + file.string() + ".";
			return false;
		}
		return true;
	}

	bool ReadRequiredString(const json& object, const char* key, std::string& value) {
		const auto iterator = object.find(key);
		if (iterator == object.end() || !iterator->is_string()) {
			return false;
		}
		value = iterator->get<std::string>();
		return !value.empty();
	}

	bool ReadRequiredUInt(const json& object, const char* key, uint32_t& value) {
		const auto iterator = object.find(key);
		if (iterator == object.end() || !iterator->is_number_unsigned()) {
			return false;
		}
		const uint64_t raw = iterator->get<uint64_t>();
		if (raw > std::numeric_limits<uint32_t>::max()) {
			return false;
		}
		value = static_cast<uint32_t>(raw);
		return true;
	}

	bool IsRegularFile(const std::filesystem::path& file) {
		std::error_code error;
		return std::filesystem::is_regular_file(file, error) && !error;
	}

	bool IsDirectory(const std::filesystem::path& directory) {
		std::error_code error;
		return std::filesystem::is_directory(directory, error) && !error;
	}

	bool HasCatalog(const std::filesystem::path& directory) {
		return IsRegularFile(directory / "catalog-content.json");
	}

	uint64_t NumericVersionRank(const std::filesystem::path& directory) {
		const std::string name = directory.filename().string();
		if (name.empty() || !std::ranges::all_of(name, [](unsigned char value) { return std::isdigit(value) != 0; })) {
			return 0;
		}
		try {
			return std::stoull(name);
		} catch (...) {
			return 0;
		}
	}

	std::string FormatOtcVersion(const std::string& versionDirectory) {
		if (versionDirectory.size() >= 3 && std::ranges::all_of(versionDirectory, [](unsigned char value) { return std::isdigit(value) != 0; })) {
			return versionDirectory.substr(0, versionDirectory.size() - 2) + "." + versionDirectory.substr(versionDirectory.size() - 2) + " (OTC)";
		}
		return versionDirectory.empty() ? "OTC Assets" : versionDirectory + " (OTC)";
	}

	void AddOtcCatalogDirectory(
		const std::filesystem::path& directory,
		std::vector<std::filesystem::path>& candidates
	) {
		if (HasCatalog(directory)) {
			candidates.push_back(directory);
		}
	}

	void AddOtcVersionDirectories(
		const std::filesystem::path& parent,
		std::vector<std::filesystem::path>& candidates
	) {
		if (!IsDirectory(parent)) {
			return;
		}
		std::error_code error;
		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(parent, error)) {
			if (error) {
				return;
			}
			if (entry.is_directory(error) && !error && HasCatalog(entry.path())) {
				candidates.push_back(entry.path());
			}
		}
	}

	std::optional<DetectedAssetsLayout> DetectLayout(const std::filesystem::path& root) {
		DetectedAssetsLayout detected;

		// Full CipSoft/Crystal client root.
		if (HasCatalog(root / "assets") && IsRegularFile(root / "package.json")) {
			detected.layout = ClientAssetsLayout::CipSoft;
			detected.assetsDirectory = root / "assets";
			detected.packageFile = root / "package.json";
			detected.assetsIndexFile = root / "assets.json";
			return detected;
		}

		// The assets directory itself may be selected.
		if (HasCatalog(root) && root.filename() == "assets" && IsRegularFile(root.parent_path() / "package.json")) {
			detected.layout = ClientAssetsLayout::CipSoft;
			detected.assetsDirectory = root;
			detected.packageFile = root.parent_path() / "package.json";
			detected.assetsIndexFile = root.parent_path() / "assets.json";
			return detected;
		}

		std::vector<std::filesystem::path> otcCandidates;
		AddOtcCatalogDirectory(root, otcCandidates);
		AddOtcCatalogDirectory(root / "data" / "things", otcCandidates);
		AddOtcVersionDirectories(root / "data" / "things", otcCandidates);
		AddOtcCatalogDirectory(root / "things", otcCandidates);
		AddOtcVersionDirectories(root / "things", otcCandidates);
		AddOtcVersionDirectories(root, otcCandidates);
		if (otcCandidates.empty()) {
			return std::nullopt;
		}

		std::ranges::sort(otcCandidates, [](const auto& left, const auto& right) {
			const uint64_t leftRank = NumericVersionRank(left);
			const uint64_t rightRank = NumericVersionRank(right);
			if (leftRank != rightRank) {
				return leftRank > rightRank;
			}
			return left.filename().string() > right.filename().string();
		});
		otcCandidates.erase(std::unique(otcCandidates.begin(), otcCandidates.end()), otcCandidates.end());

		detected.layout = ClientAssetsLayout::OtClient;
		detected.assetsDirectory = otcCandidates.front();
		detected.otcVersion = detected.assetsDirectory.filename().string();
		if (otcCandidates.size() > 1) {
			detected.warnings.push_back(
				"Multiple OTC Assets versions were found; using " + detected.assetsDirectory.string() + "."
			);
		}
		return detected;
	}

	bool IsSupportedSpriteType(uint32_t spriteType) {
		return spriteType == 0 || spriteType == 1 || spriteType == 2 || spriteType == 3 || spriteType == 11 || spriteType == 16 || spriteType == 22;
	}

	uint32_t SpriteSheetCapacity(uint32_t spriteType) {
		switch (spriteType) {
			case 1:
			case 2:
				return 72;
			case 3:
				return 36;
			case 11:
				return 16;
			case 16:
				return 9;
			case 22:
				return 4;
			case 0:
			default:
				return 144;
		}
	}
}

ClientAssetsValidationResult ClientAssetsManifestLoader::Validate(const std::filesystem::path& requestedRoot) {
	std::error_code filesystemError;
	if (requestedRoot.empty() || !std::filesystem::is_directory(requestedRoot, filesystemError) || filesystemError) {
		return Failure("The selected Canary/Crystal client root is not a readable directory.");
	}

	ClientAssetsManifest manifest;
	manifest.root = std::filesystem::weakly_canonical(requestedRoot, filesystemError);
	if (filesystemError) {
		return Failure("Could not resolve the selected Canary/Crystal client root.");
	}
	const std::optional<DetectedAssetsLayout> detected = DetectLayout(manifest.root);
	if (!detected) {
		return Failure(
			"Could not find a supported Assets layout. Select either a CipSoft/Crystal client root "
			"or an OTC root containing data/things/catalog-content.json or "
			"data/things/<version>/catalog-content.json."
		);
	}
	manifest.layout = detected->layout;
	manifest.assetsDirectory = detected->assetsDirectory;
	manifest.packageFile = detected->packageFile;
	manifest.assetsIndexFile = detected->assetsIndexFile;
	manifest.catalogFile = manifest.assetsDirectory / "catalog-content.json";
	std::string jsonError;
	if (manifest.layout == ClientAssetsLayout::CipSoft) {
		json packageDocument;
		if (!ReadJson(manifest.packageFile, packageDocument, jsonError)) {
			return Failure(std::move(jsonError));
		}
		if (!packageDocument.is_object() || !ReadRequiredString(packageDocument, "version", manifest.version)) {
			return Failure("package.json must contain a non-empty string field named \"version\".");
		}
		if (IsRegularFile(manifest.assetsIndexFile)) {
			json assetsIndexDocument;
			if (!ReadJson(manifest.assetsIndexFile, assetsIndexDocument, jsonError)) {
				return Failure(std::move(jsonError));
			}
			if (!assetsIndexDocument.is_object() || !assetsIndexDocument.contains("files") || !assetsIndexDocument["files"].is_array()) {
				return Failure("assets.json must contain an array field named \"files\".");
			}
		} else {
			manifest.warnings.emplace_back("assets.json is not present; catalog-content.json will be used directly.");
		}
		if (!IsRegularFile(manifest.root / "assets.json.sha256") && !IsRegularFile(manifest.assetsDirectory.parent_path() / "assets.json.sha256")) {
			manifest.warnings.emplace_back("assets.json.sha256 is not present; file paths will still be validated.");
		}
	} else {
		manifest.version = FormatOtcVersion(detected->otcVersion);
		manifest.warnings.insert(
			manifest.warnings.end(),
			detected->warnings.begin(),
			detected->warnings.end()
		);
	}

	json catalogDocument;
	if (!ReadJson(manifest.catalogFile, catalogDocument, jsonError)) {
		return Failure(std::move(jsonError));
	}
	if (!catalogDocument.is_array()) {
		return Failure("assets/catalog-content.json must contain a JSON array.");
	}

	for (size_t index = 0; index < catalogDocument.size(); ++index) {
		const json& entry = catalogDocument[index];
		if (!entry.is_object()) {
			return Failure("Invalid catalog entry #" + std::to_string(index) + ": expected an object.");
		}

		std::string type;
		if (!ReadRequiredString(entry, "type", type)) {
			return Failure("Invalid catalog entry #" + std::to_string(index) + ": missing string field \"type\".");
		}
		if (type != "appearances" && type != "sprite") {
			continue;
		}

		std::string relativeFile;
		if (!ReadRequiredString(entry, "file", relativeFile)) {
			return Failure("Invalid " + type + " catalog entry #" + std::to_string(index) + ": missing string field \"file\".");
		}
		// Catalog asset names are ASCII (category prefix + hexadecimal hash).
		const std::filesystem::path resolvedFile = manifest.assetsDirectory / std::filesystem::path(relativeFile);
		if (!IsPathInside(manifest.assetsDirectory, resolvedFile)) {
			return Failure("Catalog entry #" + std::to_string(index) + " points outside the assets directory.");
		}
		if (!IsRegularFile(resolvedFile)) {
			return Failure("Catalog file is missing or unreadable: " + resolvedFile.string());
		}

		if (type == "appearances") {
			if (!manifest.appearancesFile.empty()) {
				return Failure("assets/catalog-content.json contains more than one appearances entry.");
			}
			manifest.appearancesFile = resolvedFile;
			continue;
		}

		ClientSpriteSheetManifest sheet;
		sheet.file = resolvedFile;
		uint32_t spriteType = 0;
		if (!ReadRequiredUInt(entry, "firstspriteid", sheet.firstSpriteId) || !ReadRequiredUInt(entry, "lastspriteid", sheet.lastSpriteId) || !ReadRequiredUInt(entry, "spritetype", spriteType) || sheet.lastSpriteId < sheet.firstSpriteId || !IsSupportedSpriteType(spriteType) || static_cast<uint64_t>(sheet.lastSpriteId) - sheet.firstSpriteId + 1 > SpriteSheetCapacity(spriteType)) {
			return Failure("Invalid sprite range/layout in catalog entry #" + std::to_string(index) + ".");
		}
		sheet.spriteType = static_cast<uint8_t>(spriteType);
		manifest.spriteSheets.push_back(std::move(sheet));
	}

	if (manifest.appearancesFile.empty()) {
		return Failure("assets/catalog-content.json does not contain an appearances entry.");
	}
	if (manifest.spriteSheets.empty()) {
		return Failure("assets/catalog-content.json does not contain any sprite entries.");
	}

	std::ranges::sort(manifest.spriteSheets, {}, &ClientSpriteSheetManifest::firstSpriteId);
	uint32_t previousLast = 0;
	bool hasPreviousSheet = false;
	std::vector<ClientSpriteSheetManifest> nonOverlappingSheets;
	nonOverlappingSheets.reserve(manifest.spriteSheets.size());
	for (ClientSpriteSheetManifest& sheet : manifest.spriteSheets) {
		if (hasPreviousSheet && sheet.firstSpriteId <= previousLast) {
			if (sheet.lastSpriteId <= previousLast) {
				manifest.warnings.push_back(
					"Dropped duplicate/covered sprite sheet range " + std::to_string(sheet.firstSpriteId) + ".." + std::to_string(sheet.lastSpriteId) + "."
				);
				continue;
			}
			return Failure("catalog-content.json contains partially overlapping sprite ranges.");
		}
		previousLast = sheet.lastSpriteId;
		hasPreviousSheet = true;
		nonOverlappingSheets.push_back(std::move(sheet));
	}
	manifest.spriteSheets = std::move(nonOverlappingSheets);

	ClientAssetsValidationResult result;
	result.valid = true;
	result.manifest = std::move(manifest);
	return result;
}
