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

#include "application.h"
#include "editor.h"
#include "common_windows.h"
#include "preferences.h"
#include "main_menubar.h"
#include "artprovider.h"
#include "theme.h"

#include "materials.h"
#include "map.h"
#include "map_tab.h"

#include <cstdlib>
#include <filesystem>
#include <set>
#include <streambuf>
#include <utility>

#include <wx/snglinst.h>
#include <wx/dir.h>
#include <wx/stackwalk.h>

#include "../brushes/icon/editor_icon.xpm"

BEGIN_EVENT_TABLE(MainFrame, wxFrame)
EVT_CLOSE(MainFrame::OnExit)

EVT_ON_UPDATE_MENUS(wxID_ANY, MainFrame::OnUpdateMenus)

// Idle event handler
EVT_IDLE(MainFrame::OnIdle)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(MapWindow, wxPanel)
EVT_SIZE(MapWindow::OnSize)

EVT_COMMAND_SCROLL_TOP(MAP_WINDOW_HSCROLL, MapWindow::OnScroll)
EVT_COMMAND_SCROLL_BOTTOM(MAP_WINDOW_HSCROLL, MapWindow::OnScroll)
EVT_COMMAND_SCROLL_THUMBTRACK(MAP_WINDOW_HSCROLL, MapWindow::OnScroll)
EVT_COMMAND_SCROLL_LINEUP(MAP_WINDOW_HSCROLL, MapWindow::OnScrollLineUp)
EVT_COMMAND_SCROLL_LINEDOWN(MAP_WINDOW_HSCROLL, MapWindow::OnScrollLineDown)
EVT_COMMAND_SCROLL_PAGEUP(MAP_WINDOW_HSCROLL, MapWindow::OnScrollPageUp)
EVT_COMMAND_SCROLL_PAGEDOWN(MAP_WINDOW_HSCROLL, MapWindow::OnScrollPageDown)

EVT_COMMAND_SCROLL_TOP(MAP_WINDOW_VSCROLL, MapWindow::OnScroll)
EVT_COMMAND_SCROLL_BOTTOM(MAP_WINDOW_VSCROLL, MapWindow::OnScroll)
EVT_COMMAND_SCROLL_THUMBTRACK(MAP_WINDOW_VSCROLL, MapWindow::OnScroll)
EVT_COMMAND_SCROLL_LINEUP(MAP_WINDOW_VSCROLL, MapWindow::OnScrollLineUp)
EVT_COMMAND_SCROLL_LINEDOWN(MAP_WINDOW_VSCROLL, MapWindow::OnScrollLineDown)
EVT_COMMAND_SCROLL_PAGEUP(MAP_WINDOW_VSCROLL, MapWindow::OnScrollPageUp)
EVT_COMMAND_SCROLL_PAGEDOWN(MAP_WINDOW_VSCROLL, MapWindow::OnScrollPageDown)

EVT_BUTTON(MAP_WINDOW_GEM, MapWindow::OnGem)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(MapScrollBar, wxScrollBar)
EVT_KEY_DOWN(MapScrollBar::OnKey)
EVT_KEY_UP(MapScrollBar::OnKey)
EVT_CHAR(MapScrollBar::OnKey)
EVT_SET_FOCUS(MapScrollBar::OnFocus)
EVT_MOUSEWHEEL(MapScrollBar::OnWheel)
END_EVENT_TABLE()

wxIMPLEMENT_APP_NO_MAIN(Application);

namespace {

class TeeStreamBuffer final : public std::streambuf {
public:
	TeeStreamBuffer(std::streambuf* consoleBuffer, std::streambuf* logBuffer) :
		consoleBuffer(consoleBuffer), logBuffer(logBuffer) {
	}

protected:
	int_type overflow(int_type character) override {
		if (traits_type::eq_int_type(character, traits_type::eof())) {
			return traits_type::not_eof(character);
		}

		const char value = traits_type::to_char_type(character);
		consoleBuffer->sputc(value);
		if (traits_type::eq_int_type(logBuffer->sputc(value), traits_type::eof())) {
			return traits_type::eof();
		}
		return character;
	}

