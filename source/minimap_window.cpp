#include "main.h"

#include "minimap_window.h"

#include "editor.h"
#include "gui.h"
#include "map.h"
#include "map_display.h"
#include "map_tab.h"
#include "minimap_canvas.h"
#include "minimap_style.h"
#include "settings.h"

#include <wx/menu.h>
#include <wx/tokenzr.h>

#include <algorithm>
#include <vector>

MinimapWindow::MinimapWindow(wxWindow* parent) :
	wxPanel(parent, wxID_ANY, wxDefaultPosition, FROM_DIP(parent, wxSize(300, 340)), wxBORDER_NONE),
	updateTimer_(this),
	showControls_(g_settings.getBoolean(Config::MINIMAP_SHOW_CONTROLS)) {
	SetMinSize(FROM_DIP(parent, wxSize(190, 180)));
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	BuildLayout();
	canvas_->SetZoomLevel(g_settings.getInteger(Config::MINIMAP_ZOOM_LEVEL));
	canvas_->SetCompassVisible(g_settings.getBoolean(Config::MINIMAP_SHOW_COMPASS));
	canvas_->SetStateChangedCallback([this]() {
		g_settings.setInteger(Config::MINIMAP_ZOOM_LEVEL, canvas_->GetZoomLevel());
		g_settings.setInteger(Config::MINIMAP_SHOW_COMPASS, canvas_->IsCompassVisible() ? 1 : 0);
		UpdateState();
	});
	ApplyTheme();
	UpdateControlsVisibility();
	UpdateState();

	Bind(wxEVT_CLOSE_WINDOW, &MinimapWindow::OnClose, this);
	Bind(wxEVT_TIMER, &MinimapWindow::OnDelayedUpdate, this);
	Bind(wxEVT_SIZE, &MinimapWindow::OnSize, this);
	Bind(wxEVT_SYS_COLOUR_CHANGED, &MinimapWindow::OnSystemColourChanged, this);
}

