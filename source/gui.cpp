//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include <wx/display.h>
#include <wx/dir.h>

#include <utility>

#include "gui.h"
#include "favorites_manager.h"
#include "favorites_resources.h"
#include "autoborder_preview.h"
#include "main_menubar.h"
#include "multiplayer_session.h"

#include "editor.h"
#include "brush.h"
#include "map.h"
#include "materials.h"
#include "doodad_brush.h"
#include "spawn_brush.h"
#include "spawn_export_window.h"
#include "spawn_converter_window.h"
#include "map_item_id_converter_window.h"
#include "client_assets.h"
#include "workspace_session.h"

#include "common_windows.h"
#include "result_window.h"
#include "minimap_window.h"
#include "ingame_preview/ingame_preview_window.h"
#include "palette_window.h"
#include "map_display.h"
#include "application.h"
#include "theme.h"
#include "welcome_dialog.h"
#include "object_pool.h"
#include "editor_resource_session.h"
#include "new_map_tab_dialog.h"
#include "cross_client_clipboard.h"
#include "cross_client_paste_dialog.h"

#ifdef __WXOSX__
	#include <AGL/agl.h>
#endif

const wxEventType EVT_UPDATE_MENUS = wxNewEventType();

namespace {
	constexpr const char* CANARY_CRYSTAL_DATA_DIRECTORY = "canary-crystal";

	wxString GetCanaryCrystalBundledDataDirectory() {
		wxString dataDirectory = GUI::GetDataDirectory();
		if (!wxFileName(dataDirectory).DirExists()) {
			dataDirectory = g_gui.getFoundDataDirectory();
		}
		return dataDirectory + wxString::FromUTF8(CANARY_CRYSTAL_DATA_DIRECTORY) + FileName::GetPathSeparator();
	}

	FileName GetCanaryCrystalLocalDataDirectory() {
		FileName directory = GUI::GetLocalDataDirectory();
		directory.AppendDir(wxString::FromUTF8(CANARY_CRYSTAL_DATA_DIRECTORY));
		directory.Mkdir(0755, wxPATH_MKDIR_FULL);
		return directory;
	}

	wxString WorkspacePath(const std::filesystem::path& path) {
#ifdef __WINDOWS__
		return wxString(path.wstring());
#else
		return wxString::FromUTF8(path.string());
#endif
	}

	bool RefreshRequiredServerWorkspace(wxString& error, bool& changed, WorkspaceClientMode expectedClientMode) {
		changed = false;
		if (!g_workspace.hasServerSelection()) {
			error = "Server Workspace is not configured. Select the OT server root containing items.otb or appearances.dat.";
			return false;
		}

		const uint64_t previousGeneration = g_workspace.getGeneration();
		if (!g_workspace.rescanServer(error)) {
			if (error.empty()) {
				error = "Server Workspace is configured, but neither items.otb nor appearances.dat was found.";
			}
			return false;
		}
		const ServerWorkspace& workspace = g_workspace.getServer();
		if (expectedClientMode == WorkspaceClientMode::Classic && !workspace.hasItemsOtb()) {
			error = "This classic DAT/SPR client requires items.otb in the selected Server Workspace. appearances.dat is supported by Canary/Crystal clients.";
			return false;
		}
		changed = g_workspace.getGeneration() != previousGeneration;
		return true;
	}

	bool IsInapplicableMaterialItemWarning(const wxString& warning) {
		wxString normalized = warning;
		normalized.Trim(true);
		normalized.Trim(false);

		return normalized.StartsWith("Invalid item id ") || (normalized.StartsWith("Item ") && normalized.EndsWith(" is not ground item.")) || normalized == "Ground dependency equivalent is not a ground item." || normalized.StartsWith("There is no itemtype with id ") || normalized.StartsWith("Tileset: ") || normalized.StartsWith("Unknown item id #");
	}

	void AppendActionableMaterialWarnings(wxArrayString& target, const wxArrayString& materialWarnings) {
		wxString pendingBrushHeader;
		for (const wxString& warning : materialWarnings) {
			if (warning.StartsWith("Errors while loading brush \"")) {
				// A brush header is emitted only if that brush also has a warning
				// other than an item missing from/incompatible with the active OTB.
				pendingBrushHeader = warning;
				continue;
			}

			if (IsInapplicableMaterialItemWarning(warning)) {
				continue;
			}

			if (!pendingBrushHeader.empty()) {
				target.push_back(pendingBrushHeader);
				pendingBrushHeader.clear();
			}
			target.push_back(warning);
		}
	}

	void AppendActionableDedicatedCreatureWarnings(wxArrayString& target, const wxArrayString& catalogWarnings) {
		for (const wxString& warning : catalogWarnings) {
			const bool sharedMonsterNpcName = warning.StartsWith("Duplicate creature type name \"");
			const bool unsupportedCatalogOutfit = warning.StartsWith("Invalid creature \"") && warning.Contains("\" look type #");
			if (sharedMonsterNpcName || unsupportedCatalogOutfit) {
				// Canary/Crystal legitimately contains some identical monster/NPC
				// names, and the bundled catalog can be newer than the selected
				// appearance package. Both cases are non-fatal and remain available
				// in the diagnostic log without interrupting workspace opening.
				wxLogDebug("Canary/Crystal creature catalog: " + warning);
				continue;
			}
			target.push_back(warning);
		}
	}
}

// Global GUI instance
GUI g_gui;

// GUI class implementation
GUI::GUI() :
	aui_manager(nullptr),
	root(nullptr),
	minimap(nullptr),
	ingame_preview(nullptr),
	gem(nullptr),
	search_result_window(nullptr),
	secondary_map(nullptr),
	doodad_buffer_map(nullptr),

	house_brush(nullptr),
	house_exit_brush(nullptr),
	waypoint_brush(nullptr),
	optional_brush(nullptr),
	eraser(nullptr),
	spawn_brush(nullptr),
	normal_door_brush(nullptr),
	locked_door_brush(nullptr),
	magic_door_brush(nullptr),
	quest_door_brush(nullptr),
	hatch_door_brush(nullptr),
	normal_door_alt_brush(nullptr),
	archway_door_brush(nullptr),
	window_door_brush(nullptr),
	pz_brush(nullptr),
	rook_brush(nullptr),
	nolog_brush(nullptr),
	pvp_brush(nullptr),
	zone_brush(nullptr),

	OGLContext(nullptr),
	loaded_version(CLIENT_VERSION_NONE),
	canary_crystal_assets_loaded(false),
	mode(SELECTION_MODE),
	pasting(false),
	hotkeys_enabled(true),

	current_brush(nullptr),
	previous_brush(nullptr),
	brush_shape(BRUSHSHAPE_SQUARE),
	brush_size(0),
	brush_variation(0),

	creature_spawntime(0),
	draw_locked_doors(false),
	use_custom_thickness(false),
	custom_thickness_mod(0.0),
	progressBar(nullptr),
	disabled_counter(0) {
	doodad_buffer_map = newd BaseMap();
	crossClientClipboard = std::make_unique<CrossClientClipboard>();
}

GUI::~GUI() {
	delete doodad_buffer_map;
	delete g_gui.aui_manager;
	delete OGLContext;
}

wxGLContext* GUI::GetGLContext(wxGLCanvas* win) {
	if (OGLContext == nullptr) {
#ifdef __WXOSX__
		/*
		wxGLContext(AGLPixelFormat fmt, wxGLCanvas *win,
					const wxPalette& WXUNUSED(palette),
					const wxGLContext *other
					);
		*/
		OGLContext = new wxGLContext(win, nullptr);
#else
		OGLContext = newd wxGLContext(win);
#endif
	}

	return OGLContext;
}

wxString GUI::GetDataDirectory() {
	std::string cfg_str = g_settings.getString(Config::DATA_DIRECTORY);
	if (!cfg_str.empty()) {
		FileName dir;
		dir.Assign(wxstr(cfg_str));
		wxString path;
		if (dir.DirExists()) {
			path = dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
			return path;
		}
	}

	// Silently reset directory
	FileName exec_directory;
	try {
		exec_directory = dynamic_cast<wxStandardPaths&>(wxStandardPaths::Get()).GetExecutablePath();
	} catch (const std::bad_cast) {
		throw; // Crash application (this should never happend anyways...)
	}

	exec_directory.AppendDir("data");
	return exec_directory.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
}

wxString GUI::GetExecDirectory() {
	// Silently reset directory
	FileName exec_directory;
	try {
		exec_directory = dynamic_cast<wxStandardPaths&>(wxStandardPaths::Get()).GetExecutablePath();
	} catch (const std::bad_cast) {
		wxLogError("Could not fetch executable directory.");
	}
	return exec_directory.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
}

wxString GUI::GetEditorDataDirectory() {
	for (const wxString& dataDirectory : { GetDataDirectory(), g_gui.getFoundDataDirectory() }) {
		if (dataDirectory.empty()) {
			continue;
		}
		FileName editorDirectory = FileName::DirName(dataDirectory);
		editorDirectory.AppendDir("editor");
		if (editorDirectory.DirExists()) {
			return editorDirectory.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
		}
	}
	return {};
}

wxString GUI::GetLocalDataDirectory() {
	if (g_settings.getInteger(Config::INDIRECTORY_INSTALLATION)) {
		FileName dir = GetDataDirectory();
		dir.AppendDir("user");
		dir.AppendDir("data");
		dir.Mkdir(0755, wxPATH_MKDIR_FULL);
		return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
		;
	} else {
		FileName dir = dynamic_cast<wxStandardPaths&>(wxStandardPaths::Get()).GetUserDataDir();
#ifdef __WINDOWS__
		dir.AppendDir("Remere's Map Editor");
#else
		dir.AppendDir(".rme");
#endif
		dir.AppendDir("data");
		dir.Mkdir(0755, wxPATH_MKDIR_FULL);
		return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
	}
}

wxString GUI::GetLocalDirectory() {
	if (g_settings.getInteger(Config::INDIRECTORY_INSTALLATION)) {
		FileName dir = GetDataDirectory();
		dir.AppendDir("user");
		dir.Mkdir(0755, wxPATH_MKDIR_FULL);
		return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
		;
	} else {
		FileName dir = dynamic_cast<wxStandardPaths&>(wxStandardPaths::Get()).GetUserDataDir();
#ifdef __WINDOWS__
		dir.AppendDir("Remere's Map Editor");
#else
		dir.AppendDir(".rme");
#endif
		dir.Mkdir(0755, wxPATH_MKDIR_FULL);
		return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
	}
}

wxString GUI::GetExtensionsDirectory() {
	std::string cfg_str = g_settings.getString(Config::EXTENSIONS_DIRECTORY);
	if (!cfg_str.empty()) {
		FileName dir;
		dir.Assign(wxstr(cfg_str));
		wxString path;
		if (dir.DirExists()) {
			path = dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
			return path;
		}
	}

	// Silently reset directory
	FileName local_directory = GetLocalDirectory();
	local_directory.AppendDir("extensions");
	local_directory.Mkdir(0755, wxPATH_MKDIR_FULL);
	return local_directory.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
}

void GUI::discoverDataDirectory(const wxString& existentFile) {
	wxString currentDir = wxGetCwd();
	wxString execDir = GetExecDirectory();

	wxString possiblePaths[] = {
		execDir,
		currentDir + "/",

		// these are used usually when running from build directories
		execDir + "/../",
		execDir + "/../../",
		execDir + "/../../../",
		currentDir + "/../",
	};

	bool found = false;
	for (const wxString& path : possiblePaths) {
		if (wxFileName(path + "data/" + existentFile).FileExists()) {
			m_dataDirectory = path + "data/";
			found = true;
			break;
		}
	}

	if (!found) {
		wxLogError(wxString() + "Could not find data directory.\n");
	}
}

