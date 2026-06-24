//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "gui.h"
#include "palette_zones.h"
#include "zone_brush.h"
#include "map.h"
#include "iomap_otbm.h"

BEGIN_EVENT_TABLE(ZonesPalettePanel, PalettePanel)
EVT_BUTTON(PALETTE_ZONES_ADD_ZONE, ZonesPalettePanel::OnClickAddZone)
EVT_BUTTON(PALETTE_ZONES_REMOVE_ZONE, ZonesPalettePanel::OnClickRemoveZone)
EVT_BUTTON(PALETTE_ZONES_IMPORT_ZONE, ZonesPalettePanel::OnClickImportZone)
EVT_BUTTON(PALETTE_ZONES_EXPORT_ZONE, ZonesPalettePanel::OnClickExportZone)

EVT_LIST_BEGIN_LABEL_EDIT(PALETTE_ZONES_LISTBOX, NamedEntityPalettePanel::OnBeginEditLabel)
EVT_LIST_END_LABEL_EDIT(PALETTE_ZONES_LISTBOX, ZonesPalettePanel::OnEditZoneLabel)
EVT_LIST_ITEM_SELECTED(PALETTE_ZONES_LISTBOX, ZonesPalettePanel::OnClickZone)
EVT_LIST_ITEM_RIGHT_CLICK(PALETTE_ZONES_LISTBOX, ZonesPalettePanel::OnRightClickZone)
END_EVENT_TABLE()

ZonesPalettePanel::ZonesPalettePanel(wxWindow* parent, wxWindowID id) :
	NamedEntityPalettePanel(parent, id) {
	wxSizer* sidesizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Zones");

	zone_list = createEntityList(PALETTE_ZONES_LISTBOX);
	sidesizer->Add(zone_list, 1, wxEXPAND);

	wxSizer* top_button_sizer = newd wxBoxSizer(wxHORIZONTAL);
	top_button_sizer->Add(add_zone_button = newd wxButton(this, PALETTE_ZONES_ADD_ZONE, "Add", wxDefaultPosition, wxSize(50, -1)), 1, wxEXPAND);
	top_button_sizer->Add(remove_zone_button = newd wxButton(this, PALETTE_ZONES_REMOVE_ZONE, "Remove", wxDefaultPosition, wxSize(70, -1)), 1, wxEXPAND);
	sidesizer->Add(top_button_sizer, 0, wxEXPAND);

	wxSizer* bottom_button_sizer = newd wxBoxSizer(wxHORIZONTAL);
	bottom_button_sizer->Add(import_zone_button = newd wxButton(this, PALETTE_ZONES_IMPORT_ZONE, "Import", wxDefaultPosition, wxSize(70, -1)), 1, wxEXPAND);
	bottom_button_sizer->Add(export_zone_button = newd wxButton(this, PALETTE_ZONES_EXPORT_ZONE, "Export", wxDefaultPosition, wxSize(70, -1)), 1, wxEXPAND);
	sidesizer->Add(bottom_button_sizer, 0, wxEXPAND);

	SetSizerAndFit(sidesizer);
}

ZonesPalettePanel::~ZonesPalettePanel() {
	////
}

void ZonesPalettePanel::SetMap(Map* m) {
	map = m;
	this->Enable(m && m->getVersion().otbm >= MAP_OTBM_3);
}

Brush* ZonesPalettePanel::GetSelectedBrush() const {
	if (!map) {
		g_gui.zone_brush->setZone(0);
		return g_gui.zone_brush;
	}

	long item = getSelectedIndex(zone_list);
	g_gui.zone_brush->setZone(
		item == -1 || !map->zones.hasZone(getSelectedName(zone_list, item)) ? 0 : map->zones.getZoneID(getSelectedName(zone_list, item))
	);
	return g_gui.zone_brush;
}

bool ZonesPalettePanel::SelectBrush(const Brush* whatbrush) {
	ASSERT(whatbrush == g_gui.zone_brush);
	return false;
}

PaletteType ZonesPalettePanel::GetType() const {
	return TILESET_ZONES;
}

wxString ZonesPalettePanel::GetName() const {
	return "Zone Palette";
}

void ZonesPalettePanel::OnUpdate() {
	if (!map) {
		zone_list->DeleteAllItems();
		zone_list->Enable(false);
		add_zone_button->Enable(false);
		remove_zone_button->Enable(false);
		import_zone_button->Enable(false);
		export_zone_button->Enable(false);
		return;
	}

	if (wxTextCtrl* tc = zone_list->GetEditControl()) {
		std::string name = nstr(tc->GetValue());
		if (map->zones.hasZone(name)) {
			map->zones.removeZone(name);
			map->cleanDeletedZones();
		}
	}
	zone_list->DeleteAllItems();

	zone_list->Enable(true);
	add_zone_button->Enable(true);
	remove_zone_button->Enable(true);
	import_zone_button->Enable(true);
	export_zone_button->Enable(true);

	Zones& zones = map->zones;
	for (ZoneMap::const_iterator iter = zones.begin(); iter != zones.end(); ++iter) {
		zone_list->InsertItem(0, wxstr(iter->first));
	}
}

