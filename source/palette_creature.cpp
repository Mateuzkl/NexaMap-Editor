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

#include "settings.h"
#include "brush.h"
#include "gui.h"
#include "palette_creature.h"
#include "creature_brush.h"
#include "spawn_brush.h"
#include "materials.h"

#include <wx/checklst.h>

#include <algorithm>
#include <set>

// ============================================================================
// Creature palette

BEGIN_EVENT_TABLE(CreaturePalettePanel, PalettePanel)
EVT_CHOICE(PALETTE_CREATURE_TILESET_CHOICE, CreaturePalettePanel::OnTilesetChange)

EVT_LISTBOX(PALETTE_CREATURE_LISTBOX, CreaturePalettePanel::OnListBoxChange)
EVT_CHECKLISTBOX(PALETTE_CREATURE_LISTBOX, CreaturePalettePanel::OnCheckCreature)

EVT_TOGGLEBUTTON(PALETTE_CREATURE_BRUSH_BUTTON, CreaturePalettePanel::OnClickCreatureBrushButton)
EVT_TOGGLEBUTTON(PALETTE_SPAWN_BRUSH_BUTTON, CreaturePalettePanel::OnClickSpawnBrushButton)

EVT_SPINCTRL(PALETTE_CREATURE_SPAWN_TIME, CreaturePalettePanel::OnChangeSpawnTime)
EVT_SPINCTRL(PALETTE_CREATURE_SPAWN_SIZE, CreaturePalettePanel::OnChangeSpawnSize)
EVT_SPINCTRL(PALETTE_CREATURE_SPAWN_DENSITY, CreaturePalettePanel::OnChangeSpawnDensity)
EVT_SPINCTRL(PALETTE_CREATURE_DEFAULT_WEIGHT, CreaturePalettePanel::OnChangeDefaultWeight)
END_EVENT_TABLE()

CreaturePalettePanel::CreaturePalettePanel(wxWindow* parent, wxWindowID id) :
	PalettePanel(parent, id),
	handling_event(false) {
	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);

	wxSizer* sidesizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Creatures");
	tileset_choice = newd wxChoice(this, PALETTE_CREATURE_TILESET_CHOICE, wxDefaultPosition, wxDefaultSize, (int)0, (const wxString*)nullptr);
	sidesizer->Add(tileset_choice, 0, wxEXPAND);

	wxSizer* creatureNameSizer = newd wxStaticBoxSizer(wxHORIZONTAL, this);
	creature_name_text = newd wxTextCtrl(this, PALETTE_CREATURE_SEARCH, "Search name", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	creature_name_text->Bind(wxEVT_SET_FOCUS, &CreaturePalettePanel::OnSetFocus, this);
	creature_name_text->Bind(wxEVT_KILL_FOCUS, &CreaturePalettePanel::OnKillFocus, this);
	creature_name_text->Bind(wxEVT_TEXT_ENTER, &CreaturePalettePanel::OnChangeCreatureNameSearch, this);

	creature_search_button = newd wxButton(this, wxID_ANY, "Search");
	creature_search_button->Bind(wxEVT_BUTTON, &CreaturePalettePanel::OnChangeCreatureNameSearch, this);

	creatureNameSizer->Add(creature_name_text, 2, wxEXPAND);
	creatureNameSizer->Add(creature_search_button, 1, wxEXPAND);
	sidesizer->Add(creatureNameSizer, 0, wxEXPAND);

	creature_list = newd wxCheckListBox(this, PALETTE_CREATURE_LISTBOX, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SINGLE | wxLB_NEEDED_SB);
	sidesizer->Add(creature_list, 1, wxEXPAND);
	topsizer->Add(sidesizer, 1, wxEXPAND);

	// Brush selection
	sidesizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Brushes", wxDefaultPosition, wxSize(150, 200)), wxVERTICAL);

	// sidesizer->Add(180, 1, wxEXPAND);

	wxFlexGridSizer* grid = newd wxFlexGridSizer(4, 3, 10, 10);
	grid->AddGrowableCol(1);

	grid->Add(newd wxStaticText(this, wxID_ANY, "Spawntime"));
	creature_spawntime_spin = newd wxSpinCtrl(this, PALETTE_CREATURE_SPAWN_TIME, i2ws(g_settings.getInteger(Config::DEFAULT_SPAWNTIME)), wxDefaultPosition, wxSize(50, 20), wxSP_ARROW_KEYS, 0, 86400, g_settings.getInteger(Config::DEFAULT_SPAWNTIME));
	grid->Add(creature_spawntime_spin, 0, wxEXPAND);
	creature_brush_button = newd wxToggleButton(this, PALETTE_CREATURE_BRUSH_BUTTON, "Place Creature");
	grid->Add(creature_brush_button, 0, wxEXPAND);

	grid->Add(newd wxStaticText(this, wxID_ANY, "Spawn size"));
	spawn_size_spin = newd wxSpinCtrl(this, PALETTE_CREATURE_SPAWN_SIZE, i2ws(5), wxDefaultPosition, wxSize(50, 20), wxSP_ARROW_KEYS, 1, g_settings.getInteger(Config::MAX_SPAWN_RADIUS), g_settings.getInteger(Config::CURRENT_SPAWN_RADIUS));
	grid->Add(spawn_size_spin, 0, wxEXPAND);
	spawn_brush_button = newd wxToggleButton(this, PALETTE_SPAWN_BRUSH_BUTTON, "Place Spawn");
	grid->Add(spawn_brush_button, 0, wxEXPAND);

	grid->Add(newd wxStaticText(this, wxID_ANY, "Spawn density %"));
	spawn_density_spin = newd wxSpinCtrl(this, PALETTE_CREATURE_SPAWN_DENSITY, i2ws(g_settings.getInteger(Config::SPAWN_MONSTER_DENSITY)), wxDefaultPosition, wxSize(50, 20), wxSP_ARROW_KEYS, 0, 3600, g_settings.getInteger(Config::SPAWN_MONSTER_DENSITY));
	grid->Add(spawn_density_spin, 0, wxEXPAND);
	grid->AddSpacer(1);

	grid->Add(newd wxStaticText(this, wxID_ANY, "Default weight %"));
	default_weight_spin = newd wxSpinCtrl(this, PALETTE_CREATURE_DEFAULT_WEIGHT, i2ws(g_settings.getInteger(Config::MONSTER_DEFAULT_WEIGHT)), wxDefaultPosition, wxSize(50, 20), wxSP_ARROW_KEYS, 0, 100, g_settings.getInteger(Config::MONSTER_DEFAULT_WEIGHT));
	grid->Add(default_weight_spin, 0, wxEXPAND);
	grid->AddSpacer(1);

	sidesizer->Add(grid, 0, wxEXPAND);
	topsizer->Add(sidesizer, 0, wxEXPAND);
	SetSizerAndFit(topsizer);

	OnUpdate();
}

