// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RME_INGAME_PREVIEW_WINDOW_H_
#define RME_INGAME_PREVIEW_WINDOW_H_
#include <wx/panel.h>
#include <wx/timer.h>
#include <chrono>
#include <memory>
#include "playtest_controller.h"
#include "playtest_input.h"
class Editor;
class EditorResourceSession;
class MapWindow;
class PlaytestHud;
class wxCheckBox;
class wxChoice;
class wxStaticText;
class wxBoxSizer;

// Keep the existing pane/API identity so saved layouts and ActionIDs still work.
class IngamePreviewWindow final : public wxPanel {
public:
	explicit IngamePreviewWindow(wxWindow* parent);
	~IngamePreviewWindow() override;
	void UpdateState();
	void ReleaseEditor(Editor* closingEditor);
	void PrepareForClose();
	void FocusView();

private:
#ifdef NEXAMAP_MULTIPLAYER_TESTS
	friend class PlaytestIntegrationTests;
#endif
	void SyncEditor(Editor* activeEditor);
	void RemovePreviewView();
	void ResetToEditor();
	void ApplyPreviewState();
	void UseAt(std::optional<Position> target = {});
	void RequestExit();
	void OnKeyDown(wxKeyEvent& event);
	void OnKeyUp(wxKeyEvent& event);
	void OnTimer(wxTimerEvent& event);
	bool HasInputFocus() const;
	wxBoxSizer* mainSizer = nullptr;
	wxCheckBox* followSelection = nullptr;
	wxCheckBox* lightingEnabled = nullptr;
	wxChoice* weatherChoice = nullptr;
	wxStaticText* statusText = nullptr;
	PlaytestHud* hud = nullptr;
	MapWindow* previewView = nullptr;
	Editor* editor = nullptr;
	std::weak_ptr<EditorResourceSession> resourceSession;
	wxTimer timer;
	Playtest::Controller controller;
	Playtest::Input input;
	Position lastTarget { -1, -1, -1 };
	std::chrono::steady_clock::time_point previousTick = std::chrono::steady_clock::now();
	double elapsed = 0;
	uint64_t mapChanges = 0;
	bool closing = false;
};
#endif
