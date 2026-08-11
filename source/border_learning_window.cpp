#include "main.h"

#include "border_learning_window.h"

#include "border_workspace_window.h"
#include "brush.h"
#include "dcbutton.h"
#include "editor.h"
#include "ground_brush.h"
#include "gui.h"
#include "items.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

namespace {

	BorderLearningWindow*& BorderLearningWindowInstance() {
		static BorderLearningWindow* instance = nullptr;
		return instance;
	}

	constexpr std::array<BorderType, 12> displayEdges = {
		NORTHWEST_CORNER,
		NORTH_HORIZONTAL,
		NORTHEAST_CORNER,
		NORTHWEST_DIAGONAL,
		NORTHEAST_DIAGONAL,
		WEST_HORIZONTAL,
		EAST_HORIZONTAL,
		SOUTHWEST_DIAGONAL,
		SOUTHEAST_DIAGONAL,
		SOUTHWEST_CORNER,
		SOUTH_HORIZONTAL,
		SOUTHEAST_CORNER,
	};
	constexpr std::array<std::pair<int, int>, 12> displayGridPositions = { { { 4, 4 }, { 4, 2 }, { 4, 0 }, { 3, 3 }, { 3, 1 }, { 2, 4 }, { 2, 0 }, { 1, 3 }, { 1, 1 }, { 0, 4 }, { 0, 2 }, { 0, 0 } } };

	const char* EdgeName(BorderType edge) {
		switch (edge) {
			case NORTH_HORIZONTAL:
				return "n";
			case EAST_HORIZONTAL:
				return "e";
			case SOUTH_HORIZONTAL:
				return "s";
			case WEST_HORIZONTAL:
				return "w";
			case NORTHWEST_CORNER:
				return "cnw";
			case NORTHEAST_CORNER:
				return "cne";
			case SOUTHWEST_CORNER:
				return "csw";
			case SOUTHEAST_CORNER:
				return "cse";
			case NORTHWEST_DIAGONAL:
				return "dnw";
			case NORTHEAST_DIAGONAL:
				return "dne";
			case SOUTHWEST_DIAGONAL:
				return "dsw";
			case SOUTHEAST_DIAGONAL:
				return "dse";
			default:
				return "-";
		}
	}

	wxString ConfidenceLabel(double confidence, bool ambiguous) {
		if (ambiguous || confidence < 0.75) {
			return "Ambiguous";
		}
		if (confidence >= 0.90) {
			return "High";
		}
		return "Medium";
	}

	int ItemSpriteId(uint16_t itemId) {
		if (itemId == 0 || !g_items.typeExists(itemId)) {
			return 0;
		}
		return g_items[itemId].clientID;
	}

} // namespace

void BorderLearningWindow::Open(wxWindow* parent, Editor& editor, int floor) {
	if (BorderLearningWindowInstance()) {
		BorderLearningWindowInstance()->Raise();
		BorderLearningWindowInstance()->SetFocus();
		return;
	}
	if (!editor.hasSelection()) {
		wxMessageBox("No map tiles are selected. Select an area containing a ground transition and try again.", "Border Learning", wxOK | wxICON_INFORMATION, parent);
		return;
	}

	BorderLearningSnapshot snapshot = BorderLearningScanner::capture(editor.selection, editor.map, floor);
	if (snapshot.selectedTileCount == 0) {
		wxMessageBox("The selection has no tiles on the current floor. Border Learning analyzes the current floor only.", "Border Learning", wxOK | wxICON_INFORMATION, parent);
		return;
	}
	if (BorderLearningAnalyzer::detectTransitions(snapshot).empty()) {
		wxMessageBox(
			"No terrain transition was found in the selected area.\nSelect tiles containing at least two adjacent ground types and try again.",
			"Border Learning", wxOK | wxICON_INFORMATION, parent
		);
		return;
	}

	BorderLearningWindowInstance() = newd BorderLearningWindow(parent, editor, std::move(snapshot));
	BorderLearningWindowInstance()->Show();
}