CreaturePalettePanel::~CreaturePalettePanel() {
	creature_name_text->Unbind(wxEVT_SET_FOCUS, &CreaturePalettePanel::OnSetFocus, this);
	creature_name_text->Unbind(wxEVT_KILL_FOCUS, &CreaturePalettePanel::OnKillFocus, this);
	creature_name_text->Unbind(wxEVT_TEXT_ENTER, &CreaturePalettePanel::OnChangeCreatureNameSearch, this);
	creature_search_button->Unbind(wxEVT_BUTTON, &CreaturePalettePanel::OnChangeCreatureNameSearch, this);
}

PaletteType CreaturePalettePanel::GetType() const {
	return TILESET_CREATURE;
}

void CreaturePalettePanel::SelectFirstBrush() {
	SelectCreatureBrush();
}

Brush* CreaturePalettePanel::GetSelectedBrush() const {
	if (creature_brush_button->GetValue()) {
		if (creature_list->GetCount() == 0) {
			return nullptr;
		}
		const int selection = creature_list->GetSelection();
		if (selection == wxNOT_FOUND) {
			return nullptr;
		}
		Brush* brush = reinterpret_cast<Brush*>(creature_list->GetClientData(selection));
		if (brush && brush->isCreature()) {
			g_gui.SetSpawnTime(creature_spawntime_spin->GetValue());
			return brush;
		}
	} else if (spawn_brush_button->GetValue()) {
		g_settings.setInteger(Config::CURRENT_SPAWN_RADIUS, spawn_size_spin->GetValue());
		g_settings.setInteger(Config::DEFAULT_SPAWNTIME, creature_spawntime_spin->GetValue());
		g_settings.setInteger(Config::SPAWN_MONSTER_DENSITY, spawn_density_spin->GetValue());
		g_settings.setInteger(Config::MONSTER_DEFAULT_WEIGHT, default_weight_spin->GetValue());
		g_gui.SetSpawnTime(creature_spawntime_spin->GetValue());

		std::vector<SpawnBrush::CreatureEntry> creatures;
		for (size_t index = 0; index < creature_list->GetCount(); ++index) {
			if (!creature_list->IsChecked(index)) {
				continue;
			}
			CreatureBrush* brush = GetCreatureBrush(index);
			if (brush) {
				creatures.push_back({ brush, static_cast<uint8_t>(GetCreatureWeight(brush)) });
			}
		}
		g_gui.spawn_brush->setCreatures(creatures);
		return g_gui.spawn_brush;
	}
	return nullptr;
}

