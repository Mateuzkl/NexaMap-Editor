#ifndef RME_BORDER_LEARNING_WINDOW_H_
#define RME_BORDER_LEARNING_WINDOW_H_

#include "border_learning.h"

#include <wx/dialog.h>

class DCButton;
class Editor;
class wxButton;
class wxChoice;
class wxListBox;
class wxListCtrl;
class wxStaticText;

class BorderLearningWindow final : public wxDialog {
public:
	static void Open(wxWindow* parent, Editor& editor, int floor);

private:
	BorderLearningWindow(wxWindow* parent, Editor& editor, BorderLearningSnapshot snapshot);

	void BuildLayout();
	void BindEvents();
	void ReanalyzeSelection();
	void PopulateTransitions();
	void AnalyzeSelectedTransition();
	void RefreshResult();
	void RefreshSlotInspector();
	void RefreshCandidateInspector();
	void UseSelectedAlternative();
	void GoToSelectedEvidence();

	const LearnedBorderSlot* CurrentSlot() const;
	const BorderLearningCandidate* CurrentCandidate() const;

	Editor& editor_;
	BorderLearningSnapshot snapshot_;
	std::vector<BorderLearningTransition> transitions_;
	LearnedBorderResult result_;
	BorderType selectedEdge_ = BORDER_NONE;

	wxStaticText* selectionLabel_ = nullptr;
	wxStaticText* qualityLabel_ = nullptr;
	wxChoice* transitionChoice_ = nullptr;
	wxListCtrl* slotList_ = nullptr;
	DCButton* itemPreview_ = nullptr;
	wxStaticText* itemLabel_ = nullptr;
	wxChoice* alternativeChoice_ = nullptr;
	wxListBox* evidenceList_ = nullptr;
	wxButton* useCandidateButton_ = nullptr;
	wxButton* goToEvidenceButton_ = nullptr;
};

#endif
