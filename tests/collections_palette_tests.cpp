// SPDX-License-Identifier: GPL-3.0-or-later
// Real material loader and hidden wx controls, isolated from user assets/settings.
#include "main.h"
#include "brush.h"
#include "carpet_brush.h"
#include "editor_resource_session.h"
#include "editor_tabs.h"
#include "gui.h"
#include "materials.h"
#include "palette_window.h"
#include "palette_brushlist.h"
#include "raw_brush.h"
#include "theme.h"
#include <wx/file.h>
#include <wx/weakref.h>
#include <iostream>

class CollectionsPaletteTests {
	static void check(bool value, const char* message) {
		if (!value) {
			throw std::runtime_error(message);
		}
	}
	struct XmlFixture {
		wxString path = wxFileName::CreateTempFileName("nexamap-collections-test-");
		~XmlFixture() {
			wxRemoveFile(path);
		}
		wxArrayString load(const wxString& xml) {
			wxFile file(path, wxFile::write);
			check(file.IsOpened() && file.Write(xml) && file.Close(), "Cannot write collections fixture");
			wxString error;
			wxArrayString warnings;
			check(g_materials.loadMaterials(FileName(path), error, warnings), "Materials loader failed");
			return warnings;
		}
	};
	struct Sessions {
		EditorResourceSessionPtr original = GetActiveEditorResourceSession();
		Sessions() {
			activate(CreateEditorResourceSession());
		}
		~Sessions() {
			activate(original);
		}
		static void activate(const EditorResourceSessionPtr& next) {
			auto current = GetActiveEditorResourceSession();
			g_gui.SwapResourceSessionState(*current);
			current->swapWithGlobals();
			next->swapWithGlobals();
			g_gui.SwapResourceSessionState(*next);
			SetActiveEditorResourceSession(next);
		}
	};
	class PaletteProbe : public PaletteWindow {
	public:
		explicit PaletteProbe(wxWindow* parent) :
			PaletteWindow(parent, g_materials.tilesets) {
			g_gui.palettes.push_back(this);
		}
		~PaletteProbe() override {
			g_gui.palettes.remove(this);
		}
		bool hasCollectionsPage() const {
			return collection_palette != nullptr;
		}
		BrushPalettePanel* collectionsPage() const {
			return collection_palette;
		}
		void select(PaletteType type) {
			// Exercise page selection without requiring an application toolbar/canvas.
			wxEventBlocker block(choicebook);
			SelectPage(type);
			LoadCurrentContents();
		}
	};
	static void defineItems(const std::string& profile, bool clientIds) {
		g_brushes.init();
		for (uint16_t id : { 100, 101 }) {
			auto* item = new ItemType();
			item->id = id;
			item->clientID = clientIds ? id : id + 400;
			item->name = profile + " item " + std::to_string(id);
			g_items.items.set(id, item);
		}
	}
	static const TilesetCategory* category(const std::string& name) {
		const Tileset* tileset = g_materials.tilesets.at(name);
		return tileset->getCategory(TILESET_COLLECTION);
	}
	static wxChoicebook* findBook(wxWindow* window) {
		if (auto* book = dynamic_cast<wxChoicebook*>(window)) {
			return book;
		}
		for (auto* child : window->GetChildren()) {
			if (auto* book = findBook(child)) {
				return book;
			}
		}
		return nullptr;
	}
	static void loaderAndRefresh() {
		Sessions sessions;
		defineItems("Classic", false);
		XmlFixture fixture;
		check(fixture.load("<materials><brush name='carpet' type='carpet'><carpet align='center' id='101'/></brush><tileset name='Ordinary'><items><brush name='carpet'/></items></tileset></materials>").empty(), "Carpet definition failed");
		auto* carpet = g_brushes.getBrush("carpet");
		check(carpet && carpet->isCarpet() && !carpet->hasCollection(), "A carpet is not automatically a collection");
		const auto before = g_materials.tilesets.at("Ordinary")->categories.size();
		check(!g_materials.hasCollections() && !category("Ordinary"), "Missing collection category must be unavailable");
		check(g_materials.tilesets.at("Ordinary")->categories.size() == before, "Availability query created an empty category");
		wxFrame frame(nullptr, wxID_ANY, "Hidden collections validation");
		PaletteProbe palette(&frame);
		check(!palette.hasCollectionsPage(), "Empty collection page shown");
		g_gui.SelectPalettePage(TILESET_COLLECTION);
		check(g_gui.GetPalette() == &palette && palette.GetSelectedPage() == TILESET_TERRAIN, "Unavailable shortcut created/activated another palette");
		auto warnings = fixture.load("<materials><tileset name='Invalid'><collections><brush name='missing'/><item id='60000'/></collections></tileset></materials>");
		check(warnings.size() >= 2 && category("Invalid")->size() == 0 && !g_materials.hasCollections(), "Invalid references should warn, skip, and keep the page hidden");
		check(fixture.load("<materials><tileset name='Valid'><collections><brush name='carpet'/><item id='100'/></collections></tileset></materials>").empty(), "Valid collections failed to load");
		check(category("Valid")->size() == 2 && carpet->hasCollection() && g_items[100].raw_brush->hasCollection(), "Collections must reuse and mark both brushes");
		check(category("Valid")->brushlist[0] == carpet && category("Valid")->brushlist[1] == g_items[100].raw_brush && g_items[100].collection_brush == g_items[100].raw_brush, "Collections duplicated or replaced brush ownership");
		check(g_items[100].raw_brush->getItemID() == 100 && g_items[100].raw_brush->getLookID() == 500, "Classic ServerID was silently remapped to ClientID");
		palette.OnUpdate(nullptr);
		check(palette.hasCollectionsPage(), "New valid collections not shown after refresh");
		auto* book = findBook(palette.collectionsPage());
		check(book && book->GetPageCount() == 1 && book->GetPageText(0) == "Valid", "Empty/invalid tilesets leaked into Collections combo");
		palette.select(TILESET_COLLECTION);
		check(palette.GetSelectedPage() == TILESET_COLLECTION && palette.OnSelectBrush(carpet, TILESET_COLLECTION) && palette.GetSelectedBrush() == carpet, "Collection selection didn't reuse the current brush");
		g_materials.addToTileset("Added", 100, TILESET_COLLECTION);
		g_materials.addToTileset("Added", 100, TILESET_COLLECTION);
		check(category("Added")->size() == 1, "Adding the same collection item duplicated it");
		g_materials.addToTileset("Added", 101, TILESET_COLLECTION);
		check(g_items[101].raw_brush->hasCollection() && g_items[101].collection_brush == g_items[101].raw_brush, "Tileset editor did not mark its new collection brush");
		wxWeakRef<BrushPalettePanel> oldPage(palette.collectionsPage());
		palette.ReloadSettings(nullptr);
		check(!oldPage && findBook(palette.collectionsPage())->GetPageCount() == 2 && palette.GetSelectedPage() == TILESET_COLLECTION, "Rebuild must replace the old page and include new tilesets");
		for (const auto& [name, tileset] : g_materials.tilesets) {
			tileset->getCategory(TILESET_COLLECTION)->brushlist.clear();
		}
		palette.OnUpdate(nullptr);
		check(!palette.hasCollectionsPage() && palette.GetSelectedPage() == TILESET_TERRAIN, "Last collection removal must hide the page and select Terrain");
		std::cout << "PASS loader: missing/invalid=0; valid=1 collection/2 brushes; no implicit carpet migration or ID remap\n";
		std::cout << "PASS hidden palette: combo, selection, add/remove/rebuild, duplicate prevention and shortcut guard\n";
	}
	static void sessionSwitching() {
		Sessions sessions;
		auto classic = GetActiveEditorResourceSession();
		defineItems("Classic", false);
		XmlFixture fixture;
		fixture.load("<materials><tileset name='Classic'><collections><item id='100'/></collections></tileset></materials>");
		auto* classicBrush = g_items[100].raw_brush;
		for (auto theme : { Theme::Type::System, Theme::Type::Dark, Theme::Type::Light }) {
			Theme::SetType(theme);
			wxFrame frame(nullptr, wxID_ANY, "Hidden independent collections");
			auto old = std::make_unique<PaletteProbe>(&frame);
			old->select(TILESET_COLLECTION);
			check(old->GetSelectedBrush() == classicBrush, "Classic collection binding failed");
			for (const std::string profile : { "Canary", "Crystal" }) {
				auto independent = CreateEditorResourceSession();
				Sessions::activate(independent);
				defineItems(profile, true);
				check(!g_materials.hasCollections() && !old->GetSelectedBrush() && !old->OnSelectBrush(classicBrush, TILESET_COLLECTION), "Previous palette must reject events after a resource switch");
				PaletteProbe current(&frame);
				check(!current.hasCollectionsPage(), "Previous session's collections leaked into empty session");
				fixture.load("<materials><tileset name='Native'><collections_and_raw><item id='100'/></collections_and_raw></tileset></materials>");
				current.OnUpdate(nullptr);
				current.select(TILESET_COLLECTION);
				auto* brush = current.GetSelectedBrush();
				check(brush && brush != classicBrush && brush == g_items[100].raw_brush && brush->getName().find(profile) != std::string::npos && brush->getLookID() == 100, "Same numeric ID selected a brush from another session");
				for (int width : { 230, 460 }) {
					current.SetSize(current.FromDIP(wxSize(width, 850)));
					current.Layout();
					check(current.collectionsPage()->GetClientSize().x > 0, "Invalid collection layout width");
				}
				Sessions::activate(classic);
				check(!current.GetSelectedBrush() && g_items[100].raw_brush == classicBrush && g_materials.tilesets.count("Native") == 0, "Returning to Classic did not restore its collection identity");
			}
			check(old->GetSelectedBrush() == classicBrush, "Restored Classic palette lost its brush");
			// Late refreshes must not rebind an inactive palette to an empty session.
			auto empty = CreateEditorResourceSession();
			Sessions::activate(empty);
			old->OnUpdate(nullptr);
			old->ReloadSettings(nullptr);
			check(!old->GetSelectedBrush(), "Inactive palette exposed a stale brush");
			old.reset();
			Sessions::activate(classic);
		}
		{
			wxFrame frame(nullptr, wxID_ANY, "Hidden released resource validation");
			PaletteProbe retired(&frame);
			retired.select(TILESET_COLLECTION);
			Sessions::activate(CreateEditorResourceSession());
			classic.reset();
			retired.OnUpdate(nullptr);
			retired.ReloadSettings(nullptr);
			check(!retired.GetSelectedBrush(), "Destroyed resource session exposed a dangling brush");
		}
		Theme::SetType(Theme::Type::System);
		std::cout << "PASS independent resource registries: Classic/Canary/Crystal ID fixtures, swap/restore, stale events, 3 themes and narrow/wide layout\n";
	}

public:
	static void run() {
		std::cout << std::unitbuf;
#if wxUSE_ON_FATAL_EXCEPTION
		wxHandleFatalExceptions(true);
#endif
		g_settings.setDefaults();
		g_settings.setInteger(Config::INDIRECTORY_INSTALLATION, 1);
		wxFrame frame(nullptr, wxID_ANY, "Hidden collections host");
		MapTabbook tabs(&frame, wxID_ANY);
		auto* previousTabs = g_gui.tabbook;
		g_gui.tabbook = &tabs;
		try {
			loaderAndRefresh();
			sessionSwitching();
		} catch (...) {
			g_gui.tabbook = previousTabs;
			throw;
		}
		g_gui.tabbook = previousTabs;
	}
};

void RunCollectionsPaletteTests() {
	CollectionsPaletteTests::run();
}
