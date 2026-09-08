//////////////////////////////////////////////////////////////////////
// Per-workspace server content discovery and source metadata.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_SERVER_CONTENT_INDEX_H_
#define NEXAMAP_SERVER_CONTENT_INDEX_H_

#include "server_workspace.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

enum class ServerContentKind : uint8_t {
	Monster = 0,
	Npc,
	Spell,
};

enum class ServerContentFormat : uint8_t {
	Unknown = 0,
	Xml,
	Lua,
};

struct ServerContentCategoryCapabilities {
	bool xmlDefinitions = false;
	bool luaDefinitions = false;
	bool relatedLua = false;

	[[nodiscard]] bool hasDefinitions() const;
	[[nodiscard]] bool supports(ServerContentFormat format) const;
	[[nodiscard]] bool isMixed() const;

	friend bool operator==(const ServerContentCategoryCapabilities&, const ServerContentCategoryCapabilities&) = default;
};

struct ServerContentCapabilities {
	ServerContentCategoryCapabilities monsters;
	ServerContentCategoryCapabilities npcs;
	ServerContentCategoryCapabilities spells;

	[[nodiscard]] const ServerContentCategoryCapabilities& forKind(ServerContentKind kind) const;
	[[nodiscard]] bool isMixed() const;

	friend bool operator==(const ServerContentCapabilities&, const ServerContentCapabilities&) = default;
};

struct ServerContentSource {
	ServerContentKind kind = ServerContentKind::Monster;
	ServerContentFormat format = ServerContentFormat::Unknown;
	ServerType serverType = ServerType::UnknownGeneric;
	std::string name;
	std::vector<std::string> aliases;
	std::string subtype;
	std::filesystem::path declarationPath;
	std::optional<std::filesystem::path> registrationPath;
	std::optional<std::filesystem::path> relatedScriptPath;
	ResourceFingerprint declarationFingerprint;
	std::optional<ResourceFingerprint> registrationFingerprint;
	std::optional<ResourceFingerprint> relatedScriptFingerprint;
	std::size_t declarationLine = 0;
	std::size_t registrationLine = 0;
	bool registered = false;
	bool declarationExists = false;

	friend bool operator==(const ServerContentSource&, const ServerContentSource&) = default;
};

struct ServerContentScanStats {
	std::size_t filesDiscovered = 0;
	std::size_t filesFingerprinted = 0;
	std::size_t filesRead = 0;
	std::size_t filesParsed = 0;
	std::size_t filesReused = 0;
	std::size_t cacheRecordsShared = 0;
	std::size_t filesSkipped = 0;
	std::size_t fullScans = 0;
	std::size_t targetedRefreshes = 0;
	bool fileLimitReached = false;
};

struct ServerContentScanOptions {
	std::size_t maximumFiles = 12000;
	std::size_t maximumFileBytes = 8 * 1024 * 1024;
	std::size_t maximumDepth = 20;
};

struct ServerContentLookupResult {
	std::vector<const ServerContentSource*> matches;

	[[nodiscard]] bool empty() const;
	[[nodiscard]] bool unique() const;
	[[nodiscard]] bool ambiguous() const;
	[[nodiscard]] const ServerContentSource* value() const;
	[[nodiscard]] const ServerContentSource* uniqueRegisteredValue() const;

private:
	std::shared_ptr<const std::vector<ServerContentSource>> owner;

	friend class ServerContentIndex;
};

class ServerContentIndex {
public:
	[[nodiscard]] static ServerContentIndex Build(
		const ServerWorkspace& workspace,
		const ServerContentIndex* previous = nullptr,
		const ServerContentScanOptions& options = {}
	);
	[[nodiscard]] static ServerContentIndex RefreshPaths(
		const ServerWorkspace& workspace,
		const ServerContentIndex& previous,
		const std::vector<std::filesystem::path>& changedPaths,
		const ServerContentScanOptions& options = {}
	);

	[[nodiscard]] const std::vector<ServerContentSource>& entries() const;
	[[nodiscard]] const ServerContentCapabilities& capabilities() const;
	[[nodiscard]] const std::vector<std::string>& diagnostics() const;
	[[nodiscard]] const ServerContentScanStats& stats() const;
	[[nodiscard]] bool initialized() const;
	[[nodiscard]] bool matchesWorkspace(const ServerWorkspace& workspace) const;
	[[nodiscard]] std::shared_ptr<const std::vector<ServerContentSource>> snapshot() const;
	[[nodiscard]] std::vector<std::size_t> indicesForKind(ServerContentKind kind, bool existingOnly = true) const;

	[[nodiscard]] ServerContentLookupResult findExact(ServerContentKind kind, const std::string& name) const;
	[[nodiscard]] ServerContentLookupResult findCaseInsensitive(ServerContentKind kind, const std::string& name) const;
	[[nodiscard]] bool trackedSourcesChanged() const;
	[[nodiscard]] bool sameContentAs(const ServerContentIndex& other) const;

private:
	struct CacheState;
	using LookupMap = std::unordered_map<std::string, std::vector<std::size_t>>;

	[[nodiscard]] static ServerContentIndex Assemble(
		const ServerWorkspace& workspace,
		std::shared_ptr<CacheState> cache,
		ServerContentScanStats stats,
		std::vector<std::string> diagnostics = {}
	);
	void rebuildLookups();

	std::shared_ptr<const std::vector<ServerContentSource>> sources = std::make_shared<const std::vector<ServerContentSource>>();
	ServerContentCapabilities detectedCapabilities;
	std::vector<std::string> scanDiagnostics;
	ServerContentScanStats scanStats;
	std::shared_ptr<const CacheState> cache;
	LookupMap exactLookup;
	LookupMap caseFoldedLookup;
	std::filesystem::path workspaceRoot;
	std::filesystem::path monstersRoot;
	std::filesystem::path npcsRoot;
	std::filesystem::path spellsRoot;
	bool built = false;
};

[[nodiscard]] const char* ServerContentKindName(ServerContentKind kind);
[[nodiscard]] const char* ServerContentFormatName(ServerContentFormat format);

#endif // NEXAMAP_SERVER_CONTENT_INDEX_H_