BorderLearningWindow::BorderLearningWindow(wxWindow* parent, Editor& editor, BorderLearningSnapshot snapshot) :
	wxDialog(parent, wxID_ANY, "Border Learning", wxDefaultPosition, wxSize(1180, 780), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	editor_(&editor),
	snapshot_(std::move(snapshot)) {
	BuildLayout();
	BindEvents();
	PopulateTransitions();
	CentreOnParent();
}

BorderLearningWindow::~BorderLearningWindow() {
	if (BorderLearningWindowInstance() == this) {
		BorderLearningWindowInstance() = nullptr;
	}
}

void BorderLearningWindow::BuildLayout() {
	auto* rootSizer = newd wxBoxSizer(wxVERTICAL);
	auto* summaryBox = newd wxStaticBoxSizer(wxVERTICAL, this, "Selection and transition");
	selectionLabel_ = newd wxStaticText(summaryBox->GetStaticBox(), wxID_ANY, wxEmptyString);
	qualityLabel_ = newd wxStaticText(summaryBox->GetStaticBox(), wxID_ANY, "Evidence quality: -");
	existingMatchLabel_ = newd wxStaticText(summaryBox->GetStaticBox(), wxID_ANY, "Existing border match: -");
	validationLabel_ = newd wxStaticText(summaryBox->GetStaticBox(), wxID_ANY, "Observed validation: -");
	auto* transitionRow = newd wxBoxSizer(wxHORIZONTAL);
	transitionChoice_ = newd wxChoice(summaryBox->GetStaticBox(), wxID_ANY);
	auto* analyzeButton = newd wxButton(summaryBox->GetStaticBox(), wxID_REFRESH, "Analyze Selection");
	transitionRow->Add(newd wxStaticText(summaryBox->GetStaticBox(), wxID_ANY, "Detected transition:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	transitionRow->Add(transitionChoice_, 1, wxRIGHT, FromDIP(6));
	transitionRow->Add(analyzeButton, 0);
	summaryBox->Add(selectionLabel_, 0, wxEXPAND | wxALL, FromDIP(6));
	summaryBox->Add(transitionRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
	auto* evidenceActions = newd wxBoxSizer(wxHORIZONTAL);
	addEvidenceButton_ = newd wxButton(summaryBox->GetStaticBox(), wxID_ADD, "Add Current Selection to Evidence");
	resetEvidenceButton_ = newd wxButton(summaryBox->GetStaticBox(), wxID_CLEAR, "Reset Evidence");
	evidenceActions->Add(addEvidenceButton_, 0, wxRIGHT, FromDIP(6));
	evidenceActions->Add(resetEvidenceButton_, 0);
	summaryBox->Add(evidenceActions, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
	summaryBox->Add(qualityLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
	summaryBox->Add(existingMatchLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
	summaryBox->Add(validationLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
	auto* diagnosticActions = newd wxBoxSizer(wxHORIZONTAL);
	openExistingButton_ = newd wxButton(summaryBox->GetStaticBox(), wxID_OPEN, "Open Existing Border");
	goToMismatchButton_ = newd wxButton(summaryBox->GetStaticBox(), wxID_ANY, "Go to First Mismatch");
	diagnosticActions->Add(openExistingButton_, 0, wxRIGHT, FromDIP(6));
	diagnosticActions->Add(goToMismatchButton_, 0);
	summaryBox->Add(diagnosticActions, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
	rootSizer->Add(summaryBox, 0, wxEXPAND | wxALL, FromDIP(8));

	auto* contentSizer = newd wxBoxSizer(wxHORIZONTAL);
	slotList_ = newd wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(530, 430), wxLC_REPORT | wxLC_SINGLE_SEL);
	slotList_->AppendColumn("Slot", wxLIST_FORMAT_LEFT, FromDIP(70));
	slotList_->AppendColumn("Server ID", wxLIST_FORMAT_LEFT, FromDIP(100));
	slotList_->AppendColumn("Obs", wxLIST_FORMAT_RIGHT, FromDIP(55));
	slotList_->AppendColumn("Confidence", wxLIST_FORMAT_RIGHT, FromDIP(90));
	slotList_->AppendColumn("Status", wxLIST_FORMAT_LEFT, FromDIP(115));
	contentSizer->Add(slotList_, 1, wxEXPAND | wxRIGHT, FromDIP(8));

	auto* previewBox = newd wxStaticBoxSizer(wxVERTICAL, this, "Learned border sprites");
	auto* previewGrid = newd wxGridSizer(5, 5, FromDIP(3), FromDIP(3));
	for (int row = 0; row < 5; ++row) {
		for (int col = 0; col < 5; ++col) {
			int slotIndex = -1;
			for (int index = 0; index < static_cast<int>(displayGridPositions.size()); ++index) {
				if (displayGridPositions[index] == std::make_pair(row, col)) {
					slotIndex = index;
					break;
				}
			}
			auto* cell = newd wxPanel(previewBox->GetStaticBox(), wxID_ANY, wxDefaultPosition, FromDIP(wxSize(60, 55)));
			auto* cellSizer = newd wxBoxSizer(wxVERTICAL);
			if (slotIndex >= 0) {
				slotPreviewButtons_[slotIndex] = newd DCButton(cell, wxID_ANY, wxDefaultPosition, DC_BTN_TOGGLE, RENDER_SIZE_32x32, 0);
				slotPreviewLabels_[slotIndex] = newd wxStaticText(cell, wxID_ANY, "empty", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
				cellSizer->Add(slotPreviewButtons_[slotIndex], 0, wxALIGN_CENTER);
				cellSizer->Add(slotPreviewLabels_[slotIndex], 0, wxALIGN_CENTER | wxTOP, FromDIP(1));
			} else if (row == 2 && col == 2) {
				cellSizer->AddStretchSpacer();
				cellSizer->Add(newd wxStaticText(cell, wxID_ANY, "GROUND", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL), 0, wxALIGN_CENTER);
				cellSizer->AddStretchSpacer();
			}
			cell->SetSizer(cellSizer);
			previewGrid->Add(cell, 0, wxEXPAND);
		}
	}
	previewBox->Add(previewGrid, 0, wxALL, FromDIP(5));
	contentSizer->Add(previewBox, 0, wxEXPAND | wxRIGHT, FromDIP(8));

	auto* inspectorBox = newd wxStaticBoxSizer(wxVERTICAL, this, "Selected slot evidence");
	auto* itemRow = newd wxBoxSizer(wxHORIZONTAL);
	itemPreview_ = newd DCButton(inspectorBox->GetStaticBox(), wxID_ANY, wxDefaultPosition, DC_BTN_NORMAL, RENDER_SIZE_32x32, 0);
	itemLabel_ = newd wxStaticText(inspectorBox->GetStaticBox(), wxID_ANY, "Select a slot.");
	itemLabel_->Wrap(FromDIP(220));
	itemRow->Add(itemPreview_, 0, wxRIGHT, FromDIP(8));
	itemRow->Add(itemLabel_, 1, wxALIGN_CENTER_VERTICAL);
	inspectorBox->Add(itemRow, 0, wxEXPAND | wxALL, FromDIP(7));
	inspectorBox->Add(newd wxStaticText(inspectorBox->GetStaticBox(), wxID_ANY, "Candidates:"), 0, wxLEFT | wxRIGHT, FromDIP(7));
	alternativeChoice_ = newd wxChoice(inspectorBox->GetStaticBox(), wxID_ANY);
	inspectorBox->Add(alternativeChoice_, 0, wxEXPAND | wxALL, FromDIP(7));
	useCandidateButton_ = newd wxButton(inspectorBox->GetStaticBox(), wxID_APPLY, "Use candidate");
	inspectorBox->Add(useCandidateButton_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(7));
	inspectorBox->Add(newd wxStaticText(inspectorBox->GetStaticBox(), wxID_ANY, "Evidence positions:"), 0, wxLEFT | wxRIGHT, FromDIP(7));
	evidenceList_ = newd wxListBox(inspectorBox->GetStaticBox(), wxID_ANY);
	inspectorBox->Add(evidenceList_, 1, wxEXPAND | wxALL, FromDIP(7));
	goToEvidenceButton_ = newd wxButton(inspectorBox->GetStaticBox(), wxID_FIND, "Go to sample");
	inspectorBox->Add(goToEvidenceButton_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(7));
	contentSizer->Add(inspectorBox, 0, wxEXPAND);
	rootSizer->Add(contentSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	auto* observedBox = newd wxStaticBoxSizer(wxVERTICAL, this, "Observed boundary item sprites");
	observedSpritesPanel_ = newd wxScrolledWindow(observedBox->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(82)), wxHSCROLL | wxBORDER_NONE);
	observedSpritesPanel_->SetScrollRate(FromDIP(8), FromDIP(8));
	observedSpritesSizer_ = newd wxBoxSizer(wxHORIZONTAL);
	observedSpritesPanel_->SetSizer(observedSpritesSizer_);
	observedBox->Add(observedSpritesPanel_, 1, wxEXPAND | wxALL, FromDIP(5));
	rootSizer->Add(observedBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	auto* buttons = newd wxBoxSizer(wxHORIZONTAL);
	openWorkspaceButton_ = newd wxButton(this, wxID_FORWARD, "Open in Border Workspace");
	buttons->Add(openWorkspaceButton_, 0);
	buttons->AddStretchSpacer();
	buttons->Add(newd wxButton(this, wxID_CLOSE), 0);
	rootSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	SetSizer(rootSizer);
	SetMinSize(FromDIP(wxSize(980, 640)));
	Layout();
}

void BorderLearningWindow::BindEvents() {
	FindWindow(wxID_REFRESH)->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ReanalyzeSelection(); });
	addEvidenceButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddSelectionEvidence(); });
	resetEvidenceButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ResetEvidence(); });
	transitionChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { AnalyzeSelectedTransition(); });
	slotList_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& event) {
		SelectEdge(static_cast<BorderType>(slotList_->GetItemData(event.GetIndex())));
	});
	for (size_t index = 0; index < slotPreviewButtons_.size(); ++index) {
		slotPreviewButtons_[index]->Bind(wxEVT_TOGGLEBUTTON, [this, index](wxCommandEvent&) { SelectEdge(displayEdges[index]); });
	}
	alternativeChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { RefreshCandidateInspector(); });
	useCandidateButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { UseSelectedAlternative(); });
	goToEvidenceButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { GoToSelectedEvidence(); });
	openExistingButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OpenExistingBorder(); });
	goToMismatchButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { GoToFirstMismatch(); });
	openWorkspaceButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OpenInBorderWorkspace(); });
	FindWindow(wxID_CLOSE)->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });
	Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { Destroy(); });
}