void ZonesPalettePanel::OnClickZone(wxListEvent& event) {
	if (!map) {
		return;
	}

	std::string name = nstr(event.GetText());
	if (map->zones.hasZone(name)) {
		auto zoneId = map->zones.getZoneID(name);
		g_gui.zone_brush->setZone(zoneId);
	}
}

void ZonesPalettePanel::OnRightClickZone(wxListEvent& event) {
	if (!map) {
		return;
	}

	std::string name = nstr(event.GetText());
	if (map->zones.hasZone(name)) {
		auto zoneId = map->zones.getZoneID(name);
		g_gui.zone_brush->setZone(zoneId);
		g_gui.SetScreenCenterPosition(map->getZonePosition(zoneId));
	}
}

void ZonesPalettePanel::OnEditZoneLabel(wxListEvent& event) {
	g_gui.EnableHotkeys();

	if (!map) {
		return;
	}

	std::string name = nstr(event.GetLabel());
	std::string oldName = getSelectedName(zone_list, event.GetIndex());

	if (event.IsEditCancelled()) {
		return;
	}

	if (name == "") {
		map->zones.removeZone(oldName);
		g_gui.RefreshPalettes();
	} else {
		if (name != oldName) {
			if (map->zones.hasZone(name)) {
				g_gui.SetStatusText("There already is a zone with this name.");
				event.Veto();
				if (oldName == "") {
					map->zones.removeZone(oldName);
					g_gui.RefreshPalettes();
				}
			} else {
				map->zones.removeZone(oldName);
				map->zones.addZone(name);
				auto zoneId = map->zones.getZoneID(name);
				g_gui.zone_brush->setZone(zoneId);
				refresh_timer.Start(PALETTE_DELAYED_REFRESH_MS, true);
			}
		}
	}
}

void ZonesPalettePanel::OnClickAddZone(wxCommandEvent& event) {
	if (map) {
		if (map->zones.addZone("")) {
			long i = zone_list->InsertItem(0, "");
			zone_list->EditLabel(i);
		} else {
			long i = zone_list->FindItem(-1, "");
			zone_list->EditLabel(i);
		}
	}
}

void ZonesPalettePanel::OnClickRemoveZone(wxCommandEvent& event) {
	if (!map) {
		return;
	}

	long item = getSelectedIndex(zone_list);
	if (item != -1) {
		std::string name = getSelectedName(zone_list, item);
		if (map->zones.hasZone(name)) {
			map->zones.removeZone(name);
			map->cleanDeletedZones();
		}
		zone_list->DeleteItem(item);
		refresh_timer.Start(PALETTE_DELAYED_REFRESH_MS, true);
	}
}

void ZonesPalettePanel::OnClickExportZone(wxCommandEvent& event) {
	if (!map) {
		g_gui.SetStatusText("No map loaded.");
		return;
	}
	wxFileDialog dlg(this, "Export Zones", "", "", "XML files (*.xml)|*.xml", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}
	std::string filepath = nstr(dlg.GetPath());
	pugi::xml_document doc;
	if (!IOMapOTBM::saveZones(*map, doc)) {
		g_gui.SetStatusText("Failed to export zones.");
		return;
	}
	if (doc.save_file(filepath.c_str(), "\t", pugi::format_default, pugi::encoding_utf8)) {
		g_gui.SetStatusText("Zones exported successfully to " + filepath);
	} else {
		g_gui.SetStatusText("Failed to export zones.");
	}
}

void ZonesPalettePanel::OnClickImportZone(wxCommandEvent& event) {
	if (!map) {
		g_gui.SetStatusText("No map loaded.");
		return;
	}
	wxFileDialog dlg(this, "Import Zones", "", "", "XML files (*.xml)|*.xml", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}
	std::string filepath = nstr(dlg.GetPath());
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filepath.c_str());
	if (!result) {
		g_gui.SetStatusText("Failed to import zones: Invalid XML format.");
		return;
	}
	int imported = 0;
	for (pugi::xml_node zone_node : doc.child("zones").children("zone")) {
		std::string name = zone_node.attribute("name").as_string();
		unsigned int id = zone_node.attribute("id").as_uint();
		if (map->zones.hasZone(name) || map->zones.hasZone(id)) {
			continue;
		}
		if (!map->zones.addZone(name, id)) {
			continue;
		}
		++imported;
		for (pugi::xml_node pos_node : zone_node.children("position")) {
			int x = pos_node.attribute("x").as_int();
			int y = pos_node.attribute("y").as_int();
			int z = pos_node.attribute("z").as_int();
			Position pos(x, y, z);
			Tile* tile = map->getTile(pos);
			if (!tile || !tile->hasGround()) {
				g_gui.SetStatusText("Warning: Invalid tile at (" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) + ") for zone '" + name + "'.");
				continue;
			}
			tile->addZone(id);
		}
	}
	g_gui.RefreshPalettes();
	g_gui.SetStatusText("Imported " + std::to_string(imported) + " zones successfully.");
}
