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

#include "palette_common.h"
#include "brush.h"
#include "sprites.h"
#include "gui.h"

// ============================================================================
// Palette Panel

BEGIN_EVENT_TABLE(PalettePanel, wxPanel)
EVT_TIMER(PALETTE_DELAYED_REFRESH_TIMER, PalettePanel::OnRefreshTimer)
END_EVENT_TABLE()

PalettePanel::PalettePanel(wxWindow* parent, wxWindowID id, long style) :
	wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, style),
	refresh_timer(this, PALETTE_DELAYED_REFRESH_TIMER),
	last_brush_size(0) {
	////
}

PalettePanel::~PalettePanel() {
	////
}

// ============================================================================
// Named entity palette (shared base for Waypoints/Zones palettes)

NamedEntityPalettePanel::NamedEntityPalettePanel(wxWindow* parent, wxWindowID id) :
	PalettePanel(parent, id),
	map(nullptr) {
	////
}

wxListCtrl* NamedEntityPalettePanel::createEntityList(wxWindowID listId) {
	auto* list = newd wxListCtrl(this, listId, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_EDIT_LABELS | wxLC_NO_HEADER);
	list->InsertColumn(0, "UNNAMED", wxLIST_FORMAT_LEFT, 200);
	return list;
}

void NamedEntityPalettePanel::SelectFirstBrush() {
	////
}

int NamedEntityPalettePanel::GetSelectedBrushSize() const {
	return 0;
}

void NamedEntityPalettePanel::OnSwitchIn() {
	PalettePanel::OnSwitchIn();
}

void NamedEntityPalettePanel::OnSwitchOut() {
	PalettePanel::OnSwitchOut();
}

void NamedEntityPalettePanel::OnBeginEditLabel(wxListEvent& WXUNUSED(event)) {
	g_gui.DisableHotkeys();
}

long NamedEntityPalettePanel::getSelectedIndex(wxListCtrl* list) {
	return list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
}

std::string NamedEntityPalettePanel::getSelectedName(wxListCtrl* list, long index) {
	return nstr(list->GetItemText(index));
}

PaletteWindow* GetParentPalette(const wxWindow* window) {
	const wxWindow* w = window;
	while ((w = w->GetParent()) && dynamic_cast<const PaletteWindow*>(w) == nullptr)
		;
	return const_cast<PaletteWindow*>(static_cast<const PaletteWindow*>(w));
}

PaletteWindow* PalettePanel::GetParentPalette() const {
	return ::GetParentPalette(this);
}

void PalettePanel::InvalidateContents() {
	for (auto iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
		(*iter)->InvalidateContents();
	}
}

void PalettePanel::LoadCurrentContents() {
	for (auto iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
		(*iter)->OnSwitchIn();
	}
	Fit();
}

void PalettePanel::LoadAllContents() {
	for (auto iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
		(*iter)->LoadAllContents();
	}
}

void PalettePanel::AddToolPanel(PalettePanel* panel) {
	wxSizer* sp_sizer = newd wxStaticBoxSizer(wxVERTICAL, this, panel->GetName());
	sp_sizer->Add(panel, 0, wxEXPAND);
	GetSizer()->Add(sp_sizer, 0, wxEXPAND);

	// GetSizer()->SetDimension(wxDefaultCoord, wxDefaultCoord, wxDefaultCoord, wxDefaultCoord);
	// GetSizer()->Layout();
	Fit();
	//

	tool_bars.push_back(panel);
}

void PalettePanel::SetToolbarIconSize(bool large_icons) {
	for (auto iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
		(*iter)->SetToolbarIconSize(large_icons);
	}
}

wxString PalettePanel::GetName() const {
	switch (GetType()) {
		case TILESET_TERRAIN:
			return "Terrain Palette";
		case TILESET_DOODAD:
			return "Doodad Palette";
		case TILESET_ITEM:
			return "Item Palette";
		case TILESET_COLLECTION:
			return "Collections Palette";
		case TILESET_CREATURE:
			return "Creature Palette";
		case TILESET_HOUSE:
			return "House Palette";
		case TILESET_RAW:
			return "RAW Palette";
		case TILESET_WAYPOINT:
			return "Waypoint Palette";
		case TILESET_ZONES:
			return "Zones Palette";
		case TILESET_UNKNOWN:
			return "Unknown";
	}
	return wxEmptyString;
}

PaletteType PalettePanel::GetType() const {
	return TILESET_UNKNOWN;
}

Brush* PalettePanel::GetSelectedBrush() const {
	return nullptr;
}

int PalettePanel::GetSelectedBrushSize() const {
	return 0;
}

void PalettePanel::SelectFirstBrush() {
	// Do nothing
}

