// SPDX-License-Identifier: GPL-3.0-or-later
// Native controls stay hidden. No user settings, assets or maps are loaded.
#include "main.h"
#include "gui.h"
#include "creatures.h"
#include "palette_brushlist.h"
#include "palette_creature.h"
#include "palette_house.h"
#include "palette_saved_terrain.h"
#include "palette_waypoints.h"
#include "palette_zones.h"
#include "zone_brush.h"
#include "theme.h"
#include <wx/file.h>
#include <iostream>
#include <sstream>

wxLog* CreateDiagnosticLogTargetForTests();

namespace {
	void Check(bool value, const char* message) {
		if (!value) {
			throw std::runtime_error(message);
		}
	}

	class CaptureLog final : public wxLog {
	public:
		std::vector<wxString> messages;
		void DoLogRecord(wxLogLevel, const wxString& message, const wxLogRecordInfo&) override {
			messages.push_back(message);
		}
	};

	void CheckSizer(wxSizer* sizer, wxWindow* box = nullptr) {
		if (!sizer) {
			return;
		}
		if (auto* staticSizer = dynamic_cast<wxStaticBoxSizer*>(sizer)) {
			box = staticSizer->GetStaticBox();
		}
		for (wxSizerItem* item : sizer->GetChildren()) {
			if (item->IsSizer()) {
				CheckSizer(item->GetSizer(), box);
			} else if (item->IsWindow() && box) {
				Check(item->GetWindow()->GetParent() == box, "Static box contains a control with the wrong parent");
			}
		}
	}

	void CheckControls(wxWindow* window) {
		CheckSizer(window->GetSizer());
		if (auto* spin = dynamic_cast<wxSpinCtrl*>(window)) {
			Check(spin->GetSize().x >= spin->GetBestSize().x, "Spin control is narrower than its native best size");
			Check(spin->GetSize().y >= spin->GetBestSize().y, "Spin control is shorter than its native best size");
		}
		for (wxWindow* child : window->GetChildren()) {
			CheckControls(child);
		}
	}

	void CheckPalettes() {
		g_settings.setDefaults();
		// Saved Terrain uses a private empty data directory beside this test
		// executable, never the user's AppData or installed stamp library.
		g_settings.setInteger(Config::INDIRECTORY_INSTALLATION, 1);
		ZoneBrush zone;
		g_gui.zone_brush = &zone;
		for (Theme::Type theme : { Theme::Type::System, Theme::Type::Dark, Theme::Type::Light }) {
			Theme::SetType(theme);
			wxFrame frame(nullptr, wxID_ANY, "Hidden palette validation");
			TilesetContainer tilesets;
			auto* brush = new BrushPalettePanel(&frame, tilesets, TILESET_TERRAIN);
			auto* tool = new PalettePanel(brush);
			brush->AddToolPanel(tool);
			Check(dynamic_cast<wxStaticBox*>(tool->GetParent()) != nullptr, "Tool panel was not reparented");
			// Command events must still reach the palette through the static box.
			bool delivered = false;
			const int id = wxWindow::NewControlId();
			brush->Bind(
				wxEVT_BUTTON, [&](wxCommandEvent&) { delivered = true; }, id
			);
			wxCommandEvent event(wxEVT_BUTTON, id);
			event.SetEventObject(tool);
			tool->GetEventHandler()->ProcessEvent(event);
			Check(delivered, "Command event stopped at the new static box parent");
			new HousePalettePanel(&frame);
			new WaypointPalettePanel(&frame);
			new ZonesPalettePanel(&frame);
			new SavedTerrainPalettePanel(&frame);
			new CreaturePalettePanel(&frame);
			Map map;
			House house(map);
			house.setID(65535);
			EditHouseDialog dialog(&frame, &map, &house);
			CheckControls(&frame);
		}
		g_gui.zone_brush = nullptr;
		Theme::SetType(Theme::Type::System);
	}

