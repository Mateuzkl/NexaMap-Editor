#include "main.h"

#include "border_learning_window.h"

#include "border_workspace_window.h"
#include "dcbutton.h"
#include "editor.h"
#include "ground_brush.h"
#include "gui.h"
#include "items.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
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
	wxDialog(parent, wxID_ANY, "Border Learning", wxDefaultPosition, wxSize(920, 700), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
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
	rootSizer->Add(summaryBox, 0, wxEXPAND | wxALL, FromDIP(8));

	auto* contentSizer = newd wxBoxSizer(wxHORIZONTAL);
	slotList_ = newd wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(530, 430), wxLC_REPORT | wxLC_SINGLE_SEL);
	slotList_->AppendColumn("Slot", wxLIST_FORMAT_LEFT, FromDIP(70));
	slotList_->AppendColumn("Server ID", wxLIST_FORMAT_LEFT, FromDIP(100));
	slotList_->AppendColumn("Obs", wxLIST_FORMAT_RIGHT, FromDIP(55));
	slotList_->AppendColumn("Confidence", wxLIST_FORMAT_RIGHT, FromDIP(90));
	slotList_->AppendColumn("Status", wxLIST_FORMAT_LEFT, FromDIP(115));
	contentSizer->Add(slotList_, 1, wxEXPAND | wxRIGHT, FromDIP(8));

	auto* inspectorBox = newd wxStaticBoxSizer(wxVERTICAL, this, "Selected slot evidence");
	auto* itemRow = newd wxBoxSizer(wxHORIZONTAL);
	itemPreview_ = newd DCButton(inspectorBox->GetStaticBox(), wxID_ANY, wxDefaultPosition, DC_BTN_NORMAL, RENDER_SIZE_64x64, 0);
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

	auto* buttons = newd wxBoxSizer(wxHORIZONTAL);
	openWorkspaceButton_ = newd wxButton(this, wxID_FORWARD, "Open in Border Workspace");
	buttons->Add(openWorkspaceButton_, 0);
	buttons->AddStretchSpacer();
	buttons->Add(newd wxButton(this, wxID_CLOSE), 0);
	rootSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	SetSizer(rootSizer);
	SetMinSize(FromDIP(wxSize(760, 560)));
	Layout();
}

void BorderLearningWindow::BindEvents() {
	FindWindow(wxID_REFRESH)->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ReanalyzeSelection(); });
	addEvidenceButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddSelectionEvidence(); });
	resetEvidenceButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ResetEvidence(); });
	transitionChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { AnalyzeSelectedTransition(); });
	slotList_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& event) {
		selectedEdge_ = static_cast<BorderType>(slotList_->GetItemData(event.GetIndex()));
		RefreshSlotInspector();
	});
	alternativeChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { RefreshCandidateInspector(); });
	useCandidateButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { UseSelectedAlternative(); });
	goToEvidenceButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { GoToSelectedEvidence(); });
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
	RefreshSlotInspector();
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
	for (long row = 0; row < slotList_->GetItemCount(); ++row) {
		if (slotList_->GetItemData(row) == selectedEdge_) {
			slotList_->SetItemState(row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
			break;
		}
	}
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

void BorderLearningWindow::OpenInBorderWorkspace() {
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
	const auto& familyA = analyzedSnapshot.groundFamilies[result_.transition.familyA];
	const auto& familyB = analyzedSnapshot.groundFamilies[result_.transition.familyB];
	draft.description = wxString::Format("learned %s / %s border", wxString::FromUTF8(familyA.name.c_str()), wxString::FromUTF8(familyB.name.c_str()));
	if (BorderWorkspaceWindow::OpenDraft(GetParent(), draft)) {
		Close();
	}
}