bool PalettePanel::SelectBrush(const Brush* whatbrush) {
	return false;
}

void PalettePanel::OnUpdateBrushSize(BrushShape shape, int size) {
	for (auto iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
		(*iter)->OnUpdateBrushSize(shape, size);
	}
}

void PalettePanel::OnSwitchIn() {
	for (auto iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
		(*iter)->OnSwitchIn();
	}
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetBrushSize(last_brush_size);
}

void PalettePanel::OnSwitchOut() {
	last_brush_size = g_gui.GetBrushSize();
	for (auto iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
		(*iter)->OnSwitchOut();
	}
}

void PalettePanel::OnUpdate() {
	for (auto iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
		(*iter)->OnUpdate();
	}
}

void PalettePanel::OnRefreshTimer(wxTimerEvent&) {
	g_gui.RefreshOtherPalettes(GetParentPalette());
}

// Size Page

BEGIN_EVENT_TABLE(BrushSizePanel, wxPanel)
EVT_TOGGLEBUTTON(PALETTE_BRUSHSHAPE_SQUARE, BrushSizePanel::OnClickSquareBrush)
EVT_TOGGLEBUTTON(PALETTE_BRUSHSHAPE_CIRCLE, BrushSizePanel::OnClickCircleBrush)

EVT_TOGGLEBUTTON(PALETTE_TERRAIN_BRUSHSIZE_0, BrushSizePanel::OnClickBrushSize0)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_BRUSHSIZE_1, BrushSizePanel::OnClickBrushSize1)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_BRUSHSIZE_2, BrushSizePanel::OnClickBrushSize2)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_BRUSHSIZE_4, BrushSizePanel::OnClickBrushSize4)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_BRUSHSIZE_6, BrushSizePanel::OnClickBrushSize6)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_BRUSHSIZE_8, BrushSizePanel::OnClickBrushSize8)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_BRUSHSIZE_11, BrushSizePanel::OnClickBrushSize11)
END_EVENT_TABLE()

BrushSizePanel::BrushSizePanel(wxWindow* parent) :
	PalettePanel(parent, wxID_ANY),
	loaded(false),
	large_icons(true),
	brushshapeSquareButton(nullptr),
	brushshapeCircleButton(nullptr),
	brushsize0Button(nullptr),
	brushsize1Button(nullptr),
	brushsize2Button(nullptr),
	brushsize4Button(nullptr),
	brushsize6Button(nullptr),
	brushsize8Button(nullptr),
	brushsize11Button(nullptr) {
	////
}

void BrushSizePanel::InvalidateContents() {
	if (loaded) {
		DestroyChildren();
		SetSizer(nullptr);

		brushshapeSquareButton = brushshapeCircleButton = brushsize0Button = brushsize1Button = brushsize2Button = brushsize4Button = brushsize6Button = brushsize8Button = brushsize11Button = nullptr;

		loaded = false;
	}
}

void BrushSizePanel::LoadCurrentContents() {
	LoadAllContents();
}