	class CreatureXml {
	public:
		wxString path = wxFileName::CreateTempFileName("nexamap-creature-test-");
		~CreatureXml() {
			wxRemoveFile(path);
		}
		void load(CreatureDatabase& db, const wxString& contents, bool standard, wxArrayString& warnings) {
			wxFile file(path, wxFile::write);
			Check(file.IsOpened() && file.Write(contents) && file.Close(), "Cannot write creature fixture");
			wxString error;
			Check(db.loadFromXML(FileName(path), standard, error, warnings), "Cannot load creature fixture");
		}
	};

	void CheckCreatureCatalog() {
		CreatureDatabase first, independent;
		CreatureXml xml;
		wxArrayString warnings;
		xml.load(first, "<creatures><creature type='monster' name='Cobra' lookitem='10'/></creatures>", true, warnings);
		CreatureType* original = first["cobra"];
		Check(original && warnings.empty(), "Initial catalog load failed");
		xml.load(first, "<creatures><creature type='monster' name='COBRA' lookitem='10'/></creatures>", true, warnings);
		Check(first.size() == 1 && first["cobra"] == original && warnings.empty(), "Identical catalog entry was duplicated or warned");
		xml.load(first, "<npcs><npc name='Cobra' lookitem='20'/></npcs>", true, warnings);
		Check(warnings.empty() && first["cobra"] == original && !original->isNpc && original->outfit.lookItem == 10, "Shared monster/NPC name changed the existing fallback");
		xml.load(first, "<monsters><monster name='Cobra' lookitem='30'/></monsters>", true, warnings);
		Check(warnings.size() == 1 && warnings[0].Contains("conflicting definition") && original->outfit.lookItem == 10, "Real conflicting definitions must remain visible");
		warnings.clear();
		xml.load(first, "<npcs><npc name='Cobra' lookitem='40'/></npcs>", false, warnings);
		Check(warnings.size() == 1, "Conflicting user overlay was mistaken for a shared standard catalog");
		warnings.clear();
		xml.load(independent, "<npcs><npc name='Cobra' lookitem='50'/></npcs>", true, warnings);
		Check(independent["cobra"]->isNpc && independent["cobra"]->outfit.lookItem == 50 && first["cobra"]->outfit.lookItem == 10, "Independent creature databases contaminated each other");
		first.clear();
		xml.load(first, "<npcs><npc name='Cobra' lookitem='60'/></npcs>", true, warnings);
		Check(first.size() == 1 && first["cobra"]->isNpc && first["cobra"]->outfit.lookItem == 60, "Catalog reload retained a previous definition");
	}
}

void RunStartupValidationTests() {
	CaptureLog log;
	wxLog* previous = wxLog::SetActiveTarget(&log);
	try {
		CheckPalettes();
		for (const wxString& message : log.messages) {
			Check(!message.Contains("should be created") && !message.Contains("too small"), "Palette construction emitted a layout warning");
		}
		std::cout << "PASS hidden native palette hierarchy, numeric sizing, command propagation and theme construction\n";
		CheckCreatureCatalog();
		std::cout << "PASS identical, shared-name, conflicting, user-overlay, independent and reloaded creature catalogs\n";
		log.messages.clear();
		std::ostringstream captured;
		auto* oldBuffer = std::cerr.rdbuf(captured.rdbuf());
		{
			std::unique_ptr<wxLog> diagnostic(CreateDiagnosticLogTargetForTests());
			wxLogDebug("startup debug probe");
			wxLogMessage("startup info probe");
			wxLogWarning("startup warning probe");
		}
		std::cerr.rdbuf(oldBuffer);
		const std::string output = captured.str();
		const auto match = output.find("startup debug probe");
		Check(match != std::string::npos && output.find("startup debug probe", match + 1) == std::string::npos, "Debug message was lost or duplicated");
		Check(log.messages.size() == 2 && log.messages[1] == "startup warning probe", "Normal wx logging was interrupted");
		Check(output.find("startup warning probe") != std::string::npos, "Warning was not mirrored to diagnostics");
		std::cout << "PASS debug logged once; normal messages/warnings still reach both diagnostic and UI loggers\n";
	} catch (...) {
		g_gui.zone_brush = nullptr;
		wxLog::SetActiveTarget(previous);
		throw;
	}
	wxLog::SetActiveTarget(previous);
}
