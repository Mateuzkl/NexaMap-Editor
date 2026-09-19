//////////////////////////////////////////////////////////////////////
// NexaMap Editor — Workspace-specific persistent creature cache.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "creature_cache.h"
#include "creatures.h"
#include "gui.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {
	/// Simple FNV-1a 64-bit hash for deterministic workspace keying.
	uint64_t fnv1a64(const std::string& data) {
		uint64_t hash = 14695981039346656037ULL;
		for (unsigned char byte : data) {
			hash ^= byte;
			hash *= 1099511628211ULL;
		}
		return hash;
	}

	std::string toHex(uint64_t value) {
		std::ostringstream oss;
		oss << std::hex << std::setfill('0') << std::setw(16) << value;
		return oss.str();
	}

	std::string pathToUtf8(const std::filesystem::path& path) {
		const auto utf8 = path.u8string();
		return std::string(utf8.begin(), utf8.end());
	}

	std::string normalizePath(const std::filesystem::path& path) {
		std::error_code ec;
		auto canonical = std::filesystem::weakly_canonical(path, ec);
		if (ec) {
			const auto utf8 = path.generic_u8string();
			return std::string(utf8.begin(), utf8.end());
		}
		const auto utf8 = canonical.generic_u8string();
		return std::string(utf8.begin(), utf8.end());
	}

	/// Collect sorted file entries from a Lua directory.
	struct FileEntry {
		std::string relativePath;
		uintmax_t size;
		int64_t modifiedTime; // Portable time_since_epoch in seconds

		bool operator<(const FileEntry& other) const {
			return relativePath < other.relativePath;
		}
	};

	std::vector<FileEntry> collectFileEntries(const std::filesystem::path& luaDir) {
		std::vector<FileEntry> entries;
		if (!std::filesystem::exists(luaDir)) {
			return entries;
		}
		const auto root = std::filesystem::weakly_canonical(luaDir);
		std::error_code ec;
		for (auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::follow_directory_symlink, ec);
			 it != std::filesystem::recursive_directory_iterator();
			 it.increment(ec)) {
			if (ec) {
				break;
			}
			if (!it->is_regular_file(ec) || ec) {
				continue;
			}
			const auto& filePath = it->path();
			if (filePath.extension() != ".lua") {
				continue;
			}
			FileEntry entry;
			const auto relUtf8 = std::filesystem::relative(filePath, root, ec).generic_u8string();
			entry.relativePath = std::string(relUtf8.begin(), relUtf8.end());
			if (ec) {
				continue;
			}
			entry.size = it->file_size(ec);
			if (ec) {
				entry.size = 0;
			}
			auto ftime = it->last_write_time(ec);
			if (ec) {
				entry.modifiedTime = 0;
			} else {
				entry.modifiedTime = static_cast<int64_t>(ftime.time_since_epoch().count());
			}
			entries.push_back(std::move(entry));
		}
		std::sort(entries.begin(), entries.end());
		return entries;
	}

	nlohmann::json manifestToJson(
		const std::vector<FileEntry>& entries,
		const std::string& luaDirStr,
		const std::string& kind
	) {
		nlohmann::json manifest;
		manifest["schemaVersion"] = CreatureCache::SchemaVersion;
		manifest["kind"] = kind;
		manifest["luaDirectory"] = luaDirStr;
		nlohmann::json files = nlohmann::json::array();
		for (const auto& entry : entries) {
			nlohmann::json file;
			file["path"] = entry.relativePath;
			file["size"] = entry.size;
			file["modifiedAt"] = entry.modifiedTime;
			files.push_back(std::move(file));
		}
		manifest["files"] = std::move(files);
		return manifest;
	}

	std::filesystem::path manifestPath(const std::filesystem::path& cacheDir, const std::string& kind) {
		return cacheDir / ("manifest_" + kind + ".json");
	}

	std::filesystem::path cacheXmlPath(const std::filesystem::path& cacheDir, const std::string& kind) {
		return cacheDir / (kind + ".xml");
	}
}

std::string CreatureCache::WorkspaceKey::computeHash() const {
	std::string combined;
	combined += "v" + std::to_string(SchemaVersion) + "|";
	combined += "root:" + normalizePath(serverRoot) + "|";
	combined += "monsters:" + normalizePath(monstersDir) + "|";
	combined += "npcs:" + normalizePath(npcsDir) + "|";
	combined += "profile:" + clientProfile;
	return toHex(fnv1a64(combined));
}

std::filesystem::path CreatureCache::GetCacheDirectory(const WorkspaceKey& key) {
	const wxString localData = GUI::GetLocalDataDirectory();
	const std::filesystem::path basePath = std::filesystem::u8path(localData.ToStdString(wxConvUTF8));
	return basePath / "cache" / "server_creatures" / key.computeHash();
}

