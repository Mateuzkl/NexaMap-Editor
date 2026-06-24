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

#include "palette_brush_tool.h"
#include "gui.h"
#include "settings.h"

// ============================================================================
// Tool Brush Panel

BEGIN_EVENT_TABLE(BrushToolPanel, PalettePanel)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_OPTIONAL_BORDER_TOOL, BrushToolPanel::OnClickGravelButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_ERASER, BrushToolPanel::OnClickEraserButton)

EVT_TOGGLEBUTTON(PALETTE_TERRAIN_NORMAL_DOOR, BrushToolPanel::OnClickNormalDoorButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_LOCKED_DOOR, BrushToolPanel::OnClickLockedDoorButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_MAGIC_DOOR, BrushToolPanel::OnClickMagicDoorButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_QUEST_DOOR, BrushToolPanel::OnClickQuestDoorButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_HATCH_DOOR, BrushToolPanel::OnClickHatchDoorButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_WINDOW_DOOR, BrushToolPanel::OnClickWindowDoorButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_NORMAL_ALT_DOOR, BrushToolPanel::OnClickNormalAltDoorButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_ARCHWAY_DOOR, BrushToolPanel::OnClickArchwayDoorButton)

EVT_TOGGLEBUTTON(PALETTE_TERRAIN_PZ_TOOL, BrushToolPanel::OnClickPZBrushButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_NOPVP_TOOL, BrushToolPanel::OnClickNOPVPBrushButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_NOLOGOUT_TOOL, BrushToolPanel::OnClickNoLogoutBrushButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_PVPZONE_TOOL, BrushToolPanel::OnClickPVPZoneBrushButton)

EVT_CHECKBOX(PALETTE_TERRAIN_LOCK_DOOR, BrushToolPanel::OnClickLockDoorCheckbox)
END_EVENT_TABLE()

BrushToolPanel::BrushToolPanel(wxWindow* parent) :
	PalettePanel(parent, wxID_ANY),
	loaded(false),
	large_icons(true),
	optionalBorderButton(nullptr),
	eraserButton(nullptr),
	normalDoorButton(nullptr),
	lockedDoorButton(nullptr),
	magicDoorButton(nullptr),
	questDoorButton(nullptr),
	hatchDoorButton(nullptr),
	windowDoorButton(nullptr),
	normalDoorAltButton(nullptr),
	archwayDoorButton(nullptr),
	pzBrushButton(nullptr),
	nopvpBrushButton(nullptr),
	nologBrushButton(nullptr),
	pvpzoneBrushButton(nullptr) {
	////
}

BrushToolPanel::~BrushToolPanel() {
	////
}

void BrushToolPanel::InvalidateContents() {
	if (loaded) {
		DestroyChildren();
		SetSizer(nullptr);

		optionalBorderButton = eraserButton = normalDoorButton = lockedDoorButton = magicDoorButton = questDoorButton = hatchDoorButton = windowDoorButton = normalDoorAltButton = archwayDoorButton = pzBrushButton = nopvpBrushButton = nologBrushButton = pvpzoneBrushButton = nullptr;

		loaded = false;
	}
}

void BrushToolPanel::LoadCurrentContents() {
	LoadAllContents();
}

