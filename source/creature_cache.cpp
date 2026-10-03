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
#include <cctype>
#include <charconv>
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

	std::string normalizeCreatureName(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return value;
	}

	bool readNonnegativeInteger(pugi::xml_node node, const char* name, int& value) {
		const pugi::xml_attribute attribute = node.attribute(name);
		if (!attribute) {
			value = 0;
			return true;
		}
		const std::string text = attribute.as_string();
		int parsed = 0;
		const auto [end, result] = std::from_chars(text.data(), text.data() + text.size(), parsed);
		if (result != std::errc {} || end != text.data() + text.size() || parsed < 0) {
			return false;
		}
		value = parsed;
		return true;
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
	try {
		const auto mPath = manifestPath(cacheDir, kind);
		const auto xmlPath = cacheXmlPath(cacheDir, kind);

		// Both manifest and cache XML must exist.
		if (!std::filesystem::exists(mPath) || !std::filesystem::exists(xmlPath)) {
			return false;
		}

		// Load stored manifest.
		nlohmann::json storedManifest;
		std::ifstream input(mPath);
		if (!input.is_open()) {
			return false;
		}
		input >> storedManifest;

		if (!storedManifest.is_object()) {
			std::clog << "[creature_cache] Malformed manifest (not a JSON object): " << pathToUtf8(mPath) << "\n";
			return false;
		}

		// Check schema version - must be an unsigned integer matching SchemaVersion.
		if (!storedManifest.contains("schemaVersion") || !storedManifest["schemaVersion"].is_number_unsigned() || storedManifest["schemaVersion"].get<uint32_t>() != SchemaVersion) {
			std::clog << "[creature_cache] Schema version mismatch or invalid schemaVersion for " << kind << "\n";
			return false;
		}

		// Build current manifest and compare.
		const auto currentEntries = collectFileEntries(luaDir);
		const auto currentManifest = manifestToJson(currentEntries, normalizePath(luaDir), kind);

		// Compare the files arrays.
		if (!storedManifest.contains("files") || !storedManifest["files"].is_array() || !currentManifest.contains("files") || !currentManifest["files"].is_array()) {
			std::clog << "[creature_cache] Missing or malformed 'files' array in manifest for " << kind << "\n";
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
	} catch (const std::exception& e) {
		std::clog << "[creature_cache] Exception during manifest validation for " << kind << ": " << e.what() << "\n";
		return false;
	} catch (...) {
		std::clog << "[creature_cache] Unknown exception during manifest validation for " << kind << "\n";
		return false;
	}
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
	error = "MoveFileExW error " + std::to_string(winErr) + "; existing cache was preserved";
	std::filesystem::remove(from, ec);
	return false;
#else
	std::filesystem::rename(from, to, ec);
	if (!ec) {
		return true;
	}
	error = "rename error: " + ec.message() + "; existing cache was preserved";
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
	if (kind != "monsters" && kind != "npcs") {
		std::clog << "[creature_cache] Rejected unsupported cache kind: " << kind << "\n";
		return false;
	}
	const auto xmlPath = cacheXmlPath(cacheDir, kind);
	const auto invalidateCache = [&] {
		std::error_code ec;
		std::filesystem::remove(xmlPath, ec);
		std::filesystem::remove(manifestPath(cacheDir, kind), ec);
	};
	if (progress) {
		progress("Loading cached server " + kind + "...");
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());
	if (!result) {
		std::clog << "[creature_cache] Failed to parse cache XML " << pathToUtf8(xmlPath)
				  << ": " << result.description() << "\n";
		invalidateCache();
		return false;
	}

	pugi::xml_node root = doc.child("creatures");
	if (!root) {
		std::clog << "[creature_cache] Corrupted cache XML (missing <creatures> root) " << pathToUtf8(xmlPath) << "\n";
		invalidateCache();
		return false;
	}

	std::map<std::string, std::unique_ptr<CreatureType>> parsedCreatures;
	bool malformed = false;
	for (pugi::xml_node node = root.child("creature"); node; node = node.next_sibling("creature")) {
		pugi::xml_attribute attr = node.attribute("name");
		if (!attr) {
			malformed = true;
			break;
		}
		const std::string name = attr.as_string();
		if (name.empty()) {
			malformed = true;
			break;
		}

		auto ct = std::make_unique<CreatureType>();
		ct->name = name;
		const std::string typeStr = node.attribute("type").as_string();
		const std::string expectedType = kind == "npcs" ? "npc" : "monster";
		if ((typeStr != "monster" && typeStr != "npc") || typeStr != expectedType) {
			std::clog << "[creature_cache] Invalid cache record type for " << kind << ": " << name << "\n";
			malformed = true;
			break;
		}
		ct->isNpc = typeStr == "npc";

		if (!readNonnegativeInteger(node, "looktype", ct->outfit.lookType)
			|| !readNonnegativeInteger(node, "lookitem", ct->outfit.lookItem)
			|| !readNonnegativeInteger(node, "lookmount", ct->outfit.lookMount)
			|| !readNonnegativeInteger(node, "lookaddon", ct->outfit.lookAddon)
			|| !readNonnegativeInteger(node, "lookhead", ct->outfit.lookHead)
			|| !readNonnegativeInteger(node, "lookbody", ct->outfit.lookBody)
			|| !readNonnegativeInteger(node, "looklegs", ct->outfit.lookLegs)
			|| !readNonnegativeInteger(node, "lookfeet", ct->outfit.lookFeet)
			|| !readNonnegativeInteger(node, "lookmounthead", ct->outfit.lookMountHead)
			|| !readNonnegativeInteger(node, "lookmountbody", ct->outfit.lookMountBody)
			|| !readNonnegativeInteger(node, "lookmountlegs", ct->outfit.lookMountLegs)
			|| !readNonnegativeInteger(node, "lookmountfeet", ct->outfit.lookMountFeet)) {
			std::clog << "[creature_cache] Invalid cache outfit values: " << name << "\n";
			malformed = true;
			break;
		}

		parsedCreatures[normalizeCreatureName(name)] = std::move(ct);
	}

	if (malformed) {
		invalidateCache();
		return false;
	}

	const size_t count = parsedCreatures.size();
	for (auto& [normalizedName, creature] : parsedCreatures) {
		(void)normalizedName;
		db.applyWorkspaceCreature(std::move(creature), true);
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
	if (kind != "monsters" && kind != "npcs") {
		std::clog << "[creature_cache] Refused to save unsupported cache kind: " << kind << "\n";
		return false;
	}
	const bool expectedNpc = kind == "npcs";
	std::map<std::string, const CreatureDatabase::ImportedCreatureRecord*> normalizedRecords;
	for (const auto& record : creatures) {
		const std::string normalizedName = normalizeCreatureName(record.name);
		if (record.name.empty() || record.isNpc != expectedNpc
			|| (!record.normalizedName.empty() && record.normalizedName != normalizedName)) {
			std::clog << "[creature_cache] Refused invalid " << kind << " cache record: " << record.name << "\n";
			return false;
		}
		normalizedRecords[normalizedName] = &record;
	}

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
		for (const auto& [normalizedName, recordPointer] : normalizedRecords) {
			(void)normalizedName;
			const auto& record = *recordPointer;
			pugi::xml_node node = root.append_child("creature");
			node.append_attribute("name") = record.name.c_str();
			node.append_attribute("type") = record.isNpc ? "npc" : "monster";

			const Outfit& outfit = record.outfit;
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