bool CreatureCache::ValidateManifest(
	const std::filesystem::path& cacheDir,
	const std::filesystem::path& luaDir,
	const std::string& kind
) {
	const auto mPath = manifestPath(cacheDir, kind);
	const auto xmlPath = cacheXmlPath(cacheDir, kind);

	// Both manifest and cache XML must exist.
	if (!std::filesystem::exists(mPath) || !std::filesystem::exists(xmlPath)) {
		return false;
	}

	// Load stored manifest.
	nlohmann::json storedManifest;
	try {
		std::ifstream input(mPath);
		if (!input.is_open()) {
			return false;
		}
		input >> storedManifest;
	} catch (...) {
		std::clog << "[creature_cache] Failed to parse manifest: " << pathToUtf8(mPath) << "\n";
		return false;
	}

	// Check schema version.
	if (!storedManifest.contains("schemaVersion") ||
		storedManifest["schemaVersion"].get<uint32_t>() != SchemaVersion) {
		std::clog << "[creature_cache] Schema version mismatch for " << kind << "\n";
		return false;
	}

	// Build current manifest and compare.
	const auto currentEntries = collectFileEntries(luaDir);
	const auto currentManifest = manifestToJson(currentEntries, normalizePath(luaDir), kind);

	// Compare the files arrays.
	if (!storedManifest.contains("files") || !currentManifest.contains("files")) {
		return false;
	}
	if (storedManifest["files"] != currentManifest["files"]) {
		std::clog << "[creature_cache] Manifest mismatch for " << kind
				  << " (file count: stored=" << storedManifest["files"].size()
				  << " current=" << currentManifest["files"].size() << ")\n";
		return false;
	}

	std::clog << "[creature_cache] Cache hit for " << kind
			  << " (" << currentEntries.size() << " files unchanged)\n";
	return true;
}

bool CreatureCache::SafeReplaceFile(
	const std::filesystem::path& from,
	const std::filesystem::path& to,
	std::string& error
) {
	std::error_code ec;
#if defined(_WIN32)
	// On Windows, std::filesystem::rename fails if destination exists.
	// MoveFileExW with MOVEFILE_REPLACE_EXISTING is atomic and safe.
	if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		return true;
	}
	const DWORD winErr = GetLastError();
	// Safe fallback: remove destination then rename
	std::filesystem::remove(to, ec);
	std::filesystem::rename(from, to, ec);
	if (!ec) {
		return true;
	}
	error = "MoveFileExW error " + std::to_string(winErr) + "; fallback rename error: " + ec.message();
	std::filesystem::remove(from, ec);
	return false;
#else
	std::filesystem::rename(from, to, ec);
	if (!ec) {
		return true;
	}
	std::filesystem::remove(to, ec);
	std::filesystem::rename(from, to, ec);
	if (!ec) {
		return true;
	}
	error = "rename error: " + ec.message();
	std::filesystem::remove(from, ec);
	return false;
#endif
}

bool CreatureCache::LoadCached(
	CreatureDatabase& db,
	const std::filesystem::path& cacheDir,
	const std::filesystem::path& luaDir,
	const std::string& kind,
	size_t* loadedCount,
	const ProgressCallback& progress
) {
	const auto xmlPath = cacheXmlPath(cacheDir, kind);
	if (progress) {
		progress("Loading cached server " + kind + "...");
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());
	if (!result) {
		std::clog << "[creature_cache] Failed to parse cache XML " << pathToUtf8(xmlPath)
				  << ": " << result.description() << "\n";
		// Delete corrupted cache.
		std::error_code ec;
		std::filesystem::remove(xmlPath, ec);
		std::filesystem::remove(manifestPath(cacheDir, kind), ec);
		return false;
	}

	pugi::xml_node root = doc.child("creatures");
	if (!root) {
		std::clog << "[creature_cache] Corrupted cache XML (missing <creatures> root) " << pathToUtf8(xmlPath) << "\n";
		std::error_code ec;
		std::filesystem::remove(xmlPath, ec);
		std::filesystem::remove(manifestPath(cacheDir, kind), ec);
		return false;
	}

	size_t count = 0;
	for (pugi::xml_node node = root.child("creature"); node; node = node.next_sibling("creature")) {
		pugi::xml_attribute attr = node.attribute("name");
		if (!attr) {
			continue;
		}
		const std::string name = attr.as_string();
		if (name.empty()) {
			continue;
		}

		auto* ct = newd CreatureType();
		ct->name = name;
		const std::string typeStr = node.attribute("type").as_string("monster");
		ct->isNpc = (typeStr == "npc");

		ct->outfit.lookType = node.attribute("looktype").as_int(0);
		ct->outfit.lookItem = node.attribute("lookitem").as_int(0);
		ct->outfit.lookMount = node.attribute("lookmount").as_int(0);
		ct->outfit.lookAddon = node.attribute("lookaddon").as_int(0);
		ct->outfit.lookHead = node.attribute("lookhead").as_int(0);
		ct->outfit.lookBody = node.attribute("lookbody").as_int(0);
		ct->outfit.lookLegs = node.attribute("looklegs").as_int(0);
		ct->outfit.lookFeet = node.attribute("lookfeet").as_int(0);
		ct->outfit.lookMountHead = node.attribute("lookmounthead").as_int(0);
		ct->outfit.lookMountBody = node.attribute("lookmountbody").as_int(0);
		ct->outfit.lookMountLegs = node.attribute("lookmountlegs").as_int(0);
		ct->outfit.lookMountFeet = node.attribute("lookmountfeet").as_int(0);

		db.applyWorkspaceCreature(ct, true);
		++count;
	}

	if (loadedCount) {
		*loadedCount = count;
	}

	size_t sourceFileCount = 0;
	try {
		std::ifstream mFile(manifestPath(cacheDir, kind));
		if (mFile.is_open()) {
			nlohmann::json m;
			mFile >> m;
			if (m.contains("files") && m["files"].is_array()) {
				sourceFileCount = m["files"].size();
			}
		}
	} catch (...) {
	}

	std::clog << "[creature_cache] Cache hit for " << kind << ": "
			  << sourceFileCount << " source Lua files unchanged, "
			  << count << " cached creature definitions loaded\n";
	return true;
}