bool CreaturePalettePanel::SelectBrush(const Brush* whatbrush) {
	if (!whatbrush) {
		return false;
	}

	if (whatbrush->isCreature()) {
		int current_index = tileset_choice->GetSelection();
		if (current_index != wxNOT_FOUND) {
			const TilesetCategory* tsc = reinterpret_cast<const TilesetCategory*>(tileset_choice->GetClientData(current_index));
			// tsc is nullptr when "All" tileset is selected; skip iteration to avoid crash
			if (tsc) {
				for (BrushVector::const_iterator iter = tsc->brushlist.begin(); iter != tsc->brushlist.end(); ++iter) {
					if (*iter == whatbrush) {
						SelectCreature(whatbrush->getName());
						return true;
					}
				}
			}
		}
		// Not in the current display, search the hidden one's
		for (size_t i = 0; i < tileset_choice->GetCount(); ++i) {
			if (current_index != (int)i) {
				const TilesetCategory* tsc = reinterpret_cast<const TilesetCategory*>(tileset_choice->GetClientData(i));
				if (!tsc) {
					continue;
				}
				for (BrushVector::const_iterator iter = tsc->brushlist.begin();
					 iter != tsc->brushlist.end();
					 ++iter) {
					if (*iter == whatbrush) {
						SelectTileset(i);
						SelectCreature(whatbrush->getName());
						return true;
					}
				}
			}
		}
	} else if (whatbrush->isSpawn()) {
		SelectSpawnBrush();
		return true;
	}
	return false;
}

int CreaturePalettePanel::GetSelectedBrushSize() const {
	return spawn_size_spin->GetValue();
}

void CreaturePalettePanel::OnUpdate() {
	tileset_choice->Clear();
	all_creature_brushes.clear();
	g_materials.createOtherTileset();

	std::set<CreatureBrush*> allBrushes;
	std::vector<std::pair<wxString, const TilesetCategory*>> entries;
	for (TilesetContainer::const_iterator iter = g_materials.tilesets.begin(); iter != g_materials.tilesets.end(); ++iter) {
		const TilesetCategory* tsc = iter->second->getCategory(TILESET_CREATURE);
		if (tsc && tsc->size() > 0) {
			entries.push_back({ wxstr(iter->second->name), tsc });
			for (Brush* brush : tsc->brushlist) {
				if (brush && brush->isCreature()) {
					allBrushes.insert(brush->asCreature());
				}
			}
		} else if (iter->second->name == "NPCs" || iter->second->name == "Others") {
			Tileset* ts = const_cast<Tileset*>(iter->second);
			TilesetCategory* rtsc = ts->getCategory(TILESET_CREATURE);
			entries.push_back({ wxstr(ts->name), rtsc });
			for (Brush* brush : rtsc->brushlist) {
				if (brush && brush->isCreature()) {
					allBrushes.insert(brush->asCreature());
				}
			}
		}
	}

	all_creature_brushes.assign(allBrushes.begin(), allBrushes.end());
	std::sort(all_creature_brushes.begin(), all_creature_brushes.end(), [](CreatureBrush* a, CreatureBrush* b) {
		return as_lower_str(a->getName()) < as_lower_str(b->getName());
	});

	std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
		return a.first.CmpNoCase(b.first) < 0;
	});

	if (!all_creature_brushes.empty()) {
		tileset_choice->Append("All", static_cast<void*>(nullptr));
	}
	for (const auto& entry : entries) {
		tileset_choice->Append(entry.first, const_cast<TilesetCategory*>(entry.second));
	}
	SelectTileset(0);
}

void CreaturePalettePanel::OnUpdateBrushSize(BrushShape shape, int size) {
	return spawn_size_spin->SetValue(size);
}

void CreaturePalettePanel::OnSwitchIn() {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetBrushSize(spawn_size_spin->GetValue());
}

void CreaturePalettePanel::SelectTileset(size_t index) {
	ASSERT(tileset_choice->GetCount() >= index);

	if (tileset_choice->GetCount() == 0) {
		// No tilesets :(
		creature_list->Clear();
		creature_brush_button->Enable(false);
	} else {
		tileset_choice->SetSelection(index);
		RefreshCreatureList();
		SelectCreature(0);
	}
}

