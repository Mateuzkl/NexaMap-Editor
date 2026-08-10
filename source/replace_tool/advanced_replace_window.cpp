//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "../main.h"
#include "advanced_replace_window.h"

#include "replace_engine.h"

#include "../editor.h"
#include "../gui.h"
#include "../map_display.h"
#include "../theme.h"

#include <filesystem>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/radiobox.h>
#include <wx/splitter.h>
#include <wx/statline.h>
#include <wx/textdlg.h>

namespace {
	std::filesystem::path GetSavedRulesDirectory() {
		return std::filesystem::path(g_gui.GetLocalDataDirectory().ToStdWstring()) / L"replace_rules";
	}
}

AdvancedReplaceWindow::AdvancedReplaceWindow(wxWindow* parent, Editor& editor, MapCanvas& canvas) :
	wxFrame(parent, wxID_ANY, "Advanced Replace", wxDefaultPosition, wxSize(1280, 780), wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT),
	editor(editor),
	canvas(canvas),
	ruleManager(GetSavedRulesDirectory()) {
	auto* root = new wxBoxSizer(wxVERTICAL);
	auto* savedRow = new wxBoxSizer(wxHORIZONTAL);
	savedRow->Add(new wxStaticText(this, wxID_ANY, "Saved rules:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
	savedRuleChoice = new wxChoice(this, wxID_ANY);
	savedRow->Add(savedRuleChoice, 1, wxRIGHT, 4);
	auto* loadButton = new wxButton(this, wxID_ANY, "Load");
	auto* renameButton = new wxButton(this, wxID_ANY, "Rename");
	auto* deleteButton = new wxButton(this, wxID_ANY, "Delete");
	savedRow->Add(loadButton, 0, wxRIGHT, 4);
	savedRow->Add(renameButton, 0, wxRIGHT, 4);
	savedRow->Add(deleteButton, 0);
	root->Add(savedRow, 0, wxEXPAND | wxALL, 8);

	auto* splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);
	libraryPanel = new ReplaceLibraryPanel(splitter, this);
	builderPanel = new ReplaceRuleBuilderPanel(splitter, this);
	splitter->SetMinimumPaneSize(300);
	splitter->SplitVertically(libraryPanel, builderPanel, 570);
	root->Add(splitter, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

	auto* controls = new wxBoxSizer(wxHORIZONTAL);
	wxString scopes[] = { "Selection", "Viewport", "All Map" };
	scopeChoice = new wxRadioBox(this, wxID_ANY, "Scope", wxDefaultPosition, wxDefaultSize, 3, scopes, 1, wxRA_SPECIFY_ROWS);
	controls->Add(scopeChoice, 0, wxRIGHT, 10);

	auto* sourceButtons = new wxBoxSizer(wxVERTICAL);
	addSelectedButton = new wxButton(this, wxID_ANY, "Add selected item as source");
	addSelectedButton->Enable(false);
	auto* addVisibleButton = new wxButton(this, wxID_ANY, "Add visible from viewport");
	sourceButtons->Add(addSelectedButton, 0, wxEXPAND | wxBOTTOM, 4);
	sourceButtons->Add(addVisibleButton, 0, wxEXPAND);
	controls->Add(sourceButtons, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

	auto* statusSizer = new wxBoxSizer(wxVERTICAL);
	scopeStatus = new wxStaticText(this, wxID_ANY, "");
	statusLabel = new wxStaticText(this, wxID_ANY, "Select an item, then add it or drag it into the rule builder.");
	statusSizer->Add(scopeStatus, 0, wxBOTTOM, 4);
	statusSizer->Add(statusLabel, 0);
	controls->Add(statusSizer, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

	executeButton = new wxButton(this, wxID_ANY, "Execute Replace");
	dryRunCheck = new wxCheckBox(this, wxID_ANY, "Dry run");
	dryRunCheck->SetValue(true);
	dryRunCheck->SetToolTip("Count the exact result without modifying the map.");
	controls->Add(dryRunCheck, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	executeButton->Enable(false);
	controls->Add(executeButton, 0, wxALIGN_CENTER_VERTICAL);
	root->Add(new wxStaticLine(this), 0, wxEXPAND | wxALL, 8);
	root->Add(controls, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
	SetSizer(root);

	loadButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { LoadSelectedRuleSet(); });
	renameButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RenameSelectedRuleSet(); });
	deleteButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { DeleteSelectedRuleSet(); });
	addSelectedButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddSelectedSource(); });
	addVisibleButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddVisibleSources(); });
	executeButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ExecuteReplace(); });
	scopeChoice->Bind(wxEVT_RADIOBOX, [this](wxCommandEvent&) {
		pendingExecutionSeed = 0;
		UpdateScopeStatus();
	});

	RefreshSavedRuleSets();
	UpdateScopeStatus();
	UpdateExecuteState();
	CentreOnParent();
}

