//////////////////////////////////////////////////////////////////////
// NexaMap Editor — Workspace-specific persistent creature cache.
//
// This cache avoids re-parsing hundreds/thousands of Lua creature
// files on every editor startup when the same server workspace is
// opened without changes.
//
// Architecture:
//   <NexaMap local data>/cache/server_creatures/<workspace-key>/
//       monsters.xml        — cached monster CreatureTypes
//       npcs.xml            — cached NPC CreatureTypes
//       manifest_monsters.json — per-file size+mtime manifest
//       manifest_npcs.json
//
// The workspace key is a deterministic hash of:
//   - normalized server root path
//   - normalized monsters Lua directory
//   - normalized NPC Lua directory
//   - cache schema version
//
// Invalidation is done by comparing the current file listing (path,
// size, last_write_time) against the stored manifest.
//////////////////////////////////////////////////////////////////////

#ifndef RME_CREATURE_CACHE_H_
#define RME_CREATURE_CACHE_H_

#include "creatures.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class CreatureCache {
public:
	/// Inputs that uniquely identify a workspace for cache keying.
	struct WorkspaceKey {
		std::filesystem::path serverRoot;
		std::filesystem::path monstersDir;
		std::filesystem::path npcsDir;
		std::string clientProfile;

		/// Compute a deterministic hex string from the key fields.
		[[nodiscard]] std::string computeHash() const;
	};

	using ProgressCallback = std::function<void(const std::string&)>;

	/// Base cache directory for a given workspace key.
	[[nodiscard]] static std::filesystem::path GetCacheDirectory(const WorkspaceKey& key);

	/// Check whether the cached manifest matches the current state of
	/// the Lua directory.  Returns true if cache is valid (hit).
	[[nodiscard]] static bool ValidateManifest(
		const std::filesystem::path& cacheDir,
		const std::filesystem::path& luaDir,
		const std::string& kind // "monsters" or "npcs"
	);

	/// Load cached creatures from the cache XML into the database using
	/// workspace override semantics (identical to normal Lua import).
	/// @returns true on success. On any parse error the cache is
	/// deleted and false is returned so the caller falls back to
	/// a full Lua import.
	static bool LoadCached(
		CreatureDatabase& db,
		const std::filesystem::path& cacheDir,
		const std::filesystem::path& luaDir,
		const std::string& kind,
		size_t* loadedCount = nullptr,
		const ProgressCallback& progress = {}
	);

	/// Save the exact imported workspace creatures to the cache directory
	/// and write a fresh manifest of the Lua directory.
	/// Uses atomic Windows-safe replacement (temp + SafeReplaceFile).
	static bool SaveCache(
		const std::filesystem::path& cacheDir,
		const std::filesystem::path& luaDir,
		const std::string& kind,
		const std::vector<CreatureDatabase::ImportedCreatureRecord>& creatures,
		const ProgressCallback& progress = {}
	);

	/// Windows-safe atomic file replacement helper.
	static bool SafeReplaceFile(
		const std::filesystem::path& from,
		const std::filesystem::path& to,
		std::string& error
	);

	/// Current cache format version. Bump when the creature XML
	/// schema or manifest format changes.
	static constexpr uint32_t SchemaVersion = 2;
};

#endif // RME_CREATURE_CACHE_H_