void BorderLearningWindow::ReanalyzeSelection() {
	if (!editor_ || g_gui.GetCurrentEditor() != editor_) {
		wxMessageBox("Return to the map that started this learning session before analyzing its selection.", "Border Learning", wxOK | wxICON_INFORMATION, this);
		return;
	}
	snapshot_ = BorderLearningScanner::capture(editor_->selection, editor_->map, g_gui.GetCurrentFloor());
	if (snapshot_.selectedTileCount == 0) {
		wxMessageBox("The selection has no tiles on the current floor.", "Border Learning", wxOK | wxICON_INFORMATION, this);
		return;
	}
	PopulateTransitions();
}

void BorderLearningWindow::AddSelectionEvidence() {
	if (!editor_ || g_gui.GetCurrentEditor() != editor_) {
		wxMessageBox("Return to the map that started this learning session before adding evidence.", "Border Learning", wxOK | wxICON_INFORMATION, this);
		return;
	}
	const BorderLearningSnapshot additional = BorderLearningScanner::capture(editor_->selection, editor_->map, g_gui.GetCurrentFloor());
	if (additional.selectedTileCount == 0) {
		wxMessageBox("The current selection has no tiles on this learning session's floor.", "Border Learning", wxOK | wxICON_INFORMATION, this);
		return;
	}

	std::string error = "The current selection does not contain the terrain transition used by this learning session.";
	bool added = false;
	for (const auto& transition : BorderLearningAnalyzer::detectTransitions(additional)) {
		if (session_.addSnapshot(additional, transition, &error)) {
			added = true;
			break;
		}
	}
	if (!added) {
		wxMessageBox(wxString::FromUTF8(error.c_str()), "Border Learning", wxOK | wxICON_INFORMATION, this);
		return;
	}

	result_ = session_.infer(GroundBrush::classifyBorderMask);
	RefreshResult();
}

