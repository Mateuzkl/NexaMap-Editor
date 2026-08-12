//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "palette_saved_terrain.h"

#include "editor.h"
#include "gui.h"
#include "map_tab.h"
#include "terrain_stamp/terrain_stamp.h"
#include "terrain_stamp/terrain_stamp_library.h"

namespace {
	void RefreshSavedTerrainPalettes() {
		g_gui.RefreshPalettes(nullptr, true, false);
	}
}

bool PromptAndSaveTerrainFromSelection(wxWindow* parent) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		g_gui.SetStatusText("No map loaded.");
		return false;
	}
	if (!g_gui.IsSelectionMode() || editor->selection.size() == 0) {
		g_gui.SetStatusText("Select tiles first to save a terrain stamp.");
		return false;
	}

	TerrainStamp stamp = TerrainStamp::FromSelection(*editor, g_gui.GetCurrentFloor());
	if (stamp.empty()) {
		g_gui.SetStatusText("Selection has no items to save.");
		return false;
	}

	wxTextEntryDialog dialog(parent, "Name for this terrain stamp:", "Save Terrain", "terrain");
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}

	const std::string name = nstr(dialog.GetValue().Trim(true).Trim(false));
	if (!TerrainStampLibrary::IsValidName(name)) {
		wxMessageBox("Invalid name. Use letters, numbers, spaces, - or _ (max 64).", "Save Terrain", wxOK | wxICON_ERROR, parent);
		return false;
	}

	stamp.name = name;
	std::string error;
	const std::filesystem::path path = TerrainStampLibrary::GetDirectory() / (TerrainStampLibrary::SanitizeFileStem(name) + ".json");
	if (std::filesystem::exists(path)) {
		const int answer = wxMessageBox(
			"A stamp with that name already exists. Overwrite?",
			"Save Terrain",
			wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
			parent
		);
		if (answer != wxYES) {
			return false;
		}
	}

	if (!TerrainStampLibrary::Save(stamp, error, true)) {
		wxMessageBox(wxstr(error), "Save Terrain", wxOK | wxICON_ERROR, parent);
		return false;
	}

	RefreshSavedTerrainPalettes();
	g_gui.SetStatusText(wxString::Format("Saved terrain stamp \"%s\" (%zu tiles).", wxstr(name), stamp.tileCount()));
	g_gui.SelectPalettePage(TILESET_SAVED_TERRAIN);
	return true;
}

BEGIN_EVENT_TABLE(SavedTerrainPalettePanel, PalettePanel)
EVT_LIST_ITEM_SELECTED(PALETTE_SAVED_TERRAIN_LISTBOX, SavedTerrainPalettePanel::OnClickStamp)
EVT_LIST_ITEM_ACTIVATED(PALETTE_SAVED_TERRAIN_LISTBOX, SavedTerrainPalettePanel::OnClickStamp)
EVT_BUTTON(PALETTE_SAVED_TERRAIN_SAVE, SavedTerrainPalettePanel::OnClickSaveFromSelection)
EVT_BUTTON(PALETTE_SAVED_TERRAIN_PLACE, SavedTerrainPalettePanel::OnClickPlace)
EVT_BUTTON(PALETTE_SAVED_TERRAIN_RENAME, SavedTerrainPalettePanel::OnClickRename)
EVT_BUTTON(PALETTE_SAVED_TERRAIN_DELETE, SavedTerrainPalettePanel::OnClickDelete)
EVT_BUTTON(PALETTE_SAVED_TERRAIN_IMPORT, SavedTerrainPalettePanel::OnClickImport)
EVT_BUTTON(PALETTE_SAVED_TERRAIN_EXPORT, SavedTerrainPalettePanel::OnClickExport)
END_EVENT_TABLE()