ReplaceScope AdvancedReplaceWindow::GetScope() const {
	switch (scopeChoice->GetSelection()) {
		case 0:
			return ReplaceScope::Selection;
		case 1:
			return ReplaceScope::Viewport;
		default:
			return ReplaceScope::AllMap;
	}
}

const std::vector<ReplacementRule>& AdvancedReplaceWindow::GetRules() const {
	return builderPanel->GetRules();
}

void AdvancedReplaceWindow::OnReplaceLibraryItemSelected(ServerItemId serverId) {
	selectedLibraryItem = serverId;
	addSelectedButton->Enable(serverId.isValid());
	SetStatus(wxString::Format("Selected SID %u. Add it as a source or drag it to a source/target slot.", serverId.value));
}

void AdvancedReplaceWindow::OnReplaceRulesChanged(const std::vector<ReplacementRule>& rules) {
	pendingExecutionSeed = 0;
	SetStatus(wxString::Format("Draft contains %zu replacement rule(s).", rules.size()));
	UpdateExecuteState();
}

void AdvancedReplaceWindow::OnReplaceRulesSaveRequested(const std::vector<ReplacementRule>& rules) {
	wxString initialName = wxString::FromUTF8(activeRuleSetName);
	wxTextEntryDialog dialog(this, "Name for this rule set:", "Save replacement rules", initialName);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	const std::string name = dialog.GetValue().ToStdString();
	std::string error;
	if (!ruleManager.Save({ name, rules }, error)) {
		SetStatus(wxString::FromUTF8(error), true);
		return;
	}
	activeRuleSetName = name;
	RefreshSavedRuleSets(name);
	SetStatus(wxString::Format("Saved rule set '%s'.", dialog.GetValue()));
}

void AdvancedReplaceWindow::OnReplaceRulesCleared() {
	pendingExecutionSeed = 0;
	activeRuleSetName.clear();
	SetStatus("Draft cleared.");
	UpdateExecuteState();
}

void AdvancedReplaceWindow::RefreshSavedRuleSets(const std::string& preferredName) {
	std::string error;
	const std::vector<std::string> names = ruleManager.List(error);
	savedRuleChoice->Clear();
	for (const std::string& name : names) {
		savedRuleChoice->Append(wxString::FromUTF8(name));
	}
	if (!preferredName.empty()) {
		savedRuleChoice->SetStringSelection(wxString::FromUTF8(preferredName));
	} else if (!names.empty()) {
		savedRuleChoice->SetSelection(0);
	}
	if (!error.empty()) {
		SetStatus(wxString::FromUTF8(error), true);
	}
}

void AdvancedReplaceWindow::LoadSelectedRuleSet() {
	if (savedRuleChoice->GetSelection() == wxNOT_FOUND) {
		SetStatus("Choose a saved rule set first.", true);
		return;
	}
	const std::string name = savedRuleChoice->GetStringSelection().ToStdString();
	std::string error;
	std::optional<RuleSet> ruleSet = ruleManager.Load(name, error);
	if (!ruleSet) {
		SetStatus(wxString::FromUTF8(error), true);
		return;
	}
	activeRuleSetName = name;
	builderPanel->SetRules(std::move(ruleSet->rules));
	UpdateExecuteState();
	SetStatus(wxString::Format("Loaded rule set '%s'.", savedRuleChoice->GetStringSelection()));
}

void AdvancedReplaceWindow::RenameSelectedRuleSet() {
	if (savedRuleChoice->GetSelection() == wxNOT_FOUND) {
		SetStatus("Choose a saved rule set first.", true);
		return;
	}
	const std::string oldName = savedRuleChoice->GetStringSelection().ToStdString();
	wxTextEntryDialog dialog(this, "New rule set name:", "Rename replacement rules", savedRuleChoice->GetStringSelection());
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}
	const std::string newName = dialog.GetValue().ToStdString();
	std::string error;
	if (!ruleManager.Rename(oldName, newName, error)) {
		SetStatus(wxString::FromUTF8(error), true);
		return;
	}
	activeRuleSetName = newName;
	RefreshSavedRuleSets(newName);
	SetStatus(wxString::Format("Renamed rule set to '%s'.", dialog.GetValue()));
}

