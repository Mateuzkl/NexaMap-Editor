//////////////////////////////////////////////////////////////////////
// Active NexaMap client + server workspace session.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "workspace_session.h"

#include "client_assets.h"
#include "client_assets_manifest.h"
#include "settings.h"

#include <algorithm>
#include <filesystem>

WorkspaceSession g_workspace;

namespace {
	std::filesystem::path ToFilesystemPath(const wxString& path) {
#ifdef __WINDOWS__
		return std::filesystem::path(path.ToStdWstring());
#else
		return std::filesystem::u8path(path.ToStdString(wxConvUTF8));
#endif
	}

	wxString FromFilesystemPath(const std::filesystem::path& path) {
#ifdef __WINDOWS__
		return wxString(path.wstring());
#else
		return wxString::FromUTF8(path.string());
#endif
	}

	bool SameDetectedMaps(const std::vector<DetectedMap>& left, const std::vector<DetectedMap>& right) {
		if (left.size() != right.size()) {
			return false;
		}
		for (std::size_t index = 0; index < left.size(); ++index) {
			if (left[index].path != right[index].path || left[index].fingerprint != right[index].fingerprint) {
				return false;
			}
		}
		return true;
	}

	bool SameServerWorkspace(const ServerWorkspace& left, const ServerWorkspace& right) {
		return left.rootPath == right.rootPath && left.itemsOtbPath == right.itemsOtbPath && left.itemsXmlPath == right.itemsXmlPath && left.appearancesPath == right.appearancesPath && left.activeDataDirectory == right.activeDataDirectory && left.mapsDirectory == right.mapsDirectory && left.primaryMapPath == right.primaryMapPath && left.monstersDirectory == right.monstersDirectory && left.npcsDirectory == right.npcsDirectory && left.itemsOtbFingerprint == right.itemsOtbFingerprint && left.itemsXmlFingerprint == right.itemsXmlFingerprint && left.appearancesFingerprint == right.appearancesFingerprint && left.itemIdMode == right.itemIdMode && left.serverProfile == right.serverProfile && left.protocol == right.protocol && SameDetectedMaps(left.maps, right.maps);
	}
}

void WorkspaceSession::loadConfiguredPaths() {
	idModePreference = static_cast<ItemIdModePreference>(std::clamp(
		g_settings.getInteger(Config::WORKSPACE_ITEM_ID_MODE),
		static_cast<int>(ItemIdModePreference::Auto),
		static_cast<int>(ItemIdModePreference::ClientId)
	));

	wxString ignoredError;
	wxArrayString ignoredWarnings;
	const wxString serverPath = wxstr(g_settings.getString(Config::WORKSPACE_SERVER_ROOT));
	if (!serverPath.empty()) {
		configureServer(serverPath, ignoredError, false);
	}

	// The server item model decides which saved client should be restored. A
	// Crystal workspace may have been opened after a classic client, so the
	// generic workspace path is not allowed to shadow the dedicated Assets path.
	bool clientRestored = false;
	if (server.hasAppearances()) {
		const wxString appearancesClientPath = wxstr(g_settings.getString(Config::CANARY_CRYSTAL_ASSETS_DIRECTORY));
		if (!appearancesClientPath.empty() && ClientAssetsManifestLoader::Validate(ToFilesystemPath(appearancesClientPath)).valid) {
			clientRestored = configureClient(appearancesClientPath, ignoredError, ignoredWarnings, false);
		}
	}
	if (!clientRestored) {
		const wxString clientPath = wxstr(g_settings.getString(Config::WORKSPACE_CLIENT_ROOT));
		if (!clientPath.empty()) {
			configureClient(clientPath, ignoredError, ignoredWarnings, false);
		}
	}

	// Always rewrite the discovered external paths. This migrates any stale
	// item-database settings and deliberately never trusts a bundled data path.
	persistPaths();
	g_settings.save();
}

bool WorkspaceSession::configureClient(const wxString& path, wxString& error, wxArrayString& warnings, bool persist) {
	error.clear();
	warnings.clear();
	WorkspaceClientSelection selection;
	selection.rootPath = path;

	const ClientAssetsValidationResult appearances = ClientAssetsManifestLoader::Validate(ToFilesystemPath(path));
	if (appearances.valid) {
		selection.mode = WorkspaceClientMode::Appearances;
		selection.valid = true;
		selection.versionName = wxstr(appearances.manifest.version);
		selection.versionId = ClientVersion::getLatestVersion() ? ClientVersion::getLatestVersion()->getID() : CLIENT_VERSION_NONE;
		for (const std::string& warning : appearances.manifest.warnings) {
			warnings.push_back(wxstr(warning));
		}
		ClientAssets::setPath(path);
		ClientAssets::saveConfiguredPath();
	} else {
		ClientVersion* version = ClientVersion::detectFromPath(FileName(path), error);
		if (version == nullptr) {
			client = std::move(selection);
			++generation;
			if (persist) {
				persistPaths();
			}
			return false;
		}
		selection.mode = WorkspaceClientMode::Classic;
		selection.valid = true;
		selection.versionName = wxstr(version->getName());
		selection.versionId = version->getID();
		g_settings.setInteger(Config::DEFAULT_CLIENT_VERSION, version->getID());
		ClientVersion::saveVersions();
	}

	client = std::move(selection);
	++generation;
	if (persist) {
		persistPaths();
		g_settings.save();
	}
	return true;
}