bool GUI::LoadVersion(ClientVersionID version, wxString& error, wxArrayString& warnings, bool force) {
	if (ClientVersion::get(version) == nullptr) {
		error = wxString::Format("Unsupported client version! (%d)", version);
		return false;
	}
	bool serverResourcesChanged = false;
	if (!RefreshRequiredServerWorkspace(error, serverResourcesChanged, WorkspaceClientMode::Classic)) {
		return false;
	}
	force = force || serverResourcesChanged;

	if (version != loaded_version || force || canary_crystal_assets_loaded) {
		if (getLoadedVersion() != nullptr) {
			// There is another version loaded right now, save window layout
			g_gui.SavePerspective();
		}

		// Disable all rendering so the data is not accessed while reloading
		UnnamedRenderingLock();
		DestroyPalettes();
		DestroyMinimap();
		DestroyIngamePreview();

		// Destroy the previous version
		UnloadVersion();

		loaded_version = version;
		if (!getLoadedVersion()->hasValidPaths()) {
			if (!getLoadedVersion()->loadValidPaths()) {
				error = "Couldn't load relevant asset files";
				loaded_version = CLIENT_VERSION_NONE;
				return false;
			}
		}

		bool ret = LoadDataFiles(error, warnings);
		if (ret) {
			if (!resourceSessionUiPending) {
				g_gui.LoadPerspective();
			}
		} else {
			loaded_version = CLIENT_VERSION_NONE;
		}

		return ret;
	}
	return true;
}

bool GUI::LoadCanaryCrystalAssets(wxString& error, wxArrayString& warnings, bool force) {
	ClientVersion* compatibilityProfile = ClientVersion::getLatestVersion();
	if (compatibilityProfile == nullptr) {
		error = "No client compatibility profile is available for the Canary/Crystal Assets loader.";
		return false;
	}
	if (ClientAssets::getPath().empty()) {
		error = "Configure a CipSoft/Crystal or OTC Assets root in Preferences > Client Version first.";
		return false;
	}
	bool serverResourcesChanged = false;
	if (!RefreshRequiredServerWorkspace(error, serverResourcesChanged, WorkspaceClientMode::Appearances)) {
		return false;
	}
	force = force || serverResourcesChanged;
	if (canary_crystal_assets_loaded && loaded_version == compatibilityProfile->getID() && !force) {
		return true;
	}

	if (getLoadedVersion() != nullptr && !resourceSessionUiPending) {
		SavePerspective();
	}
	UnnamedRenderingLock();
	DestroyPalettes();
	DestroyMinimap();
	DestroyIngamePreview();
	UnloadVersion();

	// Assets provide their own appearances and sprites. The latest configured
	// profile is used only for editor compatibility rules; no versioned DAT/SPR
	// or data/<version> directory is loaded here.
	loaded_version = compatibilityProfile->getID();
	canary_crystal_assets_loaded = true;
	if (!LoadCanaryCrystalDataFiles(error, warnings)) {
		canary_crystal_assets_loaded = false;
		loaded_version = CLIENT_VERSION_NONE;
		return false;
	}
	if (!resourceSessionUiPending) {
		LoadPerspective();
	}
	return true;
}

bool GUI::LoadWorkspace(wxString& error, wxArrayString& warnings, bool force) {
	wxArrayString restoredClientWarnings;
	if (!g_workspace.restoreCompatibleClient(error, restoredClientWarnings)) {
		return false;
	}
	for (const wxString& warning : restoredClientWarnings) {
		warnings.push_back(warning);
	}
	bool serverResourcesChanged = false;
	if (!RefreshRequiredServerWorkspace(error, serverResourcesChanged, g_workspace.getClient().mode)) {
		return false;
	}

	const uint64_t generation = g_workspace.getGeneration();
	force = force || serverResourcesChanged || generation != loaded_workspace_generation;
	bool loaded = false;
	if (g_workspace.getServer().usesCanaryCrystalLoader()) {
		loaded = LoadCanaryCrystalAssets(error, warnings, force);
	} else {
		loaded = LoadVersion(g_workspace.getClient().versionId, error, warnings, force);
	}
	if (loaded) {
		loaded_workspace_generation = generation;
	}
	return loaded;
}

void GUI::EnableHotkeys() {
	hotkeys_enabled = true;
}

void GUI::DisableHotkeys() {
	hotkeys_enabled = false;
}

bool GUI::AreHotkeysEnabled() const {
	return hotkeys_enabled;
}

ClientVersionID GUI::GetCurrentVersionID() const {
	if (loaded_version != CLIENT_VERSION_NONE) {
		return getLoadedVersion()->getID();
	}
	return CLIENT_VERSION_NONE;
}

const ClientVersion& GUI::GetCurrentVersion() const {
	assert(loaded_version);
	return *getLoadedVersion();
}

void GUI::CycleTab(bool forward) {
	tabbook->CycleTab(forward);
}

void GUI::SwapResourceSessionState(EditorResourceSession& session) {
	using std::swap;
	swap(loaded_version, session.loadedVersion);
	swap(canary_crystal_assets_loaded, session.canaryCrystalAssetsLoaded);
	swap(loaded_workspace_generation, session.loadedWorkspaceGeneration);
	swap(current_brush, session.currentBrush);
	swap(previous_brush, session.previousBrush);
	swap(house_brush, session.houseBrush);
	swap(house_exit_brush, session.houseExitBrush);
	swap(waypoint_brush, session.waypointBrush);
	swap(optional_brush, session.optionalBrush);
	swap(eraser, session.eraser);
	swap(spawn_brush, session.spawnBrush);
	swap(normal_door_brush, session.normalDoorBrush);
	swap(locked_door_brush, session.lockedDoorBrush);
	swap(magic_door_brush, session.magicDoorBrush);
	swap(quest_door_brush, session.questDoorBrush);
	swap(hatch_door_brush, session.hatchDoorBrush);
	swap(normal_door_alt_brush, session.normalDoorAltBrush);
	swap(archway_door_brush, session.archwayDoorBrush);
	swap(window_door_brush, session.windowDoorBrush);
	swap(pz_brush, session.pzBrush);
	swap(rook_brush, session.rookBrush);
	swap(nolog_brush, session.nologBrush);
	swap(pvp_brush, session.pvpBrush);
	swap(zone_brush, session.zoneBrush);
	swap(doodad_buffer_map, session.doodadBufferMap);
}

bool GUI::ActivateResourceSession(const std::shared_ptr<EditorResourceSession>& session) {
	if (!MultiplayerSession::permitsResourceSession(session)) {
		PopupDialog("Multiplayer", "Disconnect the multiplayer session before switching to different client assets.", wxOK);
		return false;
	}
	if (!session) {
		return false;
	}
	const EditorResourceSessionPtr current = GetActiveEditorResourceSession();
	if (current == session) {
		return true;
	}

	UnnamedRenderingLock();
	if (getLoadedVersion() != nullptr && !resourceSessionUiPending) {
		SavePerspective();
	}
	DestroyPalettes();
	DestroyMinimap();
	DestroyIngamePreview();
	g_autoborder_preview.Clear();
	pasting = false;
	secondary_map = nullptr;

	SwapResourceSessionState(*current);
	current->swapWithGlobals();
	session->swapWithGlobals();
	SwapResourceSessionState(*session);
	SetActiveEditorResourceSession(session);
	if (!doodad_buffer_map) {
		doodad_buffer_map = newd BaseMap();
	}

	const WorkspaceClientSelection& client = g_workspace.getClient();
	if (loaded_version != CLIENT_VERSION_NONE && client.mode == WorkspaceClientMode::Classic && !client.rootPath.empty()) {
		if (ClientVersion* version = ClientVersion::get(loaded_version)) {
			version->setClientPath(FileName(client.rootPath));
		}
	}
	gfx.activateSpritePreloader();
	resourceSessionUiPending = true;
	if (MapTab* displayedTab = GetCurrentMapTab(); displayedTab && displayedTab->GetResourceSession() == session) {
		FinalizeResourceSessionActivation();
	}
	return true;
}

void GUI::FinalizeResourceSessionActivation() {
	if (!resourceSessionUiPending) {
		return;
	}
	resourceSessionUiPending = false;
	if (getLoadedVersion() != nullptr) {
		LoadPerspective();
	}
	InvalidateAutoborderPreview();
	UpdateIngamePreview();
	UpdateTitle();
	UpdateMenus();
}

void GUI::ShowNewMapTabDialog() {
	if (ShouldSuppressNewTabRequests()) {
		return;
	}
	NewMapTabDialog dialog(root);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}
	const NewMapTabSelection selection = dialog.GetSelection();
	if (selection.useCurrentClient) {
		if (selection.mapFile.empty()) {
			NewMap();
		} else {
			LoadMapInternal(FileName(selection.mapFile), EditorClientVersionPolicy::KeepLoaded, nullptr, false, false);
		}
		return;
	}

	const EditorResourceSessionPtr previousSession = GetActiveEditorResourceSession();
	const EditorResourceSessionPtr newSession = CreateEditorResourceSession();
	// Loading dialogs pump the event loop. Keep the old canvas from rendering
	// against the new session until its own map tab has been created.
	RenderingLock sessionCreationLock;
	if (!ActivateResourceSession(newSession)) {
		PopupDialog(root, "New Tab", "Could not activate an independent resource session.", wxOK | wxICON_ERROR);
		return;
	}

	auto restorePreviousSession = [&] {
		ActivateResourceSession(previousSession);
	};
	g_workspace.setPersistenceEnabled(false);
	gfx.loadEditorSprites();

	wxString error;
	wxArrayString warnings;
	if (!g_workspace.configureClient(selection.clientDirectory, error, warnings, false)) {
		restorePreviousSession();
		PopupDialog(root, "Client not supported", error, wxOK | wxICON_ERROR);
		return;
	}
	if (!g_workspace.configureServer(selection.serverDirectory, error, false)) {
		restorePreviousSession();
		PopupDialog(root, "Server not supported", error, wxOK | wxICON_ERROR);
		return;
	}
	if (!LoadWorkspace(error, warnings, true)) {
		restorePreviousSession();
		PopupDialog(root, "Workspace not ready", error, wxOK | wxICON_ERROR);
		return;
	}

	wxString mapPath = selection.mapFile;
	if (mapPath.empty() && !g_workspace.getServer().primaryMapPath.empty()) {
		mapPath = WorkspacePath(g_workspace.getServer().primaryMapPath);
	}
	const bool created = mapPath.empty()
		? NewMap()
		: LoadMapInternal(FileName(mapPath), EditorClientVersionPolicy::KeepLoaded, nullptr, false, false);
	if (!created) {
		restorePreviousSession();
		return;
	}
	if (!warnings.empty()) {
		ListDialog("Workspace warnings", warnings);
	}
}

