// SPDX-License-Identifier: GPL-3.0-or-later
#include "../main.h"
#include "ingame_preview_window.h"
#include "playtest_map.h"
#include "playtest_hud.h"
#include "playtest_weather.h"
#include "../editor.h"
#include "../editor_resource_session.h"
#include "../gui.h"
#include "../map_display.h"
#include "../map_tab.h"
#include "../map_window.h"
#include "../theme.h"
#include <wx/wrapsizer.h>
#include <wx/weakref.h>

namespace {
	std::optional<Playtest::Facing> KeyDirection(int key) {
		using Playtest::Facing;
		switch (key) {
			case WXK_UP:
				return Facing::North;
			case WXK_RIGHT:
				return Facing::East;
			case WXK_DOWN:
				return Facing::South;
			case WXK_LEFT:
				return Facing::West;
			default:
				return Playtest::LetterDirection(key);
		}
	}
}
IngamePreviewWindow::IngamePreviewWindow(wxWindow* parent) :
	wxPanel(parent, wxID_ANY), timer(this) {
	SetBackgroundColour(Theme::Get(Theme::Role::Background));
	SetForegroundColour(Theme::Get(Theme::Role::Text));
	mainSizer = new wxBoxSizer(wxVERTICAL);
	auto* header = new wxBoxSizer(wxHORIZONTAL);
	auto* title = new wxStaticText(this, wxID_ANY, "MAP PLAYTEST");
	title->SetFont(GetFont().Bold());
	title->SetForegroundColour(Theme::Get(Theme::Role::Accent));
	header->Add(title, 1, wxALIGN_CENTER_VERTICAL);
	auto* exit = new wxButton(this, wxID_ANY, "Exit");
	exit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RequestExit(); });
	header->Add(exit, 0, wxALIGN_CENTER_VERTICAL);
	mainSizer->Add(header, 0, wxEXPAND | wxALL, FromDIP(10));
	hud = new PlaytestHud(this);
	mainSizer->Add(hud, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));
	auto* controls = new wxWrapSizer(wxHORIZONTAL);
	followSelection = new wxCheckBox(this, wxID_ANY, "Follow selection");
	followSelection->SetValue(true);
	followSelection->SetToolTip("Follow the selection or editor camera. Walking switches to independent playtest movement.");
	lightingEnabled = new wxCheckBox(this, wxID_ANY, "Lighting");
	lightingEnabled->SetValue(true);
	weatherChoice = new wxChoice(this, wxID_ANY);
	for (const char* label : { "Weather: Off", "Rain", "Storm", "Fog", "Snow", "Desert Heat" }) {
		weatherChoice->Append(label);
	}
	weatherChoice->SetSelection(0);
	weatherChoice->SetToolTip("Preview the selected weather in any area. Storm includes occasional lightning. No weather data is saved to the map.");
	auto* reset = new wxButton(this, wxID_ANY, "Reset view");
	for (wxWindow* control : { static_cast<wxWindow*>(followSelection), static_cast<wxWindow*>(lightingEnabled), static_cast<wxWindow*>(weatherChoice), static_cast<wxWindow*>(reset) }) {
		controls->Add(control, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(4));
	}
	mainSizer->Add(controls, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(6));
	statusText = new wxStaticText(this, wxID_ANY, "Open a map to playtest.", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	statusText->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	// The viewport is inserted here; hints and status stay outside the GL view.
	mainSizer->Add(statusText, 0, wxEXPAND | wxALL, FromDIP(10));
	auto* hints = new wxStaticText(this, wxID_ANY, "[F6 / ESC] Exit    [WASD / Arrows] Move\n[Right-click / Space] Use    [PgUp / PgDn] Floor");
	hints->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	mainSizer->Add(hints, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
	SetSizer(mainSizer);
	reset->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ResetToEditor(); FocusView(); });
	followSelection->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { lastTarget = { -1, -1, -1 }; UpdateState(); });
	lightingEnabled->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { ApplyPreviewState(); });
	weatherChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { ApplyPreviewState(); FocusView(); });
	Bind(wxEVT_TIMER, &IngamePreviewWindow::OnTimer, this);
	Bind(wxEVT_CHAR_HOOK, &IngamePreviewWindow::OnKeyDown, this);
	Bind(wxEVT_KEY_UP, &IngamePreviewWindow::OnKeyUp, this);
	Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { RequestExit(); });
	timer.Start(33);
}
IngamePreviewWindow::~IngamePreviewWindow() {
	PrepareForClose();
}
void IngamePreviewWindow::PrepareForClose() {
	closing = true;
	timer.Stop();
	input.clear();
	RemovePreviewView();
	editor = nullptr;
	resourceSession.reset();
}
void IngamePreviewWindow::RequestExit() {
	if (closing) {
		return;
	}
	input.clear();
	wxWeakRef<IngamePreviewWindow> weak(this);
	CallAfter([weak] {
		if (weak && g_gui.ingame_preview == weak.get()) {
			g_gui.DestroyIngamePreview();
			if (auto* tab = g_gui.GetCurrentMapTab()) {
				tab->GetCanvas()->SetFocus();
			}
		}
	});
}
bool IngamePreviewWindow::HasInputFocus() const {
	return previewView && wxWindow::FindFocus() == previewView->GetCanvas() && wxTheApp->IsActive();
}
void IngamePreviewWindow::FocusView() {
	if (previewView) {
		previewView->GetCanvas()->SetFocus();
	}
}
void IngamePreviewWindow::RemovePreviewView() {
	if (!previewView) {
		return;
	}
	// Also used before a resource swap or background editor destruction.
	// A deferred Destroy would leave queued paints holding the old Editor&.
	mainSizer->Detach(previewView);
	delete previewView;
	previewView = nullptr;
}
void IngamePreviewWindow::ReleaseEditor(Editor* closingEditor) {
	if (closingEditor != editor) {
		return;
	}
	RemovePreviewView();
	editor = nullptr;
	resourceSession.reset();
	controller.reset({ -1, -1, -1 });
	input.clear();
	hud->SetPosition(controller.position());
	statusText->SetLabel("Open a map to playtest.");
	Layout();
}
void IngamePreviewWindow::SyncEditor(Editor* activeEditor) {
	if (editor == activeEditor && resourceSession.lock() == GetActiveEditorResourceSession()) {
		return;
	}
	RemovePreviewView();
	editor = activeEditor;
	resourceSession = GetActiveEditorResourceSession();
	controller.reset({ -1, -1, -1 });
	input.clear();
	lastTarget = { -1, -1, -1 };
	if (!editor) {
		hud->SetPosition(controller.position());
		statusText->SetLabel("Open a map to playtest.");
		Layout();
		return;
	}
	mapChanges = editor->map.getChunkRevisionTracker().getStats().contentChanges;
	previewView = new MapWindow(this, *editor, true);
	previewView->FitToMap();
	MapCanvas* canvas = previewView->GetCanvas();
	canvas->Bind(wxEVT_KEY_UP, &IngamePreviewWindow::OnKeyUp, this);
	canvas->Bind(wxEVT_RIGHT_DOWN, [this, canvas](wxMouseEvent& event) {
		canvas->SetFocus();
		int x = 0, y = 0;
		canvas->ScreenToMap(event.GetX(), event.GetY(), &x, &y);
		UseAt(Position(x, y, canvas->GetFloor()));
	});
	canvas->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) { input.clear(); event.Skip(); });
	mainSizer->Insert(3, previewView, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));
	Layout();
	ResetToEditor();
}
void IngamePreviewWindow::ResetToEditor() {
	if (!editor) {
		return;
	}
	if (auto* tab = g_gui.GetCurrentMapTab(); tab && tab->GetEditor() == editor) {
		controller.reset(tab->GetScreenCenterPosition());
		followSelection->SetValue(true);
		lastTarget = { -1, -1, -1 };
		input.clear();
	}
}
void IngamePreviewWindow::UpdateState() {
	const auto now = std::chrono::steady_clock::now();
	const double delta = std::chrono::duration<double, std::milli>(now - previousTick).count();
	previousTick = now;
	if (closing || !IsShownOnScreen() || !g_gui.IsRenderingEnabled() || g_gui.IsApplicationClosing()) {
		input.clear();
		return;
	}
	auto* tab = g_gui.GetCurrentMapTab();
	if (tab && tab->GetResourceSession() != GetActiveEditorResourceSession()) {
		input.clear();
		return;
	}
	SyncEditor(tab ? tab->GetEditor() : nullptr);
	if (!editor || !previewView || !tab) {
		return;
	}
	const uint64_t changes = editor->map.getChunkRevisionTracker().getStats().contentChanges;
	if (changes != mapChanges) {
		controller.invalidateInteractions();
		mapChanges = changes;
	}
	if (!HasInputFocus()) {
		input.clear();
	}
	if (followSelection->GetValue() && !controller.moving()) {
		Position target = tab->GetScreenCenterPosition();
		if (editor->selection.size()) {
			const Position min = editor->selection.minPosition(), max = editor->selection.maxPosition();
			target = { min.x + (max.x - min.x) / 2, min.y + (max.y - min.y) / 2, tab->GetCanvas()->GetFloor() };
		}
		if (target.isValid() && target != lastTarget) {
			controller.reset(target);
			lastTarget = target;
		}
	}
	controller.advance(delta);
	if (const auto direction = input.direction(); direction && !controller.moving()) {
		Playtest::MapWorld world(editor->map);
		controller.move(world, *direction);
	}
	elapsed += std::clamp(delta, 0.0, 100.0) / 1000.0;
	ApplyPreviewState();
}
void IngamePreviewWindow::ApplyPreviewState() {
	if (closing || !previewView || !editor || g_gui.GetCurrentEditor() != editor || resourceSession.lock() != GetActiveEditorResourceSession()) {
		return;
	}
	const Position position = controller.position();
	if (!position.isValid()) {
		return;
	}
	MapCanvas* canvas = previewView->GetCanvas();
	const wxSize size = canvas->GetClientSize();
	const double scale = canvas->GetContentScaleFactor();
	const double zoom = std::max(480.0 / std::max(1.0, size.x * scale), 352.0 / std::max(1.0, size.y * scale));
	canvas->SetZoom(std::clamp(zoom, 0.25, 2.0));
	previewView->SetScreenCenterPositionInterpolated(position, controller.offsetX(), controller.offsetY());
	canvas->SetIngamePreviewLighting(lightingEnabled->GetValue());
	canvas->SetPlaytestEffects(static_cast<Playtest::Weather>(std::max(0, weatherChoice->GetSelection())), elapsed, controller.doorOverrides());
	canvas->SetIngamePreviewPlayer(position, static_cast<Direction>(controller.facing()), controller.offsetX(), controller.offsetY(), controller.animationFrame());
	hud->SetPosition(position);
	const wxString status = wxString::FromUTF8(controller.status());
	if (statusText->GetLabel() != status) {
		statusText->SetLabel(status);
		statusText->SetToolTip(status);
	}
}
void IngamePreviewWindow::UseAt(std::optional<Position> target) {
	input.clear();
	if (closing || !editor || g_gui.GetCurrentEditor() != editor || resourceSession.lock() != GetActiveEditorResourceSession()) {
		return;
	}
	followSelection->SetValue(false);
	Playtest::MapWorld world(editor->map);
	controller.use(world, target);
	ApplyPreviewState();
}
void IngamePreviewWindow::OnKeyDown(wxKeyEvent& event) {
	const int key = event.GetKeyCode();
	if (key == WXK_ESCAPE || key == WXK_F6) {
		RequestExit();
		return;
	}
	if (!HasInputFocus() || !editor || closing || g_gui.GetCurrentEditor() != editor || resourceSession.lock() != GetActiveEditorResourceSession()) {
		event.Skip();
		return;
	}
	if (const auto direction = KeyDirection(key)) {
		if (!event.IsAutoRepeat()) {
			followSelection->SetValue(false);
			if (event.ControlDown()) {
				input.clear();
				controller.turn(*direction);
			} else {
				input.press(*direction);
			}
			UpdateState();
		}
		return;
	}
	if (key == WXK_SPACE) {
		if (!event.IsAutoRepeat()) {
			UseAt();
		}
		return;
	}
	if (key == WXK_PAGEUP || key == WXK_PAGEDOWN) {
		input.clear();
		followSelection->SetValue(false);
		Playtest::MapWorld world(editor->map);
		if (!event.IsAutoRepeat()) {
			controller.changeFloor(world, key == WXK_PAGEUP ? -1 : 1);
		}
		ApplyPreviewState();
		return;
	}
	// Do not let editor accelerators edit the map through the playtest canvas.
	if (key == WXK_TAB) {
		event.Skip();
	}
}
void IngamePreviewWindow::OnKeyUp(wxKeyEvent& event) {
	if (const auto direction = KeyDirection(event.GetKeyCode())) {
		input.release(*direction);
	} else {
		event.Skip();
	}
}
void IngamePreviewWindow::OnTimer(wxTimerEvent&) {
	UpdateState();
}
