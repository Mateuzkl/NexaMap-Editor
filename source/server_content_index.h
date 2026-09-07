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
	std::size_t filesParsed = 0;
	std::size_t filesReused = 0;
	std::size_t filesSkipped = 0;
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
};

class ServerContentIndex {
public:
	[[nodiscard]] static ServerContentIndex Build(
		const ServerWorkspace& workspace,
		const ServerContentIndex* previous = nullptr,
		const ServerContentScanOptions& options = {}
	);

	[[nodiscard]] const std::vector<ServerContentSource>& entries() const;
	[[nodiscard]] const ServerContentCapabilities& capabilities() const;
	[[nodiscard]] const std::vector<std::string>& diagnostics() const;
	[[nodiscard]] const ServerContentScanStats& stats() const;

	[[nodiscard]] ServerContentLookupResult findExact(ServerContentKind kind, const std::string& name) const;
	[[nodiscard]] ServerContentLookupResult findCaseInsensitive(ServerContentKind kind, const std::string& name) const;
	[[nodiscard]] bool trackedSourcesChanged() const;
	[[nodiscard]] bool sameContentAs(const ServerContentIndex& other) const;

private:
	struct CacheState;

	std::vector<ServerContentSource> sources;
	ServerContentCapabilities detectedCapabilities;
	std::vector<std::string> scanDiagnostics;
	ServerContentScanStats scanStats;
	std::shared_ptr<const CacheState> cache;
};

[[nodiscard]] const char* ServerContentKindName(ServerContentKind kind);
[[nodiscard]] const char* ServerContentFormatName(ServerContentFormat format);

#endif // NEXAMAP_SERVER_CONTENT_INDEX_H_