void MinimapWindow::BuildLayout() {
	auto* rootSizer = newd wxBoxSizer(wxVERTICAL);
	headerPanel_ = newd wxPanel(this, wxID_ANY, wxDefaultPosition, FROM_DIP(this, wxSize(-1, 40)), wxBORDER_NONE);
	auto* headerSizer = newd wxBoxSizer(wxHORIZONTAL);
	auto* titleBlock = newd wxBoxSizer(wxVERTICAL);
	auto* title = newd wxStaticText(headerPanel_, wxID_ANY, "MAP NAVIGATOR");
	title->SetFont(wxFontInfo(std::max(8, GetFont().GetPointSize() - 1)).Bold());
	coordinateLabel_ = newd wxStaticText(headerPanel_, wxID_ANY, "NO ACTIVE MAP", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	coordinateLabel_->SetFont(wxFontInfo(std::max(7, GetFont().GetPointSize() - 2)));
	titleBlock->Add(title, 0, wxBOTTOM, FROM_DIP(this, 2));
	titleBlock->Add(coordinateLabel_, 0, wxEXPAND);
	headerSizer->Add(titleBlock, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FROM_DIP(this, 10));
	centerButton_ = newd MinimapToolButton(headerPanel_, MinimapGlyph::Center, "Center on the editor view", wxSize(28, 28));
	headerSizer->Add(centerButton_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 4));
	optionsButton_ = newd MinimapToolButton(headerPanel_, MinimapGlyph::Options, "Minimap display options", wxSize(28, 28));
	headerSizer->Add(optionsButton_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 6));
	headerPanel_->SetSizer(headerSizer);
	rootSizer->Add(headerPanel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 6));

	canvas_ = newd MinimapCanvas(this);
	floorUpButton_ = newd MinimapToolButton(canvas_, MinimapGlyph::FloorUp, "Level up (Page Up)", wxSize(26, 26));
	floorDownButton_ = newd MinimapToolButton(canvas_, MinimapGlyph::FloorDown, "Level down (Page Down)", wxSize(26, 26));
	zoomInButton_ = newd MinimapToolButton(canvas_, MinimapGlyph::ZoomIn, "Zoom in (mouse wheel up)", wxSize(26, 26));
	zoomOutButton_ = newd MinimapToolButton(canvas_, MinimapGlyph::ZoomOut, "Zoom out (mouse wheel down)", wxSize(26, 26));
	rootSizer->Add(canvas_, 1, wxEXPAND | wxLEFT | wxRIGHT, FROM_DIP(this, 6));

	goPanel_ = newd wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	auto* goSizer = newd wxBoxSizer(wxHORIZONTAL);
	goToInput_ = newd wxTextCtrl(goPanel_, wxID_ANY, wxEmptyString, wxDefaultPosition, FROM_DIP(this, wxSize(-1, 30)), wxTE_PROCESS_ENTER);
	goToInput_->SetHint("Go to x:y:z, center, or cursor...");
	goButton_ = newd MinimapToolButton(goPanel_, MinimapGlyph::Go, "Navigate to coordinates", wxSize(34, 30));
	goSizer->Add(goToInput_, 1, wxEXPAND | wxRIGHT, FROM_DIP(this, 5));
	goSizer->Add(goButton_, 0, wxEXPAND);
	goPanel_->SetSizer(goSizer);
	rootSizer->Add(goPanel_, 0, wxEXPAND | wxALL, FROM_DIP(this, 6));
	SetSizer(rootSizer);
	canvas_->Bind(wxEVT_SIZE, &MinimapWindow::OnCanvasSize, this);

	optionsButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ShowOptionsMenu(); });
	floorUpButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		if (g_gui.GetCurrentMapTab()) {
			g_gui.ChangeFloor(g_gui.GetCurrentFloor() - 1);
			RefreshMap(true);
		}
	});
	floorDownButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		if (g_gui.GetCurrentMapTab()) {
			g_gui.ChangeFloor(g_gui.GetCurrentFloor() + 1);
			RefreshMap(true);
		}
	});
	zoomInButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { canvas_->SetZoomLevel(canvas_->GetZoomLevel() + 1); });
	zoomOutButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { canvas_->SetZoomLevel(canvas_->GetZoomLevel() - 1); });
	centerButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { canvas_->CenterOnEditor(); });
	goButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { NavigateToInput(); });
	goToInput_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { NavigateToInput(); });
	goToInput_->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
		if (inputHasError_) {
			inputHasError_ = false;
			ApplyTheme();
		}
		event.Skip();
	});
}

void MinimapWindow::ApplyTheme() {
	const MinimapColours colours = MinimapStyle::GetColours();
	SetBackgroundColour(colours.panel);
	headerPanel_->SetBackgroundColour(colours.header);
	goPanel_->SetBackgroundColour(colours.panel);
	for (wxWindow* child : headerPanel_->GetChildren()) {
		child->SetBackgroundColour(colours.header);
		child->SetForegroundColour(child == coordinateLabel_ ? colours.textSubtle : colours.text);
	}
	goToInput_->SetBackgroundColour(colours.canvas);
	goToInput_->SetForegroundColour(inputHasError_ ? wxColour(220, 74, 74) : colours.text);
	goToInput_->SetHint(inputHasError_ ? "Use x:y:z, center, or cursor" : "Go to x:y:z, center, or cursor...");
	Refresh(false);
	canvas_->Refresh(false);
}

void MinimapWindow::UpdateState() {
	int x = 0;
	int y = 0;
	int z = 0;
	const bool hasMap = canvas_->GetEditorCenter(x, y, z);
	coordinateLabel_->SetLabel(hasMap ? wxString::Format("X %d  |  Y %d  |  Z %d  |  %d%%", x, y, z, canvas_->GetZoomPercent()) : wxString("NO ACTIVE MAP"));
	floorUpButton_->Enable(hasMap && z > 0);
	floorDownButton_->Enable(hasMap && z < MAP_MAX_LAYER);
	zoomInButton_->Enable(hasMap && canvas_->GetZoomLevel() < 4);
	zoomOutButton_->Enable(hasMap && canvas_->GetZoomLevel() > 0);
	centerButton_->Enable(hasMap);
	goToInput_->Enable(hasMap);
	goButton_->Enable(hasMap);
	Layout();
}

