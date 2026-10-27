// Real editor serializers and hotkey parsing, using only synthetic map data.
#include <glad/glad.h>
#include "main.h"
#include "application.h"
#include "artprovider.h"
#include "gui.h"
#include "hotkey_manager.h"
#include "items.h"
#include "item.h"
#include "map.h"
#include "tile.h"
#include "iomap_otbm.h"
#include "editor_tabs.h"
#include "editor.h"
#include "map_tab.h"
#include "map_display.h"
#include "gl_renderer.h"
#include <chrono>
#include <filesystem>
#include <wx/evtloop.h>
#include <wx/uiaction.h>
#include <wx/dcscreen.h>

namespace {
	void require(bool condition, const char* message) {
		if (!condition) {
			throw std::runtime_error(message);
		}
	}

	void roundtrip(const std::filesystem::path& directory) {
		for (uint16_t id : { 100, 101 }) {
			auto* type = new ItemType();
			type->id = id;
			type->clientID = id == 100 ? 53000 : 53001;
			type->group = id == 100 ? ITEM_GROUP_GROUND : ITEM_GROUP_NONE;
			type->stackable = id == 101;
			g_items.items.set(id, type);
		}
		g_items.MajorVersion = 3;
		g_items.MinorVersion = CLIENT_VERSION_860;
		Map original;
		original.convert({ MAP_OTBM_4, CLIENT_VERSION_860 }, false);
		original.setMapDescription("Linux semantic roundtrip");
		original.setSpawnFilename("fixture-spawn.xml");
		original.setHouseFilename("fixture-house.xml");
		const Position position(100, 101, 7);
		auto* tile = original.allocator(original.createTileL(position));
		tile->addItem(Item::Create(100));
		auto* item = Item::Create(101, 17);
		item->setActionID(1200);
		item->setUniqueID(4500);
		tile->addItem(item);
		original.setTile(tile);
		const FileName filename(wxString::FromUTF8((directory / std::filesystem::path(u8"Mapa São João.otbm")).string()));
		IOMapOTBM writer(original.getVersion());
		require(writer.saveMap(original, filename), "save fixture OTBM and sidecars");
		Map loaded;
		IOMapOTBM reader(original.getVersion());
		require(reader.loadMap(loaded, filename), "reopen OTBM fixture");
		loaded.convert(reader.version, false); // Match Map::open's version handoff.
		auto* reopened = loaded.getTile(position);
		require(reopened && reopened->ground && reopened->ground->getID() == 100, "ground ID and position survive");
		require(reopened->items.size() == 1, "item count survives");
		require(reopened->items[0]->getID() == 101 && reopened->items[0]->getSubtype() == 17, "stack ServerID and count survive");
		require(reopened->items[0]->getActionID() == 1200 && reopened->items[0]->getUniqueID() == 4500, "action and unique attributes survive");
		reopened->items[0]->setSubtype(23);
		require(writer.saveMap(loaded, filename), "save edited OTBM");
		Map edited;
		require(reader.loadMap(edited, filename), "reopen edited OTBM");
		require(edited.getTile(position)->items[0]->getSubtype() == 23, "semantic edit survives second roundtrip");
		require(g_items.getItemType(100).clientID == 53000, "protocol 8.60 does not rewrite modern ClientIDs");
		std::cout << "PASS native OTBM load/save/edit/reopen, Unicode and modern ClientIDs\n";
	}