	std::streamsize xsputn(const char* text, std::streamsize size) override {
		const std::streamsize consoleSize = consoleBuffer->sputn(text, size);
		const std::streamsize logSize = logBuffer->sputn(text, size);
		return std::min(consoleSize, logSize);
	}

	int sync() override {
		consoleBuffer->pubsync();
		return logBuffer->pubsync();
	}

private:
	std::streambuf* consoleBuffer;
	std::streambuf* logBuffer;
};

class ConsoleLogTarget final : public wxLog {
protected:
	void DoLogText(const wxString& message) override {
		const wxScopedCharBuffer utf8 = message.ToUTF8();
		std::cerr << "[wx] " << (utf8 ? utf8.data() : "") << std::endl;
	}
};

#if wxUSE_STACKWALKER && wxUSE_ON_FATAL_EXCEPTION
class DiagnosticStackWalker final : public wxStackWalker {
protected:
	void OnStackFrame(const wxStackFrame& frame) override {
		std::cerr << "  #" << frame.GetLevel() << " "
		          << frame.GetName().ToStdString();
		if (frame.HasSourceLocation()) {
			std::cerr << " at " << frame.GetFileName().ToStdString()
			          << ":" << frame.GetLine();
		} else if (!frame.GetModule().IsEmpty()) {
			std::cerr << " in " << frame.GetModule().ToStdString();
		}
		std::cerr << " [" << frame.GetAddress() << "]" << std::endl;
	}
};
#endif

template <typename EntryPoint>
int RunApplication(EntryPoint&& entryPoint) {
	try {
		std::cerr << "[info] RME starting" << std::endl;
		const int exitCode = std::forward<EntryPoint>(entryPoint)();
		std::cerr << "[info] RME stopped with code " << exitCode << std::endl;
		return exitCode;
	} catch (const std::exception& exception) {
		std::cerr << "[critical] Unhandled startup/shutdown exception: "
		          << exception.what() << std::endl;
	} catch (...) {
		std::cerr << "[critical] Unknown startup/shutdown exception" << std::endl;
	}
	return EXIT_FAILURE;
}

template <typename EntryPoint>
int RunWithDiagnostics(const std::filesystem::path& executablePath, EntryPoint&& entryPoint) {
	const std::filesystem::path logPath = executablePath.parent_path() / "rme.log";
	std::ofstream logFile(logPath, std::ios::out | std::ios::app);
	if (!logFile) {
		std::cerr << "[warning] Could not open diagnostic log: " << logPath.string() << std::endl;
		return RunApplication(std::forward<EntryPoint>(entryPoint));
	}

	TeeStreamBuffer outputBuffer(std::cout.rdbuf(), logFile.rdbuf());
	TeeStreamBuffer errorBuffer(std::cerr.rdbuf(), logFile.rdbuf());
	std::streambuf* const originalOutputBuffer = std::cout.rdbuf(&outputBuffer);
	std::streambuf* const originalErrorBuffer = std::cerr.rdbuf(&errorBuffer);
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;

	std::cerr << std::endl << "=== RME diagnostic session ===" << std::endl;
	std::cerr << "[info] Diagnostic log: " << logPath.string() << std::endl;
	const int exitCode = RunApplication(std::forward<EntryPoint>(entryPoint));

	std::cout.rdbuf(originalOutputBuffer);
	std::cerr.rdbuf(originalErrorBuffer);
	return exitCode;
}

} // namespace

int main(int argc, char** argv) {
	return RunWithDiagnostics(
		std::filesystem::absolute(argv[0]),
		[&] { return wxEntry(argc, argv); }
	);
}

#ifdef __WINDOWS__
extern "C" int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, wxCmdLineArgType commandLine, int showCommand) {
	wxDISABLE_DEBUG_SUPPORT();
	std::array<wchar_t, 32768> executablePath {};
	const DWORD pathLength = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
	const std::filesystem::path path = pathLength > 0 ? std::filesystem::path(executablePath.data()) : std::filesystem::path("rme.exe");
	return RunWithDiagnostics(
		path,
		[&] { return wxEntry(hInstance, hPrevInstance, commandLine, showCommand); }
	);
}
#endif

Application::~Application() {
	// Destroy
}