void BorderLearningWindow::ResetEvidence() {
	AnalyzeSelectedTransition();
}

void BorderLearningWindow::PopulateTransitions() {
	transitions_ = BorderLearningAnalyzer::detectTransitions(snapshot_);
	transitionChoice_->Clear();
	selectionLabel_->SetLabel(wxString::Format("Floor: %d   Selected tiles: %zu   Ignored on other floors: %zu", snapshot_.floor, snapshot_.selectedTileCount, snapshot_.ignoredOtherFloorTiles));
	for (const auto& transition : transitions_) {
		const auto& familyA = snapshot_.groundFamilies[transition.familyA];
		const auto& familyB = snapshot_.groundFamilies[transition.familyB];
		transitionChoice_->Append(wxString::Format("%s  <->  %s   (%zu contacts)", wxString::FromUTF8(familyA.name.c_str()), wxString::FromUTF8(familyB.name.c_str()), transition.contacts));
	}
	if (transitions_.empty()) {
		qualityLabel_->SetLabel("No terrain transition was found in the current selection.");
		session_.clear();
		result_ = LearnedBorderResult {};
		RefreshResult();
		return;
	}
	transitionChoice_->SetSelection(0);
	AnalyzeSelectedTransition();
}

void BorderLearningWindow::AnalyzeSelectedTransition() {
	const int selection = transitionChoice_->GetSelection();
	if (selection == wxNOT_FOUND || static_cast<size_t>(selection) >= transitions_.size()) {
		return;
	}
	session_.clear();
	std::string error;
	if (!session_.addSnapshot(snapshot_, transitions_[selection], &error)) {
		wxMessageBox(wxString::FromUTF8(error.c_str()), "Border Learning", wxOK | wxICON_ERROR, this);
		return;
	}
	result_ = session_.infer(GroundBrush::classifyBorderMask);
	selectedEdge_ = BORDER_NONE;
	RefreshResult();
}