bool GUI::LoadDataFiles(wxString& error, wxArrayString& warnings) {
	const wxString editorDataDirectory = GetEditorDataDirectory();
	if (editorDataDirectory.empty()) {
		error = "The canonical NexaMap editor data directory (data/editor) was not found.";
		return false;
	}
	FileName client_path = getLoadedVersion()->getClientPath();
	FileName extension_path = GetExtensionsDirectory();

	FileName exec_directory;
	try {
		exec_directory = dynamic_cast<wxStandardPaths&>(wxStandardPaths::Get()).GetExecutablePath();
	} catch (std::bad_cast&) {
		error = "Couldn't establish working directory...";
		return false;
	}

	g_gui.gfx.client_version = getLoadedVersion();

	if (!g_gui.gfx.loadOTFI(client_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR), error, warnings)) {
		error = "Couldn't load otfi file: " + error;
		g_gui.DestroyLoadBar();
		UnloadVersion();
		return false;
	}

	g_gui.CreateLoadBar("Loading asset files");
	g_gui.SetLoadDone(0, "Loading metadata file...");

	wxFileName metadata_path = g_gui.gfx.getMetadataFileName();
	if (!g_gui.gfx.loadSpriteMetadata(metadata_path, error, warnings)) {
		error = "Couldn't load metadata: " + error;
		g_gui.DestroyLoadBar();
		UnloadVersion();
		return false;
	}

	g_gui.SetLoadDone(10, "Loading sprites file...");

	wxFileName sprites_path = g_gui.gfx.getSpritesFileName();
	if (!g_gui.gfx.loadSpriteData(sprites_path.GetFullPath(), error, warnings)) {
		error = "Couldn't load sprites: " + error;
		g_gui.DestroyLoadBar();
		UnloadVersion();
		return false;
	}

	const ServerWorkspace& workspace = g_workspace.getServer();
	if (!workspace.hasItemsOtb()) {
		error = "Server Workspace is configured, but items.otb was not found. NexaMap will not use a bundled item database.";
		g_gui.DestroyLoadBar();
		UnloadVersion();
		return false;
	}
	const wxString itemsOtbPath = WorkspacePath(workspace.itemsOtbPath);
	const bool loadItemsXml = workspace.hasItemsXml();
	const wxString itemsXmlPath = loadItemsXml ? WorkspacePath(workspace.itemsXmlPath) : wxString {};

	g_gui.SetLoadDone(20, "Loading server items.otb...");
	if (!g_items.loadFromOtb(itemsOtbPath, error, warnings)) {
		error = "Couldn't load items.otb: " + error;
		g_gui.DestroyLoadBar();
		UnloadVersion();
		return false;
	}

	if (loadItemsXml) {
		g_gui.SetLoadDone(30, "Loading server items.xml...");
		if (!g_items.loadFromGameXml(itemsXmlPath, error, warnings)) {
			warnings.push_back("Couldn't load items.xml: " + error);
		}
	} else {
		g_gui.SetLoadDone(30, "Server items.xml not found; continuing with items.otb...");
		warnings.push_back("Server items.xml was not found. The workspace loaded directly from items.otb without optional XML metadata.");
	}

	g_gui.SetLoadDone(45, "Loading creatures.xml ...");
	if (!g_creatures.loadFromXML(editorDataDirectory + "creatures.xml", true, error, warnings)) {
		warnings.push_back("Couldn't load creatures.xml: " + error);
	}

	g_gui.SetLoadDone(45, "Loading user creatures.xml ...");
	{
		FileName cdb = getLoadedVersion()->getLocalDataPath();
		cdb.SetFullName("creatures.xml");
		wxString nerr;
		wxArrayString nwarn;
		g_creatures.loadFromXML(cdb, false, nerr, nwarn);
	}

	if (!workspace.monstersDirectory.empty()) {
		wxString importError;
		if (!g_creatures.importMonstersFromLuaDir(WorkspacePath(workspace.monstersDirectory), importError, warnings)) {
			warnings.push_back("Couldn't import monsters from the Server Workspace: " + importError);
		}
	}
	if (!workspace.npcsDirectory.empty()) {
		wxString importError;
		if (!g_creatures.importNpcsFromLuaDir(WorkspacePath(workspace.npcsDirectory), importError, warnings)) {
			warnings.push_back("Couldn't import NPCs from the Server Workspace: " + importError);
		}
	}

	g_gui.SetLoadDone(50, "Loading materials.xml ...");
	wxArrayString materialWarnings;
	if (!g_materials.loadMaterials(editorDataDirectory + "materials.xml", error, materialWarnings)) {
		warnings.push_back("Couldn't load materials.xml: " + error);
	}
	AppendActionableMaterialWarnings(warnings, materialWarnings);

	g_gui.SetLoadDone(70, "Loading extensions...");
	wxArrayString extensionWarnings;
	if (!g_materials.loadExtensions(extension_path, error, extensionWarnings)) {
		// warnings.push_back("Couldn't load extensions: " + error);
	}
	AppendActionableMaterialWarnings(warnings, extensionWarnings);

	g_gui.SetLoadDone(70, "Finishing...");
	g_brushes.init();
	g_materials.createOtherTileset();

	g_gui.SetLoadDone(100, "Classic resources ready.");
	GetActiveEditorResourceSession()->favoritesContext = FavoriteResources::CaptureContext();
	return true;
}

bool GUI::LoadCanaryCrystalDataFiles(wxString& error, wxArrayString& warnings) {
	const wxString assetsDataDirectory = GetCanaryCrystalBundledDataDirectory();
	if (!wxDir::Exists(assetsDataDirectory)) {
		error = "Missing bundled Canary/Crystal data directory: " + assetsDataDirectory;
		return false;
	}

	gfx.client_version = getLoadedVersion();
	CreateLoadBar("Loading Canary/Crystal Assets");
	SetLoadDone(0, "Validating package and catalog...");
	wxLogMessage("Canary/Crystal: validating client package and catalog.");

	if (!ClientAssets::load(error, warnings)) {
		DestroyLoadBar();
		UnloadVersion();
		return false;
	}

	SetLoadDone(35, "Loading item metadata...");
	wxLogMessage("Canary/Crystal: loading dedicated item metadata.");
	wxString supplementalError;
	const ServerWorkspace& workspace = g_workspace.getServer();
	if (workspace.hasItemsXml()) {
		const wxString serverItemsXml = WorkspacePath(workspace.itemsXmlPath);
		if (!g_items.loadFromGameXml(serverItemsXml, supplementalError, warnings, true)) {
			warnings.push_back("Couldn't enrich Canary/Crystal items from the server items.xml: " + supplementalError);
		}
	} else {
		warnings.push_back("Server items.xml was not found. Canary/Crystal item properties are limited to appearances.dat metadata.");
	}

	SetLoadDone(50, "Loading creatures...");
	wxLogMessage("Canary/Crystal: loading dedicated monsters and NPCs.");
	wxArrayString catalogWarnings;
	supplementalError.clear();
	if (!g_creatures.loadFromXML(assetsDataDirectory + "creatures/monsters.xml", true, supplementalError, catalogWarnings)) {
		warnings.push_back("Couldn't load Canary/Crystal monsters.xml: " + supplementalError);
	}
	supplementalError.clear();
	if (!g_creatures.loadFromXML(assetsDataDirectory + "creatures/npcs.xml", true, supplementalError, catalogWarnings)) {
		warnings.push_back("Couldn't load Canary/Crystal npcs.xml: " + supplementalError);
	}
	AppendActionableDedicatedCreatureWarnings(warnings, catalogWarnings);
	{
		FileName userCreatures = GetCanaryCrystalLocalDataDirectory();
		userCreatures.SetFullName("creatures.xml");
		wxString userError;
		wxArrayString userWarnings;
		g_creatures.loadFromXML(userCreatures, false, userError, userWarnings);
		// Older NexaMap versions persisted creatures imported from an entire
		// server into this shared overlay. It can contain both stale duplicates
		// and outfits newer than the selected appearance package; neither should
		// interrupt opening because the active server import below refreshes them.
		AppendActionableDedicatedCreatureWarnings(warnings, userWarnings);
	}

	const wxString monstersDirectory = !workspace.monstersDirectory.empty()
		? WorkspacePath(workspace.monstersDirectory)
		: wxstr(g_settings.getString(Config::MONSTERS_LUA_DIRECTORY));
	if (!monstersDirectory.empty() && wxDir::Exists(monstersDirectory)) {
		wxString luaError;
		if (!g_creatures.importMonstersFromLuaDir(monstersDirectory, luaError, warnings)) {
			warnings.push_back("Couldn't import the configured monsters Lua directory: " + luaError);
		}
	}
	const wxString npcsDirectory = !workspace.npcsDirectory.empty()
		? WorkspacePath(workspace.npcsDirectory)
		: wxstr(g_settings.getString(Config::NPCS_LUA_DIRECTORY));
	if (!npcsDirectory.empty() && wxDir::Exists(npcsDirectory)) {
		wxString luaError;
		if (!g_creatures.importNpcsFromLuaDir(npcsDirectory, luaError, warnings)) {
			warnings.push_back("Couldn't import the configured NPCs Lua directory: " + luaError);
		}
	}

	SetLoadDone(65, "Loading materials...");
	wxLogMessage("Canary/Crystal: loading dedicated materials and borders.");
	supplementalError.clear();
	wxArrayString dedicatedMaterialWarnings;
	if (!g_materials.loadMaterials(assetsDataDirectory + "materials/materials.xml", supplementalError, dedicatedMaterialWarnings)) {
		warnings.push_back("Couldn't load materials.xml: " + supplementalError);
	}
	AppendActionableMaterialWarnings(warnings, dedicatedMaterialWarnings);

	// Legacy extensions are tied to versioned TFS ServerIDs and may redefine
	// brushes from the dedicated ClientID material set. Do not mix them into an
	// Assets session.
	SetLoadDone(85, "Initializing Canary/Crystal brushes...");
	wxLogMessage("Canary/Crystal: initializing editor brushes.");
	g_brushes.init();
	SetLoadDone(95, "Building Canary/Crystal palettes...");
	wxLogMessage("Canary/Crystal: building item and creature palettes.");
	g_materials.createOtherTileset();
	wxLogMessage("Canary/Crystal: dedicated data load completed.");
	SetLoadDone(100, "Canary/Crystal palettes ready.");
	GetActiveEditorResourceSession()->favoritesContext = FavoriteResources::CaptureContext();
	return true;
}

void GUI::UnloadVersion() {
	GetActiveEditorResourceSession()->favoritesContext.clear();
	UnnamedRenderingLock();
	DestroyIngamePreview();
	gfx.clear();
	current_brush = nullptr;
	previous_brush = nullptr;

	house_brush = nullptr;
	house_exit_brush = nullptr;
	waypoint_brush = nullptr;
	optional_brush = nullptr;
	eraser = nullptr;
	spawn_brush = nullptr;
	normal_door_brush = nullptr;
	locked_door_brush = nullptr;
	magic_door_brush = nullptr;
	quest_door_brush = nullptr;
	hatch_door_brush = nullptr;
	normal_door_alt_brush = nullptr;
	archway_door_brush = nullptr;
	window_door_brush = nullptr;
	pz_brush = nullptr;
	rook_brush = nullptr;
	nolog_brush = nullptr;
	pvp_brush = nullptr;
	zone_brush = nullptr;

	if (loaded_version != CLIENT_VERSION_NONE) {
		// g_gui.UnloadVersion();
		g_materials.clear();
		g_brushes.clear();
		g_items.clear();
		gfx.clear();

		SaveUserCreatures();
		g_creatures.clear();

		loaded_version = CLIENT_VERSION_NONE;
	}
	ClientAssets::unload();
	canary_crystal_assets_loaded = false;
	loaded_workspace_generation = 0;
}

void GUI::SaveUserCreatures() {
	if (loaded_version == CLIENT_VERSION_NONE) {
		return;
	}
	FileName cdb = canary_crystal_assets_loaded ? GetCanaryCrystalLocalDataDirectory() : getLoadedVersion()->getLocalDataPath();
	cdb.SetFullName("creatures.xml");
	g_creatures.saveToXML(cdb);
}

bool GUI::SaveCurrentMap(const FileName& filename, bool showdialog) {
	MapTab* mapTab = GetCurrentMapTab();
	if (mapTab) {
		Editor* editor = mapTab->GetEditor();
		if (editor) {
			if (!editor->saveMap(filename, showdialog)) {
				return false;
			}

			const std::string& filename = editor->map.getFilename();
			const Position& position = mapTab->GetScreenCenterPosition();
			std::ostringstream stream;
			stream << position;
			g_settings.setString(Config::RECENT_EDITED_MAP_PATH, filename);
			g_settings.setString(Config::RECENT_EDITED_MAP_POSITION, stream.str());
		}
	}

	UpdateTitle();
	root->UpdateMenubar();
	root->Refresh();
	return mapTab != nullptr;
}

bool GUI::IsEditorOpen() const {
	return tabbook != nullptr && GetCurrentMapTab();
}

