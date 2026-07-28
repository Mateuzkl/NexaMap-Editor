//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include <map>
#include <set>
#include <utility>
#include <vector>

#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/statline.h>

#include "action.h"
#include "editor.h"
#include "gui.h"
#include "iomap_otbm.h"
#include "map.h"
#include "palette_zones.h"
#include "settings.h"
#include "zone_brush.h"
#include "zones.h"

BEGIN_EVENT_TABLE(ZonesPalettePanel, PalettePanel)
EVT_BUTTON(PALETTE_ZONES_NEW_ZONE, ZonesPalettePanel::OnClickNewZone)
EVT_BUTTON(PALETTE_ZONES_RENAME_ZONE, ZonesPalettePanel::OnClickRenameZone)
EVT_BUTTON(PALETTE_ZONES_DELETE_ZONE, ZonesPalettePanel::OnClickDeleteZone)
EVT_TOGGLEBUTTON(PALETTE_ZONES_PAINT, ZonesPalettePanel::OnClickPaintZone)
EVT_TOGGLEBUTTON(PALETTE_ZONES_ERASE, ZonesPalettePanel::OnClickEraseZone)
EVT_BUTTON(PALETTE_ZONES_APPLY_SELECTION, ZonesPalettePanel::OnClickApplyToSelection)
EVT_BUTTON(PALETTE_ZONES_REMOVE_SELECTION, ZonesPalettePanel::OnClickRemoveFromSelection)
EVT_CHECKBOX(PALETTE_ZONES_SHOW_OVERLAY, ZonesPalettePanel::OnClickShowOverlay)
EVT_BUTTON(PALETTE_ZONES_SELECT_TILES, ZonesPalettePanel::OnClickSelectZoneTiles)
EVT_BUTTON(PALETTE_ZONES_IMPORT_ZONE, ZonesPalettePanel::OnClickImportZone)
EVT_BUTTON(PALETTE_ZONES_EXPORT_ZONE, ZonesPalettePanel::OnClickExportZone)

EVT_MENU(PALETTE_ZONES_ACTIVATE, ZonesPalettePanel::OnClickActivateZone)
EVT_MENU(PALETTE_ZONES_PAINT, ZonesPalettePanel::OnClickPaintZone)
EVT_MENU(PALETTE_ZONES_RENAME_ZONE, ZonesPalettePanel::OnClickRenameZone)
EVT_MENU(PALETTE_ZONES_FIND_FIRST_TILE, ZonesPalettePanel::OnClickFindFirstTile)
EVT_MENU(PALETTE_ZONES_SELECT_TILES, ZonesPalettePanel::OnClickSelectZoneTiles)
EVT_MENU(PALETTE_ZONES_DELETE_ZONE, ZonesPalettePanel::OnClickDeleteZone)

EVT_LIST_BEGIN_LABEL_EDIT(PALETTE_ZONES_LISTBOX, NamedEntityPalettePanel::OnBeginEditLabel)
EVT_LIST_END_LABEL_EDIT(PALETTE_ZONES_LISTBOX, ZonesPalettePanel::OnEditZoneLabel)
EVT_LIST_ITEM_SELECTED(PALETTE_ZONES_LISTBOX, ZonesPalettePanel::OnClickZone)
EVT_LIST_ITEM_RIGHT_CLICK(PALETTE_ZONES_LISTBOX, ZonesPalettePanel::OnRightClickZone)
END_EVENT_TABLE()