void BorderLearningWindow::RefreshResult() {
	slotList_->DeleteAllItems();
	for (size_t row = 0; row < displayEdges.size(); ++row) {
		const BorderType edge = displayEdges[row];
		const auto& slot = result_.slots[edge];
		const long index = slotList_->InsertItem(static_cast<long>(row), EdgeName(edge));
		slotList_->SetItemData(index, edge);
		wxString itemText = "?";
		if (slot.itemId != 0) {
			itemText = wxString::Format("%u", slot.itemId);
		} else if (!slot.alternatives.empty()) {
			itemText = wxString::Format("%u (?)", slot.alternatives.front().itemId);
		}
		slotList_->SetItem(index, 1, itemText);
		slotList_->SetItem(index, 2, slot.observations == 0 ? wxString("-") : wxString::Format("%zu", slot.observations));
		slotList_->SetItem(index, 3, slot.observations == 0 ? wxString("-") : wxString::Format("%.0f%%", slot.confidence * 100.0));
		slotList_->SetItem(index, 4, slot.alternatives.empty() ? wxString("Missing") : ConfidenceLabel(slot.confidence, slot.ambiguous));
	}

	qualityLabel_->SetLabel(wxString::Format("Evidence quality: %.0f%%   Assigned: %zu/12   Boundary samples: %zu   Selections: %zu   Unique tiles: %zu   Unclassified items: %zu", result_.overallConfidence * 100.0, result_.assignedSlotCount, result_.boundaryObservations.size(), session_.getSelectionCount(), session_.getSnapshot().selectedTileCount, result_.unclassifiedItemIds.size()));
	openWorkspaceButton_->Enable(result_.assignedSlotCount != 0);
	addEvidenceButton_->Enable(!session_.empty());
	resetEvidenceButton_->Enable(session_.getSelectionCount() > 1);
	RefreshSpritePreviews();
	RefreshObservedSprites();
	RefreshDiagnostics();
	RefreshSlotInspector();
}

void BorderLearningWindow::RefreshSpritePreviews() {
	for (size_t index = 0; index < displayEdges.size(); ++index) {
		const BorderType edge = displayEdges[index];
		const auto& slot = result_.slots[edge];
		const uint16_t itemId = slot.itemId != 0 ? slot.itemId : (!slot.alternatives.empty() ? slot.alternatives.front().itemId : 0);
		const bool suggested = slot.itemId == 0 && itemId != 0;
		slotPreviewButtons_[index]->SetSprite(ItemSpriteId(itemId));
		slotPreviewButtons_[index]->SetValue(edge == selectedEdge_);
		slotPreviewLabels_[index]->SetLabel(itemId == 0 ? wxString("empty") : wxString::Format(suggested ? "%u ?" : "%u", itemId));
		slotPreviewButtons_[index]->SetToolTip(itemId == 0 ? wxString::Format("Slot %s: no candidate", EdgeName(edge)) : wxString::Format("Slot %s: Server ID %u%s", EdgeName(edge), itemId, suggested ? " (candidate)" : ""));
	}
}

