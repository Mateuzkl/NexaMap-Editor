//////////////////////////////////////////////////////////////////////
// NexaMap server workspace discovery and resource metadata.
//////////////////////////////////////////////////////////////////////

#include "server_workspace.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <deque>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <regex>
#include <system_error>
#include <unordered_set>

namespace {
	std::string Lower(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return value;
	}

	std::filesystem::path Normalize(const std::filesystem::path& value) {
		std::error_code error;
		const std::filesystem::path absolute = std::filesystem::absolute(value, error);
		if (error) {
			return value.lexically_normal();
		}
		const std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, error);
		return error ? absolute.lexically_normal() : canonical;
	}

	bool IsRegularFile(const std::filesystem::path& path) {
		std::error_code error;
		return std::filesystem::is_regular_file(path, error) && !error;
	}

	bool IsDirectory(const std::filesystem::path& path) {
		std::error_code error;
		return std::filesystem::is_directory(path, error) && !error;
	}

	bool IsSkippedDirectory(const std::filesystem::path& path) {
		static const std::unordered_set<std::string> ignored {
			".git",
			".svn",
			".hg",
			"build",
			"builds",
			"node_modules",
			"vcpkg_installed",
			".cache",
			".idea",
			".vs",
		};
		const std::string name = Lower(path.filename().string());
		return ignored.contains(name) || name.starts_with("build-") || name.starts_with("build_");
	}

	template <std::size_t Size>
	std::filesystem::path FirstExistingDirectory(const std::filesystem::path& root, const std::array<const char*, Size>& candidates) {
		for (const char* relative : candidates) {
			const std::filesystem::path candidate = root / relative;
			if (IsDirectory(candidate)) {
				return Normalize(candidate);
			}
		}
		return {};
	}

	struct KnownItemFiles {
		std::filesystem::path otb;
		std::filesystem::path xml;
		std::filesystem::path appearances;
	};

	KnownItemFiles FindKnownItems(const std::filesystem::path& root) {
		static constexpr std::array<const char*, 4> directories {
			"data/items",
			"data",
			"items",
			"",
		};
		KnownItemFiles files;
		for (const char* relative : directories) {
			const std::filesystem::path directory = relative[0] == '\0' ? root : root / relative;
			const std::filesystem::path otb = directory / "items.otb";
			const std::filesystem::path xml = directory / "items.xml";
			const std::filesystem::path appearances = directory / "appearances.dat";
			if (IsRegularFile(otb) && files.otb.empty()) {
				files.otb = Normalize(otb);
			}
			if (IsRegularFile(xml) && files.xml.empty()) {
				files.xml = Normalize(xml);
			}
			if (IsRegularFile(appearances) && files.appearances.empty()) {
				files.appearances = Normalize(appearances);
			}
		}
		return files;
	}

	std::optional<std::string> ReadLuaStringAssignment(const std::filesystem::path& file, const std::string& setting) {
		std::ifstream stream(file);
		if (!stream.is_open()) {
			return std::nullopt;
		}
		const std::regex assignment("^\\s*" + setting + "\\s*=\\s*[\\\"']([^\\\"']+)[\\\"']");
		std::string line;
		std::smatch match;
		while (std::getline(stream, line)) {
			if (std::regex_search(line, match, assignment) && match.size() == 2) {
				return match[1].str();
			}
		}
		return std::nullopt;
	}

	bool IsSafeRelativePath(const std::filesystem::path& path) {
		if (path.empty() || path.is_absolute()) {
			return false;
		}
		return std::none_of(path.begin(), path.end(), [](const std::filesystem::path& component) {
			return component == "..";
		});
	}

	bool IsPathWithin(const std::filesystem::path& path, const std::filesystem::path& directory) {
		if (path.empty() || directory.empty()) {
			return false;
		}
		const std::filesystem::path relative = Normalize(path).lexically_relative(Normalize(directory));
		return relative == "." || IsSafeRelativePath(relative);
	}

	bool HasDirectory(const std::filesystem::path& root, std::initializer_list<const char*> candidates) {
		return std::any_of(candidates.begin(), candidates.end(), [&](const char* relative) {
			return IsDirectory(root / relative);
		});
	}

	bool HasFile(const std::filesystem::path& root, std::initializer_list<const char*> candidates) {
		return std::any_of(candidates.begin(), candidates.end(), [&](const char* relative) {
			return IsRegularFile(root / relative);
		});
	}

	ServerType ModernTypeFromName(const std::string& value) {
		const std::string name = Lower(value);
		if (name.find("crystal") != std::string::npos) {
			return ServerType::Crystal;
		}
		if (name.find("canary") != std::string::npos || name == "data-global" || name == "data-otservbr-global") {
			return ServerType::Canary;
		}
		return ServerType::CanaryCrystal;
	}

	struct ProfileEvidence {
		ServerType type = ServerType::UnknownGeneric;
		int score = 0;
	};

	ProfileEvidence DetectProfileEvidence(const std::filesystem::path& root, const std::filesystem::path& mapPath = {}) {
		const bool hasClassicItemsOtb = IsRegularFile(root / "data/items/items.otb");
		const bool hasAlternateItemsOtb = HasFile(root, { "data/items.otb", "items/items.otb", "items.otb" });
		const bool hasItemsOtb = hasClassicItemsOtb || hasAlternateItemsOtb;
		const bool hasClassicItemsXml = IsRegularFile(root / "data/items/items.xml");
		const bool hasAlternateItemsXml = HasFile(root, { "data/items.xml", "items/items.xml", "items.xml" });
		const bool hasAppearances = HasFile(root, { "data/items/appearances.dat", "data/appearances.dat", "items/appearances.dat", "appearances.dat" });
		const bool hasConfig = IsRegularFile(root / "config.lua") || IsRegularFile(root / "config.lua.dist");
		const bool hasMonsters = HasDirectory(root, { "data/monster", "data/monsters", "monster", "monsters" });
		const bool hasNpcs = HasDirectory(root, { "data/npc", "data/npcs", "npc", "npcs" });
		const bool mapInDataWorld = !mapPath.empty() && IsPathWithin(mapPath, root / "data/world");
		const bool mapInRootWorld = !mapPath.empty() && IsPathWithin(mapPath, root / "world");

		int tfsScore = 0;
		if (hasClassicItemsOtb) {
			tfsScore += 85;
		} else if (hasAlternateItemsOtb) {
			tfsScore += 55;
		}
		if (hasClassicItemsXml) {
			tfsScore += 20;
		} else if (hasAlternateItemsXml) {
			tfsScore += 10;
		}
		if (mapInDataWorld) {
			tfsScore += 50;
		} else if (mapInRootWorld) {
			tfsScore += 25;
		}
		if (hasConfig) {
			tfsScore += 10;
		}
		tfsScore += hasMonsters ? 10 : 0;
		tfsScore += hasNpcs ? 10 : 0;
		if (!hasItemsOtb) {
			tfsScore = 0;
		}

		int modernScore = hasAppearances ? 30 : 0;
		ServerType modernType = ServerType::CanaryCrystal;
		std::filesystem::path config = root / "config.lua";
		if (!IsRegularFile(config)) {
			config = root / "config.lua.dist";
		}
		if (IsRegularFile(config)) {
			const std::optional<std::string> configuredDataPack = ReadLuaStringAssignment(config, "dataPackDirectory");
			if (configuredDataPack) {
				const std::filesystem::path relativeDataPack(*configuredDataPack);
				if (IsSafeRelativePath(relativeDataPack)) {
					const std::filesystem::path configuredWorld = root / relativeDataPack / "world";
					if (mapPath.empty() || IsPathWithin(mapPath, configuredWorld)) {
						modernScore += 170;
						modernType = ModernTypeFromName(relativeDataPack.filename().string());
					}
				}
			}
		}

		struct NamedDataPack {
			const char* directory;
			ServerType type;
		};
		static constexpr std::array<NamedDataPack, 4> modernDataPacks {
			NamedDataPack { "data-global", ServerType::Canary },
			NamedDataPack { "data-canary", ServerType::Canary },
			NamedDataPack { "data-otservbr-global", ServerType::Canary },
			NamedDataPack { "data-crystal", ServerType::Crystal },
		};
		for (const NamedDataPack& dataPack : modernDataPacks) {
			const std::filesystem::path worldDirectory = root / dataPack.directory / "world";
			if (IsDirectory(worldDirectory) && (mapPath.empty() || IsPathWithin(mapPath, worldDirectory))) {
				modernScore += 150;
				modernType = dataPack.type;
				break;
			}
		}

		const std::string rootName = Lower(root.filename().string());
		if (hasAppearances && (rootName.find("canary") != std::string::npos || rootName.find("crystal") != std::string::npos)) {
			modernScore += 55;
			modernType = ModernTypeFromName(rootName);
		}

		// A stray appearances.dat is not enough to turn a classic items.otb
		// workspace into Canary/Crystal. Dedicated loading requires structural
		// evidence from the active/named datapack or an identified modern root.
		if (modernScore > tfsScore && modernScore >= 80) {
			return { modernType, modernScore };
		}
		if (tfsScore >= 75) {
			return { ServerType::Tfs, tfsScore };
		}
		return {};
	}

	struct DetectedMapContext {
		std::filesystem::path root;
		ServerType type = ServerType::UnknownGeneric;
		int score = 0;
	};

	DetectedMapContext DetectMapContext(const std::filesystem::path& mapPath, const std::filesystem::path& boundaryRoot) {
		DetectedMapContext best { Normalize(boundaryRoot), ServerType::UnknownGeneric, 0 };
		std::filesystem::path candidate = Normalize(mapPath).parent_path();
		const std::filesystem::path boundary = Normalize(boundaryRoot);
		while (!candidate.empty() && IsPathWithin(candidate, boundary)) {
			const ProfileEvidence evidence = DetectProfileEvidence(candidate, mapPath);
			if (evidence.score > best.score) {
				best = { candidate, evidence.type, evidence.score };
			}
			if (candidate == boundary || candidate == candidate.root_path()) {
				break;
			}
			candidate = candidate.parent_path();
		}
		return best;
	}

	bool IsAuxiliaryMap(const std::filesystem::path& path) {
		const std::string stem = Lower(path.stem().string());
		static constexpr std::array<const char*, 6> suffixes {
			".houses",
			"-houses",
			"_houses",
			".spawns",
			"-spawns",
			"_spawns",
		};
		return std::any_of(suffixes.begin(), suffixes.end(), [&](const char* suffix) {
			return stem.ends_with(suffix);
		});
	}

	void DetectConfiguredMap(ServerWorkspace& workspace) {
		std::filesystem::path config = workspace.rootPath / "config.lua";
		if (!IsRegularFile(config)) {
			config = workspace.rootPath / "config.lua.dist";
		}
		if (!IsRegularFile(config)) {
			return;
		}

		const std::optional<std::string> configuredMap = ReadLuaStringAssignment(config, "mapName");
		const std::optional<std::string> configuredDataPack = ReadLuaStringAssignment(config, "dataPackDirectory");
		if (!configuredMap && !configuredDataPack) {
			return;
		}

		std::vector<std::filesystem::path> mapDirectories;
		if (configuredDataPack) {
			const std::filesystem::path relativeDataPack(*configuredDataPack);
			if (!IsSafeRelativePath(relativeDataPack)) {
				workspace.warnings.push_back("config.lua contains an unsafe dataPackDirectory; automatic map selection ignored it.");
				return;
			}

			const std::filesystem::path activeData = workspace.rootPath / relativeDataPack;
			const std::filesystem::path worldDirectory = activeData / "world";
			if (!IsDirectory(worldDirectory)) {
				workspace.warnings.push_back("The dataPackDirectory from config.lua has no world directory.");
				return;
			}
			workspace.activeDataDirectory = Normalize(activeData);
			workspace.mapsDirectory = Normalize(worldDirectory);
			workspace.protocol = relativeDataPack.generic_string();
			mapDirectories.push_back(worldDirectory);
		} else {
			// Classic TFS releases use mapName without dataPackDirectory. Their map
			// lives under data/world, while generated *.houses.otbm files may live
			// under data/cache/maps and must not become the primary map.
			mapDirectories = {
				workspace.rootPath,
				workspace.rootPath / "data/world",
				workspace.rootPath / "world",
				workspace.rootPath / "data/maps",
				workspace.rootPath / "maps",
			};
		}

		std::filesystem::path mapName = configuredMap ? std::filesystem::path(*configuredMap) : std::filesystem::path("world");
		if (!IsSafeRelativePath(mapName)) {
			workspace.warnings.push_back("config.lua contains an unsafe mapName; automatic map selection ignored it.");
			return;
		}
		if (mapName.extension().empty()) {
			mapName += ".otbm";
		}

		for (const std::filesystem::path& directory : mapDirectories) {
			const std::filesystem::path primaryMap = directory / mapName;
			if (!IsRegularFile(primaryMap)) {
				continue;
			}
			workspace.primaryMapPath = Normalize(primaryMap);
			workspace.mapsDirectory = Normalize(primaryMap.parent_path());
			return;
		}
		workspace.warnings.push_back("The map selected by config.lua was not found; known map directories will be scanned.");
	}

	struct QueueEntry {
		std::filesystem::path directory;
		std::size_t depth = 0;
	};

	std::vector<std::filesystem::directory_entry> SortedEntries(const std::filesystem::path& directory) {
		std::vector<std::filesystem::directory_entry> entries;
		std::error_code error;
		for (std::filesystem::directory_iterator iterator(directory, std::filesystem::directory_options::skip_permission_denied, error), end;
			 iterator != end && !error;
			 iterator.increment(error)) {
			entries.push_back(*iterator);
		}
		std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
			return Lower(left.path().filename().string()) < Lower(right.path().filename().string());
		});
		return entries;
	}

	void AddMap(ServerWorkspace& workspace, const std::filesystem::path& path, std::size_t maximumMaps) {
		if (workspace.maps.size() >= maximumMaps || !IsRegularFile(path)) {
			return;
		}
		const std::filesystem::path normalized = Normalize(path);
		const auto duplicate = std::find_if(workspace.maps.begin(), workspace.maps.end(), [&](const DetectedMap& map) {
			return map.path == normalized;
		});
		if (duplicate == workspace.maps.end()) {
			workspace.maps.push_back({ normalized, ResourceFingerprint::Read(normalized) });
		}
	}

	void ScanMaps(ServerWorkspace& workspace, const std::filesystem::path& root, const ServerDetectionOptions& options) {
		if (root.empty() || !IsDirectory(root)) {
			return;
		}

		std::deque<QueueEntry> queue;
		queue.push_back({ root, 0 });
		std::size_t visited = 0;
		while (!queue.empty() && workspace.maps.size() < options.maximumMaps && visited < options.maximumDirectories) {
			const QueueEntry current = queue.front();
			queue.pop_front();
			++visited;
			for (const auto& entry : SortedEntries(current.directory)) {
				std::error_code error;
				if (entry.is_regular_file(error) && !error) {
					const std::string extension = Lower(entry.path().extension().string());
					if (extension == ".otbm" || extension == ".otgz") {
						AddMap(workspace, entry.path(), options.maximumMaps);
					}
				} else if (current.depth < options.mapDepth && entry.is_directory(error) && !error && !entry.is_symlink(error) && !IsSkippedDirectory(entry.path())) {
					queue.push_back({ entry.path(), current.depth + 1 });
				}
			}
		}
	}

	void SelectFallbackPrimaryMap(ServerWorkspace& workspace) {
		if (!workspace.primaryMapPath.empty()) {
			return;
		}

		const auto preferred = std::min_element(workspace.maps.begin(), workspace.maps.end(), [&](const DetectedMap& left, const DetectedMap& right) {
			const bool leftAuxiliary = IsAuxiliaryMap(left.path);
			const bool rightAuxiliary = IsAuxiliaryMap(right.path);
			if (leftAuxiliary != rightAuxiliary) {
				return !leftAuxiliary;
			}
			const bool leftInMapDirectory = !workspace.mapsDirectory.empty() && left.path.parent_path() == workspace.mapsDirectory;
			const bool rightInMapDirectory = !workspace.mapsDirectory.empty() && right.path.parent_path() == workspace.mapsDirectory;
			if (leftInMapDirectory != rightInMapDirectory) {
				return leftInMapDirectory;
			}
			const std::string leftName = Lower(left.path.filename().string());
			const std::string rightName = Lower(right.path.filename().string());
			const bool leftWorld = leftName == "world.otbm" || leftName == "world.otgz";
			const bool rightWorld = rightName == "world.otbm" || rightName == "world.otgz";
			if (leftWorld != rightWorld) {
				return leftWorld;
			}
			return leftName != rightName ? leftName < rightName : left.path < right.path;
		});
		if (preferred != workspace.maps.end() && !IsAuxiliaryMap(preferred->path)) {
			workspace.primaryMapPath = preferred->path;
		}
	}

	ItemIdMode DetectModeFromMapNames(const std::vector<DetectedMap>& maps) {
		bool hasClientEvidence = false;
		bool hasServerEvidence = false;
		for (const DetectedMap& map : maps) {
			const std::string name = Lower(map.path.stem().string());
			hasClientEvidence = hasClientEvidence || name.find("clientid") != std::string::npos || name.find("-client") != std::string::npos;
			hasServerEvidence = hasServerEvidence || name.find("serverid") != std::string::npos || name.find("-server") != std::string::npos;
		}
		if (hasClientEvidence != hasServerEvidence) {
			return hasClientEvidence ? ItemIdMode::ClientId : ItemIdMode::ServerId;
		}
		return ItemIdMode::Unknown;
	}

}