double GUI::GetCurrentZoom() {
	MapTab* tab = GetCurrentMapTab();
	if (tab) {
		return tab->GetCanvas()->GetZoom();
	}
	return 1.0;
}

void GUI::SetCurrentZoom(double zoom) {
	MapTab* tab = GetCurrentMapTab();
	if (tab) {
		tab->GetCanvas()->SetZoom(zoom);
	}
}

void GUI::FitViewToMap() {
	for (int index = 0; index < tabbook->GetTabCount(); ++index) {
		if (auto* tab = dynamic_cast<MapTab*>(tabbook->GetTab(index))) {
			tab->GetView()->FitToMap();
		}
	}
}

void GUI::FitViewToMap(MapTab* mt) {
	for (int index = 0; index < tabbook->GetTabCount(); ++index) {
		if (auto* tab = dynamic_cast<MapTab*>(tabbook->GetTab(index))) {
			if (tab->HasSameReference(mt)) {
				tab->GetView()->FitToMap();
			}
		}
	}
}

bool GUI::NewMap() {
	FinishWelcomeDialog();

	Editor* editor;
	try {
		editor = newd Editor(copybuffer);
	} catch (std::runtime_error& e) {
		PopupDialog(root, "Error!", wxString(e.what(), wxConvUTF8), wxOK);
		return false;
	}

	auto* mapTab = newd MapTab(tabbook, editor);
	mapTab->OnSwitchEditorMode(mode);
	editor->map.clearChanges();

	SetStatusText("Created new map");
	UpdateTitle();
	RefreshPalettes();
	root->UpdateMenubar();
	root->Refresh();

	return true;
}