void CreaturePalettePanel::SelectCreature(size_t index) {
	// Save the old g_settings
	ASSERT(creature_list->GetCount() >= index);

	if (creature_list->GetCount() > 0) {
		creature_list->SetSelection(index);
	}

	SelectCreatureBrush();
}

void CreaturePalettePanel::SelectCreature(std::string name) {
	for (size_t i = 0; i < creature_list->GetCount(); ++i) {
		Brush* brush = reinterpret_cast<Brush*>(creature_list->GetClientData(i));
		if (brush && brush->getName() == name) {
			creature_list->SetSelection(i);
			break;
		}
	}
	if (creature_list->GetSelection() == wxNOT_FOUND && creature_list->GetCount() > 0) {
		creature_list->SetSelection(0);
	}

	SelectCreatureBrush();
}

void CreaturePalettePanel::SelectCreatureBrush() {
	if (creature_list->GetCount() > 0) {
		creature_brush_button->Enable(true);
		creature_brush_button->SetValue(true);
		spawn_brush_button->SetValue(false);
	} else {
		creature_brush_button->Enable(false);
		SelectSpawnBrush();
	}
}

void CreaturePalettePanel::SelectSpawnBrush() {
	// g_gui.house_exit_brush->setHouse(house);
	creature_brush_button->SetValue(false);
	spawn_brush_button->SetValue(true);
}

void CreaturePalettePanel::OnTilesetChange(wxCommandEvent& event) {
	SelectTileset(event.GetSelection());
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}

void CreaturePalettePanel::OnListBoxChange(wxCommandEvent& event) {
	if (CreatureBrush* brush = GetCreatureBrush(event.GetSelection())) {
		default_weight_spin->SetValue(GetCreatureWeight(brush));
	}
	SelectCreature(event.GetSelection());
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}

void CreaturePalettePanel::OnClickCreatureBrushButton(wxCommandEvent& event) {
	SelectCreatureBrush();
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}

void CreaturePalettePanel::OnClickSpawnBrushButton(wxCommandEvent& event) {
	SelectSpawnBrush();
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}

void CreaturePalettePanel::OnChangeSpawnTime(wxSpinEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetSpawnTime(event.GetPosition());
}

void CreaturePalettePanel::OnChangeSpawnSize(wxSpinEvent& event) {
	if (!handling_event) {
		handling_event = true;
		g_gui.ActivatePalette(GetParentPalette());
		g_gui.SetBrushSize(event.GetPosition());
		handling_event = false;
	}
}

void CreaturePalettePanel::OnChangeSpawnDensity(wxSpinEvent& event) {
	g_settings.setInteger(Config::SPAWN_MONSTER_DENSITY, event.GetPosition());
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}

void CreaturePalettePanel::OnChangeDefaultWeight(wxSpinEvent& event) {
	const int weight = event.GetPosition();
	g_settings.setInteger(Config::MONSTER_DEFAULT_WEIGHT, weight);

	wxArrayInt selections;
	creature_list->GetSelections(selections);
	if (selections.IsEmpty() && creature_list->GetSelection() != wxNOT_FOUND) {
		selections.Add(creature_list->GetSelection());
	}

	for (int index : selections) {
		if (!creature_list->IsChecked(index)) {
			continue;
		}

		if (CreatureBrush* brush = GetCreatureBrush(index)) {
			UpdateCreatureWeight(brush, weight);
			UpdateVisibleCreatureLabel(index);
		}
	}

	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}

void CreaturePalettePanel::OnSetFocus(wxFocusEvent& event) {
	g_gui.DisableHotkeys();
	if (creature_name_text->GetValue() == "Search name") {
		creature_name_text->Clear();
	}
	event.Skip();
}

void CreaturePalettePanel::OnKillFocus(wxFocusEvent& event) {
	g_gui.EnableHotkeys();
	if (creature_name_text->GetValue().IsEmpty()) {
		creature_name_text->SetValue("Search name");
	}
	event.Skip();
}

void CreaturePalettePanel::OnChangeCreatureNameSearch(wxCommandEvent& event) {
	const int selection = creature_list->GetSelection();
	std::string preferredSelection;
	if (selection != wxNOT_FOUND) {
		if (Brush* brush = reinterpret_cast<Brush*>(creature_list->GetClientData(selection))) {
			preferredSelection = brush->getName();
		}
	}

	RefreshCreatureList(preferredSelection);
	if (creature_list->GetCount() > 0 && creature_list->GetSelection() == wxNOT_FOUND) {
		creature_list->SetSelection(0);
	}

	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}