SavedTerrainPalettePanel::SavedTerrainPalettePanel(wxWindow* parent, wxWindowID id) :
	PalettePanel(parent, id) {
	auto* sidesizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Saved Terrains");

	stamp_list = newd wxListCtrl(this, PALETTE_SAVED_TERRAIN_LISTBOX, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER | wxLC_VRULES);
	stamp_list->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 180);
	sidesizer->Add(stamp_list, 1, wxEXPAND);

	auto* row1 = newd wxBoxSizer(wxHORIZONTAL);
	row1->Add(save_button = newd wxButton(this, PALETTE_SAVED_TERRAIN_SAVE, "Save from Selection"), 1, wxEXPAND | wxRIGHT, 2);
	row1->Add(place_button = newd wxButton(this, PALETTE_SAVED_TERRAIN_PLACE, "Place"), 1, wxEXPAND | wxLEFT, 2);
	sidesizer->Add(row1, 0, wxEXPAND | wxTOP, 4);

	auto* row2 = newd wxBoxSizer(wxHORIZONTAL);
	row2->Add(rename_button = newd wxButton(this, PALETTE_SAVED_TERRAIN_RENAME, "Rename"), 1, wxEXPAND | wxRIGHT, 2);
	row2->Add(delete_button = newd wxButton(this, PALETTE_SAVED_TERRAIN_DELETE, "Delete"), 1, wxEXPAND | wxLEFT, 2);
	sidesizer->Add(row2, 0, wxEXPAND | wxTOP, 4);

	auto* row3 = newd wxBoxSizer(wxHORIZONTAL);
	row3->Add(import_button = newd wxButton(this, PALETTE_SAVED_TERRAIN_IMPORT, "Import"), 1, wxEXPAND | wxRIGHT, 2);
	row3->Add(export_button = newd wxButton(this, PALETTE_SAVED_TERRAIN_EXPORT, "Export"), 1, wxEXPAND | wxLEFT, 2);
	sidesizer->Add(row3, 0, wxEXPAND | wxTOP, 4);

	SetSizerAndFit(sidesizer);
	ReloadList();
	updateControlStates();
}

wxString SavedTerrainPalettePanel::GetName() const {
	return "Saved Terrains";
}

PaletteType SavedTerrainPalettePanel::GetType() const {
	return TILESET_SAVED_TERRAIN;
}

Brush* SavedTerrainPalettePanel::GetSelectedBrush() const {
	return nullptr;
}

bool SavedTerrainPalettePanel::SelectBrush(const Brush*) {
	return false;
}

void SavedTerrainPalettePanel::OnSwitchIn() {
	ReloadList();
	g_gui.SelectBrush();
}

void SavedTerrainPalettePanel::OnUpdate() {
	ReloadList();
}

