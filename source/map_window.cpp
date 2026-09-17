//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "map_window.h"
#include "gui.h"
#include "sprites.h"
#include "editor.h"
#include "replace_tool/advanced_replace_window.h"

MapWindow::MapWindow(wxWindow* parent, Editor& editor, bool ingamePreview) :
	wxPanel(parent, PANE_MAIN),
	editor(editor),
	ingamePreview(ingamePreview),
	replaceItemsDialog(nullptr),
	advancedReplaceWindow(nullptr) {
	int GL_settings[3];
	GL_settings[0] = WX_GL_RGBA;
	GL_settings[1] = WX_GL_DOUBLEBUFFER;
	GL_settings[2] = 0;
	canvas = newd MapCanvas(this, editor, GL_settings, ingamePreview);
	canvas->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
		UpdateScrollbars();
		event.Skip();
	});

	vScroll = newd MapScrollBar(this, MAP_WINDOW_VSCROLL, wxVERTICAL, canvas);
	hScroll = newd MapScrollBar(this, MAP_WINDOW_HSCROLL, wxHORIZONTAL, canvas);

	gem = newd DCButton(this, MAP_WINDOW_GEM, wxDefaultPosition, DC_BTN_NORMAL, RENDER_SIZE_16x16, EDITOR_SPRITE_SELECTION_GEM);

	if (ingamePreview) {
		canvas->SetMinSize(FromDIP(wxSize(240, 176)));
		vScroll->Hide();
		hScroll->Hide();
		gem->Hide();

		auto* previewSizer = newd wxBoxSizer(wxVERTICAL);
		previewSizer->Add(canvas, 1, wxEXPAND);
		SetSizerAndFit(previewSizer);
		return;
	}

	auto* topsizer = newd wxFlexGridSizer(2, 0, 0);

	topsizer->AddGrowableCol(0);
	topsizer->AddGrowableRow(0);

	topsizer->Add(canvas, wxSizerFlags(1).Expand());
	topsizer->Add(vScroll, wxSizerFlags(1).Expand());
	topsizer->Add(hScroll, wxSizerFlags(1).Expand());
	topsizer->Add(gem, wxSizerFlags(1));

	SetSizerAndFit(topsizer);
}

MapWindow::~MapWindow() {
	////
}

void MapWindow::ShowReplaceItemsDialog(bool selectionOnly) {
	if (replaceItemsDialog) {
		return;
	}

	replaceItemsDialog = new ReplaceItemsDialog(this, selectionOnly);
	replaceItemsDialog->Connect(wxEVT_CLOSE_WINDOW, wxCloseEventHandler(MapWindow::OnReplaceItemsDialogClose), nullptr, this);
	replaceItemsDialog->Show();
}

void MapWindow::OnReplaceItemsDialogClose(wxCloseEvent& event) {
	if (replaceItemsDialog) {
		replaceItemsDialog->Disconnect(wxEVT_CLOSE_WINDOW, wxCloseEventHandler(MapWindow::OnReplaceItemsDialogClose), nullptr, this);
		replaceItemsDialog->Destroy();
		replaceItemsDialog = nullptr;
	}
}

void MapWindow::ShowAdvancedReplaceWindow() {
	if (advancedReplaceWindow) {
		advancedReplaceWindow->Raise();
		advancedReplaceWindow->SetFocus();
		return;
	}

	advancedReplaceWindow = new AdvancedReplaceWindow(this, editor, *canvas);
	advancedReplaceWindow->Bind(wxEVT_CLOSE_WINDOW, &MapWindow::OnAdvancedReplaceWindowClose, this);
	advancedReplaceWindow->Show();
}

void MapWindow::OnAdvancedReplaceWindowClose(wxCloseEvent&) {
	if (!advancedReplaceWindow) {
		return;
	}
	advancedReplaceWindow->Unbind(wxEVT_CLOSE_WINDOW, &MapWindow::OnAdvancedReplaceWindowClose, this);
	advancedReplaceWindow->Destroy();
	advancedReplaceWindow = nullptr;
}

ViewportMetrics MapWindow::GetViewportMetrics() const {
	if (!canvas) {
		return ViewportMetrics();
	}
	int canvasW = 0;
	int canvasH = 0;
	canvas->GetSize(&canvasW, &canvasH);
	return ViewportMetrics::Compute(
		editor.map.getWidth(),
		editor.map.getHeight(),
		TileSize,
		canvasW,
		canvasH,
		canvas->GetContentScaleFactor(),
		canvas->GetZoom()
	);
}

void MapWindow::SetSize(int x, int y, bool center) {
	if (x <= 0 || y <= 0 || !hScroll || !vScroll) {
		return;
	}

	Layout();
	const ViewportMetrics metrics = GetViewportMetrics();
	const int posX = center ? (metrics.maxScrollX / 2) : std::clamp(hScroll->GetThumbPosition(), 0, metrics.maxScrollX);
	const int posY = center ? (metrics.maxScrollY / 2) : std::clamp(vScroll->GetThumbPosition(), 0, metrics.maxScrollY);

	hScroll->SetScrollbar(posX, metrics.thumbX, metrics.mapWidthPixels, metrics.pageX, true);
	vScroll->SetScrollbar(posY, metrics.thumbY, metrics.mapHeightPixels, metrics.pageY, true);

	if (center && !ingamePreview) {
		g_gui.UpdateMinimap();
	}
}