void MinimapWindow::UpdateControlsVisibility() {
	for (MinimapToolButton* button : { floorUpButton_, floorDownButton_, zoomInButton_, zoomOutButton_, centerButton_ }) {
		button->Show(showControls_);
	}
	goPanel_->Show(showControls_);
	canvas_->SetControlsVisible(showControls_);
	optionsButton_->SetSelected(!showControls_);
	g_settings.setInteger(Config::MINIMAP_SHOW_CONTROLS, showControls_ ? 1 : 0);
	Layout();
	PositionOverlayControls();
	canvas_->InvalidateMap();
}

void MinimapWindow::PositionOverlayControls() {
	if (!canvas_ || !showControls_) {
		return;
	}
	const wxSize canvasSize = canvas_->GetClientSize();
	const int edge = FROM_DIP(canvas_, 7);
	const int gap = FROM_DIP(canvas_, 3);
	const wxSize floorSize = floorUpButton_->GetSize();
	const wxSize zoomSize = zoomInButton_->GetSize();
	if (canvasSize.x <= floorSize.x + zoomSize.x + edge * 2 || canvasSize.y <= floorSize.y * 2 + gap + edge * 2) {
		return;
	}
	floorUpButton_->Move(edge, canvasSize.y - edge - floorSize.y * 2 - gap);
	floorDownButton_->Move(edge, canvasSize.y - edge - floorSize.y);
	zoomInButton_->Move(canvasSize.x - edge - zoomSize.x * 2 - gap, canvasSize.y - edge - zoomSize.y);
	zoomOutButton_->Move(canvasSize.x - edge - zoomSize.x, canvasSize.y - edge - zoomSize.y);
	for (MinimapToolButton* button : { floorUpButton_, floorDownButton_, zoomInButton_, zoomOutButton_ }) {
		button->Raise();
	}
}

void MinimapWindow::DelayedUpdate() {
	updateTimer_.Start(std::max(1, g_settings.getInteger(Config::MINIMAP_UPDATE_DELAY)), true);
}

void MinimapWindow::RefreshMap(bool invalidateContent) {
	if (invalidateContent) {
		canvas_->InvalidateMap();
	} else {
		canvas_->Refresh(false);
	}
	UpdateState();
}

bool MinimapWindow::ParsePosition(const wxString& value, Position& position) const {
	if (!g_gui.GetCurrentMapTab() || !g_gui.GetCurrentEditor()) {
		return false;
	}
	wxString input = value;
	input.Trim(true).Trim(false);
	const wxString lowered = input.Lower();
	if (lowered == "center" || lowered == "centre") {
		const Map& map = g_gui.GetCurrentEditor()->map;
		position = Position(map.getWidth() / 2, map.getHeight() / 2, g_gui.GetCurrentFloor());
		return true;
	}
	if (lowered == "cursor") {
		position = g_gui.GetCurrentMapTab()->GetCanvas()->GetCursorPosition();
		return position.isValid();
	}

	std::vector<long> values;
	wxStringTokenizer tokenizer(input, ":,; \t", wxTOKEN_STRTOK);
	while (tokenizer.HasMoreTokens()) {
		long number = 0;
		if (!tokenizer.GetNextToken().ToLong(&number)) {
			return false;
		}
		values.push_back(number);
	}
	if (values.size() != 2 && values.size() != 3) {
		return false;
	}
	position = Position(static_cast<int>(values[0]), static_cast<int>(values[1]), values.size() == 3 ? static_cast<int>(values[2]) : g_gui.GetCurrentFloor());
	const Map& map = g_gui.GetCurrentEditor()->map;
	return position.isValid() && position.x <= map.getWidth() && position.y <= map.getHeight();
}

