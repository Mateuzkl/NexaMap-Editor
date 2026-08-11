#ifndef RME_BORDER_LEARNING_WINDOW_H_
#define RME_BORDER_LEARNING_WINDOW_H_

#include "border_learning.h"

#include <wx/dialog.h>

class DCButton;
class Editor;
class wxBoxSizer;
class wxButton;
class wxChoice;
class wxListBox;
class wxListCtrl;
class wxScrolledWindow;
class wxStaticText;

class BorderLearningWindow final : public wxDialog {
public:
	static void Open(wxWindow* parent, Editor& editor, int floor);

private:
	BorderLearningWindow(wxWindow* parent, Editor& editor, BorderLearningSnapshot snapshot);
	~BorderLearningWindow() override;

	void BuildLayout();
	void BindEvents();
	void ReanalyzeSelection();
	void AddSelectionEvidence();
	void ResetEvidence();
	void PopulateTransitions();
	void AnalyzeSelectedTransition();
	void RefreshResult();
	void RefreshSpritePreviews();
	void RefreshObservedSprites();
	void RefreshDiagnostics();
	void RefreshSlotInspector();
	void RefreshCandidateInspector();
	void SelectEdge(BorderType edge);
	void UseSelectedAlternative();
	void GoToSelectedEvidence();
	void GoToFirstMismatch();
	void OpenExistingBorder();
	void OpenInBorderWorkspace();

	const LearnedBorderSlot* CurrentSlot() const;
	const BorderLearningCandidate* CurrentCandidate() const;

	Editor* editor_ = nullptr;
	BorderLearningSnapshot snapshot_;
	BorderLearningSession session_;
	std::vector<BorderLearningTransition> transitions_;
	LearnedBorderResult result_;
	BorderLearningValidation validation_;
	BorderType selectedEdge_ = BORDER_NONE;
	uint32_t matchedBorderId_ = 0;
	bool exactBorderMatch_ = false;

	wxStaticText* selectionLabel_ = nullptr;
	wxStaticText* qualityLabel_ = nullptr;
	wxStaticText* existingMatchLabel_ = nullptr;
	wxStaticText* validationLabel_ = nullptr;
	wxChoice* transitionChoice_ = nullptr;
	wxListCtrl* slotList_ = nullptr;
	std::array<DCButton*, 12> slotPreviewButtons_ {};
	std::array<wxStaticText*, 12> slotPreviewLabels_ {};
	wxScrolledWindow* observedSpritesPanel_ = nullptr;
	wxBoxSizer* observedSpritesSizer_ = nullptr;
	DCButton* itemPreview_ = nullptr;
	wxStaticText* itemLabel_ = nullptr;
	wxChoice* alternativeChoice_ = nullptr;
	wxListBox* evidenceList_ = nullptr;
	wxButton* useCandidateButton_ = nullptr;
	wxButton* goToEvidenceButton_ = nullptr;
	wxButton* openWorkspaceButton_ = nullptr;
	wxButton* addEvidenceButton_ = nullptr;
	wxButton* resetEvidenceButton_ = nullptr;
	wxButton* openExistingButton_ = nullptr;
	wxButton* goToMismatchButton_ = nullptr;
};

#endif
