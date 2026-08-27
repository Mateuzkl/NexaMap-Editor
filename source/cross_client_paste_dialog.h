#ifndef NEXAMAP_CROSS_CLIENT_PASTE_DIALOG_H_
#define NEXAMAP_CROSS_CLIENT_PASTE_DIALOG_H_

#include "cross_client_clipboard.h"

#include <wx/dialog.h>

class wxListCtrl;

class CrossClientPasteDialog final : public wxDialog {
public:
	CrossClientPasteDialog(wxWindow* parent, const CrossClientPasteAnalysis& analysis);

private:
	void PopulateRows();
	void UpdateColumnWidths();
	wxString CompactPath(const wxString& path) const;

	const CrossClientPasteAnalysis& analysis;
	wxListCtrl* itemList = nullptr;
};

#endif // NEXAMAP_CROSS_CLIENT_PASTE_DIALOG_H_