bool CreatureCache::SaveCache(
	const std::filesystem::path& cacheDir,
	const std::filesystem::path& luaDir,
	const std::string& kind,
	const std::vector<CreatureDatabase::ImportedCreatureRecord>& creatures,
	const ProgressCallback& progress
) {
	if (progress) {
		progress("Saving server creature cache (" + kind + ")...");
	}

	// Ensure cache directory exists.
	std::error_code ec;
	std::filesystem::create_directories(cacheDir, ec);
	if (ec) {
		std::clog << "[creature_cache] Failed to create cache directory: " << pathToUtf8(cacheDir) << "\n";
		return false;
	}

	const auto xmlPath = cacheXmlPath(cacheDir, kind);
	const auto tmpXmlPath = cacheDir / (kind + ".xml.tmp");

	{
		pugi::xml_document doc;
		pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
		decl.append_attribute("version") = "1.0";
		decl.append_attribute("encoding") = "UTF-8";

		pugi::xml_node root = doc.append_child("creatures");
		for (const auto& record : creatures) {
			const CreatureType& type = record.data;
			pugi::xml_node node = root.append_child("creature");
			node.append_attribute("name") = type.name.c_str();
			node.append_attribute("type") = type.isNpc ? "npc" : "monster";

			const Outfit& outfit = type.outfit;
			node.append_attribute("looktype") = outfit.lookType;
			node.append_attribute("lookitem") = outfit.lookItem;
			node.append_attribute("lookmount") = outfit.lookMount;
			node.append_attribute("lookaddon") = outfit.lookAddon;
			node.append_attribute("lookhead") = outfit.lookHead;
			node.append_attribute("lookbody") = outfit.lookBody;
			node.append_attribute("looklegs") = outfit.lookLegs;
			node.append_attribute("lookfeet") = outfit.lookFeet;
			node.append_attribute("lookmounthead") = outfit.lookMountHead;
			node.append_attribute("lookmountbody") = outfit.lookMountBody;
			node.append_attribute("lookmountlegs") = outfit.lookMountLegs;
			node.append_attribute("lookmountfeet") = outfit.lookMountFeet;
		}

		if (!doc.save_file(tmpXmlPath.c_str(), "\t", pugi::format_default, pugi::encoding_utf8)) {
			std::clog << "[creature_cache] Failed to write cache XML: " << pathToUtf8(tmpXmlPath) << "\n";
			return false;
		}
	}

	std::string error;
	if (!SafeReplaceFile(tmpXmlPath, xmlPath, error)) {
		std::clog << "[creature_cache] Failed to replace cache XML: " << error << "\n";
		return false;
	}

	// Save manifest.
	const auto entries = collectFileEntries(luaDir);
	const auto manifest = manifestToJson(entries, normalizePath(luaDir), kind);

	const auto mPath = manifestPath(cacheDir, kind);
	const auto tmpMPath = cacheDir / ("manifest_" + kind + ".json.tmp");
	{
		std::ofstream output(tmpMPath, std::ios::binary | std::ios::trunc);
		if (!output.is_open()) {
			std::clog << "[creature_cache] Failed to write manifest: " << pathToUtf8(tmpMPath) << "\n";
			return false;
		}
		output << manifest.dump(2);
	}

	if (!SafeReplaceFile(tmpMPath, mPath, error)) {
		std::clog << "[creature_cache] Failed to replace manifest: " << error << "\n";
		return false;
	}

	std::clog << "[creature_cache] Saved workspace " << kind << " cache: "
			  << creatures.size() << " definitions from " << entries.size() << " Lua files\n";
	return true;
}