void GUI::OpenMap() {
	wxString wildcard = g_settings.getInteger(Config::USE_OTGZ) != 0 ? MAP_LOAD_FILE_WILDCARD_OTGZ : MAP_LOAD_FILE_WILDCARD;
	wxFileDialog dialog(root, "Open map file", wxEmptyString, wxEmptyString, wildcard, wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (dialog.ShowModal() == wxID_OK) {
		LoadMap(dialog.GetPath());
	}
}

void GUI::SaveMap() {
	if (!IsEditorOpen()) {
		return;
	}

	if (GetCurrentMap().hasFile()) {
		SaveCurrentMap(true);
	} else {
		wxString wildcard = g_settings.getInteger(Config::USE_OTGZ) != 0 ? MAP_SAVE_FILE_WILDCARD_OTGZ : MAP_SAVE_FILE_WILDCARD;
		wxFileDialog dialog(root, "Save...", wxEmptyString, wxEmptyString, wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

		Map& map = GetCurrentMap();
		const MapVersion previousVersion = map.mapVersion;
		const MapStorageFormat previousStorageFormat = map.storageFormat;
		const uint32_t previousMajorVersion = map.sourceItemMajorVersion;
		const uint32_t previousMinorVersion = map.sourceItemMinorVersion;
		const SpawnFormat previousSpawnFormat = map.spawnFormat;
		const std::string previousSpawnFile = map.spawnfile;
		const std::string previousNpcFile = map.spawnNpcFile;
		const std::string previousZoneFile = map.zonefile;
		const bool previousExplicitFilenames = map.spawnFilenamesExplicit;
		if (dialog.ShowModal() == wxID_OK && ConfigureSpawnSaveAs(dialog.GetPath()) && !SaveCurrentMap(dialog.GetPath(), true)) {
			map.mapVersion = previousVersion;
			map.storageFormat = previousStorageFormat;
			map.sourceItemMajorVersion = previousMajorVersion;
			map.sourceItemMinorVersion = previousMinorVersion;
			map.spawnFormat = previousSpawnFormat;
			map.spawnfile = previousSpawnFile;
			map.spawnNpcFile = previousNpcFile;
			map.zonefile = previousZoneFile;
			map.spawnFilenamesExplicit = previousExplicitFilenames;
		}
	}
}

void GUI::SaveMapAs() {
	if (!IsEditorOpen()) {
		return;
	}

	wxString wildcard = g_settings.getInteger(Config::USE_OTGZ) != 0 ? MAP_SAVE_FILE_WILDCARD_OTGZ : MAP_SAVE_FILE_WILDCARD;
	wxFileDialog dialog(root, "Save As...", "", "", wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	Map& map = GetCurrentMap();
	const MapVersion previousVersion = map.mapVersion;
	const MapStorageFormat previousStorageFormat = map.storageFormat;
	const uint32_t previousMajorVersion = map.sourceItemMajorVersion;
	const uint32_t previousMinorVersion = map.sourceItemMinorVersion;
	const SpawnFormat previousSpawnFormat = map.spawnFormat;
	const std::string previousSpawnFile = map.spawnfile;
	const std::string previousNpcFile = map.spawnNpcFile;
	const std::string previousZoneFile = map.zonefile;
	const bool previousExplicitFilenames = map.spawnFilenamesExplicit;
	if (dialog.ShowModal() == wxID_OK && ConfigureSpawnSaveAs(dialog.GetPath())) {
		if (SaveCurrentMap(dialog.GetPath(), true)) {
			UpdateTitle();
			root->menu_bar->AddRecentFile(dialog.GetPath());
			root->UpdateMenubar();
		} else {
			map.mapVersion = previousVersion;
			map.storageFormat = previousStorageFormat;
			map.sourceItemMajorVersion = previousMajorVersion;
			map.sourceItemMinorVersion = previousMinorVersion;
			map.spawnFormat = previousSpawnFormat;
			map.spawnfile = previousSpawnFile;
			map.spawnNpcFile = previousNpcFile;
			map.zonefile = previousZoneFile;
			map.spawnFilenamesExplicit = previousExplicitFilenames;
		}
	}
}

bool GUI::ConfigureSpawnSaveAs(const FileName& mapFilename) {
	Map& map = GetCurrentMap();
	SpawnExportWindow dialog(root, map, mapFilename.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME), mapFilename.GetName(), true);
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}
	const SpawnExportOptions options = dialog.GetOptions();
	map.setSpawnSaveTarget(options.format, options.primaryFilename, options.npcFilename);
	map.setStorageFormat(options.mapFormat);
	map.zonefile = nstr(mapFilename.GetName()) + "-zones.xml";
	MapVersion targetVersion = map.getVersion();
	if (options.mapFormat == MapStorageFormat::CanaryCrystal) {
		targetVersion.otbm = std::max(targetVersion.otbm, MAP_OTBM_5);
		targetVersion.client = g_gui.IsCanaryCrystalAssetsLoaded() ? g_gui.GetCurrentVersionID() : ClientVersion::getLatestVersion()->getID();
		map.setSourceItemVersion(4, 4);
	} else {
		targetVersion.otbm = MAP_OTBM_3;
		targetVersion.client = CLIENT_VERSION_860;
		map.setSourceItemVersion(3, CLIENT_VERSION_860);
	}
	map.mapVersion = targetVersion;
	return true;
}

bool GUI::LoadMap(const FileName& fileName) {
	return LoadMapInternal(fileName, EditorClientVersionPolicy::DetectFromMap);
}

bool GUI::LoadValidatedConvertedMap(const FileName& fileName, const ItemIdCodec* readCodec, bool detachedDecodedView) {
	return LoadMapInternal(fileName, EditorClientVersionPolicy::KeepLoaded, readCodec, detachedDecodedView);
}

bool GUI::LoadMapInternal(const FileName& fileName, EditorClientVersionPolicy clientVersionPolicy, const ItemIdCodec* readCodec, bool detachedDecodedView, bool replaceEmptyEditor) {
	rme::bindPooledObjectOwnerThread();

	FinishWelcomeDialog();

	const bool shouldReplaceEmptyEditor = replaceEmptyEditor && GetCurrentEditor() && !GetCurrentMap().hasChanged() && !GetCurrentMap().hasFile();

	Editor* editor;
	try {
		editor = newd Editor(copybuffer, fileName, clientVersionPolicy, readCodec, detachedDecodedView);
	} catch (std::runtime_error& e) {
		PopupDialog(root, "Error!", wxString(e.what(), wxConvUTF8), wxOK);
		return false;
	}
	if (shouldReplaceEmptyEditor && GetCurrentEditor()) {
		g_gui.CloseCurrentEditor();
	}

	auto* mapTab = newd MapTab(tabbook, editor);
	mapTab->OnSwitchEditorMode(mode);

	if (!detachedDecodedView) {
		root->AddRecentFile(fileName);
	}

	mapTab->GetView()->FitToMap();
	UpdateTitle();
	ListDialog("Map loader errors", mapTab->GetMap()->getWarnings());
	root->DoQueryImportCreatures();

	FitViewToMap(mapTab);
	root->UpdateMenubar();

	std::string path = detachedDecodedView ? std::string() : g_settings.getString(Config::RECENT_EDITED_MAP_PATH);
	if (!path.empty()) {
		FileName file(path);
		if (file == fileName) {
			std::istringstream stream(g_settings.getString(Config::RECENT_EDITED_MAP_POSITION));
			Position position;
			stream >> position;
			mapTab->SetScreenCenterPosition(position);
		}
	}
	return true;
}

Editor* GUI::GetCurrentEditor() {
	MapTab* mapTab = GetCurrentMapTab();
	if (mapTab) {
		return mapTab->GetEditor();
	}
	return nullptr;
}

EditorTab* GUI::GetTab(int idx) {
	return tabbook->GetTab(idx);
}

int GUI::GetTabCount() const {
	return tabbook->GetTabCount();
}

EditorTab* GUI::GetCurrentTab() {
	return tabbook->GetCurrentTab();
}

MapTab* GUI::GetCurrentMapTab() const {
	if (tabbook && tabbook->GetTabCount() > 0) {
		EditorTab* editorTab = tabbook->GetCurrentTab();
		auto* mapTab = dynamic_cast<MapTab*>(editorTab);
		return mapTab;
	}
	return nullptr;
}

Map& GUI::GetCurrentMap() {
	Editor* editor = GetCurrentEditor();
	ASSERT(editor);
	return editor->map;
}

int GUI::GetOpenMapCount() {
	std::set<Map*> open_maps;

	for (int i = 0; i < tabbook->GetTabCount(); ++i) {
		auto* tab = dynamic_cast<MapTab*>(tabbook->GetTab(i));
		if (tab) {
			open_maps.insert(open_maps.begin(), tab->GetMap());
		}
	}

	return static_cast<int>(open_maps.size());
}

bool GUI::ShouldSave() {
	const Map& map = GetCurrentMap();
	if (map.hasChanged()) {
		if (map.getTileCount() == 0) {
			Editor* editor = GetCurrentEditor();
			ASSERT(editor);
			return editor->actionQueue->canUndo();
		}
		return true;
	}
	return false;
}

void GUI::AddPendingCanvasEvent(wxEvent& event) {
	MapTab* mapTab = GetCurrentMapTab();
	if (mapTab) {
		mapTab->GetCanvas()->GetEventHandler()->AddPendingEvent(event);
	}
}

void GUI::CloseCurrentEditor() {
	RefreshPalettes();
	tabbook->DeleteTab(tabbook->GetSelection());
	root->UpdateMenubar();
}

bool GUI::CloseAllEditors(bool querySave) {
	const bool wasClosingAllEditors = closingAllEditors;
	closingAllEditors = true;
	for (int i = 0; i < tabbook->GetTabCount(); ++i) {
		auto* mapTab = dynamic_cast<MapTab*>(tabbook->GetTab(i));
		if (mapTab) {
			if (querySave && mapTab->IsUniqueReference() && mapTab->GetMap() && mapTab->GetMap()->hasChanged()) {
				tabbook->SetFocusedTab(i);
				if (!root->DoQuerySave(false)) {
					closingAllEditors = wasClosingAllEditors;
					return false;
				} else {
					RefreshPalettes();
					tabbook->DeleteTab(i--);
				}
			} else {
				tabbook->DeleteTab(i--);
			}
		}
	}
	if (root) {
		root->UpdateMenubar();
	}
	closingAllEditors = wasClosingAllEditors;
	return true;
}

void GUI::NewMapView() {
	MapTab* mapTab = GetCurrentMapTab();
	if (mapTab) {
		auto* newMapTab = newd MapTab(mapTab);
		newMapTab->OnSwitchEditorMode(mode);

		SetStatusText("Created new view");
		UpdateTitle();
		RefreshPalettes();
		root->UpdateMenubar();
		root->Refresh();
	}
}

void GUI::LoadPerspective() {
	if (!IsVersionLoaded()) {
		if (g_settings.getInteger(Config::WINDOW_MAXIMIZED)) {
			root->Maximize();
		} else {
			root->SetSize(wxSize(
				g_settings.getInteger(Config::WINDOW_WIDTH),
				g_settings.getInteger(Config::WINDOW_HEIGHT)
			));
		}
	} else {
		std::string tmp;
		std::string layout = g_settings.getString(Config::PALETTE_LAYOUT);

		std::vector<std::string> palette_list;
		for (char c : layout) {
			if (c == '|') {
				palette_list.push_back(tmp);
				tmp.clear();
			} else {
				tmp.push_back(c);
			}
		}

		if (!tmp.empty()) {
			palette_list.push_back(tmp);
		}

		for (const std::string& name : palette_list) {
			PaletteWindow* palette = CreatePalette();

			wxAuiPaneInfo& info = aui_manager->GetPane(palette);
			aui_manager->LoadPaneInfo(wxstr(name), info);

			if (info.IsFloatable()) {
				bool offscreen = true;
				for (uint32_t index = 0; index < wxDisplay::GetCount(); ++index) {
					wxDisplay display(index);
					wxRect rect = display.GetClientArea();
					if (rect.Contains(info.floating_pos)) {
						offscreen = false;
						break;
					}
				}

				if (offscreen) {
					info.Dock();
				}
			}
		}

		if (g_settings.getInteger(Config::MINIMAP_VISIBLE)) {
			if (!minimap) {
				wxAuiPaneInfo info;

				const wxString& data = wxstr(g_settings.getString(Config::MINIMAP_LAYOUT));
				aui_manager->LoadPaneInfo(data, info);

				minimap = newd MinimapWindow(root);
				aui_manager->AddPane(minimap, info);
			} else {
				wxAuiPaneInfo& info = aui_manager->GetPane(minimap);

				const wxString& data = wxstr(g_settings.getString(Config::MINIMAP_LAYOUT));
				aui_manager->LoadPaneInfo(data, info);
			}

			wxAuiPaneInfo& info = aui_manager->GetPane(minimap);
			if (info.IsFloatable()) {
				bool offscreen = true;
				for (uint32_t index = 0; index < wxDisplay::GetCount(); ++index) {
					wxDisplay display(index);
					wxRect rect = display.GetClientArea();
					if (rect.Contains(info.floating_pos)) {
						offscreen = false;
						break;
					}
				}

				if (offscreen) {
					info.Dock();
				}
			}
		}

		if (g_settings.getInteger(Config::INGAME_PREVIEW_VISIBLE)) {
			CreateIngamePreview();
			const std::string savedLayout = g_settings.getString(Config::INGAME_PREVIEW_LAYOUT);
			if (ingame_preview && !savedLayout.empty()) {
				wxAuiPaneInfo& info = aui_manager->GetPane(ingame_preview);
				aui_manager->LoadPaneInfo(wxstr(savedLayout), info);
				info.Show(true);
			}
		}

		aui_manager->Update();
		root->UpdateMenubar();
	}

	root->GetAuiToolBar()->LoadPerspective();
}

void GUI::SavePerspective() {
	g_settings.setInteger(Config::WINDOW_MAXIMIZED, root->IsMaximized());
	g_settings.setInteger(Config::WINDOW_WIDTH, root->GetSize().GetWidth());
	g_settings.setInteger(Config::WINDOW_HEIGHT, root->GetSize().GetHeight());

	g_settings.setInteger(Config::MINIMAP_VISIBLE, minimap ? 1 : 0);
	g_settings.setInteger(Config::INGAME_PREVIEW_VISIBLE, IsIngamePreviewVisible() ? 1 : 0);

	wxString pinfo;
	for (auto& palette : palettes) {
		if (aui_manager->GetPane(palette).IsShown()) {
			pinfo << aui_manager->SavePaneInfo(aui_manager->GetPane(palette)) << "|";
		}
	}
	g_settings.setString(Config::PALETTE_LAYOUT, nstr(pinfo));

	if (minimap) {
		wxString s = aui_manager->SavePaneInfo(aui_manager->GetPane(minimap));
		g_settings.setString(Config::MINIMAP_LAYOUT, nstr(s));
	}
	if (ingame_preview) {
		const wxString layout = aui_manager->SavePaneInfo(aui_manager->GetPane(ingame_preview));
		g_settings.setString(Config::INGAME_PREVIEW_LAYOUT, nstr(layout));
	}

	root->GetAuiToolBar()->SavePerspective();
}

SearchResultWindow* GUI::ShowSearchWindow(const wxString& caption /* = "Search Results" */, bool duplicateItems /* = false */) {
	if (search_result_window == nullptr) {
		search_result_window = newd SearchResultWindow(root);
		aui_manager->AddPane(search_result_window, wxAuiPaneInfo().Caption(caption));
	} else {
		aui_manager->GetPane(search_result_window).Caption(caption).Show();
	}
	search_result_window->SetDuplicateMode(duplicateItems);
	aui_manager->Update();
	return search_result_window;
}

//=============================================================================
// Palette Window Interface implementation

PaletteWindow* GUI::GetPalette() {
	if (palettes.empty()) {
		return nullptr;
	}
	return palettes.front();
}

PaletteWindow* GUI::NewPalette() {
	return CreatePalette();
}

FavoritesManager& GUI::GetFavorites() {
	if (!favorites) {
		favorites = std::make_unique<FavoritesManager>(std::filesystem::u8path(GetLocalDataDirectory().ToStdString(wxConvUTF8)) / "favorites.json");
		std::string error;
		favorites->load(error);
		if (!error.empty()) {
			wxLogWarning("Favorites: %s", wxString::FromUTF8(error));
		}
	}
	return *favorites;
}

void GUI::RefreshFavorites() {
	for (auto* palette : palettes) {
		palette->RefreshFavorites();
	}
}

void GUI::RefreshPalettes(Map* m, bool usedefault, bool selectBrush) {
	for (auto& palette : palettes) {
		palette->OnUpdate(m ? m : (usedefault ? (IsEditorOpen() ? &GetCurrentMap() : nullptr) : nullptr));
	}
	if (selectBrush) {
		SelectBrush();
	}
}

void GUI::RefreshOtherPalettes(PaletteWindow* p) {
	for (auto& palette : palettes) {
		if (palette != p) {
			palette->OnUpdate(IsEditorOpen() ? &GetCurrentMap() : nullptr);
		}
	}
	SelectBrush();
}

PaletteWindow* GUI::CreatePalette() {
	if (!IsVersionLoaded()) {
		return nullptr;
	}

	auto* palette = newd PaletteWindow(root, g_materials.tilesets);
	aui_manager->AddPane(palette, wxAuiPaneInfo().Caption("Palette").TopDockable(false).BottomDockable(false));
	aui_manager->Update();

	// Make us the active palette
	palettes.push_front(palette);
	// Select brush from this palette
	SelectBrushInternal(palette->GetSelectedBrush());
	// fix for blank house list on f5 or new palette
	palette->OnUpdate(IsEditorOpen() ? &GetCurrentMap() : nullptr);
	return palette;
}

void GUI::ActivatePalette(PaletteWindow* p) {
	palettes.erase(std::find(palettes.begin(), palettes.end(), p));
	palettes.push_front(p);
}

void GUI::DestroyPalettes() {
	for (auto palette : palettes) {
		aui_manager->DetachPane(palette);
		palette->Destroy();
		palette = nullptr;
	}
	palettes.clear();
	aui_manager->Update();
}

void GUI::RebuildPalettes() {
	// Palette lits might be modified due to active palette changes
	// Use a temporary list for iterating
	PaletteList tmp = palettes;
	for (auto& piter : tmp) {
		piter->ReloadSettings(IsEditorOpen() ? &GetCurrentMap() : nullptr);
	}
	aui_manager->Update();
}

void GUI::ShowPalette() {
	if (palettes.empty()) {
		return;
	}

	for (auto& palette : palettes) {
		if (aui_manager->GetPane(palette).IsShown()) {
			return;
		}
	}

	aui_manager->GetPane(palettes.front()).Show(true);
	aui_manager->Update();
}

void GUI::SelectPalettePage(PaletteType pt) {
	if (palettes.empty()) {
		CreatePalette();
	}
	PaletteWindow* p = GetPalette();
	if (!p) {
		return;
	}

	ShowPalette();
	p->SelectPage(pt);
	aui_manager->Update();
	SelectBrushInternal(p->GetSelectedBrush());
}

//=============================================================================
// Minimap Window Interface Implementation

void GUI::CreateMinimap() {
	if (!IsVersionLoaded()) {
		return;
	}

	if (minimap) {
		aui_manager->GetPane(minimap).Show(true);
	} else {
		minimap = newd MinimapWindow(root);
		minimap->Show(true);
		aui_manager->AddPane(minimap, wxAuiPaneInfo().Caption("Minimap"));
	}
	aui_manager->Update();
}

void GUI::DestroyMinimap() {
	if (minimap) {
		aui_manager->DetachPane(minimap);
		aui_manager->Update();
		minimap->Destroy();
		minimap = nullptr;
	}
}

void GUI::UpdateMinimap(bool immediate) {
	if (IsMinimapVisible()) {
		if (immediate) {
			minimap->Refresh();
		} else {
			minimap->DelayedUpdate();
		}
	}
}

bool GUI::IsMinimapVisible() const {
	if (minimap) {
		const wxAuiPaneInfo& pi = aui_manager->GetPane(minimap);
		if (pi.IsShown()) {
			return true;
		}
	}
	return false;
}

//=============================================================================

void GUI::CreateIngamePreview() {
	if (!IsVersionLoaded() || !aui_manager) {
		return;
	}

	if (ingame_preview) {
		aui_manager->GetPane(ingame_preview).Show(true);
	} else {
		ingame_preview = newd IngamePreviewWindow(root);
		aui_manager->AddPane(
			ingame_preview,
			wxAuiPaneInfo()
				.Name("IngamePreview")
				.Caption("In-game Preview")
				.Right()
				.Dockable(true)
				.CloseButton(true)
				.BestSize(FROM_DIP(root, wxSize(500, 430)))
				.MinSize(FROM_DIP(root, wxSize(500, 430)))
		);
	}
	aui_manager->Update();
	UpdateIngamePreview();
}

void GUI::DestroyIngamePreview() {
	if (!ingame_preview) {
		return;
	}
	if (aui_manager) {
		aui_manager->DetachPane(ingame_preview);
		aui_manager->Update();
	}
	IngamePreviewWindow* window = ingame_preview;
	ingame_preview = nullptr;
	window->Destroy();
}

void GUI::UpdateIngamePreview() {
	if (IsIngamePreviewVisible()) {
		ingame_preview->UpdateState();
	}
}

void GUI::ReleaseIngamePreviewEditor(Editor* editor) {
	if (ingame_preview) {
		ingame_preview->ReleaseEditor(editor);
	}
}

bool GUI::IsIngamePreviewVisible() const {
	return ingame_preview && aui_manager && aui_manager->GetPane(ingame_preview).IsShown();
}

//=============================================================================

void GUI::RefreshView() {
	EditorTab* editorTab = GetCurrentTab();
	if (!editorTab) {
		return;
	}

	if (!dynamic_cast<MapTab*>(editorTab)) {
		editorTab->GetWindow()->Refresh();
		return;
	}

	std::vector<EditorTab*> editorTabs;
	for (int32_t index = 0; index < tabbook->GetTabCount(); ++index) {
		auto* mapTab = dynamic_cast<MapTab*>(tabbook->GetTab(index));
		if (mapTab) {
			editorTabs.push_back(mapTab);
		}
	}

	for (EditorTab* editorTab : editorTabs) {
		// Call GetCanvas()->Refresh() directly to ensure the FBO cache is invalidated.
		// wxPanel::Refresh() does not call the custom MapCanvas::Refresh() method,
		// which means drawer->markDirty() is never executed, causing the FBO cache
		// to remain stale when view settings (like show_special_tiles) change.
		auto* mapTab = dynamic_cast<MapTab*>(editorTab);
		if (mapTab && mapTab->GetView() && mapTab->GetView()->GetCanvas()) {
			mapTab->GetView()->GetCanvas()->Refresh();
		}
	}
}

void GUI::CreateLoadBar(wxString message, bool canCancel /* = false */) {
	progressText = std::move(message);

	progressFrom = 0;
	progressTo = 100;
	currentProgress = -1;
	lastProgressPump = std::chrono::steady_clock::now();
	progressUpdating = false;
	destroyPending = false;

	progressBar = new wxProgressDialog(
		"Loading",
		progressText + " (0%)",
		100,
		root,
		wxPD_APP_MODAL | wxPD_SMOOTH | (canCancel ? wxPD_CAN_ABORT : 0)
	);

	progressBar->SetSize(280, -1);
	progressBar->Show(true);

	progressUpdating = true;
	progressBar->Update(0);
	progressUpdating = false;

	if (destroyPending) {
		DestroyLoadBar();
	}
}

void GUI::SetLoadScale(int32_t from, int32_t to) {
	progressFrom = from;
	progressTo = to;
}

bool GUI::SetLoadDone(int32_t done, const wxString& newMessage) {
	if (done == 100) {
		DestroyLoadBar();
		return true;
	}

	const bool messageChanged = !newMessage.empty() && newMessage != progressText;
	if (messageChanged) {
		progressText = newMessage;
	}

	// currentProgress stores the scaled value, so the throttle has to compare
	// against the scaled value too. Under a SetLoadScale range the raw `done`
	// and currentProgress live in different spaces and never line up.
	int32_t newProgress = progressFrom + static_cast<int32_t>((done / 100.f) * (progressTo - progressFrom));

	newProgress = std::max<int32_t>(
		0,
		std::min<int32_t>(100, newProgress)
	);

	// Avoid excessive progress updates during very large map loads.
	// Update immediately when percentage or message changes, otherwise allow a refresh
	// every 250 ms so Windows continues receiving UI updates.
	const auto now = std::chrono::steady_clock::now();

	if (
		!messageChanged && newProgress == currentProgress && now - lastProgressPump < std::chrono::milliseconds(250)
	) {
		return true;
	}

	if (!progressBar) {
		return true;
	}

	// Prevent recursive entry if Update() causes event processing/reentrancy.
	if (progressUpdating) {
		return true;
	}

	progressUpdating = true;

	const bool shouldContinue = progressBar->Update(
		newProgress,
		wxString::Format("%s (%d%%)", progressText, newProgress)
	);

	currentProgress = newProgress;
	lastProgressPump = now;
	progressUpdating = false;

	if (destroyPending) {
		DestroyLoadBar();
	}

	return shouldContinue;
}

void GUI::DestroyLoadBar() {
	if (!progressBar) {
		return;
	}

	if (progressUpdating) {
		destroyPending = true;
		return;
	}

	destroyPending = false;
	currentProgress = -1;
	progressUpdating = false;

	// The native Windows progress dialog cannot be hidden. Destroy() defers
	// destruction, leaving its owner disabled while the caller creates/focuses
	// the next dialog or map tab. Update() has returned here (guarded above),
	// so destroy synchronously to restore the owner before continuing.
	wxProgressDialog* completedProgress = progressBar;
	progressBar = nullptr;
	delete completedProgress;

	if (root && !closingApplication) {
		if (root->IsActive()) {
			root->Raise();
		} else {
			root->RequestUserAttention();
		}
	}
}

void GUI::ShowWelcomeDialog(const wxBitmap& icon) {
	if (closingApplication) {
		return;
	}
	std::vector<wxString> recent_files = root->GetRecentFiles();
	welcomeDialog = newd WelcomeDialog(__W_RME_APPLICATION_NAME__, "Version " + __W_RME_VERSION__, FROM_DIP(root, wxSize(1000, 650)), icon, recent_files);
	welcomeDialog->Bind(wxEVT_CLOSE_WINDOW, &GUI::OnWelcomeDialogClosed, this);
	welcomeDialog->Bind(WELCOME_DIALOG_ACTION, &GUI::OnWelcomeDialogAction, this);
	welcomeDialog->Show();
	UpdateMenubar();
}

void GUI::FinishWelcomeDialog(bool showMainWindow) {
	if (welcomeDialog != nullptr) {
		welcomeDialog->Hide();
		if (showMainWindow) {
			root->Show();
		}
		welcomeDialog->Destroy();
		welcomeDialog = nullptr;
	}
}

bool GUI::IsWelcomeDialogShown() {
	return welcomeDialog != nullptr && welcomeDialog->IsShown();
}

void GUI::OnWelcomeDialogClosed(wxCloseEvent& event) {
	welcomeDialog->Destroy();
	welcomeDialog = nullptr;
	root->Close();
}

void GUI::OnWelcomeDialogAction(wxCommandEvent& event) {
	if (closingApplication) {
		return;
	}
	auto loadWorkspace = [&]() {
		wxString error;
		wxArrayString warnings;
		if (!LoadWorkspace(error, warnings)) {
			PopupDialog(welcomeDialog, "Workspace not ready", error, wxOK);
			return false;
		}
		if (!warnings.empty()) {
			ListDialog("Workspace warnings", warnings);
		}
		return true;
	};

	if (event.GetId() == WELCOME_DIALOG_OPEN_WORKSPACE) {
		if (loadWorkspace()) {
			const wxString primaryMap = WorkspacePath(g_workspace.getServer().primaryMapPath);
			if (!primaryMap.empty()) {
				LoadMapInternal(FileName(primaryMap), EditorClientVersionPolicy::KeepLoaded);
			} else {
				NewMap();
			}
		}
	} else if (event.GetId() == wxID_NEW) {
		if ((g_workspace.getClient().valid || g_workspace.hasServerSelection()) && !loadWorkspace()) {
			return;
		}
		NewMap();
	} else if (event.GetId() == wxID_OPEN) {
		if (event.GetInt() == 1) {
			const std::optional<DetectedMap> detectedMap = g_workspace.getDetectedMap(event.GetString());
			if (!detectedMap || detectedMap->serverType == ServerType::UnknownGeneric) {
				// Unknown folders follow the established manual-opening path. They
				// never inherit the previously selected Canary/Crystal workspace.
				LoadMap(FileName(event.GetString()));
				return;
			}

			wxString error;
			if (!g_workspace.selectDetectedMap(event.GetString(), error)) {
				PopupDialog(welcomeDialog, "Server not supported", error, wxOK);
				return;
			}
			if (loadWorkspace()) {
				LoadMapInternal(FileName(event.GetString()), EditorClientVersionPolicy::KeepLoaded);
			}
		} else {
			LoadMap(FileName(event.GetString()));
		}
	} else if (event.GetId() == WELCOME_DIALOG_MAP_CONVERTER) {
		static_cast<void>(RunMapItemIdConverter(welcomeDialog, MapItemIdConverterLaunchContext::Welcome));
	} else if (event.GetId() == WELCOME_DIALOG_SPAWN_CONVERTER) {
		static_cast<void>(RunSpawnConverter(welcomeDialog));
	}
}

void GUI::UpdateMenubar() {
	root->UpdateMenubar();
}

void GUI::SetScreenCenterPosition(const Position& position, bool showIndicator) {
	MapTab* mapTab = GetCurrentMapTab();
	if (mapTab) {
		mapTab->SetScreenCenterPosition(position, showIndicator);
	}
}

void GUI::DoCut() {
	if (!IsSelectionMode()) {
		return;
	}

	Editor* editor = GetCurrentEditor();
	if (!editor) {
		return;
	}

	editor->copybuffer.cut(*editor, GetCurrentFloor());
	RefreshView();
	root->UpdateMenubar();
}

void GUI::DoCopy() {
	if (!IsSelectionMode()) {
		return;
	}

	Editor* editor = GetCurrentEditor();
	if (!editor) {
		return;
	}

	editor->copybuffer.copy(*editor, GetCurrentFloor());
	RefreshView();
	root->UpdateMenubar();
}

void GUI::DoPaste() {
	MapTab* mapTab = GetCurrentMapTab();
	if (!mapTab) {
		return;
	}
	Editor* editor = mapTab->GetEditor();
	if (!PrepareCrossClientPaste(*editor)) {
		return;
	}
	if (editor->copybuffer.canPaste()) {
		editor->copybuffer.paste(*editor, mapTab->GetCanvas()->GetCursorPosition());
	}
}

void GUI::PreparePaste() {
	Editor* editor = GetCurrentEditor();
	if (editor) {
		if (!PrepareCrossClientPaste(*editor) || !editor->copybuffer.canPaste()) {
			return;
		}
		SetSelectionMode();
		editor->selection.start();
		editor->selection.clear();
		editor->selection.finish();
		StartPasting();
		RefreshView();
	}
}

bool GUI::CanPaste() const {
	if (copybuffer.canPaste()) {
		return true;
	}
	return crossClientClipboard && crossClientClipboard->canPaste()
		&& !crossClientClipboard->isFromSession(GetActiveEditorResourceSession());
}

void GUI::CaptureCrossClientCopy(CopyBuffer& source) {
	if (!crossClientClipboard) {
		crossClientClipboard = std::make_unique<CrossClientClipboard>();
	}
	wxString error;
	if (!crossClientClipboard->capture(source, GetActiveEditorResourceSession(), error) && !error.empty()) {
		wxLogWarning("Could not update the cross-client clipboard: " + error);
	}
}

bool GUI::PrepareCrossClientPaste(Editor& editor) {
	const EditorResourceSessionPtr activeSession = GetActiveEditorResourceSession();
	if (!crossClientClipboard || !crossClientClipboard->canPaste() || crossClientClipboard->isFromSession(activeSession)) {
		return editor.copybuffer.canPaste();
	}

	wxProgressDialog progress(
		"Cross-Client Paste",
		"Comparing destination sprites and item metadata...",
		100,
		root,
		wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_SMOOTH | wxPD_AUTO_HIDE
	);
	wxString error;
	CrossClientPasteAnalysis analysis = crossClientClipboard->analyze(
		activeSession,
		[&](size_t current, size_t total) {
			const int percent = total == 0 ? 100 : static_cast<int>((current * 100) / total);
			return progress.Update(std::clamp(percent, 0, 100));
		},
		error
	);
	if (!error.empty()) {
		if (error != "Cross-client comparison was cancelled.") {
			PopupDialog(root, "Cross-Client Paste", error, wxOK | wxICON_ERROR);
		}
		return false;
	}

	CrossClientPasteDialog dialog(root, analysis);
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}
	analysis = dialog.GetAnalysis();
	if (!crossClientClipboard->apply(analysis, editor.copybuffer, error)) {
		PopupDialog(root, "Cross-Client Paste", error, wxOK | wxICON_ERROR);
		return false;
	}
	SetStatusText(wxString::Format("Cross-client paste ready: %u matched, %u remapped.", analysis.matched, analysis.remapped));
	return true;
}