void BrushToolPanel::LoadAllContents() {
	if (loaded) {
		return;
	}

	wxSizer* size_sizer = newd wxBoxSizer(wxVERTICAL);
	;
	wxSizer* sub_sizer = newd wxBoxSizer(wxHORIZONTAL);

	/*RenderSize render_size;
	if(large_icons) {
		// 32x32
		render_size = RENDER_SIZE_32x32;
	} else {
		// 16x16
		render_size = RENDER_SIZE_16x16;
	}*/

	if (large_icons) {
		// Create the tool page with 32x32 icons

		ASSERT(g_gui.optional_brush);
		sub_sizer->Add(optionalBorderButton = newd BrushButton(this, g_gui.optional_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_OPTIONAL_BORDER_TOOL));
		optionalBorderButton->SetToolTip("Optional Border Tool");

		ASSERT(g_gui.eraser);
		sub_sizer->Add(eraserButton = newd BrushButton(this, g_gui.eraser, RENDER_SIZE_32x32, PALETTE_TERRAIN_ERASER));
		eraserButton->SetToolTip("Eraser");

		ASSERT(g_gui.pz_brush);
		sub_sizer->Add(pzBrushButton = newd BrushButton(this, g_gui.pz_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_PZ_TOOL));
		pzBrushButton->SetToolTip("PZ Tool");

		ASSERT(g_gui.rook_brush);
		sub_sizer->Add(nopvpBrushButton = newd BrushButton(this, g_gui.rook_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_NOPVP_TOOL));
		nopvpBrushButton->SetToolTip("NO PVP Tool");

		ASSERT(g_gui.nolog_brush);
		sub_sizer->Add(nologBrushButton = newd BrushButton(this, g_gui.nolog_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_NOLOGOUT_TOOL));
		nologBrushButton->SetToolTip("No Logout Tool");

		ASSERT(g_gui.pvp_brush);
		sub_sizer->Add(pvpzoneBrushButton = newd BrushButton(this, g_gui.pvp_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_PVPZONE_TOOL));
		pvpzoneBrushButton->SetToolTip("PVP Zone Tool");

		// New row
		size_sizer->Add(sub_sizer);
		sub_sizer = newd wxBoxSizer(wxHORIZONTAL);

		ASSERT(g_gui.normal_door_brush);
		sub_sizer->Add(normalDoorButton = newd BrushButton(this, g_gui.normal_door_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_NORMAL_DOOR));
		normalDoorButton->SetToolTip("Normal Door Tool");

		ASSERT(g_gui.locked_door_brush);
		sub_sizer->Add(lockedDoorButton = newd BrushButton(this, g_gui.locked_door_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_LOCKED_DOOR));
		lockedDoorButton->SetToolTip("Locked Door Tool");

		ASSERT(g_gui.magic_door_brush);
		sub_sizer->Add(magicDoorButton = newd BrushButton(this, g_gui.magic_door_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_MAGIC_DOOR));
		magicDoorButton->SetToolTip("Magic Door Tool");

		ASSERT(g_gui.quest_door_brush);
		sub_sizer->Add(questDoorButton = newd BrushButton(this, g_gui.quest_door_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_QUEST_DOOR));
		questDoorButton->SetToolTip("Quest Door Tool");

		ASSERT(g_gui.hatch_door_brush);
		sub_sizer->Add(hatchDoorButton = newd BrushButton(this, g_gui.hatch_door_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_HATCH_DOOR));
		hatchDoorButton->SetToolTip("Hatch Window Tool");

		ASSERT(g_gui.window_door_brush);
		sub_sizer->Add(windowDoorButton = newd BrushButton(this, g_gui.window_door_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_WINDOW_DOOR));
		windowDoorButton->SetToolTip("Window Tool");

		// New row
		size_sizer->Add(sub_sizer);
		sub_sizer = newd wxBoxSizer(wxHORIZONTAL);

		ASSERT(g_gui.normal_door_alt_brush);
		sub_sizer->Add(normalDoorAltButton = newd BrushButton(this, g_gui.normal_door_alt_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_NORMAL_ALT_DOOR));
		normalDoorAltButton->SetToolTip("Normal Door (alt)");

		ASSERT(g_gui.archway_door_brush);
		sub_sizer->Add(archwayDoorButton = newd BrushButton(this, g_gui.archway_door_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_ARCHWAY_DOOR));
		archwayDoorButton->SetToolTip("Archway Tool");
	} else {
		// Create the tool page with 16x16 icons
		// Create tool window #1

		ASSERT(g_gui.optional_brush);
		sub_sizer->Add(optionalBorderButton = newd BrushButton(this, g_gui.optional_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_OPTIONAL_BORDER_TOOL));
		optionalBorderButton->SetToolTip("Optional Border Tool");

		ASSERT(g_gui.eraser);
		sub_sizer->Add(eraserButton = newd BrushButton(this, g_gui.eraser, RENDER_SIZE_16x16, PALETTE_TERRAIN_ERASER));
		eraserButton->SetToolTip("Eraser");

		// sub_sizer->AddSpacer(20);
		ASSERT(g_gui.normal_door_alt_brush);
		sub_sizer->Add(normalDoorAltButton = newd BrushButton(this, g_gui.normal_door_alt_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_NORMAL_ALT_DOOR));
		normalDoorAltButton->SetToolTip("Normal Door (alt)");

		ASSERT(g_gui.normal_door_brush);
		sub_sizer->Add(normalDoorButton = newd BrushButton(this, g_gui.normal_door_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_NORMAL_DOOR));
		normalDoorButton->SetToolTip("Normal Door Tool");

		ASSERT(g_gui.locked_door_brush);
		sub_sizer->Add(lockedDoorButton = newd BrushButton(this, g_gui.locked_door_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_LOCKED_DOOR));
		lockedDoorButton->SetToolTip("Locked Door Tool");

		ASSERT(g_gui.magic_door_brush);
		sub_sizer->Add(magicDoorButton = newd BrushButton(this, g_gui.magic_door_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_MAGIC_DOOR));
		magicDoorButton->SetToolTip("Magic Door Tool");

		ASSERT(g_gui.quest_door_brush);
		sub_sizer->Add(questDoorButton = newd BrushButton(this, g_gui.quest_door_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_QUEST_DOOR));
		questDoorButton->SetToolTip("Quest Door Tool");

		ASSERT(g_gui.hatch_door_brush);
		sub_sizer->Add(hatchDoorButton = newd BrushButton(this, g_gui.hatch_door_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_HATCH_DOOR));
		hatchDoorButton->SetToolTip("Hatch Window Tool");

		ASSERT(g_gui.window_door_brush);
		sub_sizer->Add(windowDoorButton = newd BrushButton(this, g_gui.window_door_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_WINDOW_DOOR));
		windowDoorButton->SetToolTip("Window Tool");

		ASSERT(g_gui.archway_door_brush);
		sub_sizer->Add(archwayDoorButton = newd BrushButton(this, g_gui.archway_door_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_ARCHWAY_DOOR));
		archwayDoorButton->SetToolTip("Archway Tool");

		// Next row
		size_sizer->Add(sub_sizer);
		sub_sizer = newd wxBoxSizer(wxHORIZONTAL);

		ASSERT(g_gui.pz_brush);
		sub_sizer->Add(pzBrushButton = newd BrushButton(this, g_gui.pz_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_PZ_TOOL));
		pzBrushButton->SetToolTip("PZ Tool");

		ASSERT(g_gui.rook_brush);
		sub_sizer->Add(nopvpBrushButton = newd BrushButton(this, g_gui.rook_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_NOPVP_TOOL));
		nopvpBrushButton->SetToolTip("NO PVP Tool");

		ASSERT(g_gui.nolog_brush);
		sub_sizer->Add(nologBrushButton = newd BrushButton(this, g_gui.nolog_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_NOLOGOUT_TOOL));
		nologBrushButton->SetToolTip("No Logout Tool");

		ASSERT(g_gui.pvp_brush);
		sub_sizer->Add(pvpzoneBrushButton = newd BrushButton(this, g_gui.pvp_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_PVPZONE_TOOL));
		pvpzoneBrushButton->SetToolTip("PVP Zone Tool");
	}

	sub_sizer->AddSpacer(large_icons ? 42 : 24);

	wxSizer* checkbox_sub_sizer = newd wxBoxSizer(wxVERTICAL);
	checkbox_sub_sizer->AddSpacer(large_icons ? 12 : 3);

	lockDoorCheckbox = newd wxCheckBox(this, PALETTE_TERRAIN_LOCK_DOOR, "Lock door");
	lockDoorCheckbox->SetToolTip("Prefer to draw \"locked\" variant of selected door brush if applicable.");
	lockDoorCheckbox->SetValue(g_settings.getInteger(Config::DRAW_LOCKED_DOOR));
	checkbox_sub_sizer->Add(lockDoorCheckbox);

	sub_sizer->Add(checkbox_sub_sizer);

	size_sizer->Add(sub_sizer);

	SetSizerAndFit(size_sizer);

	loaded = true;
}