void MapWindow::UpdateScrollbars() {
	if (!hScroll || !vScroll) {
		return;
	}

	const ViewportMetrics metrics = GetViewportMetrics();
	if (metrics.mapWidthPixels <= 0 || metrics.mapHeightPixels <= 0) {
		return;
	}

	const int posX = std::clamp(hScroll->GetThumbPosition(), 0, metrics.maxScrollX);
	const int posY = std::clamp(vScroll->GetThumbPosition(), 0, metrics.maxScrollY);

	hScroll->SetScrollbar(posX, metrics.thumbX, metrics.mapWidthPixels, metrics.pageX, true);
	vScroll->SetScrollbar(posY, metrics.thumbY, metrics.mapHeightPixels, metrics.pageY, true);
}

void MapWindow::UpdateScrollbars(int nx, int ny) {
	UpdateScrollbars();
}

void MapWindow::UpdateDialogs(bool show) {
	if (replaceItemsDialog) {
		replaceItemsDialog->Show(show);
	}
	if (advancedReplaceWindow) {
		advancedReplaceWindow->Show(show);
	}
}

void MapWindow::GetViewStart(int* x, int* y) {
	*x = hScroll->GetThumbPosition();
	*y = vScroll->GetThumbPosition();
}

void MapWindow::GetViewSize(int* x, int* y) {
	canvas->GetSize(x, y);
	*x *= canvas->GetContentScaleFactor();
	*y *= canvas->GetContentScaleFactor();
}

void MapWindow::FitToMap() {
	SetSize(editor.map.getWidth() * TileSize, editor.map.getHeight() * TileSize, true);
}

Position MapWindow::GetScreenCenterPosition() {
	int x, y;
	canvas->GetScreenCenter(&x, &y);
	return Position(x, y, canvas->GetFloor());
}

void MapWindow::SetScreenCenterPosition(const Position& position, bool showIndicator) {
	if (!position.isValid()) {
		return;
	}

	const Position target = position;
	int x = target.x * TileSize;
	int y = target.y * TileSize;
	int z = target.z;
	if (target.z <= GROUND_LAYER) {
		// Compensate for floor offset above ground
		x -= (GROUND_LAYER - z) * TileSize;
		y -= (GROUND_LAYER - z) * TileSize;
	}

	const Position& center = GetScreenCenterPosition();
	if (previous_position != center) {
		previous_position.x = center.x;
		previous_position.y = center.y;
		previous_position.z = center.z;
	}

	Scroll(x, y, true);
	canvas->ChangeFloor(z);

	if (showIndicator) {
		canvas->ShowPositionIndicator(target);
		Refresh();
	}
}

void MapWindow::SetScreenCenterPositionInterpolated(const Position& position, int pixelOffsetX, int pixelOffsetY) {
	if (!position.isValid()) {
		return;
	}

	int x = position.x * TileSize + pixelOffsetX;
	int y = position.y * TileSize + pixelOffsetY;
	if (position.z <= GROUND_LAYER) {
		x -= (GROUND_LAYER - position.z) * TileSize;
		y -= (GROUND_LAYER - position.z) * TileSize;
	}
	Scroll(x, y, true);
	if (canvas->GetFloor() != position.z) {
		canvas->ChangeFloor(position.z);
	}
}

void MapWindow::GoToPreviousCenterPosition() {
	SetScreenCenterPosition(previous_position, true);
}

void MapWindow::Scroll(int x, int y, bool center) {
	const ViewportMetrics metrics = GetViewportMetrics();
	if (center) {
		x -= metrics.viewportWidthPixels / 2;
		y -= metrics.viewportHeightPixels / 2;
	}

	x = std::clamp(x, 0, metrics.maxScrollX);
	y = std::clamp(y, 0, metrics.maxScrollY);

	hScroll->SetThumbPosition(x);
	vScroll->SetThumbPosition(y);
	if (!ingamePreview) {
		g_gui.UpdateMinimap();
	}
}

void MapWindow::ScrollRelative(int x, int y) {
	const ViewportMetrics metrics = GetViewportMetrics();
	const int newX = std::clamp(hScroll->GetThumbPosition() + x, 0, metrics.maxScrollX);
	const int newY = std::clamp(vScroll->GetThumbPosition() + y, 0, metrics.maxScrollY);

	hScroll->SetThumbPosition(newX);
	vScroll->SetThumbPosition(newY);
	if (!ingamePreview) {
		g_gui.UpdateMinimap();
	}
}

void MapWindow::OnGem(wxCommandEvent& WXUNUSED(event)) {
	g_gui.SwitchMode();
}

void MapWindow::OnSize(wxSizeEvent& event) {
	Layout();
	UpdateScrollbars();
	event.Skip();
}

void MapWindow::OnScroll(wxScrollEvent& event) {
	if (!ingamePreview) {
		g_gui.UpdateMinimap();
	}
	Refresh();
}

void MapWindow::OnScrollLineDown(wxScrollEvent& event) {
	if (event.GetOrientation() == wxHORIZONTAL) {
		ScrollRelative(96, 0);
	} else {
		ScrollRelative(0, 96);
	}
	Refresh();
}

void MapWindow::OnScrollLineUp(wxScrollEvent& event) {
	if (event.GetOrientation() == wxHORIZONTAL) {
		ScrollRelative(-96, 0);
	} else {
		ScrollRelative(0, -96);
	}
	Refresh();
}

void MapWindow::OnScrollPageDown(wxScrollEvent& event) {
	if (event.GetOrientation() == wxHORIZONTAL) {
		ScrollRelative(5 * 96, 0);
	} else {
		ScrollRelative(0, 5 * 96);
	}
	Refresh();
}

void MapWindow::OnScrollPageUp(wxScrollEvent& event) {
	if (event.GetOrientation() == wxHORIZONTAL) {
		ScrollRelative(-5 * 96, 0);
	} else {
		ScrollRelative(0, -5 * 96);
	}
	Refresh();
}
