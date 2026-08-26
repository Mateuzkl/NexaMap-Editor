//////////////////////////////////////////////////////////////////////
// NexaMap server workspace discovery and resource metadata.
//////////////////////////////////////////////////////////////////////

#include "server_workspace.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <deque>
#include <system_error>
#include <tuple>
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
			".git", ".svn", ".hg", "build", "builds", "node_modules", "vcpkg_installed", ".cache", ".idea", ".vs",
		};
		return ignored.contains(Lower(path.filename().string()));
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

	std::pair<std::filesystem::path, std::filesystem::path> FindKnownItems(const std::filesystem::path& root) {
		static constexpr std::array<const char*, 4> directories {
			"data/items", "data", "items", "",
		};
		std::filesystem::path firstOtb;
		std::filesystem::path firstXml;
		for (const char* relative : directories) {
			const std::filesystem::path directory = relative[0] == '\0' ? root : root / relative;
			const std::filesystem::path otb = directory / "items.otb";
			const std::filesystem::path xml = directory / "items.xml";
			if (IsRegularFile(otb) && firstOtb.empty()) {
				firstOtb = Normalize(otb);
			}
			if (IsRegularFile(xml) && firstXml.empty()) {
				firstXml = Normalize(xml);
			}
			if (IsRegularFile(otb) && IsRegularFile(xml)) {
				return { Normalize(otb), Normalize(xml) };
			}
		}
		return { firstOtb, firstXml };
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

	std::string DetectProfile(const std::filesystem::path& root) {
		const std::string name = Lower(root.filename().string());
		if (name.find("canary") != std::string::npos) {
			return "Canary";
		}
		if (name.find("crystal") != std::string::npos) {
			return "Crystal";
		}
		if (name.find("forgotten") != std::string::npos || name.find("tfs") != std::string::npos || IsRegularFile(root / "config.lua")) {
			return "TFS";
		}
		return "OT Server";
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
	return !rootPath.empty() && itemsOtbFingerprint.exists;
}

bool ServerWorkspace::hasItemsXml() const {
	return itemsXmlFingerprint.exists;
}

bool ServerWorkspace::containsMap(const std::filesystem::path& path) const {
	const std::filesystem::path normalized = Normalize(path);
	return std::any_of(maps.begin(), maps.end(), [&](const DetectedMap& map) {
		return map.path == normalized;
	});
}

bool ServerWorkspace::trackedResourcesChanged() const {
	if (!itemsOtbFingerprint.MatchesCurrentFile()) {
		return true;
	}
	if (!itemsXmlPath.empty() && !itemsXmlFingerprint.MatchesCurrentFile()) {
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
	workspace.serverProfile = DetectProfile(root);
	std::tie(workspace.itemsOtbPath, workspace.itemsXmlPath) = FindKnownItems(root);

	static constexpr std::array<const char*, 4> mapDirectories { "data/world", "data/maps", "world", "maps" };
	static constexpr std::array<const char*, 4> monsterDirectories { "data/monster", "data/monsters", "monster", "monsters" };
	static constexpr std::array<const char*, 4> npcDirectories { "data/npc", "data/npcs", "npc", "npcs" };
	workspace.mapsDirectory = FirstExistingDirectory(root, mapDirectories);
	workspace.monstersDirectory = FirstExistingDirectory(root, monsterDirectories);
	workspace.npcsDirectory = FirstExistingDirectory(root, npcDirectories);

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
				}
				const std::string extension = Lower(entry.path().extension().string());
				if (extension == ".otbm" || extension == ".otgz") {
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
		ScanMaps(workspace, workspace.mapsDirectory, options);
	}
	std::sort(workspace.maps.begin(), workspace.maps.end(), [](const DetectedMap& left, const DetectedMap& right) {
		return Lower(left.path.filename().string()) < Lower(right.path.filename().string());
	});

	workspace.itemsOtbFingerprint = ResourceFingerprint::Read(workspace.itemsOtbPath);
	workspace.itemsXmlFingerprint = ResourceFingerprint::Read(workspace.itemsXmlPath);
	workspace.itemIdMode = DetectModeFromMapNames(workspace.maps);
	if (!workspace.itemsOtbFingerprint.exists) {
		result.error = "Server folder selected, but items.otb was not found.";
	} else if (!workspace.itemsXmlFingerprint.exists) {
		workspace.warnings.push_back("items.xml was not found. The OTB item database can still be loaded, but server item metadata will be incomplete.");
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
