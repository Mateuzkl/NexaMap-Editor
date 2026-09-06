#ifndef NEXAMAP_CROSS_CLIENT_PASTE_DIALOG_H_
#define NEXAMAP_CROSS_CLIENT_PASTE_DIALOG_H_

#include "cross_client_clipboard.h"

#include <wx/dialog.h>

class wxListCtrl;
class wxButton;
class wxPopupWindow;
class wxStaticText;

class CrossClientPasteDialog final : public wxDialog {
public:
	CrossClientPasteDialog(wxWindow* parent, const CrossClientPasteAnalysis& analysis);
	~CrossClientPasteDialog() override;

	const CrossClientPasteAnalysis& GetAnalysis() const noexcept {
		return analysis;
	}

private:
	void PopulateRows();
	void RefreshState();
	void UpdateColumnWidths();
	void UpdateItemPreview(const wxPoint& position);
	void HideItemPreview();
	void ResolveSelected();
	void ResolveRow(size_t rowIndex);
	void ApplyRecommendedMappings();
	wxString CompactPath(const wxString& path) const;

	CrossClientPasteAnalysis analysis;
	wxListCtrl* itemList = nullptr;
	wxStaticText* matchedValue = nullptr;
	wxStaticText* remappedValue = nullptr;
	wxStaticText* missingValue = nullptr;
	wxStaticText* footerStatus = nullptr;
	wxButton* resolveButton = nullptr;
	wxButton* recommendedButton = nullptr;
	wxButton* applyButton = nullptr;
	wxPopupWindow* itemPreview = nullptr;
	long previewListRow = -1;
};

#endif // NEXAMAP_CROSS_CLIENT_PASTE_DIALOG_H_