ResourceFingerprint ResourceFingerprint::Read(const std::filesystem::path& path) {
	ResourceFingerprint fingerprint;
	fingerprint.path = Normalize(path);
	std::error_code error;
	fingerprint.exists = std::filesystem::is_regular_file(fingerprint.path, error) && !error;
	if (!fingerprint.exists) {
		return fingerprint;
	}
	fingerprint.size = std::filesystem::file_size(fingerprint.path, error);
	if (error) {
		fingerprint.exists = false;
		fingerprint.size = 0;
		return fingerprint;
	}
	fingerprint.modifiedAt = std::filesystem::last_write_time(fingerprint.path, error);
	if (error) {
		fingerprint.exists = false;
	}
	return fingerprint;
}

bool ResourceFingerprint::MatchesCurrentFile() const {
	return *this == Read(path);
}

bool ServerWorkspace::hasRequiredResources() const {
	return !rootPath.empty() && (hasItemsOtb() || hasAppearances());
}

bool ServerWorkspace::hasItemsOtb() const {
	return itemsOtbFingerprint.exists;
}

bool ServerWorkspace::hasItemsXml() const {
	return itemsXmlFingerprint.exists;
}

bool ServerWorkspace::hasAppearances() const {
	return appearancesFingerprint.exists;
}

