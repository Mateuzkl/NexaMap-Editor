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

#include "main.h"

#include "remove_unreachable_dialog.h"

#include "editor.h"
#include "gui.h"
#include "map_backup.h"
#include "unreachable_cleaner.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/msgdlg.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

enum {
	ID_ANALYZE_REMOVE = wxID_HIGHEST + 5000,
	ID_VIEWPORT_STANDARD,
	ID_VIEWPORT_CUSTOM,
};

BEGIN_EVENT_TABLE(RemoveUnreachableDialog, wxDialog)
EVT_BUTTON(ID_ANALYZE_REMOVE, RemoveUnreachableDialog::OnAnalyzeRemove)
EVT_BUTTON(wxID_CANCEL, RemoveUnreachableDialog::OnCancel)
EVT_RADIOBUTTON(ID_VIEWPORT_STANDARD, RemoveUnreachableDialog::OnViewportRadio)
EVT_RADIOBUTTON(ID_VIEWPORT_CUSTOM, RemoveUnreachableDialog::OnViewportRadio)
END_EVENT_TABLE()

static wxString formatCount(int64_t val) {
	std::string s = std::to_string(val);
	int n = static_cast<int>(s.length()) - 3;
	while (n > 0) {
		s.insert(n, ",");
		n -= 3;
	}
	return wxString::FromUTF8(s);
}