	void hotkeys() {
		g_settings.setString(Config::DATA_DIRECTORY, "");
		g_gui.discoverDataDirectory("clients.xml");
		require(wxFileExists(g_gui.GetDataDirectory() + "clients.xml"), "all resource consumers use discovered data directory");
		g_gui.root = new MainFrame("Linux hotkey regression", wxDefaultPosition, wxSize(800, 600));
		auto* menu = g_gui.root->GetMainMenuBar();
		auto& config = Settings::getConfigObject();
		config.Write("/HotkeyOverrides/NEW", "T");
		config.Write("/HotkeyOverrides/SELECT_TERRAIN", "");
		g_hotkey_manager.DiscoverActions(menu);
		int toolCommands = 0;
		g_gui.root->Bind(
			wxEVT_MENU, [&](wxCommandEvent&) { ++toolCommands; }, MAIN_FRAME_MENU + MenuBar::NEW
		);
		auto* text = new wxTextCtrl(g_gui.root, wxID_ANY, "", wxPoint(30, 30), wxSize(200, 30));
		auto* focusPanel = new wxPanel(g_gui.root, wxID_ANY, wxPoint(30, 80), wxSize(200, 60), wxWANTS_CHARS);
		g_gui.root->Show();
		text->SetFocus();
		wxYield();
#if wxUSE_UIACTIONSIMULATOR
		wxUIActionSimulator input;
		require(input.Text("trd"), "inject native GTK typing");
#else
		// The pinned vcpkg wxWidgets port disables wxUIActionSimulator.
		require(std::system("xdotool type --clearmodifiers --delay 20 trd") == 0, "inject native GTK typing with xdotool");
#endif
		wxMilliSleep(80);
		wxYield();
		require(text->GetValue() == "trd" && toolCommands == 0, "text focus accepts tool letters without executing commands");
		focusPanel->SetFocus();
		wxYield();
#if wxUSE_UIACTIONSIMULATOR
		require(input.Char('T'), "inject native GTK accelerator");
#else
		require(std::system("xdotool key t") == 0, "inject native GTK accelerator with xdotool");
#endif
		wxMilliSleep(80);
		wxYield();
		require(toolCommands == 1, "plain accelerator dispatches exactly once after text loses focus");
		config.DeleteEntry("/HotkeyOverrides/NEW");
		config.DeleteEntry("/HotkeyOverrides/SELECT_TERRAIN");
		g_hotkey_manager.DiscoverActions(menu);
		const bool shadeBefore = g_settings.getBoolean(Config::SHOW_SHADE);
#if wxUSE_UIACTIONSIMULATOR
		require(input.Char('Q'), "inject checkable GTK shortcut");
#else
		require(std::system("xdotool key q") == 0, "inject checkable GTK shortcut");
#endif
		wxMilliSleep(80);
		wxYield();
		require(g_settings.getBoolean(Config::SHOW_SHADE) != shadeBefore, "keyboard command toggles the menu check state");
		require(menu->DispatchAction(MenuBar::SHOW_SHADE), "same checkable menu command dispatches");
		require(g_settings.getBoolean(Config::SHOW_SHADE) == shadeBefore, "menu and keyboard produce the same toggle result");
		wxKeyEvent key(wxEVT_KEY_DOWN);
		key.m_keyCode = 'Z';
		key.SetControlDown(true);
		require(g_hotkey_manager.FindAction(key) == MenuBar::UNDO, "Ctrl+Z resolves Undo before canvas brush rotation");
		key.m_keyCode = 'S';
		require(g_hotkey_manager.FindAction(key) == MenuBar::SAVE, "Ctrl+S resolves Save");
		key.SetControlDown(false);
		key.m_keyCode = 'T';
		require(g_hotkey_manager.FindAction(key) == MenuBar::SELECT_TERRAIN, "plain T resolves terrain palette");
		config.Write("/HotkeyOverrides/SAVE", "Ctrl+Shift+S");
		config.Write("/HotkeyOverrides/SELECT_TERRAIN", "");
		g_settings.save();
		g_hotkey_manager.DiscoverActions(menu);
		require(!g_hotkey_manager.FindAction(key), "empty override disables binding");
		key.m_keyCode = 'S';
		key.SetControlDown(true);
		key.SetShiftDown(true);
		require(g_hotkey_manager.FindAction(key) == MenuBar::SAVE, "custom binding resolves Save");
		std::cout << "PASS default/custom/unbound keyboard commands and user preference persistence\n";
		g_gui.aui_manager->UnInit();
		delete g_gui.aui_manager;
		g_gui.aui_manager = nullptr;
		delete g_gui.root;
		g_gui.root = nullptr;
		g_gui.tabbook = nullptr;
	}