void BorderLearningWindow::RefreshObservedSprites() {
	std::set<uint16_t> observedItemIds(result_.unclassifiedItemIds.begin(), result_.unclassifiedItemIds.end());
	for (const auto& slot : result_.slots) {
		for (const auto& candidate : slot.alternatives) {
			observedItemIds.insert(candidate.itemId);
		}
	}

	observedSpritesPanel_->Freeze();
	observedSpritesSizer_->Clear(true);
	if (observedItemIds.empty()) {
		observedSpritesSizer_->Add(newd wxStaticText(observedSpritesPanel_, wxID_ANY, "No candidate items were observed on this transition boundary."), 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(8));
	} else {
		for (const uint16_t itemId : observedItemIds) {
			auto* cell = newd wxPanel(observedSpritesPanel_, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(62, 62)));
			auto* cellSizer = newd wxBoxSizer(wxVERTICAL);
			auto* sprite = newd DCButton(cell, wxID_ANY, wxDefaultPosition, DC_BTN_NORMAL, RENDER_SIZE_32x32, ItemSpriteId(itemId));
			auto* label = newd wxStaticText(cell, wxID_ANY, wxString::Format("%u", itemId), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
			const uint16_t clientId = g_items.typeExists(itemId) ? g_items[itemId].clientID : 0;
			const wxString tooltip = wxString::Format("Server ID %u, Client ID %u", itemId, clientId);
			sprite->SetToolTip(tooltip);
			label->SetToolTip(tooltip);
			cellSizer->Add(sprite, 0, wxALIGN_CENTER);
			cellSizer->Add(label, 0, wxALIGN_CENTER | wxTOP, FromDIP(1));
			cell->SetSizer(cellSizer);
			observedSpritesSizer_->Add(cell, 0, wxRIGHT, FromDIP(5));
		}
	}
	observedSpritesPanel_->Layout();
	observedSpritesPanel_->FitInside();
	observedSpritesPanel_->Thaw();
}