void AdvancedReplaceWindow::DeleteSelectedRuleSet() {
	if (savedRuleChoice->GetSelection() == wxNOT_FOUND) {
		SetStatus("Choose a saved rule set first.", true);
		return;
	}
	const wxString selected = savedRuleChoice->GetStringSelection();
	if (wxMessageBox(wxString::Format("Delete saved rule set '%s'?", selected), "Advanced Replace", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
		return;
	}
	std::string error;
	if (!ruleManager.Delete(selected.ToStdString(), error)) {
		SetStatus(wxString::FromUTF8(error), true);
		return;
	}
	if (activeRuleSetName == selected.ToStdString()) {
		activeRuleSetName.clear();
	}
	RefreshSavedRuleSets();
	SetStatus(wxString::Format("Deleted rule set '%s'.", selected));
}

void AdvancedReplaceWindow::AddSelectedSource() {
	if (!selectedLibraryItem.isValid()) {
		return;
	}
	if (builderPanel->AddSourceRule(selectedLibraryItem)) {
		SetStatus(wxString::Format("Added SID %u as a source rule.", selectedLibraryItem.value));
	} else {
		SetStatus(wxString::Format("SID %u is already a source rule.", selectedLibraryItem.value), true);
	}
}

void AdvancedReplaceWindow::AddVisibleSources() {
	const ReplaceViewportBounds viewport = GetViewportBounds();
	const std::vector<Tile*> tiles = CollectReplaceScopeTiles(editor.map, editor.selection, ReplaceScope::Viewport, viewport);
	const std::vector<ServerItemId> ids = CollectVisibleServerItemIds(tiles);
	const size_t added = builderPanel->AddSourceRules(ids);
	SetStatus(wxString::Format("Added %zu of %zu visible ServerIDs as source rules.", added, ids.size()));
}

void AdvancedReplaceWindow::ExecuteReplace() {
	const std::vector<Tile*> tiles = CollectReplaceScopeTiles(editor.map, editor.selection, GetScope(), GetViewportBounds());
	const bool dryRun = dryRunCheck->GetValue();
	wxBusyCursor busy;
	const ReplaceExecutionResult result = ReplaceEngine::Run(editor, tiles, builderPanel->GetRules(), { dryRun, dryRun ? 0 : pendingExecutionSeed });
	if (!result.validation.isValid()) {
		SetStatus("The replacement rules are invalid.", true);
		UpdateExecuteState();
		return;
	}
	if (dryRun) {
		pendingExecutionSeed = result.randomSeed;
	} else {
		pendingExecutionSeed = 0;
	}

	const wxString mode = dryRun ? "Dry run" : "Replace complete";
	const wxString summary = wxString::Format(
		"%s\n\nTiles scanned: %zu\nItems scanned: %zu\nMatched: %zu\nReplaced: %zu\nDeleted: %zu\nUnchanged by probability: %zu\nChanged tiles: %zu\nRandom seed: %u%s",
		mode,
		result.tilesScanned,
		result.itemsScanned,
		result.matchedItems,
		result.replacements,
		result.deletions,
		result.unchangedByProbability,
		result.changedTiles,
		result.randomSeed,
		result.committed ? "\n\nThe operation is available as one Undo action." : ""
	);
	SetStatus(wxString::Format(dryRun ? "%s: %zu item(s) would change." : "%s: %zu item(s) changed.", mode, result.ChangedItems()));
	if (result.committed) {
		g_gui.InvalidateAutoborderPreview();
		canvas.RefreshViewport();
	}
	wxMessageBox(summary, "Advanced Replace", wxOK | wxICON_INFORMATION, this);
}

void AdvancedReplaceWindow::UpdateScopeStatus() {
	const std::vector<Tile*> tiles = CollectReplaceScopeTiles(editor.map, editor.selection, GetScope(), GetViewportBounds());
	scopeStatus->SetLabel(wxString::Format("Current scope: %zu tile(s)", tiles.size()));
}

void AdvancedReplaceWindow::UpdateExecuteState() {
	const auto& rules = builderPanel->GetRules();
	const bool valid = !rules.empty() && ValidateRuleSet({ "Draft", rules }).isValid();
	executeButton->Enable(valid);
}

void AdvancedReplaceWindow::SetStatus(const wxString& message, bool error) {
	statusLabel->SetLabel(message);
	statusLabel->SetForegroundColour(error ? wxColour(220, 90, 90) : Theme::Get(Theme::Role::TextSubtle));
}

ReplaceViewportBounds AdvancedReplaceWindow::GetViewportBounds() const {
	int scrollX = 0;
	int scrollY = 0;
	int screenWidth = 0;
	int screenHeight = 0;
	canvas.GetViewBox(&scrollX, &scrollY, &screenWidth, &screenHeight);
	return CalculateReplaceViewportBounds(scrollX, scrollY, screenWidth, screenHeight, canvas.GetZoom(), canvas.GetFloor());
}
