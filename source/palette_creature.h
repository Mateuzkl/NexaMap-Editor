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

#ifndef RME_TILESET_CREATURE_H_
#define RME_TILESET_CREATURE_H_

#include "palette_common.h"

#include <map>

class CreatureBrush;
class wxCheckListBox;

class CreaturePalettePanel : public PalettePanel {
public:
	CreaturePalettePanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	~CreaturePalettePanel() override;

	PaletteType GetType() const override;

	// Select the first brush
	void SelectFirstBrush() override;
	// Returns the currently selected brush (first brush if panel is not loaded)
	Brush* GetSelectedBrush() const override;
	// Returns the currently selected brush size
	int GetSelectedBrushSize() const override;
	// Select the brush in the parameter, this only changes the look of the panel
	bool SelectBrush(const Brush* whatbrush) override;

	// Updates the palette window to use the current brush size
	void OnUpdateBrushSize(BrushShape shape, int size) override;
	// Called when this page is displayed
	void OnSwitchIn() override;
	// Called sometimes?
	void OnUpdate() override;

protected:
	void SelectTileset(size_t index);
	void SelectCreature(size_t index);
	void SelectCreature(std::string name);

public:
	// Event handling
	void OnChangeSpawnTime(wxSpinEvent& event);
	void OnChangeSpawnSize(wxSpinEvent& event);
	void OnChangeSpawnDensity(wxSpinEvent& event);
	void OnChangeDefaultWeight(wxSpinEvent& event);

	void OnTilesetChange(wxCommandEvent& event);
	void OnListBoxChange(wxCommandEvent& event);
	void OnCheckCreature(wxCommandEvent& event);
	void OnClickCreatureBrushButton(wxCommandEvent& event);
	void OnClickSpawnBrushButton(wxCommandEvent& event);
	void OnSetFocus(wxFocusEvent& event);
	void OnKillFocus(wxFocusEvent& event);
	void OnChangeCreatureNameSearch(wxCommandEvent& event);

protected:
	void SelectCreatureBrush();
	void SelectSpawnBrush();
	void RefreshCreatureList(const std::string& preferredSelection = "");
	std::vector<CreatureBrush*> GetCurrentCreatureBrushes() const;
	CreatureBrush* GetCreatureBrush(size_t index) const;
	wxString GetCreatureLabel(CreatureBrush* brush) const;
	int GetCreatureWeight(CreatureBrush* brush) const;
	void UpdateCreatureWeight(CreatureBrush* brush, int weight);
	void UpdateVisibleCreatureLabel(size_t index);
	std::string GetSearchText() const;

	wxChoice* tileset_choice;
	wxTextCtrl* creature_name_text;
	wxButton* creature_search_button;
	wxCheckListBox* creature_list;
	wxToggleButton* creature_brush_button;
	wxToggleButton* spawn_brush_button;
	wxSpinCtrl* creature_spawntime_spin;
	wxSpinCtrl* spawn_size_spin;
	wxSpinCtrl* spawn_density_spin;
	wxSpinCtrl* default_weight_spin;

	std::vector<CreatureBrush*> all_creature_brushes;
	std::map<CreatureBrush*, int> creature_weights;

	bool handling_event;

	DECLARE_EVENT_TABLE();
};

#endif