void SavedTerrainPalettePanel::ReloadList() {
	const long selected = stamp_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	wxString selectedName;
	if (selected != -1) {
		selectedName = stamp_list->GetItemText(selected);
	}

	stamp_list->DeleteAllItems();
	const std::vector<std::string> names = TerrainStampLibrary::ListNames();
	long restoreIndex = -1;
	for (size_t index = 0; index < names.size(); ++index) {
		const long item = stamp_list->InsertItem(static_cast<long>(index), wxstr(names[index]));
		if (selectedName == wxstr(names[index])) {
			restoreIndex = item;
		}
	}
	if (restoreIndex != -1) {
		stamp_list->SetItemState(restoreIndex, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
	}
	updateControlStates();
}

wxString SavedTerrainPalettePanel::getSelectedName() const {
	const long selected = stamp_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (selected == -1) {
		return wxEmptyString;
	}
	return stamp_list->GetItemText(selected);
}

void SavedTerrainPalettePanel::updateControlStates() {
	const bool hasSelection = !getSelectedName().empty();
	const bool mapLoaded = g_gui.IsEditorOpen();
	save_button->Enable(mapLoaded);
	place_button->Enable(mapLoaded && hasSelection);
	rename_button->Enable(hasSelection);
	delete_button->Enable(hasSelection);
	export_button->Enable(hasSelection);
	import_button->Enable(true);
}

void SavedTerrainPalettePanel::OnClickStamp(wxListEvent& event) {
	updateControlStates();
	if (event.GetEventType() == wxEVT_LIST_ITEM_ACTIVATED) {
		placeSelectedStamp();
	}
}

void SavedTerrainPalettePanel::OnClickSaveFromSelection(wxCommandEvent&) {
	PromptAndSaveTerrainFromSelection(this);
	ReloadList();
}

bool SavedTerrainPalettePanel::placeSelectedStamp() {
	const wxString name = getSelectedName();
	if (name.empty()) {
		return false;
	}
	if (!g_gui.IsEditorOpen()) {
		g_gui.SetStatusText("No map loaded.");
		return false;
	}

	TerrainStamp stamp;
	std::string error;
	if (!TerrainStampLibrary::Load(nstr(name), stamp, error)) {
		wxMessageBox(wxstr(error), "Place Terrain", wxOK | wxICON_ERROR, this);
		return false;
	}

	if (stamp.clientVersion != -1 && stamp.clientVersion != g_gui.GetCurrentVersionID()) {
		const int answer = wxMessageBox(
			wxString::Format(
				"This stamp was saved for client version %d, current is %d.\nPlace anyway?",
				stamp.clientVersion,
				g_gui.GetCurrentVersionID()
			),
			"Place Terrain",
			wxYES_NO | wxICON_WARNING,
			this
		);
		if (answer != wxYES) {
			return false;
		}
	}

	if (!stamp.LoadIntoCopyBuffer(g_gui.copybuffer)) {
		wxMessageBox("Failed to load stamp into the paste buffer.", "Place Terrain", wxOK | wxICON_ERROR, this);
		return false;
	}

	g_gui.PreparePaste();
	g_gui.SetStatusText(wxString::Format("Placing \"%s\" — click the map to paste.", name));
	return true;
}

void SavedTerrainPalettePanel::OnClickPlace(wxCommandEvent&) {
	placeSelectedStamp();
}

void SavedTerrainPalettePanel::OnClickRename(wxCommandEvent&) {
	const wxString oldName = getSelectedName();
	if (oldName.empty()) {
		return;
	}

	wxTextEntryDialog dialog(this, "New name:", "Rename Terrain", oldName);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}
	const std::string newName = nstr(dialog.GetValue().Trim(true).Trim(false));
	std::string error;
	if (!TerrainStampLibrary::Rename(nstr(oldName), newName, error)) {
		wxMessageBox(wxstr(error), "Rename Terrain", wxOK | wxICON_ERROR, this);
		return;
	}
	ReloadList();
}

void SavedTerrainPalettePanel::OnClickDelete(wxCommandEvent&) {
	const wxString name = getSelectedName();
	if (name.empty()) {
		return;
	}
	const int answer = wxMessageBox(
		wxString::Format("Delete terrain stamp \"%s\"?", name),
		"Delete Terrain",
		wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
		this
	);
	if (answer != wxYES) {
		return;
	}
	std::string error;
	if (!TerrainStampLibrary::Delete(nstr(name), error)) {
		wxMessageBox(wxstr(error), "Delete Terrain", wxOK | wxICON_ERROR, this);
		return;
	}
	ReloadList();
}

void SavedTerrainPalettePanel::OnClickImport(wxCommandEvent&) {
	wxFileDialog dialog(this, "Import Terrain Stamp", "", "", "JSON files (*.json)|*.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}
	TerrainStamp stamp;
	std::string error;
	if (!TerrainStampLibrary::ImportFrom(std::filesystem::path(dialog.GetPath().ToStdWstring()), stamp, error)) {
		wxMessageBox(wxstr(error), "Import Terrain", wxOK | wxICON_ERROR, this);
		return;
	}
	ReloadList();
	g_gui.SetStatusText(wxString::Format("Imported terrain stamp \"%s\".", wxstr(stamp.name)));
}

void SavedTerrainPalettePanel::OnClickExport(wxCommandEvent&) {
	const wxString name = getSelectedName();
	if (name.empty()) {
		return;
	}
	wxFileDialog dialog(
		this,
		"Export Terrain Stamp",
		"",
		name + ".json",
		"JSON files (*.json)|*.json",
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT
	);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}
	std::string error;
	if (!TerrainStampLibrary::ExportTo(nstr(name), std::filesystem::path(dialog.GetPath().ToStdWstring()), error)) {
		wxMessageBox(wxstr(error), "Export Terrain", wxOK | wxICON_ERROR, this);
		return;
	}
	g_gui.SetStatusText("Terrain stamp exported.");
}