wxString BrushToolPanel::GetName() const {
	return "Tools";
}

void BrushToolPanel::SetToolbarIconSize(bool d) {
	InvalidateContents();
	large_icons = d;
}

void BrushToolPanel::DeselectAll() {
	if (loaded) {
		optionalBorderButton->SetValue(false);
		eraserButton->SetValue(false);
		normalDoorButton->SetValue(false);
		lockedDoorButton->SetValue(false);
		magicDoorButton->SetValue(false);
		questDoorButton->SetValue(false);
		hatchDoorButton->SetValue(false);
		windowDoorButton->SetValue(false);
		normalDoorAltButton->SetValue(false);
		archwayDoorButton->SetValue(false);
		pzBrushButton->SetValue(false);
		nopvpBrushButton->SetValue(false);
		nologBrushButton->SetValue(false);
		pvpzoneBrushButton->SetValue(false);
	}
}

Brush* BrushToolPanel::GetSelectedBrush() const {
	if (optionalBorderButton->GetValue()) {
		return g_gui.optional_brush;
	}
	if (eraserButton->GetValue()) {
		return g_gui.eraser;
	}
	if (normalDoorButton->GetValue()) {
		return g_gui.normal_door_brush;
	}
	if (lockedDoorButton->GetValue()) {
		return g_gui.locked_door_brush;
	}
	if (magicDoorButton->GetValue()) {
		return g_gui.magic_door_brush;
	}
	if (questDoorButton->GetValue()) {
		return g_gui.quest_door_brush;
	}
	if (hatchDoorButton->GetValue()) {
		return g_gui.hatch_door_brush;
	}
	if (windowDoorButton->GetValue()) {
		return g_gui.window_door_brush;
	}
	if (normalDoorAltButton->GetValue()) {
		return g_gui.normal_door_alt_brush;
	}
	if (archwayDoorButton->GetValue()) {
		return g_gui.archway_door_brush;
	}
	if (pzBrushButton->GetValue()) {
		return g_gui.pz_brush;
	}
	if (nopvpBrushButton->GetValue()) {
		return g_gui.rook_brush;
	}
	if (nologBrushButton->GetValue()) {
		return g_gui.nolog_brush;
	}
	if (pvpzoneBrushButton->GetValue()) {
		return g_gui.pvp_brush;
	}
	return nullptr;
}