void MinimapWindow::NavigateToInput() {
	Position position;
	if (!ParsePosition(goToInput_->GetValue(), position)) {
		SetInputError("Enter coordinates inside the active map.");
		return;
	}
	inputHasError_ = false;
	ApplyTheme();
	g_gui.ChangeFloor(position.z);
	g_gui.SetScreenCenterPosition(position, true);
	g_gui.RefreshView();
	goToInput_->Clear();
	canvas_->InvalidateMap();
	UpdateState();
	canvas_->SetFocus();
}

void MinimapWindow::ShowOptionsMenu() {
	wxMenu menu;
	const int controlsId = wxWindow::NewControlId();
	const int compassId = wxWindow::NewControlId();
	const int viewBoxId = wxWindow::NewControlId();
	const int actionIdsId = wxWindow::NewControlId();
	const int uniqueIdsId = wxWindow::NewControlId();
	menu.AppendCheckItem(controlsId, "Show navigation controls")->Check(showControls_);
	menu.AppendCheckItem(compassId, "Show compass")->Check(canvas_->IsCompassVisible());
	menu.AppendCheckItem(viewBoxId, "Show current viewport")->Check(g_settings.getBoolean(Config::MINIMAP_VIEW_BOX));
	menu.AppendSeparator();
	menu.AppendCheckItem(actionIdsId, "Show Action IDs")->Check(canvas_->IsShowingActionIds());
	menu.AppendCheckItem(uniqueIdsId, "Show Unique IDs")->Check(canvas_->IsShowingUniqueIds());
	menu.Bind(wxEVT_MENU, [this, controlsId, compassId, viewBoxId, actionIdsId, uniqueIdsId](wxCommandEvent& event) {
		if (event.GetId() == controlsId) {
			showControls_ = !showControls_;
			UpdateControlsVisibility();
		} else if (event.GetId() == compassId) {
			canvas_->SetCompassVisible(!canvas_->IsCompassVisible());
			g_settings.setInteger(Config::MINIMAP_SHOW_COMPASS, canvas_->IsCompassVisible() ? 1 : 0);
		} else if (event.GetId() == viewBoxId) {
			g_settings.setInteger(Config::MINIMAP_VIEW_BOX, g_settings.getBoolean(Config::MINIMAP_VIEW_BOX) ? 0 : 1);
			canvas_->Refresh(false);
		} else if (event.GetId() == actionIdsId) {
			canvas_->SetShowActionIds(!canvas_->IsShowingActionIds());
		} else if (event.GetId() == uniqueIdsId) {
			canvas_->SetShowUniqueIds(!canvas_->IsShowingUniqueIds());
		}
	});
	PopupMenu(&menu, optionsButton_->GetPosition() + wxPoint(0, optionsButton_->GetSize().y));
}

void MinimapWindow::SetInputError(const wxString& message) {
	inputHasError_ = true;
	goToInput_->SetToolTip(message);
	ApplyTheme();
	goToInput_->SetFocus();
	goToInput_->SelectAll();
	g_gui.SetStatusText(message);
}

void MinimapWindow::OnClose(wxCloseEvent&) {
	g_gui.DestroyMinimap();
}

void MinimapWindow::OnDelayedUpdate(wxTimerEvent&) {
	RefreshMap(true);
}

void MinimapWindow::OnSize(wxSizeEvent& event) {
	Layout();
	PositionOverlayControls();
	canvas_->InvalidateMap();
	event.Skip();
}

void MinimapWindow::OnCanvasSize(wxSizeEvent& event) {
	PositionOverlayControls();
	event.Skip();
}

void MinimapWindow::OnSystemColourChanged(wxSysColourChangedEvent& event) {
	ApplyTheme();
	canvas_->InvalidateMap();
	event.Skip();
}