void BrushSizePanel::LoadAllContents() {
	if (loaded) {
		return;
	}

	wxSizer* size_sizer = newd wxBoxSizer(wxVERTICAL);
	;
	wxSizer* sub_sizer = newd wxBoxSizer(wxHORIZONTAL);
	RenderSize render_size;

	if (large_icons) {
		// 32x32
		render_size = RENDER_SIZE_32x32;
	} else {
		// 16x16
		render_size = RENDER_SIZE_16x16;
	}

	sub_sizer->Add(brushshapeSquareButton = newd DCButton(this, PALETTE_BRUSHSHAPE_SQUARE, wxDefaultPosition, DC_BTN_TOGGLE, render_size, EDITOR_SPRITE_BRUSH_SD_9x9));
	brushshapeSquareButton->SetToolTip("Square brush");

	sub_sizer->Add(brushshapeCircleButton = newd DCButton(this, PALETTE_BRUSHSHAPE_CIRCLE, wxDefaultPosition, DC_BTN_TOGGLE, render_size, EDITOR_SPRITE_BRUSH_CD_9x9));
	brushshapeCircleButton->SetToolTip("Circle brush");
	brushshapeSquareButton->SetValue(true);

	if (large_icons) {
		sub_sizer->AddSpacer(36);
	} else {
		sub_sizer->AddSpacer(18);
	}

	sub_sizer->Add(brushsize0Button = newd DCButton(this, PALETTE_TERRAIN_BRUSHSIZE_0, wxDefaultPosition, DC_BTN_TOGGLE, render_size, EDITOR_SPRITE_BRUSH_CD_1x1));
	brushsize0Button->SetToolTip("Brush size 1");
	brushsize0Button->SetValue(true);

	sub_sizer->Add(brushsize1Button = newd DCButton(this, PALETTE_TERRAIN_BRUSHSIZE_1, wxDefaultPosition, DC_BTN_TOGGLE, render_size, EDITOR_SPRITE_BRUSH_SD_3x3));
	brushsize1Button->SetToolTip("Brush size 2");

	if (large_icons) {
		size_sizer->Add(sub_sizer);
		sub_sizer = newd wxBoxSizer(wxHORIZONTAL);
	}

	sub_sizer->Add(brushsize2Button = newd DCButton(this, PALETTE_TERRAIN_BRUSHSIZE_2, wxDefaultPosition, DC_BTN_TOGGLE, render_size, EDITOR_SPRITE_BRUSH_SD_5x5));
	brushsize2Button->SetToolTip("Brush size 3");

	sub_sizer->Add(brushsize4Button = newd DCButton(this, PALETTE_TERRAIN_BRUSHSIZE_4, wxDefaultPosition, DC_BTN_TOGGLE, render_size, EDITOR_SPRITE_BRUSH_SD_7x7));
	brushsize4Button->SetToolTip("Brush size 5");

	sub_sizer->Add(brushsize6Button = newd DCButton(this, PALETTE_TERRAIN_BRUSHSIZE_6, wxDefaultPosition, DC_BTN_TOGGLE, render_size, EDITOR_SPRITE_BRUSH_SD_9x9));
	brushsize6Button->SetToolTip("Brush size 7");

	sub_sizer->Add(brushsize8Button = newd DCButton(this, PALETTE_TERRAIN_BRUSHSIZE_8, wxDefaultPosition, DC_BTN_TOGGLE, render_size, EDITOR_SPRITE_BRUSH_SD_15x15));
	brushsize8Button->SetToolTip("Brush size 9");

	sub_sizer->Add(brushsize11Button = newd DCButton(this, PALETTE_TERRAIN_BRUSHSIZE_11, wxDefaultPosition, DC_BTN_TOGGLE, render_size, EDITOR_SPRITE_BRUSH_SD_19x19));
	brushsize11Button->SetToolTip("Brush size 12");

	size_sizer->Add(sub_sizer);
	SetSizerAndFit(size_sizer);

	loaded = true;
}

wxString BrushSizePanel::GetName() const {
	return "Brush Size";
}

void BrushSizePanel::SetToolbarIconSize(bool d) {
	InvalidateContents();
	large_icons = d;
}

void BrushSizePanel::OnSwitchIn() {
	LoadCurrentContents();
}

void BrushSizePanel::OnUpdateBrushSize(BrushShape shape, int size) {
	if (shape == BRUSHSHAPE_SQUARE) {
		brushshapeCircleButton->SetValue(false);
		brushshapeSquareButton->SetValue(true);

		brushsize0Button->SetSprite(EDITOR_SPRITE_BRUSH_CD_1x1);
		brushsize1Button->SetSprite(EDITOR_SPRITE_BRUSH_SD_3x3);
		brushsize2Button->SetSprite(EDITOR_SPRITE_BRUSH_SD_5x5);
		brushsize4Button->SetSprite(EDITOR_SPRITE_BRUSH_SD_7x7);
		brushsize6Button->SetSprite(EDITOR_SPRITE_BRUSH_SD_9x9);
		brushsize8Button->SetSprite(EDITOR_SPRITE_BRUSH_SD_15x15);
		brushsize11Button->SetSprite(EDITOR_SPRITE_BRUSH_SD_19x19);
	} else {
		brushshapeSquareButton->SetValue(false);
		brushshapeCircleButton->SetValue(true);

		brushsize0Button->SetSprite(EDITOR_SPRITE_BRUSH_CD_1x1);
		brushsize1Button->SetSprite(EDITOR_SPRITE_BRUSH_CD_3x3);
		brushsize2Button->SetSprite(EDITOR_SPRITE_BRUSH_CD_5x5);
		brushsize4Button->SetSprite(EDITOR_SPRITE_BRUSH_CD_7x7);
		brushsize6Button->SetSprite(EDITOR_SPRITE_BRUSH_CD_9x9);
		brushsize8Button->SetSprite(EDITOR_SPRITE_BRUSH_CD_15x15);
		brushsize11Button->SetSprite(EDITOR_SPRITE_BRUSH_CD_19x19);
	}

	if (brushsize0Button) {
		brushsize0Button->SetValue(false);
	}
	if (brushsize1Button) {
		brushsize1Button->SetValue(false);
	}
	if (brushsize2Button) {
		brushsize2Button->SetValue(false);
	}
	if (brushsize4Button) {
		brushsize4Button->SetValue(false);
	}
	if (brushsize6Button) {
		brushsize6Button->SetValue(false);
	}
	if (brushsize8Button) {
		brushsize8Button->SetValue(false);
	}
	if (brushsize11Button) {
		brushsize11Button->SetValue(false);
	}

	switch (size) {
		case 0:
			brushsize0Button->SetValue(true);
			break;
		case 1:
			brushsize1Button->SetValue(true);
			break;
		case 2:
			brushsize2Button->SetValue(true);
			break;
		case 4:
			brushsize4Button->SetValue(true);
			break;
		case 6:
			brushsize6Button->SetValue(true);
			break;
		case 8:
			brushsize8Button->SetValue(true);
			break;
		case 11:
			brushsize11Button->SetValue(true);
			break;
		default:
			brushsize0Button->SetValue(true);
			break;
	}
}