void CreaturePalettePanel::OnCheckCreature(wxCommandEvent& event) {
	const int index = event.GetSelection();
	CreatureBrush* brush = GetCreatureBrush(index);
	if (!brush) {
		return;
	}

	if (creature_list->IsChecked(index)) {
		UpdateCreatureWeight(brush, default_weight_spin->GetValue());
	} else {
		creature_weights.erase(brush);
	}

	UpdateVisibleCreatureLabel(index);
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}

void CreaturePalettePanel::RefreshCreatureList(const std::string& preferredSelection) {
	std::vector<CreatureBrush*> brushes = GetCurrentCreatureBrushes();
	const std::string searchText = GetSearchText();
	if (!searchText.empty()) {
		brushes.erase(std::remove_if(brushes.begin(), brushes.end(), [&searchText](CreatureBrush* brush) {
			return as_lower_str(brush->getName()).find(searchText) == std::string::npos;
		}), brushes.end());
	}

	std::sort(brushes.begin(), brushes.end(), [](CreatureBrush* a, CreatureBrush* b) {
		return as_lower_str(a->getName()) < as_lower_str(b->getName());
	});

	creature_list->Freeze();
	creature_list->Clear();
	int preferredIndex = wxNOT_FOUND;
	for (CreatureBrush* brush : brushes) {
		const int index = creature_list->Append(GetCreatureLabel(brush), brush);
		if (creature_weights.find(brush) != creature_weights.end()) {
			creature_list->Check(index, true);
		}
		if (!preferredSelection.empty() && brush->getName() == preferredSelection) {
			preferredIndex = index;
		}
	}
	creature_list->Thaw();

	if (preferredIndex != wxNOT_FOUND) {
		creature_list->SetSelection(preferredIndex);
	} else if (creature_list->GetCount() > 0) {
		creature_list->SetSelection(0);
	}
}

std::vector<CreatureBrush*> CreaturePalettePanel::GetCurrentCreatureBrushes() const {
	std::vector<CreatureBrush*> brushes;
	if (tileset_choice->GetSelection() == wxNOT_FOUND) {
		return brushes;
	}

	const TilesetCategory* tsc = reinterpret_cast<const TilesetCategory*>(tileset_choice->GetClientData(tileset_choice->GetSelection()));
	if (!tsc) {
		return all_creature_brushes;
	}

	for (Brush* brush : tsc->brushlist) {
		if (brush && brush->isCreature()) {
			brushes.push_back(brush->asCreature());
		}
	}

	return brushes;
}

CreatureBrush* CreaturePalettePanel::GetCreatureBrush(size_t index) const {
	if (index >= creature_list->GetCount()) {
		return nullptr;
	}

	Brush* brush = reinterpret_cast<Brush*>(creature_list->GetClientData(index));
	if (brush && brush->isCreature()) {
		return brush->asCreature();
	}
	return nullptr;
}

wxString CreaturePalettePanel::GetCreatureLabel(CreatureBrush* brush) const {
	if (!brush) {
		return "";
	}

	auto it = creature_weights.find(brush);
	if (it == creature_weights.end()) {
		return wxstr(brush->getName());
	}

	return wxstr(brush->getName() + " (" + std::to_string(it->second) + "%)");
}

int CreaturePalettePanel::GetCreatureWeight(CreatureBrush* brush) const {
	auto it = creature_weights.find(brush);
	if (it != creature_weights.end()) {
		return it->second;
	}
	return g_settings.getInteger(Config::MONSTER_DEFAULT_WEIGHT);
}

void CreaturePalettePanel::UpdateCreatureWeight(CreatureBrush* brush, int weight) {
	if (!brush) {
		return;
	}
	creature_weights[brush] = std::max(0, std::min(weight, 100));
}

void CreaturePalettePanel::UpdateVisibleCreatureLabel(size_t index) {
	CreatureBrush* brush = GetCreatureBrush(index);
	if (!brush) {
		return;
	}

	const bool checked = creature_list->IsChecked(index);
	const bool selected = creature_list->IsSelected(index);
	creature_list->SetString(index, GetCreatureLabel(brush));
	creature_list->Check(index, checked);
	if (selected) {
		creature_list->SetSelection(index);
	}
}

std::string CreaturePalettePanel::GetSearchText() const {
	std::string searchText = as_lower_str(creature_name_text->GetValue().ToStdString());
	if (searchText == "search name") {
		searchText.clear();
	}
	return searchText;
}