bool BrushToolPanel::SelectBrush(const Brush* whatbrush) {
	BrushButton* button = nullptr;
	if (whatbrush == g_gui.optional_brush) {
		button = optionalBorderButton;
	} else if (whatbrush == g_gui.eraser) {
		button = eraserButton;
	} else if (whatbrush == g_gui.normal_door_brush) {
		button = normalDoorButton;
	} else if (whatbrush == g_gui.locked_door_brush) {
		button = lockedDoorButton;
	} else if (whatbrush == g_gui.magic_door_brush) {
		button = magicDoorButton;
	} else if (whatbrush == g_gui.quest_door_brush) {
		button = questDoorButton;
	} else if (whatbrush == g_gui.hatch_door_brush) {
		button = hatchDoorButton;
	} else if (whatbrush == g_gui.window_door_brush) {
		button = windowDoorButton;
	} else if (whatbrush == g_gui.normal_door_alt_brush) {
		button = normalDoorAltButton;
	} else if (whatbrush == g_gui.archway_door_brush) {
		button = archwayDoorButton;
	} else if (whatbrush == g_gui.pz_brush) {
		button = pzBrushButton;
	} else if (whatbrush == g_gui.rook_brush) {
		button = nopvpBrushButton;
	} else if (whatbrush == g_gui.nolog_brush) {
		button = nologBrushButton;
	} else if (whatbrush == g_gui.pvp_brush) {
		button = pvpzoneBrushButton;
	}

	DeselectAll();
	if (button) {
		button->SetValue(true);
		return true;
	}

	return false;
}