bool Application::OnInit() {
#if defined __DEBUG_MODE__ && defined __WINDOWS__
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
	// Load the persisted theme before creating any windows.
	g_settings.load();
	const int rawTheme = g_settings.getInteger(Config::THEME);
	const int theme = rawTheme >= 0 && rawTheme <= 2 ? rawTheme : 0;
	Theme::SetType(static_cast<Theme::Type>(theme));
#if wxCHECK_VERSION(3, 3, 0)
	switch (theme) {
		case 1:
			SetAppearance(wxApp::Appearance::Dark);
			std::cerr << "[info] Theme: Dark" << std::endl;
			break;
		case 2:
			SetAppearance(wxApp::Appearance::Light);
			std::cerr << "[info] Theme: Light" << std::endl;
			break;
		case 0:
		default:
			SetAppearance(wxApp::Appearance::System);
			std::cerr << "[info] Theme: System Default" << std::endl;
			break;
	}
#ifdef __WXMSW__
	if (theme == 1) {
		MSWEnableDarkMode(wxApp::DarkMode_Always);
	} else if (theme == 0) {
		MSWEnableDarkMode(wxApp::DarkMode_Auto);
	}
#endif
#endif
	// Keep the normal wxWidgets UI logger and mirror all messages to the
	// diagnostic console/file stream.
	wxLog::GetActiveTarget();
	new wxLogChain(new ConsoleLogTarget());

	std::cout << "This is free software: you are free to change and redistribute it." << '\n';
	std::cout << "There is NO WARRANTY, to the extent permitted by law." << '\n';
	std::cout << "Review COPYING in RME distribution for details." << '\n';
	mt_seed(time(nullptr));
	srand(time(nullptr));

	// Discover data directory
	g_gui.discoverDataDirectory("clients.xml");

	// Tell that we are the real thing
	wxAppConsole::SetInstance(this);
	// NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks) - wxArtProvider::Push takes ownership.
	wxArtProvider::Push(new ArtProvider());

	// Load some internal stuff
	FixVersionDiscrapencies();
	g_gui.LoadHotkeys();
	ClientVersion::loadVersions();

#ifdef _USE_PROCESS_COM
	m_single_instance_checker = newd wxSingleInstanceChecker; // Instance checker has to stay alive throughout the applications lifetime
	if (g_settings.getInteger(Config::ONLY_ONE_INSTANCE) && m_single_instance_checker->IsAnotherRunning()) {
		RMEProcessClient client;
		wxConnectionBase* connection = client.MakeConnection("localhost", "rme_host", "rme_talk");
		if (connection) {
			wxString fileName;
			if (ParseCommandLineMap(fileName)) {
				wxLogNull nolog; // We might get a timeout message if the file fails to open on the running instance. Let's not show that message.
				connection->Execute(fileName);
			}
			connection->Disconnect();
			wxDELETE(connection);
		}
		wxDELETE(m_single_instance_checker);
		return false; // Since we return false - OnExit is never called
	}
	// We act as server then
	m_proc_server = newd RMEProcessServer();
	if (!m_proc_server->Create("rme_host")) {
		wxLogWarning("Could not register IPC service!");
	}
#endif

	// Image handlers
	// wxImage::AddHandler(newd wxBMPHandler);
	wxImage::AddHandler(newd wxPNGHandler);
	wxImage::AddHandler(newd wxJPEGHandler);
	wxImage::AddHandler(newd wxTGAHandler);

	g_gui.gfx.loadEditorSprites();

	// Let wxWidgets report fatal platform exceptions through OnFatalException().
#ifdef __RELEASE__
	wxHandleFatalExceptions(true);
#endif
	m_file_to_open = wxEmptyString;
	ParseCommandLineMap(m_file_to_open);

	g_gui.root = newd MainFrame(__W_RME_APPLICATION_NAME__, wxDefaultPosition, wxSize(700, 500));
	SetTopWindow(g_gui.root);
	g_gui.SetTitle("");

	g_gui.root->LoadRecentFiles();

	// Load palette
	g_gui.LoadPerspective();

	wxIcon icon(editor_icon);
	g_gui.root->SetIcon(icon);

	if (g_settings.getInteger(Config::WELCOME_DIALOG) == 1 && m_file_to_open == wxEmptyString) {
		g_gui.ShowWelcomeDialog(icon);
	} else {
		g_gui.root->Show();
	}

	// Set idle event handling mode
	wxIdleEvent::SetMode(wxIDLE_PROCESS_SPECIFIED);

	FileName save_failed_file = GUI::GetLocalDataDirectory();
	save_failed_file.SetName(".saving.txt");
	if (save_failed_file.FileExists()) {
		std::ifstream f(nstr(save_failed_file.GetFullPath()).c_str(), std::ios::in);

		std::string backup_otbm, backup_house, backup_spawn;

		getline(f, backup_otbm);
		getline(f, backup_house);
		getline(f, backup_spawn);

		// Remove the file
		f.close();
		std::remove(nstr(save_failed_file.GetFullPath()).c_str());

		// Query file retrieval if possible
		if (!backup_otbm.empty()) {
			long ret = g_gui.PopupDialog(
				"Editor Crashed",
				wxString(
					"IMPORTANT! THE EDITOR CRASHED WHILE SAVING!\n\n"
					"Do you want to recover the lost map? (it will be opened immediately):\n"
				) << wxstr(backup_otbm)
				  << "\n"
				  << wxstr(backup_house) << "\n"
				  << wxstr(backup_spawn) << "\n",
				wxYES | wxNO
			);

			if (ret == wxID_YES) {
				// Recover if the user so wishes
				std::remove(backup_otbm.substr(0, backup_otbm.size() - 1).c_str());
				std::rename(backup_otbm.c_str(), backup_otbm.substr(0, backup_otbm.size() - 1).c_str());

				if (!backup_house.empty()) {
					std::remove(backup_house.substr(0, backup_house.size() - 1).c_str());
					std::rename(backup_house.c_str(), backup_house.substr(0, backup_house.size() - 1).c_str());
				}
				if (!backup_spawn.empty()) {
					std::remove(backup_spawn.substr(0, backup_spawn.size() - 1).c_str());
					std::rename(backup_spawn.c_str(), backup_spawn.substr(0, backup_spawn.size() - 1).c_str());
				}

				// Load the map
				g_gui.LoadMap(wxstr(backup_otbm.substr(0, backup_otbm.size() - 1)));
				return true;
			}
		}
	}
	// Keep track of first event loop entry
	m_startup = true;
	return true;
}