void BorderLearningWindow::SelectEdge(BorderType edge) {
	if (edge < NORTH_HORIZONTAL || edge > SOUTHWEST_DIAGONAL) {
		return;
	}
	selectedEdge_ = edge;
	for (long row = 0; row < slotList_->GetItemCount(); ++row) {
		const bool selected = slotList_->GetItemData(row) == edge;
		slotList_->SetItemState(row, selected ? wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED : 0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
	}
	RefreshSpritePreviews();
	RefreshSlotInspector();
}

void BorderLearningWindow::RefreshDiagnostics() {
	std::vector<BorderLearningBorderDefinition> definitions;
	definitions.reserve(g_brushes.getBorders().size());
	for (const auto& [borderId, border] : g_brushes.getBorders()) {
		if (!border || border->ground) {
			continue;
		}
		BorderLearningBorderDefinition definition;
		definition.borderId = borderId;
		for (size_t edgeIndex = NORTH_HORIZONTAL; edgeIndex <= SOUTHWEST_DIAGONAL; ++edgeIndex) {
			definition.items[edgeIndex] = static_cast<uint16_t>(border->tiles[edgeIndex]);
		}
		definitions.push_back(definition);
	}

	matchedBorderId_ = 0;
	exactBorderMatch_ = false;
	const auto matches = BorderLearningAnalyzer::matchExistingBorders(result_, definitions);
	if (!matches.empty() && (matches.front().exact || (matches.front().matchingSlots >= 3 && matches.front().similarity >= 0.60))) {
		const auto& best = matches.front();
		matchedBorderId_ = best.borderId;
		exactBorderMatch_ = best.exact;
		if (best.exact) {
			existingMatchLabel_->SetLabel(wxString::Format("Existing border match: Border %u is an exact duplicate (%zu slots).", best.borderId, best.matchingSlots));
		} else {
			existingMatchLabel_->SetLabel(wxString::Format("Existing border match: Border %u is close — %zu/%zu learned slots match (%.0f%%), %zu conflict.", best.borderId, best.matchingSlots, best.learnedSlots, best.similarity * 100.0, best.conflictingSlots));
		}
	} else {
		existingMatchLabel_->SetLabel("Existing border match: no exact or close loaded border found.");
	}
	openExistingButton_->Enable(matchedBorderId_ != 0);

	validation_ = BorderLearningAnalyzer::validateLearnedBorder(result_);
	validationLabel_->SetLabel(wxString::Format("Observed validation: %zu match, %zu mismatch, %zu unresolved (%.0f%% comparable agreement).", validation_.matchedRoles, validation_.mismatchedRoles, validation_.unresolvedRoles, validation_.matchRate * 100.0));
	goToMismatchButton_->Enable(!validation_.mismatchPositions.empty());
}

const LearnedBorderSlot* BorderLearningWindow::CurrentSlot() const {
	if (selectedEdge_ < NORTH_HORIZONTAL || selectedEdge_ > SOUTHWEST_DIAGONAL) {
		return nullptr;
	}
	return &result_.slots[selectedEdge_];
}

const BorderLearningCandidate* BorderLearningWindow::CurrentCandidate() const {
	const auto* slot = CurrentSlot();
	if (!slot || slot->alternatives.empty()) {
		return nullptr;
	}
	const int selection = alternativeChoice_->GetSelection();
	const size_t index = selection == wxNOT_FOUND ? 0 : static_cast<size_t>(selection);
	return index < slot->alternatives.size() ? &slot->alternatives[index] : nullptr;
}

void BorderLearningWindow::RefreshSlotInspector() {
	const auto* slot = CurrentSlot();
	alternativeChoice_->Clear();
	evidenceList_->Clear();
	if (!slot) {
		itemPreview_->SetSprite(0);
		itemLabel_->SetLabel("Select a slot to inspect its candidates and evidence.");
		useCandidateButton_->Enable(false);
		goToEvidenceButton_->Enable(false);
		return;
	}

	for (const auto& candidate : slot->alternatives) {
		alternativeChoice_->Append(wxString::Format("ID %u - %.0f%% (%zu obs)", candidate.itemId, candidate.confidence * 100.0, candidate.observations));
	}
	if (!slot->alternatives.empty()) {
		size_t selectedCandidate = 0;
		if (slot->itemId != 0) {
			const auto assigned = std::find_if(slot->alternatives.begin(), slot->alternatives.end(), [slot](const BorderLearningCandidate& candidate) {
				return candidate.itemId == slot->itemId;
			});
			if (assigned != slot->alternatives.end()) {
				selectedCandidate = static_cast<size_t>(std::distance(slot->alternatives.begin(), assigned));
			}
		}
		alternativeChoice_->SetSelection(static_cast<int>(selectedCandidate));
	}
	RefreshCandidateInspector();
}

void BorderLearningWindow::RefreshCandidateInspector() {
	evidenceList_->Clear();
	const auto* candidate = CurrentCandidate();
	if (!candidate) {
		itemPreview_->SetSprite(0);
		itemLabel_->SetLabel(wxString::Format("Slot %s has no correlated item candidate.", EdgeName(selectedEdge_)));
		useCandidateButton_->Enable(false);
		goToEvidenceButton_->Enable(false);
		return;
	}

	const uint16_t clientId = g_items.typeExists(candidate->itemId) ? g_items[candidate->itemId].clientID : 0;
	itemPreview_->SetSprite(ItemSpriteId(candidate->itemId));
	itemLabel_->SetLabel(wxString::Format("Slot: %s\nServer ID: %u\nClient ID: %u\nPurity: %.0f%%\nBoundary rate: %.0f%%\nAverage stack: %.1f", EdgeName(selectedEdge_), candidate->itemId, clientId, candidate->purity * 100.0, candidate->boundaryOccurrenceRate * 100.0, candidate->averageStackIndex));
	for (const Position& position : candidate->evidence) {
		evidenceList_->Append(wxString::Format("%d:%d:%d", position.x, position.y, position.z));
	}
	if (!candidate->evidence.empty()) {
		evidenceList_->SetSelection(0);
	}
	useCandidateButton_->Enable(true);
	goToEvidenceButton_->Enable(!candidate->evidence.empty());
}

void BorderLearningWindow::UseSelectedAlternative() {
	auto* slot = selectedEdge_ >= NORTH_HORIZONTAL && selectedEdge_ <= SOUTHWEST_DIAGONAL ? &result_.slots[selectedEdge_] : nullptr;
	const auto* candidate = CurrentCandidate();
	if (!slot || !candidate) {
		return;
	}
	const BorderLearningCandidate selected = *candidate;
	slot->itemId = selected.itemId;
	slot->observations = selected.observations;
	slot->confidence = selected.confidence;
	slot->evidence = selected.evidence;
	slot->ambiguous = false;

	result_.assignedSlotCount = 0;
	double assignedConfidenceTotal = 0.0;
	std::vector<uint16_t> assignedItemIds;
	for (size_t edgeIndex = NORTH_HORIZONTAL; edgeIndex <= SOUTHWEST_DIAGONAL; ++edgeIndex) {
		const auto& assignedSlot = result_.slots[edgeIndex];
		if (assignedSlot.itemId == 0) {
			continue;
		}
		++result_.assignedSlotCount;
		assignedConfidenceTotal += assignedSlot.confidence;
		assignedItemIds.push_back(assignedSlot.itemId);
	}
	result_.overallConfidence = result_.assignedSlotCount == 0 ? 0.0 : assignedConfidenceTotal / result_.assignedSlotCount;
	result_.unclassifiedItemIds.erase(
		std::remove_if(result_.unclassifiedItemIds.begin(), result_.unclassifiedItemIds.end(), [&assignedItemIds](uint16_t itemId) {
			return std::find(assignedItemIds.begin(), assignedItemIds.end(), itemId) != assignedItemIds.end();
		}),
		result_.unclassifiedItemIds.end()
	);
	RefreshResult();
	SelectEdge(selectedEdge_);
}

void BorderLearningWindow::GoToSelectedEvidence() {
	const auto* candidate = CurrentCandidate();
	if (!candidate || candidate->evidence.empty()) {
		return;
	}
	const int selection = evidenceList_->GetSelection();
	const size_t index = selection == wxNOT_FOUND ? 0 : static_cast<size_t>(selection);
	if (index >= candidate->evidence.size()) {
		return;
	}
	const Position position = candidate->evidence[index];
	g_gui.ChangeFloor(position.z);
	g_gui.SetScreenCenterPosition(position, true);
}

void BorderLearningWindow::GoToFirstMismatch() {
	if (validation_.mismatchPositions.empty()) {
		return;
	}
	const Position position = validation_.mismatchPositions.front();
	g_gui.ChangeFloor(position.z);
	g_gui.SetScreenCenterPosition(position, true);
}

void BorderLearningWindow::OpenExistingBorder() {
	if (matchedBorderId_ != 0 && BorderWorkspaceWindow::OpenBorder(GetParent(), static_cast<int>(matchedBorderId_))) {
		Close();
	}
}

void BorderLearningWindow::OpenInBorderWorkspace() {
	if (exactBorderMatch_ && matchedBorderId_ != 0) {
		wxMessageDialog duplicateDialog(
			this,
			wxString::Format("Loaded Border %u already has the same learned slots. Open the existing border instead of creating a duplicate?", matchedBorderId_),
			"Existing border detected", wxYES_NO | wxCANCEL | wxICON_INFORMATION
		);
		duplicateDialog.SetYesNoCancelLabels("Open Existing", "Create New Draft", "Cancel");
		const int choice = duplicateDialog.ShowModal();
		if (choice == wxCANCEL) {
			return;
		}
		if (choice == wxYES) {
			OpenExistingBorder();
			return;
		}
	}

	BorderWorkspaceWindow::Draft draft;
	for (size_t slotIndex = 0; slotIndex < displayEdges.size(); ++slotIndex) {
		draft.items[slotIndex] = result_.slots[displayEdges[slotIndex]].itemId;
	}

	std::vector<uint32_t> suggestedGroups;
	size_t optionalItems = 0;
	size_t assignedItems = 0;
	for (const BorderType edge : displayEdges) {
		const auto& slot = result_.slots[edge];
		if (slot.itemId == 0) {
			continue;
		}
		++assignedItems;
		const auto candidate = std::find_if(slot.alternatives.begin(), slot.alternatives.end(), [&slot](const BorderLearningCandidate& alternative) {
			return alternative.itemId == slot.itemId;
		});
		if (candidate == slot.alternatives.end()) {
			continue;
		}
		if (candidate->borderGroup != 0) {
			suggestedGroups.push_back(candidate->borderGroup);
		}
		optionalItems += candidate->optionalBorder ? 1 : 0;
	}
	if (!suggestedGroups.empty()) {
		std::sort(suggestedGroups.begin(), suggestedGroups.end());
		draft.group = static_cast<int>(*std::max_element(suggestedGroups.begin(), suggestedGroups.end(), [&suggestedGroups](uint32_t left, uint32_t right) {
			return std::count(suggestedGroups.begin(), suggestedGroups.end(), left) < std::count(suggestedGroups.begin(), suggestedGroups.end(), right);
		}));
	}
	draft.optional = assignedItems != 0 && optionalItems * 2 >= assignedItems;

	const auto& analyzedSnapshot = session_.getSnapshot();
	const BorderGroundFamilyIndex familyAIndex = result_.transition.familyA;
	const BorderGroundFamilyIndex familyBIndex = result_.transition.familyB;
	if (familyAIndex == BORDER_GROUND_FAMILY_NONE || familyBIndex == BORDER_GROUND_FAMILY_NONE || familyAIndex >= analyzedSnapshot.groundFamilies.size() || familyBIndex >= analyzedSnapshot.groundFamilies.size()) {
		return;
	}
	const auto& familyA = analyzedSnapshot.groundFamilies[familyAIndex];
	const auto& familyB = analyzedSnapshot.groundFamilies[familyBIndex];
	draft.description = wxString::Format("learned %s / %s border", wxString::FromUTF8(familyA.name.c_str()), wxString::FromUTF8(familyB.name.c_str()));
	if (BorderWorkspaceWindow::OpenDraft(GetParent(), draft)) {
		Close();
	}
}