void BrushSizePanel::OnClickCircleBrush(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetBrushShape(BRUSHSHAPE_CIRCLE);
}

void BrushSizePanel::OnClickSquareBrush(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetBrushShape(BRUSHSHAPE_SQUARE);
}

void BrushSizePanel::OnClickBrushSize(int which) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetBrushSize(which);
}

// ============================================================================
// Brush Thickness Panel

BEGIN_EVENT_TABLE(BrushThicknessPanel, PalettePanel)
#ifdef __WINDOWS__
// This only works in wxmsw
EVT_COMMAND_SCROLL_CHANGED(PALETTE_DOODAD_SLIDER, BrushThicknessPanel::OnScroll)
#else
EVT_COMMAND_SCROLL_TOP(PALETTE_DOODAD_SLIDER, BrushThicknessPanel::OnScroll)
EVT_COMMAND_SCROLL_BOTTOM(PALETTE_DOODAD_SLIDER, BrushThicknessPanel::OnScroll)
EVT_COMMAND_SCROLL_LINEUP(PALETTE_DOODAD_SLIDER, BrushThicknessPanel::OnScroll)
EVT_COMMAND_SCROLL_LINEDOWN(PALETTE_DOODAD_SLIDER, BrushThicknessPanel::OnScroll)
EVT_COMMAND_SCROLL_PAGEUP(PALETTE_DOODAD_SLIDER, BrushThicknessPanel::OnScroll)
EVT_COMMAND_SCROLL_PAGEDOWN(PALETTE_DOODAD_SLIDER, BrushThicknessPanel::OnScroll)
EVT_COMMAND_SCROLL_THUMBRELEASE(PALETTE_DOODAD_SLIDER, BrushThicknessPanel::OnScroll)
#endif

EVT_CHECKBOX(PALETTE_DOODAD_USE_THICKNESS, BrushThicknessPanel::OnClickCustomThickness)
END_EVENT_TABLE()

BrushThicknessPanel::BrushThicknessPanel(wxWindow* parent) :
	PalettePanel(parent, wxID_ANY) {
	wxSizer* thickness_sizer = newd wxBoxSizer(wxVERTICAL);

	wxSizer* thickness_sub_sizer = newd wxBoxSizer(wxHORIZONTAL);
	thickness_sub_sizer->Add(20, 10);
	use_button = newd wxCheckBox(this, PALETTE_DOODAD_USE_THICKNESS, "Use custom thickness");
	thickness_sub_sizer->Add(use_button);
	thickness_sizer->Add(thickness_sub_sizer, 1, wxEXPAND);

	slider = newd wxSlider(this, PALETTE_DOODAD_SLIDER, 5, 1, 10, wxDefaultPosition);
	thickness_sizer->Add(slider, 1, wxEXPAND);

	SetSizerAndFit(thickness_sizer);
}

BrushThicknessPanel::~BrushThicknessPanel() {
	////
}

wxString BrushThicknessPanel::GetName() const {
	return "Brush Thickness";
}

void BrushThicknessPanel::OnScroll(wxScrollEvent& event) {
	static const int lookup_table[10] = { 1, 2, 3, 5, 8, 13, 23, 35, 50, 80 };
	use_button->SetValue(true);

	ASSERT(event.GetPosition() >= 1);
	ASSERT(event.GetPosition() <= 10);

	// printf("SELECT[%d] = %d\n", event.GetPosition()-1, lookup_table[event.GetPosition()-1]);
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetBrushThickness(true, lookup_table[event.GetPosition() - 1], 100);
}

void BrushThicknessPanel::OnClickCustomThickness(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetBrushThickness(event.IsChecked());
}

void BrushThicknessPanel::OnSwitchIn() {
	static const int lookup_table[10] = { 1, 2, 3, 5, 8, 13, 23, 35, 50, 80 };
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetBrushThickness(lookup_table[slider->GetValue() - 1], 100);
}
