//////////////////////////////////////////////////////////////////////
// NexaMap Editor — Workspace-specific persistent creature cache.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "creature_cache.h"
#include "creatures.h"
#include "gui.h"

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
				entry.modifiedTime = std::chrono::duration_cast<std::chrono::seconds>(
					ftime.time_since_epoch()
				).count();
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

bool CreatureCache::LoadCached(
	CreatureDatabase& db,
	const std::filesystem::path& cacheDir,
	const std::string& kind,
	const ProgressCallback& progress
) {
	const auto xmlPath = cacheXmlPath(cacheDir, kind);
	if (progress) {
		progress("Loading cached server " + kind + "...");
	}

	wxString error;
	wxArrayString warnings;
	const FileName cacheFile(wxString::FromUTF8(pathToUtf8(xmlPath)));

	const auto wxProgress = [&](const wxString& status) {
		if (progress) {
			progress(status.ToStdString(wxConvUTF8));
		}
	};

	// Load as standard=true creatures (same as server-workspace imports).
	if (!db.loadFromXML(cacheFile, true, error, warnings, wxProgress)) {
		std::clog << "[creature_cache] Failed to load cache XML " << pathToUtf8(xmlPath)
				  << ": " << error.ToStdString() << "\n";
		// Delete corrupted cache.
		std::error_code ec;
		std::filesystem::remove(xmlPath, ec);
		std::filesystem::remove(manifestPath(cacheDir, kind), ec);
		return false;
	}

	if (!warnings.empty()) {
		for (const auto& w : warnings) {
			std::clog << "[creature_cache] Warning loading cache: " << w.ToStdString() << "\n";
		}
	}

	std::clog << "[creature_cache] Loaded cached " << kind
			  << " from " << pathToUtf8(xmlPath) << "\n";
	return true;
}

bool CreatureCache::SaveCache(
	const CreatureDatabase& db,
	const std::filesystem::path& cacheDir,
	const std::filesystem::path& luaDir,
	const std::string& kind,
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

	// Save creatures XML via pugi (reusing the existing saveToXML infrastructure).
	// We save ALL standard creatures to a temporary file, then rename atomically.
	const auto xmlPath = cacheXmlPath(cacheDir, kind);
	const auto tmpXmlPath = cacheDir / (kind + ".xml.tmp");

	// Use the existing saveToXML but filter for standard creatures.
	// Since saveToXML only saves non-standard, we need a dedicated save.
	{
		pugi::xml_document doc;
		pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
		decl.append_attribute("version") = "1.0";

		pugi::xml_node root = doc.append_child("creatures");
		const bool isNpcKind = (kind == "npcs");
		size_t count = 0;
		for (auto it = const_cast<CreatureDatabase&>(db).begin();
			 it != const_cast<CreatureDatabase&>(db).end(); ++it) {
			CreatureType* type = it->second;
			// Only cache standard (server-workspace-imported) creatures.
			if (!type->standard) {
				continue;
			}
			// Filter by kind.
			if (type->isNpc != isNpcKind) {
				continue;
			}
			pugi::xml_node node = root.append_child("creature");
			node.append_attribute("name") = type->name.c_str();
			node.append_attribute("type") = type->isNpc ? "npc" : "monster";

			const Outfit& outfit = type->outfit;
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
			++count;
		}

		if (!doc.save_file(tmpXmlPath.c_str(), "\t", pugi::format_default, pugi::encoding_utf8)) {
			std::clog << "[creature_cache] Failed to write cache XML: " << pathToUtf8(tmpXmlPath) << "\n";
			return false;
		}
		std::clog << "[creature_cache] Wrote " << count << " " << kind << " to cache\n";
	}

	// Atomic rename.
	std::filesystem::rename(tmpXmlPath, xmlPath, ec);
	if (ec) {
		std::clog << "[creature_cache] Failed to rename cache XML: " << ec.message() << "\n";
		std::filesystem::remove(tmpXmlPath, ec);
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
	std::filesystem::rename(tmpMPath, mPath, ec);
	if (ec) {
		std::clog << "[creature_cache] Failed to rename manifest: " << ec.message() << "\n";
		std::filesystem::remove(tmpMPath, ec);
		return false;
	}

	std::clog << "[creature_cache] Cache saved for " << kind
			  << " (" << entries.size() << " Lua files)\n";
	return true;
}