bool ServerWorkspace::usesCanaryCrystalLoader() const {
	return UsesCanaryCrystalLoader(serverType);
}

bool ServerWorkspace::containsMap(const std::filesystem::path& path) const {
	return findMap(path) != nullptr;
}

const DetectedMap* ServerWorkspace::findMap(const std::filesystem::path& path) const {
	const std::filesystem::path normalized = Normalize(path);
	const auto found = std::find_if(maps.begin(), maps.end(), [&](const DetectedMap& map) {
		return map.path == normalized;
	});
	return found == maps.end() ? nullptr : &*found;
}

bool ServerWorkspace::trackedResourcesChanged() const {
	if (!itemsOtbPath.empty() && !itemsOtbFingerprint.MatchesCurrentFile()) {
		return true;
	}
	if (!itemsXmlPath.empty() && !itemsXmlFingerprint.MatchesCurrentFile()) {
		return true;
	}
	if (!appearancesPath.empty() && !appearancesFingerprint.MatchesCurrentFile()) {
		return true;
	}
	return std::any_of(maps.begin(), maps.end(), [](const DetectedMap& map) {
		return !map.fingerprint.MatchesCurrentFile();
	});
}

ServerDetectionResult ServerResourceDetector::Detect(const std::filesystem::path& requestedRoot, const ServerDetectionOptions& options) {
	ServerDetectionResult result;
	if (requestedRoot.empty()) {
		result.error = "Select the OT server root folder.";
		return result;
	}

	const std::filesystem::path root = Normalize(requestedRoot);
	if (!IsDirectory(root)) {
		result.error = "The selected server folder does not exist or cannot be read.";
		return result;
	}

	result.validRoot = true;
	ServerWorkspace& workspace = result.workspace;
	workspace.rootPath = root;
	const KnownItemFiles knownItems = FindKnownItems(root);
	workspace.itemsOtbPath = knownItems.otb;
	workspace.itemsXmlPath = knownItems.xml;
	workspace.appearancesPath = knownItems.appearances;
	DetectConfiguredMap(workspace);
	if (!workspace.activeDataDirectory.empty()) {
		const KnownItemFiles activeItems = FindKnownItems(workspace.activeDataDirectory);
		if (!activeItems.otb.empty()) {
			workspace.itemsOtbPath = activeItems.otb;
		}
		if (!activeItems.xml.empty()) {
			workspace.itemsXmlPath = activeItems.xml;
		}
		if (!activeItems.appearances.empty()) {
			workspace.appearancesPath = activeItems.appearances;
		}
	}

	static constexpr std::array<const char*, 8> mapDirectories {
		"data/world",
		"data-global/world",
		"data-canary/world",
		"data-crystal/world",
		"data-otservbr-global/world",
		"data/maps",
		"world",
		"maps",
	};
	static constexpr std::array<const char*, 4> monsterDirectories { "data/monster", "data/monsters", "monster", "monsters" };
	static constexpr std::array<const char*, 4> npcDirectories { "data/npc", "data/npcs", "npc", "npcs" };
	if (workspace.mapsDirectory.empty()) {
		workspace.mapsDirectory = FirstExistingDirectory(root, mapDirectories);
	}
	if (!workspace.activeDataDirectory.empty()) {
		workspace.monstersDirectory = FirstExistingDirectory(workspace.activeDataDirectory, monsterDirectories);
		workspace.npcsDirectory = FirstExistingDirectory(workspace.activeDataDirectory, npcDirectories);
	}
	if (workspace.monstersDirectory.empty()) {
		workspace.monstersDirectory = FirstExistingDirectory(root, monsterDirectories);
	}
	if (workspace.npcsDirectory.empty()) {
		workspace.npcsDirectory = FirstExistingDirectory(root, npcDirectories);
	}

	std::deque<QueueEntry> queue;
	queue.push_back({ root, 0 });
	while (!queue.empty() && workspace.directoriesScanned < options.maximumDirectories) {
		const QueueEntry current = queue.front();
		queue.pop_front();
		++workspace.directoriesScanned;

		for (const auto& entry : SortedEntries(current.directory)) {
			std::error_code error;
			if (entry.is_regular_file(error) && !error) {
				const std::string fileName = Lower(entry.path().filename().string());
				if (workspace.itemsOtbPath.empty() && fileName == "items.otb") {
					workspace.itemsOtbPath = Normalize(entry.path());
				} else if (workspace.itemsXmlPath.empty() && fileName == "items.xml") {
					workspace.itemsXmlPath = Normalize(entry.path());
				} else if (workspace.appearancesPath.empty() && fileName == "appearances.dat") {
					workspace.appearancesPath = Normalize(entry.path());
				}
				const std::string extension = Lower(entry.path().extension().string());
				if (workspace.activeDataDirectory.empty() && (extension == ".otbm" || extension == ".otgz")) {
					AddMap(workspace, entry.path(), options.maximumMaps);
					if (workspace.mapsDirectory.empty()) {
						workspace.mapsDirectory = Normalize(entry.path().parent_path());
					}
				}
				continue;
			}

			if (current.depth >= options.fallbackDepth || !entry.is_directory(error) || error || entry.is_symlink(error) || IsSkippedDirectory(entry.path())) {
				continue;
			}
			const std::string directoryName = Lower(entry.path().filename().string());
			if (workspace.monstersDirectory.empty() && (directoryName == "monster" || directoryName == "monsters")) {
				workspace.monstersDirectory = Normalize(entry.path());
			}
			if (workspace.npcsDirectory.empty() && (directoryName == "npc" || directoryName == "npcs")) {
				workspace.npcsDirectory = Normalize(entry.path());
			}
			queue.push_back({ entry.path(), current.depth + 1 });
		}
	}

	workspace.scanLimitReached = !queue.empty();
	if (workspace.scanLimitReached) {
		workspace.warnings.push_back("The bounded server scan reached its directory limit; known resources were kept.");
	}
	if (!workspace.mapsDirectory.empty()) {
		if (!workspace.primaryMapPath.empty()) {
			AddMap(workspace, workspace.primaryMapPath, options.maximumMaps);
		}
		ScanMaps(workspace, workspace.mapsDirectory, options);
	}
	SelectFallbackPrimaryMap(workspace);
	for (DetectedMap& map : workspace.maps) {
		const DetectedMapContext context = DetectMapContext(map.path, root);
		map.serverRootPath = context.root;
		map.serverType = context.type;
	}
	std::sort(workspace.maps.begin(), workspace.maps.end(), [&](const DetectedMap& left, const DetectedMap& right) {
		const bool leftPrimary = left.path == workspace.primaryMapPath;
		const bool rightPrimary = right.path == workspace.primaryMapPath;
		if (leftPrimary != rightPrimary) {
			return leftPrimary;
		}
		const std::string leftName = Lower(left.path.filename().string());
		const std::string rightName = Lower(right.path.filename().string());
		return leftName != rightName ? leftName < rightName : left.path < right.path;
	});

	workspace.itemsOtbFingerprint = ResourceFingerprint::Read(workspace.itemsOtbPath);
	workspace.itemsXmlFingerprint = ResourceFingerprint::Read(workspace.itemsXmlPath);
	workspace.appearancesFingerprint = ResourceFingerprint::Read(workspace.appearancesPath);
	const DetectedMap* primaryMap = workspace.findMap(workspace.primaryMapPath);
	if (primaryMap != nullptr) {
		workspace.serverType = primaryMap->serverType;
	} else {
		workspace.serverType = DetectProfileEvidence(root).type;
	}
	workspace.serverProfile = ServerTypeName(workspace.serverType);
	workspace.itemIdMode = workspace.usesCanaryCrystalLoader()
		? ItemIdMode::ClientId
		: DetectModeFromMapNames(workspace.maps);
	if (!workspace.hasRequiredResources()) {
		result.error = "Server folder selected, but neither items.otb nor appearances.dat was found.";
	} else if (!workspace.itemsXmlFingerprint.exists) {
		workspace.warnings.push_back("items.xml was not found. Server item metadata will be incomplete.");
	}
	return result;
}

