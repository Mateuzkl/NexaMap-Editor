//////////////////////////////////////////////////////////////////////
// Active NexaMap client + server workspace session.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_WORKSPACE_SESSION_H_
#define NEXAMAP_WORKSPACE_SESSION_H_

#include "client_version.h"
#include "server_workspace.h"

#include <wx/string.h>

#include <cstdint>
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

class WorkspaceSession {
public:
	void loadConfiguredPaths();
	void swap(WorkspaceSession& other) noexcept;
	void setPersistenceEnabled(bool enabled);

	bool configureClient(const wxString& path, wxString& error, wxArrayString& warnings, bool persist = true);
	bool configureServer(const wxString& path, wxString& error, bool persist = true);
	bool rescanServer(wxString& error);
	bool restoreCompatibleClient(wxString& error, wxArrayString& warnings, bool persist = true);

	void setItemIdModePreference(ItemIdModePreference preference);
	[[nodiscard]] ItemIdModePreference getItemIdModePreference() const;
	[[nodiscard]] ItemIdMode getEffectiveItemIdMode() const;

	[[nodiscard]] const WorkspaceClientSelection& getClient() const;
	[[nodiscard]] const ServerWorkspace& getServer() const;
	[[nodiscard]] const wxString& getServerError() const;
	[[nodiscard]] bool hasServerSelection() const;
	[[nodiscard]] bool hasCompatibleServerResources() const;
	[[nodiscard]] bool isReady() const;
	[[nodiscard]] bool containsMap(const wxString& path) const;
	[[nodiscard]] std::vector<wxString> getDetectedMaps() const;
	[[nodiscard]] uint64_t getGeneration() const;

private:
	void persistPaths();

	WorkspaceClientSelection client;
	ServerWorkspace server;
	wxString serverError;
	ItemIdModePreference idModePreference = ItemIdModePreference::Auto;
	uint64_t generation = 0;
	bool persistenceEnabled = true;
};

extern WorkspaceSession g_workspace;

#endif // NEXAMAP_WORKSPACE_SESSION_H_