void BrushToolPanel::OnSwitchIn() {
	LoadCurrentContents();
}

void BrushToolPanel::onClickToolBrush(Brush* brush) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(brush);
}

void BrushToolPanel::OnClickGravelButton(wxCommandEvent& event) {
	onClickToolBrush(g_gui.optional_brush);
}

void BrushToolPanel::OnClickEraserButton(wxCommandEvent& event) {
	onClickToolBrush(g_gui.eraser);
}

void BrushToolPanel::OnClickPZBrushButton(wxCommandEvent& event) {
	onClickToolBrush(g_gui.pz_brush);
}

void BrushToolPanel::OnClickNOPVPBrushButton(wxCommandEvent& event) {
	onClickToolBrush(g_gui.rook_brush);
}

void BrushToolPanel::OnClickNoLogoutBrushButton(wxCommandEvent& event) {
	onClickToolBrush(g_gui.nolog_brush);
}

void BrushToolPanel::OnClickPVPZoneBrushButton(wxCommandEvent& event) {
	onClickToolBrush(g_gui.pvp_brush);
}

void BrushToolPanel::onClickDoorBrush(Brush* brush) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(brush);
	g_gui.SetDoorLocked(lockDoorCheckbox->GetValue());
}

void BrushToolPanel::OnClickNormalDoorButton(wxCommandEvent& event) {
	onClickDoorBrush(g_gui.normal_door_brush);
}

void BrushToolPanel::OnClickLockedDoorButton(wxCommandEvent& event) {
	onClickDoorBrush(g_gui.locked_door_brush);
}

void BrushToolPanel::OnClickMagicDoorButton(wxCommandEvent& event) {
	onClickDoorBrush(g_gui.magic_door_brush);
}

void BrushToolPanel::OnClickQuestDoorButton(wxCommandEvent& event) {
	onClickDoorBrush(g_gui.quest_door_brush);
}

void BrushToolPanel::OnClickHatchDoorButton(wxCommandEvent& event) {
	onClickDoorBrush(g_gui.hatch_door_brush);
}

void BrushToolPanel::OnClickWindowDoorButton(wxCommandEvent& event) {
	onClickDoorBrush(g_gui.window_door_brush);
}

void BrushToolPanel::OnClickNormalAltDoorButton(wxCommandEvent& event) {
	onClickDoorBrush(g_gui.normal_door_alt_brush);
}

void BrushToolPanel::OnClickArchwayDoorButton(wxCommandEvent& event) {
	onClickDoorBrush(g_gui.archway_door_brush);
}

void BrushToolPanel::OnClickLockDoorCheckbox(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());

	// apply to current brush
	g_gui.SetDoorLocked(event.IsChecked());

	// save user preference
	g_settings.setInteger(Config::DRAW_LOCKED_DOOR, event.IsChecked());
}

// ============================================================================
// Brush Button

BEGIN_EVENT_TABLE(BrushButton, ItemToggleButton)
EVT_KEY_DOWN(BrushButton::OnKey)
END_EVENT_TABLE()

BrushButton::BrushButton(wxWindow* parent, Brush* _brush, RenderSize sz, uint32_t id) :
	ItemToggleButton(parent, sz, uint16_t(0), id),
	brush(_brush) {
	ASSERT(sz != RENDER_SIZE_64x64);
	ASSERT(brush);
	SetSprite(brush->getLookID());
	SetToolTip(wxstr(brush->getName()));
}

BrushButton::~BrushButton() {
	////
}

void BrushButton::OnKey(wxKeyEvent& event) {
	g_gui.AddPendingCanvasEvent(event);
}
