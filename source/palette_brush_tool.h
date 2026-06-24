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

#ifndef RME_PALETTE_BRUSH_TOOL_H_
#define RME_PALETTE_BRUSH_TOOL_H_

#include "palette_common.h"

class BrushButton : public ItemToggleButton {
public:
	BrushButton(wxWindow* parent, Brush* brush, RenderSize, uint32_t id = wxID_ANY);
	~BrushButton() override;

	Brush* brush;

	void OnKey(wxKeyEvent& event);

	DECLARE_EVENT_TABLE()
};

class BrushToolPanel : public PalettePanel {
public:
	BrushToolPanel(wxWindow* parent);
	~BrushToolPanel() override;

	// Interface
	// Flushes this panel and consequent views will feature reloaded data
	void InvalidateContents() override;
	// Loads the currently displayed page
	void LoadCurrentContents() override;
	// Loads all content in this panel
	void LoadAllContents() override;

	wxString GetName() const override;
	void SetToolbarIconSize(bool large) override;

	// Returns the currently selected brush (First brush if panel is not loaded)
	Brush* GetSelectedBrush() const override;
	// Select the brush in the parameter, this only changes the look of the panel
	bool SelectBrush(const Brush* whatbrush) override;

	// Called when this page is displayed
	void OnSwitchIn() override;

	// wxWidgets event handling
	void OnClickGravelButton(wxCommandEvent& event);
	void OnClickEraserButton(wxCommandEvent& event);
	// ----
	void OnClickNormalDoorButton(wxCommandEvent& event);
	void OnClickLockedDoorButton(wxCommandEvent& event);
	void OnClickMagicDoorButton(wxCommandEvent& event);
	void OnClickQuestDoorButton(wxCommandEvent& event);
	void OnClickHatchDoorButton(wxCommandEvent& event);
	void OnClickWindowDoorButton(wxCommandEvent& event);
	void OnClickNormalAltDoorButton(wxCommandEvent& event);
	void OnClickArchwayDoorButton(wxCommandEvent& event);
	// ----
	void OnClickPZBrushButton(wxCommandEvent& event);
	void OnClickNOPVPBrushButton(wxCommandEvent& event);
	void OnClickNoLogoutBrushButton(wxCommandEvent& event);
	void OnClickPVPZoneBrushButton(wxCommandEvent& event);
	// ----
	void OnClickLockDoorCheckbox(wxCommandEvent& event);

private:
	void onClickToolBrush(Brush* brush);
	void onClickDoorBrush(Brush* brush);

public:
	void DeselectAll() override;

	bool loaded;
	bool large_icons;

	BrushButton* optionalBorderButton;
	BrushButton* eraserButton;
	// ----
	BrushButton* normalDoorButton;
	BrushButton* lockedDoorButton;
	BrushButton* magicDoorButton;
	BrushButton* questDoorButton;
	BrushButton* hatchDoorButton;
	BrushButton* windowDoorButton;
	BrushButton* normalDoorAltButton;
	BrushButton* archwayDoorButton;
	// ----
	BrushButton* pzBrushButton;
	BrushButton* nopvpBrushButton;
	BrushButton* nologBrushButton;
	BrushButton* pvpzoneBrushButton;

	wxCheckBox* lockDoorCheckbox;

	DECLARE_EVENT_TABLE()
};

#endif
