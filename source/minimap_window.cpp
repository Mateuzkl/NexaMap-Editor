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
	optionsButton_ = newd MinimapToolButton(headerPanel_, MinimapGlyph::Options, "Minimap display options", wxSize(28, 28));
	headerSizer->Add(optionsButton_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 6));
	headerPanel_->SetSizer(headerSizer);
	rootSizer->Add(headerPanel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 6));

	auto* contentSizer = newd wxBoxSizer(wxHORIZONTAL);
	canvas_ = newd MinimapCanvas(this);
	contentSizer->Add(canvas_, 1, wxEXPAND);

	navigationPanel_ = newd wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	auto* navigationSizer = newd wxBoxSizer(wxVERTICAL);
	floorUpButton_ = newd MinimapToolButton(navigationPanel_, MinimapGlyph::FloorUp, "Level up (Page Up)");
	floorLabel_ = newd wxStaticText(navigationPanel_, wxID_ANY, "F 7", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
	floorLabel_->SetMinSize(FROM_DIP(this, wxSize(30, 18)));
	floorDownButton_ = newd MinimapToolButton(navigationPanel_, MinimapGlyph::FloorDown, "Level down (Page Down)");
	zoomInButton_ = newd MinimapToolButton(navigationPanel_, MinimapGlyph::ZoomIn, "Zoom in (mouse wheel up)");
	zoomLabel_ = newd wxStaticText(navigationPanel_, wxID_ANY, "100%", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
	zoomLabel_->SetMinSize(FROM_DIP(this, wxSize(34, 18)));
	zoomOutButton_ = newd MinimapToolButton(navigationPanel_, MinimapGlyph::ZoomOut, "Zoom out (mouse wheel down)");
	centerButton_ = newd MinimapToolButton(navigationPanel_, MinimapGlyph::Center, "Center on the editor view");

	navigationSizer->Add(floorUpButton_, 0, wxALIGN_CENTER | wxBOTTOM, FROM_DIP(this, 3));
	navigationSizer->Add(floorLabel_, 0, wxALIGN_CENTER | wxBOTTOM, FROM_DIP(this, 3));
	navigationSizer->Add(floorDownButton_, 0, wxALIGN_CENTER);
	navigationSizer->AddStretchSpacer();
	navigationSizer->Add(zoomInButton_, 0, wxALIGN_CENTER | wxBOTTOM, FROM_DIP(this, 3));
	navigationSizer->Add(zoomLabel_, 0, wxALIGN_CENTER | wxBOTTOM, FROM_DIP(this, 3));
	navigationSizer->Add(zoomOutButton_, 0, wxALIGN_CENTER | wxBOTTOM, FROM_DIP(this, 5));
	navigationSizer->Add(centerButton_, 0, wxALIGN_CENTER);
	navigationPanel_->SetSizer(navigationSizer);
	contentSizer->Add(navigationPanel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FROM_DIP(this, 6));
	rootSizer->Add(contentSizer, 1, wxEXPAND | wxLEFT | wxRIGHT, FROM_DIP(this, 6));

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
	navigationPanel_->SetBackgroundColour(colours.panel);
	goPanel_->SetBackgroundColour(colours.panel);
	for (wxWindow* child : headerPanel_->GetChildren()) {
		child->SetBackgroundColour(colours.header);
		child->SetForegroundColour(child == coordinateLabel_ ? colours.textSubtle : colours.text);
	}
	floorLabel_->SetBackgroundColour(colours.panel);
	floorLabel_->SetForegroundColour(colours.textSubtle);
	zoomLabel_->SetBackgroundColour(colours.panel);
	zoomLabel_->SetForegroundColour(colours.textSubtle);
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
	coordinateLabel_->SetLabel(hasMap ? wxString::Format("X %d  |  Y %d  |  Z %d", x, y, z) : wxString("NO ACTIVE MAP"));
	floorLabel_->SetLabel(hasMap ? wxString::Format("F %d", z) : wxString("F -"));
	zoomLabel_->SetLabel(wxString::Format("%d%%", canvas_->GetZoomPercent()));
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
	navigationPanel_->Show(showControls_);
	goPanel_->Show(showControls_);
	optionsButton_->SetSelected(!showControls_);
	g_settings.setInteger(Config::MINIMAP_SHOW_CONTROLS, showControls_ ? 1 : 0);
	Layout();
	canvas_->InvalidateMap();
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
	menu.AppendCheckItem(controlsId, "Show navigation controls")->Check(showControls_);
	menu.AppendCheckItem(compassId, "Show compass")->Check(canvas_->IsCompassVisible());
	menu.AppendCheckItem(viewBoxId, "Show current viewport")->Check(g_settings.getBoolean(Config::MINIMAP_VIEW_BOX));
	menu.Bind(wxEVT_MENU, [this, controlsId, compassId, viewBoxId](wxCommandEvent& event) {
		if (event.GetId() == controlsId) {
			showControls_ = !showControls_;
			UpdateControlsVisibility();
		} else if (event.GetId() == compassId) {
			canvas_->SetCompassVisible(!canvas_->IsCompassVisible());
			g_settings.setInteger(Config::MINIMAP_SHOW_COMPASS, canvas_->IsCompassVisible() ? 1 : 0);
		} else if (event.GetId() == viewBoxId) {
			g_settings.setInteger(Config::MINIMAP_VIEW_BOX, g_settings.getBoolean(Config::MINIMAP_VIEW_BOX) ? 0 : 1);
			canvas_->Refresh(false);
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
	canvas_->InvalidateMap();
	event.Skip();
}

void MinimapWindow::OnSystemColourChanged(wxSysColourChangedEvent& event) {
	ApplyTheme();
	canvas_->InvalidateMap();
	event.Skip();
}
