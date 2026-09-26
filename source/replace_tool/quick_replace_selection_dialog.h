//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_REPLACE_TOOL_QUICK_REPLACE_SELECTION_DIALOG_H_
#define RME_REPLACE_TOOL_QUICK_REPLACE_SELECTION_DIALOG_H_

#include "../position.h"
#include "quick_replace_selection_model.h"

#include <wx/dialog.h>

class DCButton;
class Editor;
class MapCanvas;
class QuickReplaceCandidateList;
class Tile;
class wxButton;
class wxCheckBox;
class wxCommandEvent;
class wxStaticText;

class QuickReplaceSelectionDialog final : public wxDialog {
public:
	QuickReplaceSelectionDialog(MapCanvas* parent, Editor& editor, PositionVector selectedPositions);
	~QuickReplaceSelectionDialog() override;

private:
	[[nodiscard]] std::vector<Tile*> ResolveSelectedTiles() const;
	[[nodiscard]] const QuickReplaceCandidate* CurrentCandidate() const;
	void RefreshCandidates(uint16_t preferredSourceId = 0);
	void SelectCandidate(size_t index);
	void ClearTarget();
	void UpdateSourcePreview();
	void UpdateTargetPreview();
	void UpdateReplaceState();
	void SetStatus(const wxString& message, bool error = false);

	void OnSourceSelected(wxCommandEvent& event);
	void OnChooseReplacement(wxCommandEvent& event);
	void OnSameCategoryChanged(wxCommandEvent& event);
	void OnReplace(wxCommandEvent& event);
	void OnClose(wxCommandEvent& event);

	MapCanvas& canvas_;
	Editor& editor_;
	PositionVector selectedPositions_;
	std::vector<QuickReplaceCandidate> candidates_;
	uint16_t sourceId_ = 0;
	uint16_t targetId_ = 0;
	QuickReplaceCategory targetCategory_ = QuickReplaceCategory::Item;

	QuickReplaceCandidateList* sourceList_ = nullptr;
	wxStaticText* scopeLabel_ = nullptr;
	DCButton* beforeSprite_ = nullptr;
	wxStaticText* beforeDetails_ = nullptr;
	DCButton* afterSprite_ = nullptr;
	wxStaticText* afterDetails_ = nullptr;
	wxButton* chooseButton_ = nullptr;
	wxCheckBox* sameCategoryCheck_ = nullptr;
	wxButton* replaceButton_ = nullptr;
	wxButton* closeButton_ = nullptr;
	wxStaticText* statusLabel_ = nullptr;
};

#endif
