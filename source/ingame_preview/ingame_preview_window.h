//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_INGAME_PREVIEW_WINDOW_H_
#define RME_INGAME_PREVIEW_WINDOW_H_

#include "../position.h"

class Editor;
class MapWindow;

class IngamePreviewWindow final : public wxPanel {
public:
	explicit IngamePreviewWindow(wxWindow* parent);
	~IngamePreviewWindow() override;

	void UpdateState();
	void ReleaseEditor(Editor* closingEditor);

private:
	void SyncEditor(Editor* activeEditor);
	void RemovePreviewView();
	void OnTimer(wxTimerEvent& event);
	void OnClose(wxCloseEvent& event);
	void OnDestroy(wxWindowDestroyEvent& event);

	wxBoxSizer* mainSizer;
	wxCheckBox* followSelection;
	wxStaticText* statusText;
	MapWindow* previewView;
	Editor* editor;
	wxTimer timer;
	Position lastTarget { -1, -1, -1 };
};

#endif