void GUI::StartPasting() {
	if (GetCurrentEditor()) {
		pasting = true;
		secondary_map = &copybuffer.getBufferMap();
		const uint64_t tileCount = copybuffer.getTileCount();
		SetStatusText(wxString::Format("Paste ready: %llu tiles. Move the outline and click the map to place the complete area.", static_cast<unsigned long long>(tileCount)));
	}
}

void GUI::EndPasting() {
	if (pasting) {
		pasting = false;
		secondary_map = nullptr;
	}
}

bool GUI::DoUndo() {
	Editor* editor = GetCurrentEditor();
	if (editor && editor->actionQueue->canUndo()) {
		const ActionIdentifier action_type = editor->actionQueue->getUndoType();
		const bool refreshDynamicPalettes = action_type == ACTION_ZONE_EDIT || action_type == ACTION_PASTE_TILES;
		if (!editor->actionQueue->undo()) {
			SetStatusText("Undo cancelled: the pasted House was changed or removed.");
			root->UpdateMenubar();
			return false;
		}
		InvalidateAutoborderPreview();
		if (refreshDynamicPalettes) {
			RefreshPalettes(nullptr, true, false);
		}
		if (editor->selection.size() > 0) {
			SetSelectionMode();
		}
		SetStatusText("Undo action");
		UpdateMinimap();
		root->UpdateMenubar();
		root->Refresh();
		return true;
	}
	return false;
}