bool WorkspaceSession::configureServer(const wxString& path, wxString& error, bool persist) {
	const ServerDetectionResult detection = ServerResourceDetector::Detect(ToFilesystemPath(path));
	const bool changed = !SameServerWorkspace(server, detection.workspace) || serverError != wxstr(detection.error);
	server = detection.workspace;
	serverError = wxstr(detection.error);
	error = serverError;
	if (changed) {
		++generation;
	}
	if (persist) {
		persistPaths();
		g_settings.save();
	}
	return detection.validRoot && server.hasRequiredResources();
}

bool WorkspaceSession::rescanServer(wxString& error) {
	if (server.rootPath.empty()) {
		error = "Select the OT server root folder first.";
		return false;
	}
	return configureServer(FromFilesystemPath(server.rootPath), error, true);
}

bool WorkspaceSession::restoreCompatibleClient(wxString& error, wxArrayString& warnings, bool persist) {
	error.clear();
	warnings.clear();
	if (hasCompatibleServerResources()) {
		return true;
	}
	if (!server.hasAppearances()) {
		error = client.valid
			? "The selected client is not compatible with this Server Workspace."
			: "Select a valid client folder before opening the workspace.";
		return false;
	}

	const wxString savedPath = wxstr(g_settings.getString(Config::CANARY_CRYSTAL_ASSETS_DIRECTORY));
	if (savedPath.empty()) {
		error = "This server uses appearances.dat. Select a Canary/Crystal or OTC Assets client once; NexaMap will remember it for detected maps.";
		return false;
	}
	const ClientAssetsValidationResult validation = ClientAssetsManifestLoader::Validate(ToFilesystemPath(savedPath));
	if (!validation.valid) {
		error = wxString("The saved Canary/Crystal client is no longer valid: ") + wxstr(validation.error);
		return false;
	}
	return configureClient(savedPath, error, warnings, persist);
}

void WorkspaceSession::setItemIdModePreference(ItemIdModePreference preference) {
	idModePreference = preference;
	g_settings.setInteger(Config::WORKSPACE_ITEM_ID_MODE, static_cast<int>(preference));
	++generation;
	g_settings.save();
}

ItemIdModePreference WorkspaceSession::getItemIdModePreference() const {
	return idModePreference;
}

ItemIdMode WorkspaceSession::getEffectiveItemIdMode() const {
	// The loaded client asset model defines the in-memory ID space. Classic
	// DAT/SPR item databases are keyed by ServerID and carry ClientID as an
	// attribute; appearances catalogs are keyed by ClientID. Map-name evidence
	// is useful before a client is selected, but must never override that fact.
	const ItemIdMode clientAssetMode = client.mode == WorkspaceClientMode::Appearances
		? ItemIdMode::ClientId
		: (client.mode == WorkspaceClientMode::Classic ? ItemIdMode::ServerId : ItemIdMode::Unknown);
	return ResolveEffectiveItemIdMode(idModePreference, clientAssetMode, server.itemIdMode);
}

const WorkspaceClientSelection& WorkspaceSession::getClient() const {
	return client;
}

const ServerWorkspace& WorkspaceSession::getServer() const {
	return server;
}

const wxString& WorkspaceSession::getServerError() const {
	return serverError;
}

bool WorkspaceSession::hasServerSelection() const {
	return !server.rootPath.empty();
}

bool WorkspaceSession::hasCompatibleServerResources() const {
	if (!client.valid) {
		return false;
	}
	if (client.mode == WorkspaceClientMode::Classic) {
		return server.hasItemsOtb();
	}
	if (client.mode == WorkspaceClientMode::Appearances) {
		return server.hasRequiredResources();
	}
	return false;
}

bool WorkspaceSession::isReady() const {
	return hasCompatibleServerResources() && getEffectiveItemIdMode() != ItemIdMode::Unknown;
}

bool WorkspaceSession::containsMap(const wxString& path) const {
	return server.containsMap(ToFilesystemPath(path));
}

std::vector<wxString> WorkspaceSession::getDetectedMaps() const {
	std::vector<wxString> maps;
	maps.reserve(server.maps.size());
	for (const DetectedMap& map : server.maps) {
		maps.push_back(FromFilesystemPath(map.path));
	}
	return maps;
}

uint64_t WorkspaceSession::getGeneration() const {
	return generation;
}

void WorkspaceSession::persistPaths() {
	g_settings.setString(Config::WORKSPACE_CLIENT_ROOT, nstr(client.rootPath));
	g_settings.setString(Config::WORKSPACE_SERVER_ROOT, server.rootPath.empty() ? std::string() : nstr(FromFilesystemPath(server.rootPath)));
	g_settings.setString(Config::WORKSPACE_ITEMS_OTB_PATH, server.itemsOtbPath.empty() ? std::string() : nstr(FromFilesystemPath(server.itemsOtbPath)));
	g_settings.setString(Config::WORKSPACE_ITEMS_XML_PATH, server.itemsXmlPath.empty() ? std::string() : nstr(FromFilesystemPath(server.itemsXmlPath)));
	g_settings.setString(Config::WORKSPACE_APPEARANCES_PATH, server.appearancesPath.empty() ? std::string() : nstr(FromFilesystemPath(server.appearancesPath)));
	g_settings.setInteger(Config::WORKSPACE_ITEM_ID_MODE, static_cast<int>(idModePreference));
}