const char* ItemIdModeName(ItemIdMode mode) {
	switch (mode) {
		case ItemIdMode::ServerId:
			return "ServerID";
		case ItemIdMode::ClientId:
			return "ClientID";
		default:
			return "Unknown";
	}
}

const char* ServerTypeName(ServerType type) {
	switch (type) {
		case ServerType::Tfs:
			return "TFS";
		case ServerType::Canary:
			return "Canary";
		case ServerType::Crystal:
			return "Crystal";
		case ServerType::CanaryCrystal:
			return "Canary/Crystal";
		default:
			return "Unknown/Generic";
	}
}

bool UsesCanaryCrystalLoader(ServerType type) {
	return type == ServerType::Canary || type == ServerType::Crystal || type == ServerType::CanaryCrystal;
}

ItemIdMode ResolveEffectiveItemIdMode(ItemIdModePreference preference, ItemIdMode clientAssetMode, ItemIdMode serverEvidence) {
	if (clientAssetMode != ItemIdMode::Unknown) {
		return clientAssetMode;
	}
	if (preference == ItemIdModePreference::ServerId) {
		return ItemIdMode::ServerId;
	}
	if (preference == ItemIdModePreference::ClientId) {
		return ItemIdMode::ClientId;
	}
	return serverEvidence;
}
