//////////////////////////////////////////////////////////////////////
// Active NexaMap client + server workspace session.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_WORKSPACE_SESSION_H_
#define NEXAMAP_WORKSPACE_SESSION_H_

#include "client_version.h"
#include "mount_id_resolver.h"
#include "server_content_index.h"
#include "server_visual_catalog.h"
#include "server_vocation_catalog.h"
#include "server_workspace.h"
#include "spell_area_resolver.h"

#include <wx/string.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

enum class WorkspaceClientMode : uint8_t {
	None = 0,
	Classic,
	Appearances,
};

struct WorkspaceClientSelection {
	wxString rootPath;
	wxString versionName;
	WorkspaceClientMode mode = WorkspaceClientMode::None;
	ClientVersionID versionId = CLIENT_VERSION_NONE;
	bool valid = false;
};

struct WorkspaceMetadataCacheStats {
	std::size_t spellAreaBuilds = 0;
	std::size_t visualCatalogBuilds = 0;
	std::size_t vocationCatalogBuilds = 0;
};

class WorkspaceSession {
public:
	void loadConfiguredPaths();
	void swap(WorkspaceSession& other) noexcept;
	void setPersistenceEnabled(bool enabled);

	bool configureClient(const wxString& path, wxString& error, wxArrayString& warnings, bool persist = true);
	bool configureServer(const wxString& path, wxString& error, bool persist = true);
	bool selectDetectedMap(const wxString& path, wxString& error, bool persist = true);
	bool rescanServer(wxString& error);
	bool ensureServerContent(wxString& error);
	bool refreshServerContentPaths(const std::vector<std::filesystem::path>& changedPaths, wxString& error);
	bool restoreCompatibleClient(wxString& error, wxArrayString& warnings, bool persist = true);

	void setItemIdModePreference(ItemIdModePreference preference);
	[[nodiscard]] ItemIdModePreference getItemIdModePreference() const;
	[[nodiscard]] ItemIdMode getEffectiveItemIdMode() const;

	[[nodiscard]] const WorkspaceClientSelection& getClient() const;
	[[nodiscard]] const ServerWorkspace& getServer() const;
	[[nodiscard]] const ServerContentIndex& getServerContent() const;
	[[nodiscard]] const wxString& getServerError() const;
	[[nodiscard]] bool hasServerSelection() const;
	[[nodiscard]] bool hasCompatibleServerResources() const;
	[[nodiscard]] bool isReady() const;
	[[nodiscard]] bool containsMap(const wxString& path) const;
	[[nodiscard]] std::optional<DetectedMap> getDetectedMap(const wxString& path) const;
	[[nodiscard]] std::vector<wxString> getDetectedMaps() const;
	[[nodiscard]] const MountIdResolver& getMountIdResolver() const;
	[[nodiscard]] int resolveMountClientId(int mountId) const;
	[[nodiscard]] std::shared_ptr<const SpellAreaResolver> getSpellAreaResolver();
	[[nodiscard]] std::shared_ptr<const ServerVisualCatalog> getServerVisualCatalog();
	[[nodiscard]] std::shared_ptr<const std::vector<ServerVocation>> getServerVocations();
	[[nodiscard]] const WorkspaceMetadataCacheStats& getMetadataCacheStats() const;
	[[nodiscard]] uint64_t getGeneration() const;
	[[nodiscard]] uint64_t getContentGeneration() const;

private:
	void persistPaths();
	void invalidateServerMetadata();
	void invalidateServerMetadataForPaths(const std::vector<std::filesystem::path>& changedPaths);

	WorkspaceClientSelection client;
	ServerWorkspace server;
	ServerContentIndex serverContent;
	MountIdResolver mountIdResolver;
	std::shared_ptr<const SpellAreaResolver> spellAreaResolver;
	std::shared_ptr<const ServerVisualCatalog> visualCatalog;
	std::shared_ptr<const std::vector<ServerVocation>> vocationCatalog;
	WorkspaceMetadataCacheStats metadataCacheStats;
	std::filesystem::path selectedDetectedMapPath;
	wxString serverError;
	ItemIdModePreference idModePreference = ItemIdModePreference::Auto;
	uint64_t generation = 0;
	uint64_t contentGeneration = 0;
	bool persistenceEnabled = true;
};

extern WorkspaceSession g_workspace;

#endif // NEXAMAP_WORKSPACE_SESSION_H_
