//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "../main.h"

#include "ingame_preview_window.h"

#include "../editor.h"
#include "../gui.h"
#include "../map_display.h"
#include "../map_tab.h"
#include "../map_window.h"

namespace {
	constexpr int PreviewUpdateIntervalMs = 100;
}

IngamePreviewWindow::IngamePreviewWindow(wxWindow* parent) :
	wxPanel(parent, wxID_ANY),
	mainSizer(newd wxBoxSizer(wxVERTICAL)),
	followSelection(newd wxCheckBox(this, wxID_ANY, "Follow selection")),
	statusText(newd wxStaticText(this, wxID_ANY, "Open a map to preview.")),
	previewView(nullptr),
	editor(nullptr),
	timer(this) {
	followSelection->SetValue(true);
	followSelection->SetToolTip("Center the 15 x 11 preview on the current selection, or on the editor camera when nothing is selected.");

	auto* toolbar = newd wxBoxSizer(wxHORIZONTAL);
	toolbar->Add(followSelection, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 12));
	toolbar->Add(newd wxStaticText(this, wxID_ANY, "Viewport: 15 x 11 | Current floor | In-game rendering"), 0, wxALIGN_CENTER_VERTICAL);

	mainSizer->Add(toolbar, 0, wxEXPAND | wxALL, FROM_DIP(this, 6));
	mainSizer->Add(statusText, 0, wxALIGN_CENTER | wxALL, FROM_DIP(this, 12));
	SetSizer(mainSizer);

	Bind(wxEVT_TIMER, &IngamePreviewWindow::OnTimer, this);
	Bind(wxEVT_CLOSE_WINDOW, &IngamePreviewWindow::OnClose, this);
	Bind(wxEVT_DESTROY, &IngamePreviewWindow::OnDestroy, this);
	timer.Start(PreviewUpdateIntervalMs);
	UpdateState();
}

IngamePreviewWindow::~IngamePreviewWindow() {
	timer.Stop();
}

void IngamePreviewWindow::UpdateState() {
	if (!IsShownOnScreen()) {
		return;
	}

	MapTab* mapTab = g_gui.GetCurrentMapTab();
	Editor* activeEditor = mapTab ? mapTab->GetEditor() : nullptr;
	SyncEditor(activeEditor);
	if (!previewView || !mapTab) {
		return;
	}

	MapCanvas* sourceCanvas = mapTab->GetCanvas();
	const int currentFloor = sourceCanvas->GetFloor();
	Position target = previewView->GetScreenCenterPosition();
	target.z = currentFloor;

	if (followSelection->GetValue()) {
		if (activeEditor->selection.size() != 0) {
			const Position min = activeEditor->selection.minPosition();
			const Position max = activeEditor->selection.maxPosition();
			target.x = min.x + (max.x - min.x) / 2;
			target.y = min.y + (max.y - min.y) / 2;
		} else {
			target = mapTab->GetScreenCenterPosition();
			target.z = currentFloor;
		}
	}

	if (target.isValid() && target != lastTarget) {
		previewView->SetScreenCenterPosition(target);
		lastTarget = target;
	} else if (previewView->GetCanvas()->GetFloor() != currentFloor) {
		previewView->GetCanvas()->ChangeFloor(currentFloor);
	}
	previewView->GetCanvas()->Refresh();
}

void IngamePreviewWindow::ReleaseEditor(Editor* closingEditor) {
	if (editor != closingEditor) {
		return;
	}

	// The editor is deleted on a worker immediately after its last map tab
	// closes. Destroy this canvas synchronously so no queued paint can retain a
	// reference to that editor.
	if (previewView) {
		previewView->Hide();
		mainSizer->Detach(previewView);
		MapWindow* closingView = previewView;
		previewView = nullptr;
		delete closingView;
	}
	editor = nullptr;
	lastTarget = Position(-1, -1, -1);
	statusText->Show();
	Layout();
}

void IngamePreviewWindow::SyncEditor(Editor* activeEditor) {
	if (editor == activeEditor) {
		return;
	}

	RemovePreviewView();
	editor = activeEditor;
	lastTarget = Position(-1, -1, -1);
	if (!editor) {
		statusText->Show();
		Layout();
		return;
	}

	previewView = newd MapWindow(this, *editor, true);
	previewView->FitToMap();
	statusText->Hide();
	mainSizer->Add(previewView, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 6));
	Layout();
}

void IngamePreviewWindow::RemovePreviewView() {
	if (!previewView) {
		return;
	}
	previewView->Hide();
	mainSizer->Detach(previewView);
	previewView->Destroy();
	previewView = nullptr;
}

void IngamePreviewWindow::OnTimer(wxTimerEvent&) {
	UpdateState();
}

void IngamePreviewWindow::OnClose(wxCloseEvent&) {
	g_gui.DestroyIngamePreview();
}

void IngamePreviewWindow::OnDestroy(wxWindowDestroyEvent& event) {
	if (event.GetEventObject() == this && g_gui.ingame_preview == this) {
		g_gui.ingame_preview = nullptr;
	}
	event.Skip();
}