bool GUI::DoRedo() {
	Editor* editor = GetCurrentEditor();
	if (editor && editor->actionQueue->canRedo()) {
		const ActionIdentifier action_type = editor->actionQueue->getRedoType();
		const bool refreshDynamicPalettes = action_type == ACTION_ZONE_EDIT || action_type == ACTION_PASTE_TILES;
		if (!editor->actionQueue->redo()) {
			SetStatusText("Redo cancelled: a House id needed by the paste is already in use.");
			root->UpdateMenubar();
			return false;
		}
		InvalidateAutoborderPreview();
		if (refreshDynamicPalettes) {
			RefreshPalettes(nullptr, true, false);
		}
		if (editor->selection.size() > 0) {
			SetSelectionMode();
		}
		SetStatusText("Redo action");
		UpdateMinimap();
		root->UpdateMenubar();
		root->Refresh();
		return true;
	}
	return false;
}

int GUI::GetCurrentFloor() {
	MapTab* tab = GetCurrentMapTab();
	ASSERT(tab);
	return tab->GetCanvas()->GetFloor();
}

void GUI::ChangeFloor(int new_floor) {
	MapTab* tab = GetCurrentMapTab();
	if (tab) {
		int old_floor = GetCurrentFloor();
		if (new_floor < 0 || new_floor > MAP_MAX_LAYER) {
			return;
		}

		if (old_floor != new_floor) {
			tab->GetCanvas()->ChangeFloor(new_floor);
		}
	}
}

void GUI::SetStatusText(const wxString& text) {
	g_gui.root->SetStatusText(text, 0);
}

void GUI::SetTitle(wxString title) {
	if (g_gui.root == nullptr) {
		return;
	}

#ifdef NIGHTLY_BUILD
	#ifdef SVN_BUILD
		#define TITLE_APPEND (wxString(" (Nightly Build #") << i2ws(SVN_BUILD) << ")")
	#else
		#define TITLE_APPEND (wxString(" (Nightly Build)"))
	#endif
#else
	#ifdef SVN_BUILD
		#define TITLE_APPEND (wxString(" (Build #") << i2ws(SVN_BUILD) << ")")
	#else
		#define TITLE_APPEND (wxString(""))
	#endif
#endif
	wxString applicationTitle = __W_RME_APPLICATION_NAME__;
#ifdef __EXPERIMENTAL__
	applicationTitle << " BETA";
#elif defined(__SNAPSHOT__)
	applicationTitle << " - SNAPSHOT";
#endif
	if (!title.empty()) {
		g_gui.root->SetTitle(title << " - " << applicationTitle << TITLE_APPEND);
	} else {
		g_gui.root->SetTitle(applicationTitle << TITLE_APPEND);
	}
}

void GUI::UpdateTitle() {
	if (!tabbook) {
		SetTitle("");
		return;
	}

	EditorTab* current = tabbook->GetCurrentTab();
	SetTitle(current ? current->GetTitle() : wxString {});
	for (int idx = 0; idx < tabbook->GetTabCount(); ++idx) {
		if (EditorTab* tab = tabbook->GetTab(idx)) {
			tabbook->SetTabLabel(idx, tab->GetTitle());
		}
	}
}

void GUI::UpdateMenus() {
	wxCommandEvent evt(EVT_UPDATE_MENUS);
	g_gui.root->AddPendingEvent(evt);
}

void GUI::ShowToolbar(ToolBarID id, bool show) {
	if (root && root->GetAuiToolBar()) {
		root->GetAuiToolBar()->Show(id, show);
	}
}

void GUI::SwitchMode() {
	if (mode == DRAWING_MODE) {
		SetSelectionMode();
	} else {
		SetDrawingMode();
	}
}

void GUI::SetSelectionMode() {
	if (mode == SELECTION_MODE) {
		return;
	}

	if (g_autoborder_preview.Owns(secondary_map)) {
		g_autoborder_preview.Clear();
		secondary_map = nullptr;
	} else if (current_brush && current_brush->isDoodad()) {
		secondary_map = nullptr;
	}

	tabbook->OnSwitchEditorMode(SELECTION_MODE);
	mode = SELECTION_MODE;
}

void GUI::SetDrawingMode(bool preserveSelection) {
	if (!preserveSelection) {
		std::set<MapTab*> al;
		for (int idx = 0; idx < tabbook->GetTabCount(); ++idx) {
			EditorTab* editorTab = tabbook->GetTab(idx);
			if (auto* mapTab = dynamic_cast<MapTab*>(editorTab)) {
				if (al.find(mapTab) != al.end()) {
					continue;
				}

				Editor* editor = mapTab->GetEditor();
				editor->selection.start();
				editor->selection.clear();
				editor->selection.finish();
				al.insert(mapTab);
			}
		}
	}

	if (mode == DRAWING_MODE) {
		return;
	}

	if (current_brush && current_brush->isDoodad()) {
		secondary_map = doodad_buffer_map;
	} else {
		secondary_map = nullptr;
	}

	tabbook->OnSwitchEditorMode(DRAWING_MODE);
	mode = DRAWING_MODE;
}

void GUI::SetBrushSizeInternal(int nz) {
	if (nz != brush_size && current_brush && current_brush->isDoodad() && !current_brush->oneSizeFitsAll()) {
		brush_size = nz;
		FillDoodadPreviewBuffer();
		secondary_map = doodad_buffer_map;
	} else {
		brush_size = nz;
	}
}

void GUI::SetBrushSize(int nz) {
	SetBrushSizeInternal(nz);

	for (auto& palette : palettes) {
		palette->OnUpdateBrushSize(brush_shape, brush_size);
	}

	root->GetAuiToolBar()->UpdateBrushSize(brush_shape, brush_size);
	RefreshAutoborderPreview();
}

void GUI::SetBrushVariation(int nz) {
	if (nz != brush_variation && current_brush && current_brush->isDoodad()) {
		// Monkey!
		brush_variation = nz;
		FillDoodadPreviewBuffer();
		secondary_map = doodad_buffer_map;
	}
}

void GUI::SetBrushShape(BrushShape bs) {
	if (bs != brush_shape && current_brush && current_brush->isDoodad() && !current_brush->oneSizeFitsAll()) {
		// Donkey!
		brush_shape = bs;
		FillDoodadPreviewBuffer();
		secondary_map = doodad_buffer_map;
	}
	brush_shape = bs;

	for (auto& palette : palettes) {
		palette->OnUpdateBrushSize(brush_shape, brush_size);
	}

	root->GetAuiToolBar()->UpdateBrushSize(brush_shape, brush_size);
	RefreshAutoborderPreview();
}

void GUI::SetBrushThickness(bool on, int x, int y) {
	use_custom_thickness = on;

	if (x != -1 || y != -1) {
		custom_thickness_mod = float(max(x, 1)) / float(max(y, 1));
	}

	if (current_brush && current_brush->isDoodad()) {
		FillDoodadPreviewBuffer();
	}

	RefreshView();
}

void GUI::SetBrushThickness(int low, int ceil) {
	custom_thickness_mod = float(max(low, 1)) / float(max(ceil, 1));

	if (use_custom_thickness && current_brush && current_brush->isDoodad()) {
		FillDoodadPreviewBuffer();
	}

	RefreshView();
}

void GUI::DecreaseBrushSize(bool wrap) {
	switch (brush_size) {
		case 0: {
			if (wrap) {
				SetBrushSize(11);
			}
			break;
		}
		case 1: {
			SetBrushSize(0);
			break;
		}
		case 2:
		case 3: {
			SetBrushSize(1);
			break;
		}
		case 4:
		case 5: {
			SetBrushSize(2);
			break;
		}
		case 6:
		case 7: {
			SetBrushSize(4);
			break;
		}
		case 8:
		case 9:
		case 10: {
			SetBrushSize(6);
			break;
		}
		case 11:
		default: {
			SetBrushSize(8);
			break;
		}
	}
}

void GUI::IncreaseBrushSize(bool wrap) {
	switch (brush_size) {
		case 0: {
			SetBrushSize(1);
			break;
		}
		case 1: {
			SetBrushSize(2);
			break;
		}
		case 2:
		case 3: {
			SetBrushSize(4);
			break;
		}
		case 4:
		case 5: {
			SetBrushSize(6);
			break;
		}
		case 6:
		case 7: {
			SetBrushSize(8);
			break;
		}
		case 8:
		case 9:
		case 10: {
			SetBrushSize(11);
			break;
		}
		case 11:
		default: {
			if (wrap) {
				SetBrushSize(0);
			}
			break;
		}
	}
}

void GUI::SetDoorLocked(bool on) {
	draw_locked_doors = on;
	RefreshView();
}

bool GUI::HasDoorLocked() {
	return draw_locked_doors;
}

Brush* GUI::GetCurrentBrush() const {
	return current_brush;
}

BrushShape GUI::GetBrushShape() const {
	if (current_brush == spawn_brush) {
		return BRUSHSHAPE_SQUARE;
	}

	return brush_shape;
}

int GUI::GetBrushSize() const {
	return brush_size;
}

int GUI::GetBrushVariation() const {
	return brush_variation;
}

int GUI::GetSpawnTime() const {
	return creature_spawntime;
}

void GUI::SelectBrush() {
	if (palettes.empty()) {
		return;
	}

	SelectBrushInternal(palettes.front()->GetSelectedBrush());

	RefreshView();
}

bool GUI::SelectBrush(const Brush* whatbrush, PaletteType primary) {
	if (palettes.empty()) {
		if (!CreatePalette()) {
			return false;
		}
	}

	if (!palettes.front()->OnSelectBrush(whatbrush, primary)) {
		return false;
	}

	SelectBrushInternal(const_cast<Brush*>(whatbrush));
	root->GetAuiToolBar()->UpdateBrushButtons();
	return true;
}

void GUI::SelectBrushInternal(Brush* brush) {
	// Fear no evil don't you say no evil
	if (current_brush != brush && brush) {
		previous_brush = current_brush;
	}

	current_brush = brush;
	if (!current_brush) {
		return;
	}

	brush_variation = min(brush_variation, brush->getMaxVariation());
	FillDoodadPreviewBuffer();
	if (brush->isDoodad()) {
		secondary_map = doodad_buffer_map;
	}

	SetDrawingMode(current_brush->isZone());
	InvalidateAutoborderPreview();
	RefreshView();
}

void GUI::RefreshAutoborderPreview() {
	MapTab* mapTab = GetCurrentMapTab();
	if (mapTab && mapTab->GetView() && mapTab->GetView()->GetCanvas()) {
		mapTab->GetView()->GetCanvas()->UpdateAutoborderPreview(wxGetKeyState(WXK_ALT));
	}
}