void Application::OnEventLoopEnter(wxEventLoopBase* loop) {

	// First startup?
	if (!m_startup) {
		return;
	}
	m_startup = false;

	// Don't try to create a map if we didn't load the client map.
	if (ClientVersion::getLatestVersion() == nullptr) {
		return;
	}

	// Open a map.
	if (m_file_to_open != wxEmptyString) {
		g_gui.LoadMap(FileName(m_file_to_open));
	} else if (!g_gui.IsWelcomeDialogShown() && g_gui.NewMap()) { // Open a new empty map
		// You generally don't want to save this map...
		g_gui.GetCurrentEditor()->map.clearChanges();
	}
}

void Application::MacOpenFiles(const wxArrayString& fileNames) {
	if (!fileNames.IsEmpty()) {
		g_gui.LoadMap(FileName(fileNames.Item(0)));
	}
}

void Application::FixVersionDiscrapencies() {
	// Here the registry should be fixed, if the version has been changed
	if (g_settings.getInteger(Config::VERSION_ID) < MAKE_VERSION_ID(1, 0, 5)) {
		g_settings.setInteger(Config::USE_MEMCACHED_SPRITES_TO_SAVE, 0);
	}

	if (g_settings.getInteger(Config::VERSION_ID) < __RME_VERSION_ID__ && ClientVersion::getLatestVersion() != nullptr) {
		g_settings.setInteger(Config::DEFAULT_CLIENT_VERSION, ClientVersion::getLatestVersion()->getID());
	}

	wxString ss = wxstr(g_settings.getString(Config::SCREENSHOT_DIRECTORY));
	if (ss.empty()) {
		ss = wxStandardPaths::Get().GetDocumentsDir();
#ifdef __WINDOWS__
		ss += "/My Pictures/RME/";
#endif
	}
	g_settings.setString(Config::SCREENSHOT_DIRECTORY, nstr(ss));

	// Set registry to newest version
	g_settings.setInteger(Config::VERSION_ID, __RME_VERSION_ID__);
}