ZonesPalettePanel::ZonesPalettePanel(wxWindow* parent, wxWindowID id) :
	NamedEntityPalettePanel(parent, id),
	zone_list(nullptr),
	active_zone_label(nullptr),
	new_zone_button(nullptr),
	rename_zone_button(nullptr),
	delete_zone_button(nullptr),
	paint_zone_button(nullptr),
	erase_zone_button(nullptr),
	apply_selection_button(nullptr),
	remove_selection_button(nullptr),
	select_tiles_button(nullptr),
	show_overlay_checkbox(nullptr),
	import_zone_button(nullptr),
	export_zone_button(nullptr),
	editing_new_zone(false),
	rebuilding_list(false) {
	auto* sidesizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Zones");

	zone_list = createEntityList(PALETTE_ZONES_LISTBOX);
	sidesizer->Add(zone_list, 1, wxEXPAND | wxBOTTOM, 4);
	sidesizer->Add(newd wxStaticLine(this), 0, wxEXPAND | wxBOTTOM, 4);

	active_zone_label = newd wxStaticText(this, wxID_ANY, "Active Zone: None");
	sidesizer->Add(active_zone_label, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

	auto* row = newd wxBoxSizer(wxHORIZONTAL);
	row->Add(new_zone_button = newd wxButton(this, PALETTE_ZONES_NEW_ZONE, "New Zone"), 1, wxEXPAND | wxRIGHT, 2);
	row->Add(rename_zone_button = newd wxButton(this, PALETTE_ZONES_RENAME_ZONE, "Rename"), 1, wxEXPAND | wxLEFT, 2);
	sidesizer->Add(row, 0, wxEXPAND | wxBOTTOM, 4);

	delete_zone_button = newd wxButton(this, PALETTE_ZONES_DELETE_ZONE, "Delete Zone");
	sidesizer->Add(delete_zone_button, 0, wxEXPAND | wxBOTTOM, 4);

	row = newd wxBoxSizer(wxHORIZONTAL);
	row->Add(paint_zone_button = newd wxToggleButton(this, PALETTE_ZONES_PAINT, "Paint Zone"), 1, wxEXPAND | wxRIGHT, 2);
	row->Add(erase_zone_button = newd wxToggleButton(this, PALETTE_ZONES_ERASE, "Erase Zone"), 1, wxEXPAND | wxLEFT, 2);
	sidesizer->Add(row, 0, wxEXPAND | wxBOTTOM, 4);

	apply_selection_button = newd wxButton(this, PALETTE_ZONES_APPLY_SELECTION, "Apply to Selection");
	sidesizer->Add(apply_selection_button, 0, wxEXPAND | wxBOTTOM, 2);

	remove_selection_button = newd wxButton(this, PALETTE_ZONES_REMOVE_SELECTION, "Remove from Selection");
	sidesizer->Add(remove_selection_button, 0, wxEXPAND | wxBOTTOM, 4);

	select_tiles_button = newd wxButton(this, PALETTE_ZONES_SELECT_TILES, "Select Zone Tiles");
	sidesizer->Add(select_tiles_button, 0, wxEXPAND | wxBOTTOM, 4);

	show_overlay_checkbox = newd wxCheckBox(this, PALETTE_ZONES_SHOW_OVERLAY, "Show Zones Overlay");
	sidesizer->Add(show_overlay_checkbox, 0, wxEXPAND | wxBOTTOM, 4);

	row = newd wxBoxSizer(wxHORIZONTAL);
	row->Add(import_zone_button = newd wxButton(this, PALETTE_ZONES_IMPORT_ZONE, "Import"), 1, wxEXPAND | wxRIGHT, 2);
	row->Add(export_zone_button = newd wxButton(this, PALETTE_ZONES_EXPORT_ZONE, "Export"), 1, wxEXPAND | wxLEFT, 2);
	sidesizer->Add(row, 0, wxEXPAND);

	SetSizerAndFit(sidesizer);
	updateControlStates();
}

void ZonesPalettePanel::SetMap(Map* m) {
	if (map != m) {
		editing_new_zone = false;
	}
	map = m;
	if (!map || !map->zones.hasZone(g_gui.zone_brush->getZone())) {
		g_gui.zone_brush->setZone(0);
		g_gui.zone_brush->setEraseMode(false);
	}
	Enable(m && m->getVersion().otbm >= MAP_OTBM_3);
}

Brush* ZonesPalettePanel::GetSelectedBrush() const {
	const unsigned int zoneId = g_gui.zone_brush->getZone();
	if (!map || !map->zones.hasZone(zoneId)) {
		g_gui.zone_brush->setZone(0);
		g_gui.zone_brush->setEraseMode(false);
		return g_gui.zone_brush;
	}

	return g_gui.zone_brush;
}

bool ZonesPalettePanel::SelectBrush(const Brush* whatbrush) {
	if (whatbrush != g_gui.zone_brush) {
		return false;
	}
	const unsigned int zoneId = g_gui.zone_brush->getZone();
	if (map && map->zones.hasZone(zoneId)) {
		selectZoneItem(zoneId);
		updateActiveZoneDisplay();
		updateControlStates();
	}
	return true;
}

PaletteType ZonesPalettePanel::GetType() const {
	return TILESET_ZONES;
}

wxString ZonesPalettePanel::GetName() const {
	return "Zone Palette";
}

void ZonesPalettePanel::OnSwitchIn() {
	NamedEntityPalettePanel::OnSwitchIn();
	OnUpdate();
}

void ZonesPalettePanel::OnUpdate() {
	if (zone_list->GetEditControl()) {
		updateActiveZoneDisplay();
		updateControlStates();
		return;
	}

	std::string topName;
	const long previousTop = zone_list->GetTopItem();
	if (previousTop >= 0 && previousTop < zone_list->GetItemCount()) {
		topName = getSelectedName(zone_list, previousTop);
	}

	rebuilding_list = true;
	zone_list->DeleteAllItems();

	if (!map) {
		g_gui.zone_brush->setZone(0);
		g_gui.zone_brush->setEraseMode(false);
		rebuilding_list = false;
		updateActiveZoneDisplay();
		updateControlStates();
		return;
	}

	unsigned int activeZoneId = g_gui.zone_brush->getZone();
	if (!map->zones.hasZone(activeZoneId)) {
		activeZoneId = 0;
		g_gui.zone_brush->setZone(0);
		g_gui.zone_brush->setEraseMode(false);
	}

	for (const auto& zone : map->zones) {
		zone_list->InsertItem(zone_list->GetItemCount(), wxstr(zone.first));
	}

	if (activeZoneId != 0) {
		selectZoneItem(activeZoneId, false);
	}

	if (!topName.empty()) {
		const long topItem = zone_list->FindItem(-1, wxstr(topName));
		if (topItem != -1) {
			zone_list->EnsureVisible(topItem);
		}
	}

	rebuilding_list = false;
	show_overlay_checkbox->SetValue(g_settings.getBoolean(Config::SHOW_ZONE_AREAS));
	updateActiveZoneDisplay();
	updateControlStates();
}

unsigned int ZonesPalettePanel::getSelectedZoneId() const {
	if (!map) {
		return 0;
	}

	const long item = getSelectedIndex(zone_list);
	if (item == -1) {
		return 0;
	}

	const std::string name = getSelectedName(zone_list, item);
	return map->zones.hasZone(name) ? map->zones.getZoneID(name) : 0;
}

long ZonesPalettePanel::findZoneItem(unsigned int zoneId) const {
	if (!map || zoneId == 0) {
		return -1;
	}

	const std::string name = map->zones.getZoneName(zoneId);
	return name.empty() ? -1 : zone_list->FindItem(-1, wxstr(name));
}

void ZonesPalettePanel::selectZoneItem(unsigned int zoneId, bool ensureVisible) {
	const long item = findZoneItem(zoneId);
	if (item == -1) {
		return;
	}

	const bool wasRebuilding = rebuilding_list;
	rebuilding_list = true;
	zone_list->SetItemState(item, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
	if (ensureVisible) {
		zone_list->EnsureVisible(item);
	}
	rebuilding_list = wasRebuilding;
}

bool ZonesPalettePanel::activateZone(unsigned int zoneId, bool eraseMode, bool showStatus) {
	if (!map || zoneId == 0 || !map->zones.hasZone(zoneId)) {
		g_gui.SetStatusText("Select a zone first.");
		return false;
	}

	g_gui.zone_brush->setZone(zoneId);
	g_gui.zone_brush->setEraseMode(eraseMode);
	selectZoneItem(zoneId);
	updateActiveZoneDisplay();
	updateControlStates();

	g_gui.SelectBrush();
	g_gui.RefreshView();

	if (showStatus) {
		const std::string name = map->zones.getZoneName(zoneId);
		g_gui.SetStatusText("Active Zone: " + name + " [ID: " + std::to_string(zoneId) + "]");
	}
	return true;
}

void ZonesPalettePanel::updateActiveZoneDisplay() {
	const unsigned int activeZoneId = g_gui.zone_brush->getZone();
	if (!map || activeZoneId == 0 || !map->zones.hasZone(activeZoneId)) {
		active_zone_label->SetLabel("Active Zone: None");
		Layout();
		return;
	}

	active_zone_label->SetLabel(
		wxstr("Active Zone: " + map->zones.getZoneName(activeZoneId) + " [ID: " + std::to_string(activeZoneId) + "]")
	);
	Layout();
}

void ZonesPalettePanel::updateControlStates() {
	const bool hasMap = map != nullptr;
	const bool hasSelectedZone = hasMap && getSelectedZoneId() != 0;
	const unsigned int activeZoneId = g_gui.zone_brush->getZone();
	const bool hasActiveZone = hasMap && activeZoneId != 0 && map->zones.hasZone(activeZoneId);

	zone_list->Enable(hasMap);
	new_zone_button->Enable(hasMap);
	rename_zone_button->Enable(hasSelectedZone);
	delete_zone_button->Enable(hasSelectedZone);
	paint_zone_button->Enable(hasActiveZone || hasSelectedZone);
	erase_zone_button->Enable(hasActiveZone || hasSelectedZone);
	apply_selection_button->Enable(hasActiveZone);
	remove_selection_button->Enable(hasActiveZone);
	select_tiles_button->Enable(hasActiveZone || hasSelectedZone);
	show_overlay_checkbox->Enable(hasMap);
	import_zone_button->Enable(hasMap);
	export_zone_button->Enable(hasMap);

	paint_zone_button->SetValue(hasActiveZone && !g_gui.zone_brush->isEraseMode());
	erase_zone_button->SetValue(hasActiveZone && g_gui.zone_brush->isEraseMode());
	if (!hasMap) {
		show_overlay_checkbox->SetValue(false);
	}
}

void ZonesPalettePanel::OnClickZone(wxListEvent& event) {
	if (!map || rebuilding_list) {
		return;
	}

	const std::string name = nstr(event.GetText());
	if (!map->zones.hasZone(name)) {
		return;
	}

	activateZone(map->zones.getZoneID(name), false);
}

void ZonesPalettePanel::OnRightClickZone(wxListEvent& event) {
	if (!map || event.GetIndex() < 0) {
		return;
	}

	const std::string name = getSelectedName(zone_list, event.GetIndex());
	if (!map->zones.hasZone(name)) {
		return;
	}

	const bool wasRebuilding = rebuilding_list;
	rebuilding_list = true;
	zone_list->SetItemState(
		event.GetIndex(),
		wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
		wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED
	);
	rebuilding_list = wasRebuilding;
	updateControlStates();

	wxMenu menu;
	menu.Append(PALETTE_ZONES_ACTIVATE, "Activate Zone");
	menu.Append(PALETTE_ZONES_PAINT, "Paint Zone");
	menu.Append(PALETTE_ZONES_RENAME_ZONE, "Rename");
	menu.AppendSeparator();
	menu.Append(PALETTE_ZONES_FIND_FIRST_TILE, "Find First Tile");
	menu.Append(PALETTE_ZONES_SELECT_TILES, "Select All Tiles in Zone (Current Floor)");
	menu.AppendSeparator();
	menu.Append(PALETTE_ZONES_DELETE_ZONE, "Delete Zone");
	PopupMenu(&menu);
}

void ZonesPalettePanel::OnEditZoneLabel(wxListEvent& event) {
	g_gui.EnableHotkeys();

	if (!map || event.GetIndex() < 0) {
		editing_new_zone = false;
		return;
	}

	if (event.IsEditCancelled()) {
		if (editing_new_zone) {
			event.Veto();
			const long item = event.GetIndex();
			CallAfter([this, item]() {
				if (item >= 0 && item < zone_list->GetItemCount() && getSelectedName(zone_list, item).empty()) {
					zone_list->DeleteItem(item);
				}
				selectZoneItem(g_gui.zone_brush->getZone());
				updateControlStates();
			});
		}
		editing_new_zone = false;
		updateControlStates();
		return;
	}

	const std::string name = nstr(event.GetLabel());
	const std::string oldName = getSelectedName(zone_list, event.GetIndex());
	const bool validName = Zones::isValidName(name);

	if (!validName || (name != oldName && map->zones.hasZone(name))) {
		event.Veto();
		g_gui.SetStatusText(!validName ? "Zone name cannot be empty." : "There already is a zone with this name.");
		if (editing_new_zone) {
			const long item = event.GetIndex();
			CallAfter([this, item]() {
				if (item >= 0 && item < zone_list->GetItemCount() && getSelectedName(zone_list, item).empty()) {
					zone_list->DeleteItem(item);
				}
				selectZoneItem(g_gui.zone_brush->getZone());
				updateControlStates();
			});
		}
		editing_new_zone = false;
		updateControlStates();
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor || &editor->map != map) {
		event.Veto();
		editing_new_zone = false;
		return;
	}

	if (editing_new_zone) {
		const unsigned int zoneId = map->zones.getEmptyID();
		if (zoneId == 0) {
			event.Veto();
			zone_list->DeleteItem(event.GetIndex());
			editing_new_zone = false;
			g_gui.SetStatusText("No free zone IDs are available.");
			return;
		}
		Action* action = editor->actionQueue->createAction(ACTION_ZONE_EDIT);
		action->addChange(Change::CreateZone(name, zoneId, true));
		editor->addAction(action);

		if (!map->zones.hasZone(name)) {
			event.Veto();
			zone_list->DeleteItem(event.GetIndex());
			editing_new_zone = false;
			g_gui.SetStatusText("Failed to create zone.");
			return;
		}

		editing_new_zone = false;
		zone_list->SetItem(event.GetIndex(), 0, wxstr(name));
		g_gui.zone_brush->setZone(zoneId);
		g_gui.zone_brush->setEraseMode(false);
		updateActiveZoneDisplay();
		updateControlStates();
		g_gui.SelectBrush();
		g_gui.RefreshView();
		g_gui.SetStatusText("Created and activated Zone: " + name + " [ID: " + std::to_string(zoneId) + "]");
		return;
	}

	if (name == oldName) {
		updateControlStates();
		return;
	}

	if (!map->zones.hasZone(oldName)) {
		event.Veto();
		g_gui.SetStatusText("The selected zone no longer exists.");
		return;
	}

	const unsigned int zoneId = map->zones.getZoneID(oldName);
	Action* action = editor->actionQueue->createAction(ACTION_ZONE_EDIT);
	action->addChange(Change::RenameZone(oldName, name));
	editor->addAction(action);

	if (!map->zones.hasZone(name) || map->zones.getZoneID(name) != zoneId) {
		event.Veto();
		g_gui.SetStatusText("Failed to rename zone.");
		return;
	}

	zone_list->SetItem(event.GetIndex(), 0, wxstr(name));
	updateActiveZoneDisplay();
	updateControlStates();
	g_gui.RefreshView();
	g_gui.SetStatusText("Renamed Zone: " + name + " [ID: " + std::to_string(zoneId) + "]");
}

void ZonesPalettePanel::OnClickNewZone(wxCommandEvent&) {
	if (!map || zone_list->GetEditControl()) {
		return;
	}

	editing_new_zone = true;
	const long item = zone_list->InsertItem(zone_list->GetItemCount(), "");
	rebuilding_list = true;
	zone_list->SetItemState(item, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
	rebuilding_list = false;
	zone_list->EnsureVisible(item);
	zone_list->EditLabel(item);
}

void ZonesPalettePanel::OnClickRenameZone(wxCommandEvent&) {
	if (!map || zone_list->GetEditControl()) {
		return;
	}

	const long item = getSelectedIndex(zone_list);
	if (item == -1 || getSelectedZoneId() == 0) {
		g_gui.SetStatusText("Select a zone first.");
		return;
	}

	editing_new_zone = false;
	zone_list->EditLabel(item);
}

void ZonesPalettePanel::OnClickDeleteZone(wxCommandEvent&) {
	if (!map) {
		return;
	}

	const unsigned int zoneId = getSelectedZoneId();
	if (zoneId == 0) {
		g_gui.SetStatusText("Select a zone first.");
		return;
	}
	const std::string name = map->zones.getZoneName(zoneId);

	const wxString message = wxstr(
		"Delete Zone \"" + name + "\" [ID: " + std::to_string(zoneId) + "]?\n\n"
																		"This will remove "
		+ name + " from every tile in the map.\n"
				 "This operation is stored as one undo/redo action."
	);
	wxMessageDialog dialog(this, message, "Delete Zone", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
	dialog.SetYesNoLabels("Delete", "Cancel");
	if (dialog.ShowModal() != wxID_YES) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor || &editor->map != map) {
		return;
	}

	Action* action = editor->actionQueue->createAction(ACTION_ZONE_EDIT);
	action->addChange(Change::CreateZone(name, zoneId, false));

	const uint64_t total = map->getTileCount();
	const bool showProgress = total > 10000;
	if (showProgress) {
		g_gui.CreateLoadBar("Deleting zone from map...");
	}

	uint64_t scanned = 0;
	size_t changedTiles = 0;
	for (MapIterator it = map->begin(); it != map->end(); ++it) {
		Tile* tile = (*it)->get();
		if (tile && tile->hasZone(zoneId)) {
			Tile* newTile = tile->deepCopy(*map);
			newTile->removeZone(zoneId);
			action->addChange(newd Change(newTile));
			++changedTiles;
		}

		++scanned;
		if (showProgress && scanned % 4096 == 0) {
			g_gui.SetLoadDone(static_cast<int32_t>(scanned / double(total) * 100.0));
		}
	}
	editor->addAction(action);
	if (showProgress) {
		g_gui.DestroyLoadBar();
	}

	if (g_gui.zone_brush->getZone() == zoneId) {
		g_gui.zone_brush->setZone(0);
		g_gui.zone_brush->setEraseMode(false);
	}
	OnUpdate();
	g_gui.RefreshView();
	g_gui.SetStatusText(
		"Deleted Zone: " + name + " [ID: " + std::to_string(zoneId) + "] from " + std::to_string(changedTiles) + " tiles."
	);
}

void ZonesPalettePanel::OnClickActivateZone(wxCommandEvent&) {
	unsigned int zoneId = getSelectedZoneId();
	if (zoneId == 0) {
		zoneId = g_gui.zone_brush->getZone();
	}
	activateZone(zoneId, g_gui.zone_brush->isEraseMode());
}

void ZonesPalettePanel::OnClickPaintZone(wxCommandEvent&) {
	unsigned int zoneId = getSelectedZoneId();
	if (zoneId == 0) {
		zoneId = g_gui.zone_brush->getZone();
	}
	if (activateZone(zoneId, false, false)) {
		g_gui.SetStatusText(
			"Paint Zone: " + map->zones.getZoneName(zoneId) + " [ID: " + std::to_string(zoneId) + "]. Left click paints; Ctrl+left click erases the active zone."
		);
	}
}

void ZonesPalettePanel::OnClickEraseZone(wxCommandEvent&) {
	unsigned int zoneId = getSelectedZoneId();
	if (zoneId == 0) {
		zoneId = g_gui.zone_brush->getZone();
	}
	if (activateZone(zoneId, true, false)) {
		g_gui.SetStatusText(
			"Erase Zone: " + map->zones.getZoneName(zoneId) + " [ID: " + std::to_string(zoneId) + "]. Left click removes only the active zone."
		);
	}
}

void ZonesPalettePanel::applyZoneToSelection(bool remove) {
	const unsigned int activeZoneId = g_gui.zone_brush->getZone();
	if (!map || activeZoneId == 0 || !map->zones.hasZone(activeZoneId)) {
		g_gui.SetStatusText("Select a zone first.");
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor || &editor->map != map) {
		return;
	}
	if (editor->selection.size() == 0) {
		g_gui.SetStatusText("Select one or more tiles first.");
		return;
	}

	Action* action = editor->actionQueue->createAction(ACTION_DRAW);
	size_t changedTiles = 0;
	for (Tile* tile : editor->selection.getTiles()) {
		if (!tile || !tile->hasGround()) {
			continue;
		}

		const bool alreadyHasZone = tile->hasZone(activeZoneId);
		if ((!remove && alreadyHasZone) || (remove && !alreadyHasZone)) {
			continue;
		}

		Tile* newTile = tile->deepCopy(*map);
		if (remove) {
			newTile->removeZone(activeZoneId);
		} else {
			newTile->addZone(activeZoneId);
		}
		action->addChange(newd Change(newTile));
		++changedTiles;
	}
	editor->addAction(action);

	g_gui.RefreshView();
	const std::string name = map->zones.getZoneName(activeZoneId);
	g_gui.SetStatusText(
		std::string(remove ? "Removed " : "Applied ") + name + " [ID: " + std::to_string(activeZoneId) + (remove ? "] from " : "] to ") + std::to_string(changedTiles) + " tiles."
	);
}

void ZonesPalettePanel::OnClickApplyToSelection(wxCommandEvent&) {
	applyZoneToSelection(false);
}

void ZonesPalettePanel::OnClickRemoveFromSelection(wxCommandEvent&) {
	applyZoneToSelection(true);
}

void ZonesPalettePanel::OnClickShowOverlay(wxCommandEvent& event) {
	g_settings.setInteger(Config::SHOW_ZONE_AREAS, event.IsChecked() ? 1 : 0);
	g_gui.UpdateMenubar();
	g_gui.RefreshView();
	g_gui.SetStatusText(event.IsChecked() ? "Zones overlay enabled." : "Zones overlay disabled.");
}

void ZonesPalettePanel::OnClickFindFirstTile(wxCommandEvent&) {
	if (!map) {
		return;
	}

	unsigned int zoneId = getSelectedZoneId();
	if (zoneId == 0) {
		zoneId = g_gui.zone_brush->getZone();
	}
	if (zoneId == 0 || !map->zones.hasZone(zoneId)) {
		g_gui.SetStatusText("Select a zone first.");
		return;
	}

	const Position position = map->getZonePosition(zoneId);
	Tile* tile = map->getTile(position);
	if (!tile || !tile->hasZone(zoneId)) {
		g_gui.SetStatusText("No tiles use the selected zone.");
		return;
	}

	g_gui.SetScreenCenterPosition(position);
	g_gui.SetStatusText(
		"Found " + map->zones.getZoneName(zoneId) + " [ID: " + std::to_string(zoneId) + "] at " + std::to_string(position.x) + ", " + std::to_string(position.y) + ", " + std::to_string(position.z) + "."
	);
}

void ZonesPalettePanel::OnClickSelectZoneTiles(wxCommandEvent&) {
	if (!map) {
		return;
	}

	unsigned int zoneId = getSelectedZoneId();
	if (zoneId == 0) {
		zoneId = g_gui.zone_brush->getZone();
	}
	if (!activateZone(zoneId, g_gui.zone_brush->isEraseMode(), false)) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor || &editor->map != map) {
		return;
	}

	const int floor = g_gui.GetCurrentFloor();
	const uint64_t total = map->getTileCount();
	const bool showProgress = total > 10000;
	if (showProgress) {
		g_gui.CreateLoadBar("Selecting zone tiles on current floor...");
	}

	g_gui.SetSelectionMode();
	editor->selection.start();
	editor->selection.clear();
	editor->selection.commit();

	uint64_t scanned = 0;
	size_t selectedTiles = 0;
	for (MapIterator it = map->begin(); it != map->end(); ++it) {
		Tile* tile = (*it)->get();
		if (tile && tile->getZ() == floor && tile->hasZone(zoneId)) {
			editor->selection.add(tile);
			++selectedTiles;
		}

		++scanned;
		if (showProgress && scanned % 4096 == 0) {
			g_gui.SetLoadDone(static_cast<int32_t>(scanned / double(total) * 100.0));
		}
	}
	editor->selection.finish();
	editor->selection.updateSelectionCount();

	if (showProgress) {
		g_gui.DestroyLoadBar();
	}
	g_gui.RefreshView();
	g_gui.SetStatusText(
		"Selected " + std::to_string(selectedTiles) + " tiles from " + map->zones.getZoneName(zoneId) + " on floor " + std::to_string(floor) + "."
	);
}

void ZonesPalettePanel::OnClickExportZone(wxCommandEvent&) {
	if (!map) {
		g_gui.SetStatusText("No map loaded.");
		return;
	}

	wxFileDialog dialog(this, "Export Zones", "", "", "XML files (*.xml)|*.xml", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	const std::string filepath = nstr(dialog.GetPath());
	pugi::xml_document document;
	if (!IOMapOTBM::saveZones(*map, document)) {
		g_gui.SetStatusText("Failed to export zones.");
		return;
	}

	if (document.save_file(filepath.c_str(), "\t", pugi::format_default, pugi::encoding_utf8)) {
		g_gui.SetStatusText("Zones exported successfully to " + filepath);
	} else {
		g_gui.SetStatusText("Failed to export zones.");
	}
}

void ZonesPalettePanel::OnClickImportZone(wxCommandEvent&) {
	if (!map) {
		g_gui.SetStatusText("No map loaded.");
		return;
	}

	wxFileDialog dialog(this, "Import Zones", "", "", "XML files (*.xml)|*.xml", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	const std::string filepath = nstr(dialog.GetPath());
	pugi::xml_document document;
	const pugi::xml_parse_result result = document.load_file(filepath.c_str());
	if (!result || !document.child("zones")) {
		g_gui.SetStatusText("Failed to import zones: invalid XML format.");
		return;
	}

	struct ImportedZone {
		std::string name;
		unsigned int id;
		bool create;
		std::set<Position> positions;
	};

	std::vector<ImportedZone> importedZones;
	std::set<std::string> fileNames;
	std::set<unsigned int> fileIds;
	size_t invalidDefinitions = 0;
	size_t duplicateDefinitions = 0;
	size_t conflicts = 0;
	size_t invalidPositions = 0;
	size_t reusedZones = 0;

	for (pugi::xml_node zoneNode : document.child("zones").children("zone")) {
		const std::string name = zoneNode.attribute("name").as_string();
		const unsigned int id = zoneNode.attribute("zoneid").as_uint(zoneNode.attribute("id").as_uint());
		if (!Zones::isValidName(name) || !Zones::isValidID(id)) {
			++invalidDefinitions;
			continue;
		}
		const bool uniqueName = fileNames.insert(name).second;
		const bool uniqueId = fileIds.insert(id).second;
		if (!uniqueName || !uniqueId) {
			++duplicateDefinitions;
			continue;
		}

		const bool nameExists = map->zones.hasZone(name);
		const bool idExists = map->zones.hasZone(id);
		const bool exactExisting = map->zones.hasZone(name, id);
		const bool conflict = (nameExists || idExists) && !exactExisting;

		ImportedZone imported { name, id, !exactExisting, {} };
		for (pugi::xml_node positionNode : zoneNode.children("position")) {
			if (!positionNode.attribute("x") || !positionNode.attribute("y") || !positionNode.attribute("z")) {
				++invalidPositions;
				continue;
			}

			const Position position(
				positionNode.attribute("x").as_int(),
				positionNode.attribute("y").as_int(),
				positionNode.attribute("z").as_int()
			);
			Tile* tile = position.isValid() ? map->getTile(position) : nullptr;
			if (!tile || !tile->hasGround()) {
				++invalidPositions;
				continue;
			}
			imported.positions.insert(position);
		}

		if (conflict) {
			++conflicts;
			continue;
		}
		if (exactExisting) {
			++reusedZones;
		}
		importedZones.push_back(std::move(imported));
	}

	if (invalidDefinitions != 0 || duplicateDefinitions != 0) {
		const std::string report = "Import aborted before changing the map.\n\nInvalid zone definitions: " + std::to_string(invalidDefinitions) + "\nDuplicate names or IDs in file: " + std::to_string(duplicateDefinitions);
		wxMessageBox(wxstr(report), "Zone Import Error", wxOK | wxICON_ERROR, this);
		g_gui.SetStatusText("Zone import aborted: invalid or duplicate definitions.");
		return;
	}

	if (importedZones.empty()) {
		g_gui.SetStatusText(
			"No zones imported. Conflicts: " + std::to_string(conflicts) + ", invalid positions: " + std::to_string(invalidPositions) + "."
		);
		return;
	}

	if (conflicts != 0 || invalidPositions != 0) {
		const std::string report = "Zone import validation found issues:\n\nConflicting zones (kept unchanged): " + std::to_string(conflicts) + "\nInvalid positions (skipped): " + std::to_string(invalidPositions) + "\n\nImport all remaining valid data as one undoable action?";
		wxMessageDialog confirm(this, wxstr(report), "Confirm Zone Import", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
		confirm.SetYesNoLabels("Import", "Cancel");
		if (confirm.ShowModal() != wxID_YES) {
			return;
		}
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor || &editor->map != map) {
		return;
	}

	Action* action = editor->actionQueue->createAction(ACTION_ZONE_EDIT);
	std::map<Position, std::set<unsigned int>> tileZones;
	size_t createdZones = 0;
	for (const ImportedZone& imported : importedZones) {
		if (imported.create) {
			action->addChange(Change::CreateZone(imported.name, imported.id, true));
			++createdZones;
		}
		for (const Position& position : imported.positions) {
			tileZones[position].insert(imported.id);
		}
	}

	size_t appliedAssociations = 0;
	for (const auto& [position, zoneIds] : tileZones) {
		Tile* tile = map->getTile(position);
		if (!tile) {
			continue;
		}

		Tile* newTile = nullptr;
		for (unsigned int zoneId : zoneIds) {
			if (!tile->hasZone(zoneId)) {
				if (!newTile) {
					newTile = tile->deepCopy(*map);
				}
				newTile->addZone(zoneId);
				++appliedAssociations;
			}
		}
		if (newTile) {
			action->addChange(newd Change(newTile));
		}
	}

	editor->addAction(action);

	const unsigned int activeZoneId = importedZones.front().id;
	g_gui.zone_brush->setZone(activeZoneId);
	g_gui.zone_brush->setEraseMode(false);
	OnUpdate();
	activateZone(activeZoneId, false, false);
	g_gui.SetStatusText(
		"Imported " + std::to_string(createdZones) + " new zones, reused " + std::to_string(reusedZones) + ", applied " + std::to_string(appliedAssociations) + " tile associations, skipped " + std::to_string(conflicts) + " conflicts and " + std::to_string(invalidPositions) + " invalid positions."
	);
}