void GUI::InvalidateAutoborderPreview() {
	g_autoborder_preview.Invalidate();
	RefreshAutoborderPreview();
}

void GUI::SelectPreviousBrush() {
	if (previous_brush) {
		SelectBrush(previous_brush);
	}
}

void GUI::FillDoodadPreviewBuffer() {
	if (!current_brush || !current_brush->isDoodad()) {
		return;
	}

	doodad_buffer_map->clear();

	DoodadBrush* brush = current_brush->asDoodad();
	if (brush->isEmpty(GetBrushVariation())) {
		return;
	}

	int object_count = 0;
	int area;
	if (GetBrushShape() == BRUSHSHAPE_SQUARE) {
		area = 2 * GetBrushSize();
		area = area * area + 1;
	} else {
		if (GetBrushSize() == 1) {
			// There is a huge deviation here with the other formula.
			area = 5;
		} else {
			area = int(0.5 + GetBrushSize() * GetBrushSize() * PI);
		}
	}
	const int object_range = (use_custom_thickness ? int(area * custom_thickness_mod) : brush->getThickness() * area / max(1, brush->getThicknessCeiling()));
	const int final_object_count = max(1, object_range + random(object_range));

	Position center_pos(0x8000, 0x8000, 0x8);

	if (brush_size > 0 && !brush->oneSizeFitsAll()) {
		while (object_count < final_object_count) {
			int retries = 0;
			bool exit = false;

			// Try to place objects 5 times
			while (retries < 5 && !exit) {

				int pos_retries = 0;
				int xpos = 0, ypos = 0;
				bool found_pos = false;
				if (GetBrushShape() == BRUSHSHAPE_CIRCLE) {
					while (pos_retries < 5 && !found_pos) {
						xpos = random(-brush_size, brush_size);
						ypos = random(-brush_size, brush_size);
						float distance = sqrt(float(xpos * xpos) + float(ypos * ypos));
						if (distance < g_gui.GetBrushSize() + 0.005) {
							found_pos = true;
						} else {
							++pos_retries;
						}
					}
				} else {
					found_pos = true;
					xpos = random(-brush_size, brush_size);
					ypos = random(-brush_size, brush_size);
				}

				if (!found_pos) {
					++retries;
					continue;
				}

				// Decide whether the zone should have a composite or several single objects.
				bool fail = false;
				if (random(brush->getTotalChance(GetBrushVariation())) <= brush->getCompositeChance(GetBrushVariation())) {
					// Composite
					const CompositeTileList& composites = brush->getComposite(GetBrushVariation());

					// Figure out if the placement is valid
					for (const auto& composite : composites) {
						Position pos = center_pos + composite.first + Position(xpos, ypos, 0);
						if (Tile* tile = doodad_buffer_map->getTile(pos)) {
							if (!tile->empty()) {
								fail = true;
								break;
							}
						}
					}
					if (fail) {
						++retries;
						break;
					}

					// Transfer items to the stack
					for (const auto& composite : composites) {
						Position pos = center_pos + composite.first + Position(xpos, ypos, 0);
						const ItemVector& items = composite.second;
						Tile* tile = doodad_buffer_map->getTile(pos);

						if (!tile) {
							tile = doodad_buffer_map->allocator(doodad_buffer_map->createTileL(pos));
						}

						for (auto item : items) {
							tile->addItem(item->deepCopy());
						}
						doodad_buffer_map->setTile(tile->getPosition(), tile);
					}
					exit = true;
				} else if (brush->hasSingleObjects(GetBrushVariation())) {
					Position pos = center_pos + Position(xpos, ypos, 0);
					Tile* tile = doodad_buffer_map->getTile(pos);
					if (tile) {
						if (!tile->empty()) {
							fail = true;
							break;
						}
					} else {
						tile = doodad_buffer_map->allocator(doodad_buffer_map->createTileL(pos));
					}
					int variation = GetBrushVariation();
					brush->draw(doodad_buffer_map, tile, &variation);
					// std::cout << "\tpos: " << tile->getPosition() << std::endl;
					doodad_buffer_map->setTile(tile->getPosition(), tile);
					exit = true;
				}
				if (fail) {
					++retries;
					break;
				}
			}
			++object_count;
		}
	} else {
		if (brush->hasCompositeObjects(GetBrushVariation()) && random(brush->getTotalChance(GetBrushVariation())) <= brush->getCompositeChance(GetBrushVariation())) {
			// Composite
			const CompositeTileList& composites = brush->getComposite(GetBrushVariation());

			// All placement is valid...

			// Transfer items to the buffer
			for (const auto& composite : composites) {
				Position pos = center_pos + composite.first;
				const ItemVector& items = composite.second;
				Tile* tile = doodad_buffer_map->allocator(doodad_buffer_map->createTileL(pos));
				// std::cout << pos << " = " << center_pos << " + " << buffer_tile->getPosition() << std::endl;

				for (auto item : items) {
					tile->addItem(item->deepCopy());
				}
				doodad_buffer_map->setTile(tile->getPosition(), tile);
			}
		} else if (brush->hasSingleObjects(GetBrushVariation())) {
			Tile* tile = doodad_buffer_map->allocator(doodad_buffer_map->createTileL(center_pos));
			int variation = GetBrushVariation();
			brush->draw(doodad_buffer_map, tile, &variation);
			doodad_buffer_map->setTile(center_pos, tile);
		}
	}
}

long GUI::PopupDialog(wxWindow* parent, const wxString& title, const wxString& text, long style, const wxString& confisavename, uint32_t configsavevalue) {
	if (text.empty()) {
		return wxID_ANY;
	}

	wxMessageDialog dlg(parent, text, title, style);
	return dlg.ShowModal();
}

long GUI::PopupDialog(wxString title, wxString text, long style, wxString configsavename, uint32_t configsavevalue) {
	return g_gui.PopupDialog(g_gui.root, title, text, style, configsavename, configsavevalue);
}

void GUI::ListDialog(wxWindow* parent, const wxString& title, const wxArrayString& param_items) {
	if (param_items.empty()) {
		return;
	}

	wxArrayString list_items(param_items);

	// Create the window
	auto* dlg = newd wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxRESIZE_BORDER | wxCAPTION | wxCLOSE_BOX);

	wxSizer* sizer = newd wxBoxSizer(wxVERTICAL);
	auto* item_list = newd wxListBox(dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SINGLE);
	item_list->SetMinSize(wxSize(500, 300));

	for (size_t i = 0; i != list_items.GetCount();) {
		wxString str = list_items[i];
		size_t pos = str.find("\n");
		if (pos != wxString::npos) {
			// Split string!
			item_list->Append(str.substr(0, pos));
			list_items[i] = str.substr(pos + 1);
			continue;
		}
		item_list->Append(list_items[i]);
		++i;
	}
	sizer->Add(item_list, 1, wxEXPAND);

	wxSizer* stdsizer = newd wxBoxSizer(wxHORIZONTAL);
	auto* copyButton = newd wxButton(dlg, wxID_COPY, "Copy");
	copyButton->Bind(wxEVT_BUTTON, [item_list](wxCommandEvent&) {
		wxString text;
		for (unsigned int index = 0; index < item_list->GetCount(); ++index) {
			if (!text.empty()) {
				text += "\n";
			}
			text += item_list->GetString(index);
		}
		if (!text.empty() && wxTheClipboard->Open()) {
			wxTheClipboard->SetData(newd wxTextDataObject(text));
			wxTheClipboard->Close();
		}
	});
	stdsizer->Add(copyButton, wxSizerFlags(0).Center().Border(wxRIGHT, 8));
	stdsizer->Add(newd wxButton(dlg, wxID_OK, "OK"), wxSizerFlags(0).Center());
	sizer->Add(stdsizer, wxSizerFlags(0).Center());

	dlg->SetSizerAndFit(sizer);

	// Show the window
	dlg->ShowModal();
	delete dlg;
}

void GUI::ShowTextBox(wxWindow* parent, const wxString& title, const wxString& content) {
	wxDialog dlg(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxRESIZE_BORDER | wxCAPTION | wxCLOSE_BOX);
	dlg.SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);
	auto* text_field = newd wxTextCtrl(&dlg, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
	text_field->SetBackgroundColour(Theme::Get(Theme::Role::Background));
	text_field->SetForegroundColour(Theme::Get(Theme::Role::Text));
	text_field->SetFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE));
	text_field->ChangeValue(content.empty() ? wxString("No text content was provided.") : content);
	text_field->SetInsertionPoint(0);
	text_field->ShowPosition(0);
	text_field->SetMinSize(wxSize(400, 550));
	topsizer->Add(text_field, wxSizerFlags(5).Expand());

	wxSizer* choicesizer = newd wxBoxSizer(wxHORIZONTAL);
	choicesizer->Add(newd wxButton(&dlg, wxID_CANCEL, "OK"), wxSizerFlags(1).Center());
	topsizer->Add(choicesizer, wxSizerFlags(0).Center());
	dlg.SetSizerAndFit(topsizer);

	dlg.ShowModal();
}

void GUI::SetHotkey(int index, Hotkey& hotkey) {
	ASSERT(index >= 0 && index <= 9);
	hotkeys[index] = hotkey;
	SetStatusText("Set hotkey " + i2ws(index) + ".");
}

const Hotkey& GUI::GetHotkey(int index) const {
	ASSERT(index >= 0 && index <= 9);
	return hotkeys[index];
}

void GUI::SaveHotkeys() const {
	std::ostringstream os;
	for (const auto& hotkey : hotkeys) {
		os << hotkey << '\n';
	}
	g_settings.setString(Config::NUMERICAL_HOTKEYS, os.str());
}

void GUI::LoadHotkeys() {
	std::istringstream is;
	is.str(g_settings.getString(Config::NUMERICAL_HOTKEYS));

	std::string line;
	int index = 0;
	while (getline(is, line)) {
		std::istringstream line_is;
		line_is.str(line);
		line_is >> hotkeys[index];

		++index;
	}
}

Hotkey::Hotkey() :
	type(NONE) {
	////
}

Hotkey::Hotkey(Position _pos) :
	type(POSITION), pos(_pos) {
	////
}

Hotkey::Hotkey(Brush* brush) :
	type(BRUSH), brushname(brush->getName()) {
	////
}

Hotkey::Hotkey(std::string _name) :
	type(BRUSH), brushname(std::move(_name)) {
	////
}

Hotkey::~Hotkey() {
	////
}

std::ostream& operator<<(std::ostream& os, const Hotkey& hotkey) {
	switch (hotkey.type) {
		case Hotkey::POSITION: {
			os << "pos:{" << hotkey.pos << "}";
		} break;
		case Hotkey::BRUSH: {
			if (hotkey.brushname.find('{') != std::string::npos || hotkey.brushname.find('}') != std::string::npos) {
				break;
			}
			os << "brush:{" << hotkey.brushname << "}";
		} break;
		default: {
			os << "none:{}";
		} break;
	}
	return os;
}

std::istream& operator>>(std::istream& is, Hotkey& hotkey) {
	std::string type;
	getline(is, type, ':');
	if (type == "none") {
		is.ignore(2); // ignore "{}"
	} else if (type == "pos") {
		is.ignore(1); // ignore "{"
		Position pos;
		is >> pos;
		hotkey = Hotkey(pos);
		is.ignore(1); // ignore "}"
	} else if (type == "brush") {
		is.ignore(1); // ignore "{"
		std::string brushname;
		getline(is, brushname, '}');
		hotkey = Hotkey(brushname);
	} else {
		// Do nothing...
	}

	return is;
}

void SetWindowToolTip(wxWindow* a, const wxString& tip) {
	a->SetToolTip(tip);
}

void SetWindowToolTip(wxWindow* a, wxWindow* b, const wxString& tip) {
	a->SetToolTip(tip);
	b->SetToolTip(tip);
}