void Application::Unload() {
	g_gui.CloseAllEditors();
	g_gui.UnloadVersion();
	g_gui.SaveHotkeys();
	g_gui.SavePerspective();
	g_gui.root->SaveRecentFiles();
	ClientVersion::saveVersions();
	ClientVersion::unloadVersions();
	g_settings.save(true);
	g_gui.root = nullptr;
}

int Application::OnExit() {
#ifdef _USE_PROCESS_COM
	wxDELETE(m_proc_server);
	wxDELETE(m_single_instance_checker);
#endif
	if (m_restart_requested) {
		std::vector<wxString> arguments;
		arguments.push_back(wxStandardPaths::Get().GetExecutablePath());
		for (int index = 1; index < argc; ++index) {
			arguments.emplace_back(argv[index]);
		}
		std::vector<const wxChar*> argument_pointers;
		argument_pointers.reserve(arguments.size() + 1);
		for (const wxString& argument : arguments) {
			argument_pointers.push_back(argument.c_str());
		}
		argument_pointers.push_back(nullptr);
		if (wxExecute(argument_pointers.data(), wxEXEC_ASYNC) == 0) {
			wxLogError("The application closed normally, but could not start the new instance.");
		}
	}
	return 0;
}

bool Application::RequestApplicationRestart() {
	if (m_restart_requested || !g_gui.root) {
		return false;
	}
	m_restart_requested = true;
	if (!g_gui.root->Close()) {
		m_restart_requested = false;
		return false;
	}
	return true;
}

int Application::OnRun() {
	std::cerr << "[info] Application event loop started" << std::endl;
	const int exitCode = wxApp::OnRun();
	std::cerr << "[info] Application event loop stopped with code " << exitCode << std::endl;
	return exitCode;
}

void Application::ShutdownServices() {
#ifdef _USE_PROCESS_COM
	wxDELETE(m_proc_server);
	wxDELETE(m_single_instance_checker);
#endif
}

void Application::OnFatalException() {
	std::cerr << "[critical] Fatal platform exception: RME crashed" << std::endl;
#if wxUSE_STACKWALKER && wxUSE_ON_FATAL_EXCEPTION
	std::cerr << "[critical] Crash stack trace:" << std::endl;
	DiagnosticStackWalker stackWalker;
	stackWalker.WalkFromException(64);
#endif
}

bool Application::OnExceptionInMainLoop() {
	try {
		throw;
	} catch (const std::exception& exception) {
		std::cerr << "[error] Exception while handling an event: "
		          << exception.what() << std::endl;
	} catch (...) {
		std::cerr << "[error] Unknown exception while handling an event" << std::endl;
	}
	return false;
}

void Application::OnUnhandledException() {
	try {
		throw;
	} catch (const std::exception& exception) {
		std::cerr << "[error] Unhandled application exception: "
		          << exception.what() << std::endl;
	} catch (...) {
		std::cerr << "[error] Unknown unhandled application exception" << std::endl;
	}
}

bool Application::ParseCommandLineMap(wxString& fileName) {
	if (argc == 2) {
		fileName = wxString(argv[1]);
		return true;
	} else if (argc == 3) {
		if (argv[1] == "-ws") {
			g_settings.setInteger(Config::WELCOME_DIALOG, argv[2] == "1" ? 1 : 0);
		}
	}
	return false;
}

MainFrame::MainFrame(const wxString& title, const wxPoint& pos, const wxSize& size) :
	wxFrame((wxFrame*)nullptr, -1, title, pos, size, wxDEFAULT_FRAME_STYLE) {
	// Receive idle events
	SetExtraStyle(wxWS_EX_PROCESS_IDLE);

#if wxCHECK_VERSION(3, 1, 0) // 3.1.0 or higher
	// Make sure ShowFullScreen() uses the full screen API on macOS
	EnableFullScreenView(true);
#endif

	// Creates the file-dropdown menu
	menu_bar = newd MainMenuBar(this);
	wxArrayString warnings;
	wxString error;

	wxFileName filename;
	filename.Assign(g_gui.getFoundDataDirectory() + "menubar.xml");
	if (!filename.FileExists()) {
		filename = FileName(GUI::GetDataDirectory() + "menubar.xml");
	}

	if (!menu_bar->Load(filename, warnings, error)) {
		wxLogError(wxString() + "Could not load menubar.xml, editor will NOT be able to show its menu.\n");
	}

	wxStatusBar* statusbar = CreateStatusBar();
	statusbar->SetFieldsCount(4);
	SetStatusText(wxString("Welcome to ") << __W_RME_APPLICATION_NAME__ << " " << __W_RME_VERSION__);

	// Le sizer
	g_gui.aui_manager = newd wxAuiManager(this);
	g_gui.tabbook = newd MapTabbook(this, wxID_ANY);

	tool_bar = newd MainToolBar(this, g_gui.aui_manager);

	g_gui.aui_manager->AddPane(g_gui.tabbook, wxAuiPaneInfo().CenterPane().Floatable(false).CloseButton(false).PaneBorder(false));
	g_gui.aui_manager->Update();

	UpdateMenubar();
}

