//////////////////////////////////////////////////////////////////////
// This file is part of NexaMap Editor
//////////////////////////////////////////////////////////////////////
// NexaMap Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// NexaMap Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#ifndef RME_REMOVE_UNREACHABLE_DIALOG_H_
#define RME_REMOVE_UNREACHABLE_DIALOG_H_

#include <wx/dialog.h>

#include "unreachable_cleaner.h"

class wxRadioButton;
class wxCheckBox;
class wxSpinCtrl;
class wxStaticText;

// ---------------------------------------------------------------------------
// Dialog for the advanced "Remove Unreachable Tiles" operation.
// Collects settings, runs analysis, shows summary, and executes removal.
// ---------------------------------------------------------------------------
class RemoveUnreachableDialog : public wxDialog {
public:
	RemoveUnreachableDialog(wxWindow* parent);

private:
	void OnAnalyzeRemove(wxCommandEvent& event);
	void OnCancel(wxCommandEvent& event);
	void OnViewportRadio(wxCommandEvent& event);
	void UpdateCustomFieldsState();

	// Viewport
	wxRadioButton* standardViewportRadio_;
	wxRadioButton* customViewportRadio_;
	wxSpinCtrl* customWidthSpin_;
	wxSpinCtrl* customHeightSpin_;
	wxStaticText* customWidthLabel_;
	wxStaticText* customHeightLabel_;

	// Multi-floor
	wxCheckBox* multiFloorCheck_;

	// Floor scope
	wxRadioButton* allFloorsRadio_;
	wxRadioButton* surfaceOnlyRadio_;
	wxRadioButton* undergroundOnlyRadio_;

	// Safety margin
	wxSpinCtrl* safetyMarginSpin_;

	// Protection
	wxCheckBox* preserveHousesCheck_;
	wxCheckBox* preserveSpawnsCheck_;
	wxCheckBox* preserveCreaturesCheck_;
	wxCheckBox* preserveTeleportsCheck_;
	wxCheckBox* preserveActionUidCheck_;
	wxCheckBox* preserveWaypointsCheck_;
	wxCheckBox* createBackupCheck_;

	DECLARE_EVENT_TABLE()
};

#endif