	void canvasPresentation() {
		const bool wasClosing = g_gui.IsApplicationClosing();
		g_gui.SetApplicationClosing(true);
		wxFrame frame(nullptr, wxID_ANY, "Linux map canvas presentation", wxDefaultPosition, wxSize(640, 480));
		auto* tabs = new MapTabbook(&frame, wxID_ANY);
		auto* layout = new wxBoxSizer(wxVERTICAL);
		layout->Add(tabs, 1, wxEXPAND);
		frame.SetSizer(layout);
		frame.Show();
		wxYield();

		auto* first = new MapTab(tabs, std::make_unique<Editor>(g_gui.copybuffer, nullptr));
		auto* firstCanvas = first->GetCanvas();
		wxGLContext context(firstCanvas);
		bool painted = false;
		auto paint = [&](wxPaintEvent& event) {
			auto* canvas = static_cast<MapCanvas*>(event.GetEventObject());
			wxPaintDC dc(canvas);
			require(context.IsOK() && canvas->SetCurrent(context), "map tab native GL context");
			GLRenderer renderer;
			renderer.init();
			require(renderer.isInitialized(), "map tab renderer initialization");
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glDisable(GL_SCISSOR_TEST);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glClearColor(0.1f, 0.8f, 0.2f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			require(canvas->SwapBuffers(), "map tab buffer presentation");
			glFinish();
			painted = true;
		};
		firstCanvas->Bind(wxEVT_PAINT, paint);
		auto* second = new MapTab(first);
		second->GetCanvas()->Bind(wxEVT_PAINT, paint);
		auto checkVisible = [&](MapCanvas* canvas) {
			painted = false;
			canvas->Refresh();
			for (int attempt = 0; attempt < 20; ++attempt) {
				wxYield();
				wxMilliSleep(10);
			}
			require(painted, "selected map canvas received paint");
			// Read the displayed X11 window, not the GL backbuffer: orphaned EGL
			// surfaces report successful swaps while their visible window is gray.
			wxScreenDC screen;
			wxBitmap bitmap(16, 16);
			wxMemoryDC target(bitmap);
			const wxPoint point = canvas->ClientToScreen(wxPoint(16, 16));
			require(target.Blit(0, 0, 16, 16, &screen, point.x, point.y), "capture presented map canvas");
			target.SelectObject(wxNullBitmap);
			const wxImage image = bitmap.ConvertToImage();
			require(image.GetGreen(8, 8) > 170 && image.GetRed(8, 8) < 60, "visible map canvas is gray despite successful GL swap");
		};
		checkVisible(second->GetCanvas());
		tabs->SetFocusedTab(0);
		checkVisible(firstCanvas);
		frame.SetSize(900, 650);
		checkVisible(firstCanvas);
		tabs->SetFocusedTab(1);
		checkVisible(second->GetCanvas());
		std::cout << "PASS visible OpenGL map tabs: new view, switching and resize\n";
		// Dispose the canvases while the local context still exists.
		tabs->DeleteTab(1);
		tabs->DeleteTab(0);
		g_gui.DrainEditorDisposals();
		g_gui.SetApplicationClosing(wasClosing);
	}
}

int RunMultiplayerSessionTests(int argc, char** argv) {
	const auto directory = std::filesystem::temp_directory_path() / ("nexamap-linux-editor-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(directory / ".rme");
	std::ofstream(directory / ".rme/editor.cfg") << "MigrationProbe=legacy-preserved\n";
	setenv("HOME", directory.c_str(), 1);
	setenv("XDG_CONFIG_HOME", directory.c_str(), 1);
	setenv("XDG_DATA_HOME", directory.c_str(), 1);
	if (!wxEntryStart(argc, argv)) {
		return 1;
	}
	wxTheApp->SetExitOnFrameDelete(false);
	wxLog::SetActiveTarget(new wxLogStderr());
	wxInitAllImageHandlers();
	wxArtProvider::Push(new ArtProvider());
	wxSetAssertHandler([](const wxString& file, int line, const wxString&, const wxString& condition, const wxString& message) {
		std::cerr << "wx assertion: " << file.ToStdString() << ':' << line << ' ' << condition.ToStdString() << ' ' << message.ToStdString() << std::endl;
		std::abort();
	});
	int result = 0;
	{
		wxEventLoop loop;
		wxEventLoopActivator activate(&loop);
		try {
			g_settings.load();
			require(Settings::getConfigObject().Read("/MigrationProbe", "") == "legacy-preserved", "legacy preferences imported");
			roundtrip(directory);
			hotkeys();
			canvasPresentation();
			g_settings.save(true);
			require(std::filesystem::is_regular_file(directory / "nexamap/editor.cfg"), "migrated preferences saved under XDG_CONFIG_HOME");
			require(std::filesystem::is_regular_file(directory / ".rme/editor.cfg"), "legacy preferences left intact");
		} catch (const std::exception& error) {
			std::cerr << "FAIL " << error.what() << '\n';
			result = 1;
		}
	}
	std::filesystem::remove_all(directory);
	wxEntryCleanup();
	return result;
}