MainFrame::~MainFrame() = default;

void MainFrame::OnIdle(wxIdleEvent& event) {
	////
}

void MainFrame::OnUpdateMenus(wxCommandEvent&) {
	UpdateMenubar();
	g_gui.UpdateMinimap(true);
	g_gui.UpdateTitle();
}

#ifdef __WINDOWS__
bool MainFrame::MSWTranslateMessage(WXMSG* msg) {
	if (g_gui.AreHotkeysEnabled()) {
		if (wxFrame::MSWTranslateMessage(msg)) {
			return true;
		}
	} else {
		if (wxWindow::MSWTranslateMessage(msg)) {
			return true;
		}
	}
	return false;
}
#endif

void MainFrame::UpdateMenubar() {
	menu_bar->Update();
	tool_bar->UpdateButtons();
}

bool MainFrame::DoQuerySaveTileset(bool doclose) {

	if (!g_materials.needSave()) {
		// skip dialog when there is nothing to save
		return true;
	}

	long ret = g_gui.PopupDialog(
		"Export tileset",
		"Do you want to export your tileset changes before exiting?",
		wxYES | wxNO | wxCANCEL
	);

	if (ret == wxID_NO) {
		// "no" - exit without saving
		return true;
	} else if (ret == wxID_CANCEL) {
		// "cancel" - just close the dialog
		return false;
	}

	// "yes" button was pressed, open tileset exporting dialog
	if (g_gui.GetCurrentEditor()) {
		ExportTilesetsWindow dlg(this, *g_gui.GetCurrentEditor());
		dlg.ShowModal();
		dlg.Destroy();
	}

	return !g_materials.needSave();
}