RemoveUnreachableDialog::RemoveUnreachableDialog(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Remove Unreachable Tiles", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE) {
	wxBoxSizer* mainSizer = newd wxBoxSizer(wxVERTICAL);

	// ---- Viewport Dimensions ----
	wxStaticBoxSizer* viewportBox = newd wxStaticBoxSizer(wxVERTICAL, this, "Viewport Dimensions");

	standardViewportRadio_ = newd wxRadioButton(viewportBox->GetStaticBox(), ID_VIEWPORT_STANDARD, "Standard Client (15 x 11)", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	standardViewportRadio_->SetValue(true);
	viewportBox->Add(standardViewportRadio_, 0, wxALL, 4);

	customViewportRadio_ = newd wxRadioButton(viewportBox->GetStaticBox(), ID_VIEWPORT_CUSTOM, "Custom");
	viewportBox->Add(customViewportRadio_, 0, wxALL, 4);

	wxBoxSizer* customRow = newd wxBoxSizer(wxHORIZONTAL);
	customRow->AddSpacer(20);
	customWidthLabel_ = newd wxStaticText(viewportBox->GetStaticBox(), wxID_ANY, "Width:");
	customRow->Add(customWidthLabel_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	customWidthSpin_ = newd wxSpinCtrl(viewportBox->GetStaticBox(), wxID_ANY, "26", wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 3, 100, 26);
	customRow->Add(customWidthSpin_, 0, wxRIGHT, 10);
	customHeightLabel_ = newd wxStaticText(viewportBox->GetStaticBox(), wxID_ANY, "Height:");
	customRow->Add(customHeightLabel_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	customHeightSpin_ = newd wxSpinCtrl(viewportBox->GetStaticBox(), wxID_ANY, "11", wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 3, 100, 11);
	customRow->Add(customHeightSpin_, 0, 0, 0);
	viewportBox->Add(customRow, 0, wxALL, 4);

	mainSizer->Add(viewportBox, 0, wxEXPAND | wxALL, 8);

	// ---- Multi-Floor Visibility ----
	wxStaticBoxSizer* multiFloorBox = newd wxStaticBoxSizer(wxVERTICAL, this, "Multi-Floor Visibility");

	multiFloorCheck_ = newd wxCheckBox(multiFloorBox->GetStaticBox(), wxID_ANY, "Consider multi-floor visibility (Z-axis)");
	multiFloorCheck_->SetValue(true);
	multiFloorBox->Add(multiFloorCheck_, 0, wxALL, 4);

	mainSizer->Add(multiFloorBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	// ---- Floor Scope ----
	wxStaticBoxSizer* floorScopeBox = newd wxStaticBoxSizer(wxVERTICAL, this, "Floor Scope");

	allFloorsRadio_ = newd wxRadioButton(floorScopeBox->GetStaticBox(), wxID_ANY, "All Floors", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	allFloorsRadio_->SetValue(true);
	floorScopeBox->Add(allFloorsRadio_, 0, wxALL, 4);

	surfaceOnlyRadio_ = newd wxRadioButton(floorScopeBox->GetStaticBox(), wxID_ANY, "Surface Only (0-7)");
	floorScopeBox->Add(surfaceOnlyRadio_, 0, wxALL, 4);

	undergroundOnlyRadio_ = newd wxRadioButton(floorScopeBox->GetStaticBox(), wxID_ANY, "Underground Only (8-15)");
	floorScopeBox->Add(undergroundOnlyRadio_, 0, wxALL, 4);

	mainSizer->Add(floorScopeBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	// ---- Cleanup Options ----
	wxStaticBoxSizer* cleanupBox = newd wxStaticBoxSizer(wxVERTICAL, this, "Cleanup Options");

	wxBoxSizer* marginRow = newd wxBoxSizer(wxHORIZONTAL);
	marginRow->Add(newd wxStaticText(cleanupBox->GetStaticBox(), wxID_ANY, "Safety Margin (tiles):"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	safetyMarginSpin_ = newd wxSpinCtrl(cleanupBox->GetStaticBox(), wxID_ANY, "2", wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 0, 20, 2);
	marginRow->Add(safetyMarginSpin_, 0, 0, 0);
	cleanupBox->Add(marginRow, 0, wxALL, 4);

	mainSizer->Add(cleanupBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	// ---- Protection ----
	wxStaticBoxSizer* protectionBox = newd wxStaticBoxSizer(wxVERTICAL, this, "Protection");

	preserveHousesCheck_ = newd wxCheckBox(protectionBox->GetStaticBox(), wxID_ANY, "Preserve houses");
	preserveHousesCheck_->SetValue(true);
	protectionBox->Add(preserveHousesCheck_, 0, wxALL, 3);

	preserveSpawnsCheck_ = newd wxCheckBox(protectionBox->GetStaticBox(), wxID_ANY, "Preserve spawns");
	preserveSpawnsCheck_->SetValue(true);
	protectionBox->Add(preserveSpawnsCheck_, 0, wxALL, 3);

	preserveCreaturesCheck_ = newd wxCheckBox(protectionBox->GetStaticBox(), wxID_ANY, "Preserve monsters/NPCs");
	preserveCreaturesCheck_->SetValue(true);
	protectionBox->Add(preserveCreaturesCheck_, 0, wxALL, 3);

	preserveTeleportsCheck_ = newd wxCheckBox(protectionBox->GetStaticBox(), wxID_ANY, "Preserve teleports");
	preserveTeleportsCheck_->SetValue(true);
	protectionBox->Add(preserveTeleportsCheck_, 0, wxALL, 3);

	preserveActionUidCheck_ = newd wxCheckBox(protectionBox->GetStaticBox(), wxID_ANY, "Preserve ActionID / UniqueID");
	preserveActionUidCheck_->SetValue(true);
	protectionBox->Add(preserveActionUidCheck_, 0, wxALL, 3);

	preserveWaypointsCheck_ = newd wxCheckBox(protectionBox->GetStaticBox(), wxID_ANY, "Preserve waypoints");
	preserveWaypointsCheck_->SetValue(true);
	protectionBox->Add(preserveWaypointsCheck_, 0, wxALL, 3);

	protectionBox->AddSpacer(4);

	createBackupCheck_ = newd wxCheckBox(protectionBox->GetStaticBox(), wxID_ANY, "Create map backup before modifying");
	createBackupCheck_->SetValue(true);
	protectionBox->Add(createBackupCheck_, 0, wxALL, 3);

	mainSizer->Add(protectionBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	// ---- Buttons ----
	wxBoxSizer* buttonSizer = newd wxBoxSizer(wxHORIZONTAL);
	buttonSizer->AddStretchSpacer();
	buttonSizer->Add(newd wxButton(this, ID_ANALYZE_REMOVE, "Analyze / Remove"), 0, wxRIGHT, 8);
	buttonSizer->Add(newd wxButton(this, wxID_CANCEL, "Cancel"), 0, 0, 0);
	mainSizer->Add(buttonSizer, 0, wxEXPAND | wxALL, 8);

	SetSizerAndFit(mainSizer);
	Centre();

	UpdateCustomFieldsState();
}

void RemoveUnreachableDialog::OnViewportRadio(wxCommandEvent& WXUNUSED(event)) {
	UpdateCustomFieldsState();
}

void RemoveUnreachableDialog::UpdateCustomFieldsState() {
	bool isCustom = customViewportRadio_->GetValue();
	customWidthSpin_->Enable(isCustom);
	customHeightSpin_->Enable(isCustom);
	customWidthLabel_->Enable(isCustom);
	customHeightLabel_->Enable(isCustom);
}

void RemoveUnreachableDialog::OnCancel(wxCommandEvent& WXUNUSED(event)) {
	EndModal(wxID_CANCEL);
}

void RemoveUnreachableDialog::OnAnalyzeRemove(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return;
	}

	// Gather settings from the dialog
	UnreachableCleanerSettings settings;

	if (standardViewportRadio_->GetValue()) {
		settings.viewportWidth = 15;
		settings.viewportHeight = 11;
	} else {
		settings.viewportWidth = customWidthSpin_->GetValue();
		settings.viewportHeight = customHeightSpin_->GetValue();
	}

	settings.multiFloor = multiFloorCheck_->GetValue();
	settings.safetyMargin = safetyMarginSpin_->GetValue();

	if (surfaceOnlyRadio_->GetValue()) {
		settings.floorScope = UnreachableCleanerSettings::SurfaceOnly;
	} else if (undergroundOnlyRadio_->GetValue()) {
		settings.floorScope = UnreachableCleanerSettings::UndergroundOnly;
	} else {
		settings.floorScope = UnreachableCleanerSettings::AllFloors;
	}

	settings.preserveHouses = preserveHousesCheck_->GetValue();
	settings.preserveSpawns = preserveSpawnsCheck_->GetValue();
	settings.preserveCreatures = preserveCreaturesCheck_->GetValue();
	settings.preserveTeleports = preserveTeleportsCheck_->GetValue();
	settings.preserveActionUniqueID = preserveActionUidCheck_->GetValue();
	settings.preserveWaypoints = preserveWaypointsCheck_->GetValue();
	settings.createBackup = createBackupCheck_->GetValue();

	// Run analysis with progress bar
	Map& map = editor->getMap();
	UnreachableCleaner cleaner(map, settings);

	g_gui.CreateLoadBar("Analyzing unreachable tiles...");

	auto progressCallback = [](int pct, const std::string& msg) -> bool {
		return g_gui.SetLoadDone(pct, wxString::FromUTF8(msg));
	};

	UnreachableAnalysisResult result = cleaner.analyze(progressCallback);
	g_gui.DestroyLoadBar();

	// Check if analysis was cancelled or failed
	if (result.isCancelled()) {
		return;
	}
	if (result.isFailed()) {
		wxMessageBox("Analysis failed.", "Remove Unreachable Tiles", wxOK | wxICON_ERROR, this);
		return;
	}

	// Format and show analysis summary
	wxString summary;
	summary << "Analysis Results\n"
			<< "─────────────────────────────────\n\n"
			<< wxString::Format("Tiles scanned:               %s\n", formatCount(result.totalScanned))
			<< wxString::Format("Walkable viewpoints:         %s\n", formatCount(result.walkableViewpoints))
			<< wxString::Format("Unreachable candidates:      %s\n", formatCount(result.unreachableCandidates))
			<< wxString::Format("Protected tiles skipped:     %s\n", formatCount(result.protectedSkipped));

	if (result.protectedSkipped > 0) {
		summary << "\nProtected breakdown:\n";
		if (result.housesProtected > 0) {
			summary << wxString::Format("  Houses:         %lld\n", static_cast<long long>(result.housesProtected));
		}
		if (result.spawnsProtected > 0) {
			summary << wxString::Format("  Spawns:         %lld\n", static_cast<long long>(result.spawnsProtected));
		}
		if (result.creaturesProtected > 0) {
			summary << wxString::Format("  Creatures:      %lld\n", static_cast<long long>(result.creaturesProtected));
		}
		if (result.teleportsProtected > 0) {
			summary << wxString::Format("  Teleports:      %lld\n", static_cast<long long>(result.teleportsProtected));
		}
		if (result.actionUidProtected > 0) {
			summary << wxString::Format("  ActionID/UID:   %lld\n", static_cast<long long>(result.actionUidProtected));
		}
		if (result.waypointsProtected > 0) {
			summary << wxString::Format("  Waypoints:      %lld\n", static_cast<long long>(result.waypointsProtected));
		}
		if (result.neighborProtected > 0) {
			summary << wxString::Format("  Neighbors:      %lld\n", static_cast<long long>(result.neighborProtected));
		}
	}

	summary << wxString::Format("\nTiles to remove:             %s\n", formatCount(result.tilesToRemove))
			<< wxString::Format("\nAnalysis time:               %.1f ms\n", result.analysisTimeMs);

	if (result.tilesToRemove == 0) {
		wxMessageBox(summary + "\nNo tiles to remove.", "Remove Unreachable Tiles", wxOK | wxICON_INFORMATION, this);
		return;
	}

	summary << "\nThis operation cannot be undone. The undo history will be cleared.\n";
	if (settings.createBackup) {
		summary << "A backup will be created before modifying the map.\n";
	}
	summary << "\nProceed with removal?";

	int confirm = wxMessageBox(summary, "Remove Unreachable Tiles — Confirm", wxYES_NO | wxICON_WARNING, this);
	if (confirm != wxYES) {
		return;
	}

	// Create backup if enabled (without nesting load bars)
	if (settings.createBackup) {
		std::string backupPath = MapBackupService::createBackup(*editor, this);

		if (backupPath.empty()) {
			int proceed = wxMessageBox(
				"Failed to create backup.\n\nDo you want to continue without a backup?",
				"Backup Failed",
				wxYES_NO | wxICON_WARNING,
				this
			);
			if (proceed != wxYES) {
				return;
			}
		} else {
			std::printf("[unreachable_cleaner] backup saved to: %s\n", backupPath.c_str());
		}
	}

	// Clear selection and action queue before destructive operation
	editor->selection.clear();
	editor->actionQueue->clear();

	// Execute removal
	g_gui.CreateLoadBar("Removing unreachable tiles...");

	int64_t removed = cleaner.execute(progressCallback);

	g_gui.DestroyLoadBar();

	// Mark map as changed
	map.doChange();
	g_gui.RefreshView();

	// Show result
	wxString resultMsg;
	resultMsg << wxString::Format("%s tiles removed.", formatCount(removed));
	wxMessageBox(resultMsg, "Remove Unreachable Tiles", wxOK | wxICON_INFORMATION, this);

	EndModal(wxID_OK);
}
