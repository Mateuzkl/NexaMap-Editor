// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_REFERENCE_STYLE_WINDOW_H_
#define NEXAMAP_REFERENCE_STYLE_WINDOW_H_

#include <wx/dialog.h>

class Editor;
class wxButton;
class wxStaticBitmap;
class wxStaticText;
class wxListBox;

/// Compact non-blocking panel/dialog showing the captured AI style reference.
/// Displays a visual preview thumbnail, metrics (tiles, floor, visible IDs, brushes),
/// top materials, and quick actions: [Refresh from Selection], [Clear Reference],
/// [Copy Example Prompt], [Close].
class ReferenceStyleWindow final : public wxDialog {
public:
	static void Open(wxWindow* parent);
	static void CaptureAndOpen(wxWindow* parent);

private:
	explicit ReferenceStyleWindow(wxWindow* parent);
	~ReferenceStyleWindow() override;

	void BuildLayout();
	void BindEvents();
	void RefreshView();
	void OnRefreshFromSelection(wxCommandEvent& event);
	void OnClearReference(wxCommandEvent& event);
	void OnCopyExamplePrompt(wxCommandEvent& event);
	void OnCloseButton(wxCommandEvent& event);
	void OnClose(wxCloseEvent& event);

	wxStaticBitmap* thumbnailBitmap_ = nullptr;
	wxStaticText* statusLabel_ = nullptr;
	wxStaticText* metricsLabel_ = nullptr;
	wxStaticText* brushesLabel_ = nullptr;
	wxListBox* materialsListBox_ = nullptr;
	wxButton* refreshButton_ = nullptr;
	wxButton* clearButton_ = nullptr;
	wxButton* copyPromptButton_ = nullptr;
};

#endif // NEXAMAP_REFERENCE_STYLE_WINDOW_H_