bool MainFrame::DoQuerySave(bool doclose) {
	if (!g_gui.IsEditorOpen()) {
		return true;
	}

	if (!DoQuerySaveTileset()) {
		return false;
	}

	if (g_gui.ShouldSave()) {
		long ret = g_gui.PopupDialog(
			"Save changes",
			"Do you want to save your changes to \"" + wxstr(g_gui.GetCurrentMap().getName()) + "\"?",
			wxYES | wxNO | wxCANCEL
		);

		if (ret == wxID_YES) {
			if (g_gui.GetCurrentMap().hasFile()) {
				g_gui.SaveCurrentMap(true);
			} else {
				wxFileDialog file(this, "Save...", "", "", "*.otbm", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
				int32_t result = file.ShowModal();
				if (result == wxID_OK) {
					g_gui.SaveCurrentMap(file.GetPath(), true);
				} else {
					return false;
				}
			}
		} else if (ret == wxID_CANCEL) {
			return false;
		}
	}

	if (doclose) {
		UnnamedRenderingLock();
		g_gui.CloseCurrentEditor();
	}

	return true;
}

bool MainFrame::DoQueryImportCreatures() {
	if (g_creatures.hasMissing()) {
		long ret = g_gui.PopupDialog("Missing creatures", "There are missing creatures and/or NPC in the editor, do you want to load them from Lua monster/NPC directories?", wxYES | wxNO);
		if (ret == wxID_YES) {
			bool missingMonsters = false;
			bool missingNpcs = false;
			for (auto iter = g_creatures.begin(); iter != g_creatures.end(); ++iter) {
				if (!iter->second->missing) {
					continue;
				}
				if (iter->second->isNpc) {
					missingNpcs = true;
				} else {
					missingMonsters = true;
				}
			}

			std::string monstersLuaDir = g_settings.getString(Config::MONSTERS_LUA_DIRECTORY);
			std::string npcsLuaDir = g_settings.getString(Config::NPCS_LUA_DIRECTORY);

			const bool monstersLuaReady = !monstersLuaDir.empty() && wxDir::Exists(wxstr(monstersLuaDir));
			const bool npcsLuaReady = !npcsLuaDir.empty() && wxDir::Exists(wxstr(npcsLuaDir));
			if ((missingMonsters && !monstersLuaReady) || (missingNpcs && !npcsLuaReady)) {
				PreferencesWindow dialog(g_gui.root, true);
				dialog.ShowModal();
				dialog.Destroy();
				monstersLuaDir = g_settings.getString(Config::MONSTERS_LUA_DIRECTORY);
				npcsLuaDir = g_settings.getString(Config::NPCS_LUA_DIRECTORY);
			}

			if (missingMonsters && !monstersLuaDir.empty() && wxDir::Exists(wxstr(monstersLuaDir))) {
				wxString error;
				wxArrayString warnings;
				if (g_creatures.importMonstersFromLuaDir(wxstr(monstersLuaDir), error, warnings)) {
					g_gui.ListDialog("Monster Lua loader warnings", warnings);
				} else {
					wxMessageBox(error, "Error", wxOK | wxICON_INFORMATION, g_gui.root);
				}
			}

			if (missingNpcs && !npcsLuaDir.empty() && wxDir::Exists(wxstr(npcsLuaDir))) {
				wxString error;
				wxArrayString warnings;
				if (g_creatures.importNpcsFromLuaDir(wxstr(npcsLuaDir), error, warnings)) {
					g_gui.ListDialog("NPC Lua loader warnings", warnings);
				} else {
					wxMessageBox(error, "Error", wxOK | wxICON_INFORMATION, g_gui.root);
				}
			}
		}
	}
	g_gui.RefreshPalettes();
	return true;
}

void MainFrame::UpdateFloorMenu() {
	menu_bar->UpdateFloorMenu();
}

bool MainFrame::LoadMap(const FileName& name) {
	return g_gui.LoadMap(name);
}

void MainFrame::OnExit(wxCloseEvent& event) {
	// clicking 'x' button

	// Ask to save changed maps before starting the normal destruction sequence.
	std::set<Map*> prompted;
	for (int i = 0; i < g_gui.tabbook->GetTabCount(); ++i) {
		auto* mapTab = dynamic_cast<MapTab*>(g_gui.tabbook->GetTab(i));
		if (!mapTab || !mapTab->GetMap() || !mapTab->GetMap()->hasChanged()
			|| prompted.count(mapTab->GetMap())) {
			continue;
		}
		prompted.insert(mapTab->GetMap());
		g_gui.tabbook->SetFocusedTab(i);
		if (!DoQuerySave(false)) {
			if (event.CanVeto()) {
				event.Veto();
				return;
			}
			break;
		}
	}

	// The close has been authorized. Persist state and release editor resources
	// through the normal wxWidgets lifecycle before the event loop ends.
	g_gui.SaveHotkeys();
	g_gui.SavePerspective();
	g_gui.root->SaveRecentFiles();
	ClientVersion::saveVersions();
	g_gui.FinishWelcomeDialog(false);
	g_gui.CloseAllEditors(false);
	g_gui.UnloadVersion();
	ClientVersion::unloadVersions();
	g_settings.save(true);
	Destroy();
}

void MainFrame::AddRecentFile(const FileName& file) {
	menu_bar->AddRecentFile(file);
}

void MainFrame::LoadRecentFiles() {
	menu_bar->LoadRecentFiles();
}

void MainFrame::SaveRecentFiles() {
	menu_bar->SaveRecentFiles();
}

std::vector<wxString> MainFrame::GetRecentFiles() {
	return menu_bar->GetRecentFiles();
}

void MainFrame::PrepareDC(wxDC& dc) {
	dc.SetLogicalOrigin(0, 0);
	dc.SetAxisOrientation(1, 0);
	dc.SetUserScale(1.0, 1.0);
	dc.SetMapMode(wxMM_TEXT);
}
