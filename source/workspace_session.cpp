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
			if (left[index].path != right[index].path || left[index].fingerprint != right[index].fingerprint || left[index].serverRootPath != right[index].serverRootPath || left[index].serverType != right[index].serverType) {
				return false;
			}
		}
		return true;
	}

	bool SameServerWorkspace(const ServerWorkspace& left, const ServerWorkspace& right) {
		return left.rootPath == right.rootPath && left.itemsOtbPath == right.itemsOtbPath && left.itemsXmlPath == right.itemsXmlPath && left.appearancesPath == right.appearancesPath && left.activeDataDirectory == right.activeDataDirectory && left.mapsDirectory == right.mapsDirectory && left.primaryMapPath == right.primaryMapPath && left.monstersDirectory == right.monstersDirectory && left.npcsDirectory == right.npcsDirectory && left.itemsOtbFingerprint == right.itemsOtbFingerprint && left.itemsXmlFingerprint == right.itemsXmlFingerprint && left.appearancesFingerprint == right.appearancesFingerprint && left.itemIdMode == right.itemIdMode && left.serverType == right.serverType && left.serverProfile == right.serverProfile && left.protocol == right.protocol && SameDetectedMaps(left.maps, right.maps);
	}

	bool ApplyDetectedMapSelection(ServerWorkspace& workspace, const std::filesystem::path& path) {
		const DetectedMap* selected = workspace.findMap(path);
		if (selected == nullptr) {
			return false;
		}
		workspace.primaryMapPath = selected->path;
		workspace.mapsDirectory = selected->path.parent_path();
		workspace.serverType = selected->serverType;
		workspace.serverProfile = ServerTypeName(selected->serverType);
		workspace.itemIdMode = workspace.usesCanaryCrystalLoader()
			? ItemIdMode::ClientId
			: (workspace.serverType == ServerType::Tfs ? ItemIdMode::ServerId : ItemIdMode::Unknown);
		return true;
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

	if (hasServerSelection()) {
		restoreCompatibleClient(ignoredError, ignoredWarnings, false);
	} else {
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

void WorkspaceSession::swap(WorkspaceSession& other) noexcept {
	using std::swap;
	swap(client, other.client);
	swap(server, other.server);
	swap(selectedDetectedMapPath, other.selectedDetectedMapPath);
	swap(serverError, other.serverError);
	swap(idModePreference, other.idModePreference);
	swap(generation, other.generation);
	swap(persistenceEnabled, other.persistenceEnabled);
}

void WorkspaceSession::setPersistenceEnabled(bool enabled) {
	persistenceEnabled = enabled;
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
		if (persist && persistenceEnabled) {
			ClientAssets::saveConfiguredPath();
		}
	} else {
		ClientVersion* version = ClientVersion::detectFromPath(FileName(path), error);
		if (version == nullptr) {
			client = std::move(selection);
			++generation;
			if (persist && persistenceEnabled) {
				persistPaths();
			}
			return false;
		}
		selection.mode = WorkspaceClientMode::Classic;
		selection.valid = true;
		selection.versionName = wxstr(version->getName());
		selection.versionId = version->getID();
		if (persist && persistenceEnabled) {
			g_settings.setInteger(Config::DEFAULT_CLIENT_VERSION, version->getID());
			ClientVersion::saveVersions();
		}
	}

	client = std::move(selection);
	++generation;
	if (persist && persistenceEnabled) {
		persistPaths();
		g_settings.save();
	}
	return true;
}

bool WorkspaceSession::configureServer(const wxString& path, wxString& error, bool persist) {
	ServerDetectionOptions options;
	options.diagnosticLogging = g_settings.getBoolean(Config::ENABLE_DIAGNOSTIC_LOG);
	if (options.diagnosticLogging) {
		std::cerr << "[workspace] Detecting server: " << path.ToStdString(wxConvUTF8) << std::endl;
	}
	ServerDetectionResult detection = ServerResourceDetector::Detect(ToFilesystemPath(path), options);
	if (options.diagnosticLogging) {
		std::cerr << "[workspace] Detection result: profile=" << detection.workspace.serverProfile
				  << ", maps=" << detection.workspace.maps.size()
				  << ", directories=" << detection.workspace.directoriesScanned
				  << ", error=" << detection.error << std::endl;
		for (const auto& warning : detection.workspace.warnings) {
			std::cerr << "[workspace] Warning: " << warning << std::endl;
		}
	}
	if (!selectedDetectedMapPath.empty() && !ApplyDetectedMapSelection(detection.workspace, selectedDetectedMapPath)) {
		selectedDetectedMapPath.clear();
	}
	const bool changed = !SameServerWorkspace(server, detection.workspace) || serverError != wxstr(detection.error);
	server = detection.workspace;
	serverError = wxstr(detection.error);
	error = serverError;
	if (changed) {
		++generation;
	}
	if (persist && persistenceEnabled) {
		if (options.diagnosticLogging) {
			std::cerr << "[workspace] Saving server selection" << std::endl;
		}
		persistPaths();
		g_settings.save();
	}
	if (options.diagnosticLogging) {
		std::cerr << "[workspace] Server selection applied" << std::endl;
	}
	return detection.validRoot && server.hasRequiredResources();
}

bool WorkspaceSession::selectDetectedMap(const wxString& path, wxString& error, bool persist) {
	const std::optional<DetectedMap> detected = getDetectedMap(path);
	if (!detected) {
		error = "The selected map is no longer part of the detected workspace.";
		return false;
	}

	if (!detected->serverRootPath.empty() && detected->serverRootPath != server.rootPath) {
		if (!configureServer(FromFilesystemPath(detected->serverRootPath), error, false)) {
			return false;
		}
	}

	const DetectedMap* refreshed = server.findMap(ToFilesystemPath(path));
	const DetectedMap& selected = refreshed != nullptr ? *refreshed : *detected;
	const bool changed = server.primaryMapPath != selected.path || server.serverType != selected.serverType;
	selectedDetectedMapPath = selected.path;
	if (!ApplyDetectedMapSelection(server, selectedDetectedMapPath)) {
		selectedDetectedMapPath.clear();
		error = "The selected map could not be resolved after refreshing its server root.";
		return false;
	}
	if (changed) {
		++generation;
	}

	error.clear();
	if (persist && persistenceEnabled) {
		persistPaths();
		g_settings.save();
	}
	return server.hasRequiredResources();
}

bool WorkspaceSession::rescanServer(wxString& error) {
	if (server.rootPath.empty()) {
		error = "Select the OT server root folder first.";
		return false;
	}
	return configureServer(FromFilesystemPath(server.rootPath), error, persistenceEnabled);
}

bool WorkspaceSession::restoreCompatibleClient(wxString& error, wxArrayString& warnings, bool persist) {
	error.clear();
	warnings.clear();
	if (hasCompatibleServerResources()) {
		return true;
	}
	if (server.usesCanaryCrystalLoader()) {
		const wxString savedPath = wxstr(g_settings.getString(Config::CANARY_CRYSTAL_ASSETS_DIRECTORY));
		if (savedPath.empty()) {
			error = "This Canary/Crystal server requires a compatible Assets client. Select one once; NexaMap will remember it for detected maps.";
			return false;
		}
		const ClientAssetsValidationResult validation = ClientAssetsManifestLoader::Validate(ToFilesystemPath(savedPath));
		if (!validation.valid) {
			error = wxString("The saved Canary/Crystal client is no longer valid: ") + wxstr(validation.error);
			return false;
		}
		return configureClient(savedPath, error, warnings, persist);
	}

	// TFS and unknown/generic workspaces must never inherit the dedicated
	// appearances client merely because it was used by the previous project.
	const wxString savedClassicPath = wxstr(g_settings.getString(Config::WORKSPACE_CLIENT_ROOT));
	if (!savedClassicPath.empty() && !ClientAssetsManifestLoader::Validate(ToFilesystemPath(savedClassicPath)).valid) {
		if (configureClient(savedClassicPath, error, warnings, persist) && client.mode == WorkspaceClientMode::Classic) {
			return true;
		}
	}

	ClientVersionID defaultVersionId = static_cast<ClientVersionID>(g_settings.getInteger(Config::DEFAULT_CLIENT_VERSION));
	if (defaultVersionId == CLIENT_VERSION_NONE && ClientVersion::getLatestVersion() != nullptr) {
		defaultVersionId = ClientVersion::getLatestVersion()->getID();
	}
	ClientVersion* defaultVersion = ClientVersion::get(defaultVersionId);
	if (defaultVersion != nullptr) {
		const FileName defaultPath = defaultVersion->getClientPath();
		if (defaultPath.DirExists() && configureClient(defaultPath.GetFullPath(), error, warnings, persist) && client.mode == WorkspaceClientMode::Classic) {
			return true;
		}
	}

	error = server.hasItemsOtb()
		? "This TFS/Generic server requires a classic DAT/SPR or OTC .otfi client. The Canary/Crystal Assets loader was not started."
		: "This Unknown/Generic server has no items.otb. Select the correct server root or open the map manually.";
	return false;
}

void WorkspaceSession::setItemIdModePreference(ItemIdModePreference preference) {
	idModePreference = preference;
	++generation;
	if (persistenceEnabled) {
		g_settings.setInteger(Config::WORKSPACE_ITEM_ID_MODE, static_cast<int>(preference));
		g_settings.save();
	}
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
	if (server.usesCanaryCrystalLoader()) {
		return client.mode == WorkspaceClientMode::Appearances && server.hasRequiredResources();
	}
	return client.mode == WorkspaceClientMode::Classic && server.hasItemsOtb();
}

bool WorkspaceSession::isReady() const {
	return hasCompatibleServerResources() && getEffectiveItemIdMode() != ItemIdMode::Unknown;
}

bool WorkspaceSession::containsMap(const wxString& path) const {
	return server.containsMap(ToFilesystemPath(path));
}

std::optional<DetectedMap> WorkspaceSession::getDetectedMap(const wxString& path) const {
	const DetectedMap* map = server.findMap(ToFilesystemPath(path));
	if (map == nullptr) {
		return std::nullopt;
	}
	return *map;
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
