#include "main.h"

#include "minimap_canvas.h"

#include "editor.h"
#include "graphics.h"
#include "gui.h"
#include "map.h"
#include "map_display.h"
#include "map_tab.h"
#include "minimap_style.h"

#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/menu.h>

#include <algorithm>
#include <cmath>

namespace {
	constexpr std::array<double, 5> ZoomFactors = { 0.25, 0.5, 1.0, 2.0, 4.0 };
	constexpr std::array<int, 5> ZoomPercents = { 25, 50, 100, 200, 400 };
}

MinimapCanvas::MinimapCanvas(wxWindow* parent) :
	wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS) {
	SetMinSize(FROM_DIP(parent, wxSize(110, 100)));
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetToolTip("Click or drag to navigate. Use the mouse wheel to zoom.");
	for (size_t index = 0; index < pens_.size(); ++index) {
		const wxColour colour(minimap_color[index].red, minimap_color[index].green, minimap_color[index].blue);
		pens_[index] = wxPen(colour);
		brushes_[index] = wxBrush(colour);
	}

	Bind(wxEVT_PAINT, &MinimapCanvas::OnPaint, this);
	Bind(wxEVT_ERASE_BACKGROUND, &MinimapCanvas::OnEraseBackground, this);
	Bind(wxEVT_SIZE, &MinimapCanvas::OnSize, this);
	Bind(wxEVT_LEFT_DOWN, &MinimapCanvas::OnLeftDown, this);
	Bind(wxEVT_LEFT_UP, &MinimapCanvas::OnLeftUp, this);
	Bind(wxEVT_MOTION, &MinimapCanvas::OnMotion, this);
	Bind(wxEVT_MOUSE_CAPTURE_LOST, &MinimapCanvas::OnCaptureLost, this);
	Bind(wxEVT_MOUSEWHEEL, &MinimapCanvas::OnMouseWheel, this);
	Bind(wxEVT_KEY_DOWN, &MinimapCanvas::OnKeyDown, this);
	Bind(wxEVT_CONTEXT_MENU, &MinimapCanvas::OnContextMenu, this);
}

void MinimapCanvas::InvalidateMap() {
	mapDirty_ = true;
	Refresh(false);
}

void MinimapCanvas::SetZoomLevel(int level) {
	level = std::clamp(level, MinimumZoomLevel, MaximumZoomLevel);
	if (zoomLevel_ == level) {
		return;
	}
	zoomLevel_ = level;
	InvalidateMap();
	NotifyStateChanged();
}

int MinimapCanvas::GetZoomLevel() const {
	return zoomLevel_;
}

int MinimapCanvas::GetZoomPercent() const {
	return ZoomPercents[zoomLevel_];
}

void MinimapCanvas::SetCompassVisible(bool visible) {
	if (compassVisible_ == visible) {
		return;
	}
	compassVisible_ = visible;
	Refresh(false);
}

bool MinimapCanvas::IsCompassVisible() const {
	return compassVisible_;
}

void MinimapCanvas::CenterOnEditor() {
	InvalidateMap();
	NotifyStateChanged();
}

void MinimapCanvas::SetStateChangedCallback(std::function<void()> callback) {
	stateChanged_ = std::move(callback);
}

bool MinimapCanvas::GetEditorCenter(int& x, int& y, int& z) const {
	MapTab* tab = g_gui.GetCurrentMapTab();
	if (!tab || !tab->GetCanvas()) {
		return false;
	}
	tab->GetCanvas()->GetScreenCenter(&x, &y);
	z = tab->GetCanvas()->GetFloor();
	return true;
}

void MinimapCanvas::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	const MinimapColours colours = MinimapStyle::GetColours();
	dc.SetBackground(wxBrush(colours.panel));
	dc.Clear();

	const wxRect mapRect = GetMapRect();
	dc.SetBrush(wxBrush(colours.canvas));
	dc.SetPen(wxPen(colours.border, FROM_DIP(this, 1)));
	dc.DrawRoundedRectangle(mapRect, FROM_DIP(this, 6));

	Editor* editor = g_gui.GetCurrentEditor();
	MapTab* tab = g_gui.GetCurrentMapTab();
	MapCanvas* mapCanvas = tab ? tab->GetCanvas() : nullptr;
	if (!editor || !mapCanvas || !g_gui.IsRenderingEnabled() || mapRect.width <= 2 || mapRect.height <= 2) {
		DrawEmptyState(dc);
		return;
	}

	int centerX = 0;
	int centerY = 0;
	mapCanvas->GetScreenCenter(&centerX, &centerY);
	const int floor = mapCanvas->GetFloor();
	EnsureMapBitmap(*editor, *mapCanvas, mapRect, centerX, centerY, floor);
	if (mapBitmap_.IsOk()) {
		dc.DrawBitmap(mapBitmap_, mapRect.GetTopLeft(), false);
	}

	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	dc.SetPen(wxPen(colours.border, FROM_DIP(this, 1)));
	dc.DrawRoundedRectangle(mapRect, FROM_DIP(this, 6));
	DrawViewport(dc, *mapCanvas, mapRect, centerX, centerY, floor);
	if (compassVisible_ && mapRect.width >= FROM_DIP(this, 110) && mapRect.height >= FROM_DIP(this, 100)) {
		DrawCompass(dc, mapRect);
	}
}

void MinimapCanvas::OnEraseBackground(wxEraseEvent&) {
}

void MinimapCanvas::OnSize(wxSizeEvent& event) {
	mapDirty_ = true;
	event.Skip();
}

void MinimapCanvas::OnLeftDown(wxMouseEvent& event) {
	if (!g_gui.IsEditorOpen() || !GetMapRect().Contains(event.GetPosition())) {
		return;
	}
	dragging_ = true;
	SetFocus();
	if (!HasCapture()) {
		CaptureMouse();
	}
	NavigateAt(event.GetPosition());
}

void MinimapCanvas::OnLeftUp(wxMouseEvent& event) {
	if (!dragging_) {
		return;
	}
	dragging_ = false;
	if (HasCapture()) {
		ReleaseMouse();
	}
	if (GetMapRect().Contains(event.GetPosition())) {
		NavigateAt(event.GetPosition());
	}
}

void MinimapCanvas::OnMotion(wxMouseEvent& event) {
	if (dragging_ && event.Dragging() && event.LeftIsDown()) {
		NavigateAt(event.GetPosition());
	}
}

void MinimapCanvas::OnCaptureLost(wxMouseCaptureLostEvent&) {
	dragging_ = false;
}

void MinimapCanvas::OnMouseWheel(wxMouseEvent& event) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}
	SetZoomLevel(zoomLevel_ + (event.GetWheelRotation() > 0 ? 1 : -1));
}

void MinimapCanvas::OnKeyDown(wxKeyEvent& event) {
	if (g_gui.GetCurrentMapTab()) {
		g_gui.GetCurrentMapTab()->GetEventHandler()->AddPendingEvent(event);
	}
}

void MinimapCanvas::OnContextMenu(wxContextMenuEvent&) {
	wxMenu menu;
	const int compassId = wxWindow::NewControlId();
	const int centerId = wxWindow::NewControlId();
	menu.AppendCheckItem(compassId, "Show compass")->Check(compassVisible_);
	menu.Append(centerId, "Center on editor view");
	menu.Bind(wxEVT_MENU, [this, compassId, centerId](wxCommandEvent& event) {
		if (event.GetId() == compassId) {
			SetCompassVisible(!compassVisible_);
			NotifyStateChanged();
		} else if (event.GetId() == centerId) {
			CenterOnEditor();
		}
	});
	PopupMenu(&menu);
}

wxRect MinimapCanvas::GetMapRect() const {
	wxRect rect = GetClientRect();
	rect.Deflate(FROM_DIP(this, 6));
	if (rect.width < 0) {
		rect.width = 0;
	}
	if (rect.height < 0) {
		rect.height = 0;
	}
	return rect;
}

double MinimapCanvas::GetPixelsPerTile() const {
	return ZoomFactors[zoomLevel_];
}

void MinimapCanvas::EnsureMapBitmap(Editor& editor, MapCanvas&, const wxRect& mapRect, int centerX, int centerY, int floor) {
	const uintptr_t editorIdentity = reinterpret_cast<uintptr_t>(&editor);
	if (!mapDirty_ && mapBitmap_.IsOk() && cachedEditorIdentity_ == editorIdentity && cachedFloor_ == floor
		&& cachedCenterX_ == centerX && cachedCenterY_ == centerY && cachedWidth_ == mapRect.width && cachedHeight_ == mapRect.height) {
		return;
	}

	cachedEditorIdentity_ = editorIdentity;
	cachedFloor_ = floor;
	cachedCenterX_ = centerX;
	cachedCenterY_ = centerY;
	cachedWidth_ = mapRect.width;
	cachedHeight_ = mapRect.height;
	const double pixelsPerTile = GetPixelsPerTile();
	const int visibleTilesX = std::max(1, static_cast<int>(std::ceil(mapRect.width / pixelsPerTile)));
	const int visibleTilesY = std::max(1, static_cast<int>(std::ceil(mapRect.height / pixelsPerTile)));
	const int mapWidth = std::max(1, editor.map.getWidth());
	const int mapHeight = std::max(1, editor.map.getHeight());
	renderedStartX_ = mapWidth < visibleTilesX ? -(visibleTilesX - mapWidth) / 2 : std::clamp(centerX - visibleTilesX / 2, 0, mapWidth - visibleTilesX);
	renderedStartY_ = mapHeight < visibleTilesY ? -(visibleTilesY - mapHeight) / 2 : std::clamp(centerY - visibleTilesY / 2, 0, mapHeight - visibleTilesY);
	RenderMapBitmap(editor, mapRect.GetSize(), pixelsPerTile);
	mapDirty_ = false;
}

void MinimapCanvas::RenderMapBitmap(Editor& editor, const wxSize& size, double pixelsPerTile) {
	if (size.x <= 0 || size.y <= 0) {
		mapBitmap_ = wxNullBitmap;
		return;
	}

	mapBitmap_ = wxBitmap(size.x, size.y);
	wxMemoryDC dc(mapBitmap_);
	const MinimapColours colours = MinimapStyle::GetColours();
	dc.SetBackground(wxBrush(colours.canvas));
	dc.Clear();
	const int floor = cachedFloor_;
	const int mapWidth = editor.map.getWidth();
	const int mapHeight = editor.map.getHeight();
	uint8_t lastColour = 0;

	if (pixelsPerTile < 1.0) {
		for (int screenY = 0; screenY < size.y; ++screenY) {
			const int mapY = renderedStartY_ + static_cast<int>((screenY + 0.5) / pixelsPerTile);
			if (mapY < 0 || mapY > mapHeight) {
				continue;
			}
			for (int screenX = 0; screenX < size.x; ++screenX) {
				const int mapX = renderedStartX_ + static_cast<int>((screenX + 0.5) / pixelsPerTile);
				if (mapX < 0 || mapX > mapWidth) {
					continue;
				}
				Tile* tile = editor.map.getTile(mapX, mapY, floor);
				const uint8_t colour = tile ? tile->getMiniMapColor() : 0;
				if (colour == 0) {
					continue;
				}
				if (lastColour != colour) {
					dc.SetPen(pens_[colour]);
					lastColour = colour;
				}
				dc.DrawPoint(screenX, screenY);
			}
		}
	} else {
		const int firstX = std::max(0, renderedStartX_);
		const int firstY = std::max(0, renderedStartY_);
		const int lastX = std::min(mapWidth, renderedStartX_ + static_cast<int>(std::ceil(size.x / pixelsPerTile)));
		const int lastY = std::min(mapHeight, renderedStartY_ + static_cast<int>(std::ceil(size.y / pixelsPerTile)));
		const int tilePixels = std::max(1, static_cast<int>(pixelsPerTile));
		dc.SetPen(*wxTRANSPARENT_PEN);
		for (int mapY = firstY; mapY <= lastY; ++mapY) {
			const int screenY = static_cast<int>(std::floor((mapY - renderedStartY_) * pixelsPerTile));
			for (int mapX = firstX; mapX <= lastX; ++mapX) {
				Tile* tile = editor.map.getTile(mapX, mapY, floor);
				const uint8_t colour = tile ? tile->getMiniMapColor() : 0;
				if (colour == 0) {
					continue;
				}
				dc.SetBrush(brushes_[colour]);
				const int screenX = static_cast<int>(std::floor((mapX - renderedStartX_) * pixelsPerTile));
				dc.DrawRectangle(screenX, screenY, tilePixels, tilePixels);
			}
		}
	}
	dc.SelectObject(wxNullBitmap);
}

void MinimapCanvas::DrawViewport(wxDC& dc, MapCanvas& mapCanvas, const wxRect& mapRect, int centerX, int centerY, int floor) const {
	const MinimapColours colours = MinimapStyle::GetColours();
	const double pixelsPerTile = GetPixelsPerTile();
	dc.SetClippingRegion(mapRect);
	if (g_settings.getBoolean(Config::MINIMAP_VIEW_BOX)) {
		int screenSizeX = 0;
		int screenSizeY = 0;
		int viewScrollX = 0;
		int viewScrollY = 0;
		mapCanvas.GetViewBox(&viewScrollX, &viewScrollY, &screenSizeX, &screenSizeY);
		const int tileSize = std::max(1, static_cast<int>(TileSize / mapCanvas.GetZoom()));
		const int floorOffset = floor > GROUND_LAYER ? 0 : GROUND_LAYER - floor;
		const int viewStartX = viewScrollX / TileSize + floorOffset;
		const int viewStartY = viewScrollY / TileSize + floorOffset;
		const int viewEndX = viewStartX + screenSizeX / tileSize + 1;
		const int viewEndY = viewStartY + screenSizeY / tileSize + 1;
		const int left = mapRect.x + static_cast<int>(std::round((viewStartX - renderedStartX_) * pixelsPerTile));
		const int top = mapRect.y + static_cast<int>(std::round((viewStartY - renderedStartY_) * pixelsPerTile));
		const int right = mapRect.x + static_cast<int>(std::round((viewEndX - renderedStartX_) * pixelsPerTile));
		const int bottom = mapRect.y + static_cast<int>(std::round((viewEndY - renderedStartY_) * pixelsPerTile));
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.SetPen(wxPen(colours.canvas, FROM_DIP(this, 3)));
		dc.DrawRectangle(left, top, std::max(1, right - left), std::max(1, bottom - top));
		dc.SetPen(wxPen(colours.viewport, FROM_DIP(this, 1)));
		dc.DrawRectangle(left, top, std::max(1, right - left), std::max(1, bottom - top));
	}

	const int markerX = mapRect.x + static_cast<int>(std::round((centerX - renderedStartX_) * pixelsPerTile));
	const int markerY = mapRect.y + static_cast<int>(std::round((centerY - renderedStartY_) * pixelsPerTile));
	dc.SetPen(wxPen(colours.accent, FROM_DIP(this, 1)));
	dc.DrawLine(markerX - FROM_DIP(this, 4), markerY, markerX + FROM_DIP(this, 4), markerY);
	dc.DrawLine(markerX, markerY - FROM_DIP(this, 4), markerX, markerY + FROM_DIP(this, 4));
	dc.SetBrush(wxBrush(colours.accent));
	dc.DrawCircle(markerX, markerY, FROM_DIP(this, 2));
	dc.DestroyClippingRegion();
}

void MinimapCanvas::DrawCompass(wxDC& dc, const wxRect& mapRect) const {
	const MinimapColours colours = MinimapStyle::GetColours();
	const int radius = FROM_DIP(this, 16);
	const int centerX = mapRect.GetRight() - radius - FROM_DIP(this, 8);
	const int centerY = mapRect.y + radius + FROM_DIP(this, 8);
	dc.SetBrush(wxBrush(colours.panel));
	dc.SetPen(wxPen(colours.border, FROM_DIP(this, 1)));
	dc.DrawCircle(centerX, centerY, radius);

	wxPoint north[] = {
		wxPoint(centerX, centerY - FROM_DIP(this, 10)),
		wxPoint(centerX - FROM_DIP(this, 4), centerY + FROM_DIP(this, 3)),
		wxPoint(centerX, centerY + FROM_DIP(this, 1)),
		wxPoint(centerX + FROM_DIP(this, 4), centerY + FROM_DIP(this, 3)),
	};
	dc.SetBrush(wxBrush(colours.accent));
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.DrawPolygon(4, north);
	dc.SetTextForeground(colours.text);
	dc.SetFont(wxFontInfo(std::max(7, GetFont().GetPointSize() - 2)).Bold());
	const wxSize labelSize = dc.GetTextExtent("N");
	dc.DrawText("N", centerX - labelSize.x / 2, centerY - radius - labelSize.y + FROM_DIP(this, 1));
}

void MinimapCanvas::DrawEmptyState(wxDC& dc) const {
	const wxRect mapRect = GetMapRect();
	if (mapRect.width < FROM_DIP(this, 50) || mapRect.height < FROM_DIP(this, 50)) {
		return;
	}
	const MinimapColours colours = MinimapStyle::GetColours();
	const int centerX = mapRect.x + mapRect.width / 2;
	const int centerY = mapRect.y + mapRect.height / 2 - FROM_DIP(this, 10);
	const int iconSize = FROM_DIP(this, 17);
	wxPoint diamond[] = {
		wxPoint(centerX, centerY - iconSize),
		wxPoint(centerX + iconSize, centerY),
		wxPoint(centerX, centerY + iconSize),
		wxPoint(centerX - iconSize, centerY),
	};
	dc.SetBrush(wxBrush(colours.accentSoft));
	dc.SetPen(wxPen(colours.border, FROM_DIP(this, 1)));
	dc.DrawPolygon(4, diamond);
	dc.SetPen(wxPen(colours.accent, FROM_DIP(this, 2)));
	dc.DrawLine(centerX, centerY - FROM_DIP(this, 9), centerX, centerY + FROM_DIP(this, 9));
	dc.DrawLine(centerX - FROM_DIP(this, 9), centerY, centerX + FROM_DIP(this, 9), centerY);

	dc.SetFont(wxFontInfo(std::max(9, GetFont().GetPointSize())).Bold());
	dc.SetTextForeground(colours.text);
	const wxString title = "No map open";
	const wxSize titleSize = dc.GetTextExtent(title);
	dc.DrawText(title, centerX - titleSize.x / 2, centerY + iconSize + FROM_DIP(this, 10));
	if (mapRect.height >= FROM_DIP(this, 125)) {
		dc.SetFont(GetFont());
		dc.SetTextForeground(colours.textSubtle);
		const wxString message = "Open or create a map to navigate.";
		const wxString fitted = wxControl::Ellipsize(message, dc, wxELLIPSIZE_END, std::max(1, mapRect.width - FROM_DIP(this, 20)));
		const wxSize messageSize = dc.GetTextExtent(fitted);
		dc.DrawText(fitted, centerX - messageSize.x / 2, centerY + iconSize + FROM_DIP(this, 30));
	}
}

void MinimapCanvas::NavigateAt(const wxPoint& point) {
	Editor* editor = g_gui.GetCurrentEditor();
	MapTab* tab = g_gui.GetCurrentMapTab();
	if (!editor || !tab || !tab->GetCanvas()) {
		return;
	}
	const wxRect mapRect = GetMapRect();
	if (mapRect.width <= 0 || mapRect.height <= 0) {
		return;
	}
	const double pixelsPerTile = GetPixelsPerTile();
	const int mapX = std::clamp(renderedStartX_ + static_cast<int>((point.x - mapRect.x) / pixelsPerTile), 0, editor->map.getWidth());
	const int mapY = std::clamp(renderedStartY_ + static_cast<int>((point.y - mapRect.y) / pixelsPerTile), 0, editor->map.getHeight());
	g_gui.SetScreenCenterPosition(Position(mapX, mapY, tab->GetCanvas()->GetFloor()), true);
	mapDirty_ = true;
	g_gui.RefreshView();
	Refresh(false);
	NotifyStateChanged();
}

void MinimapCanvas::NotifyStateChanged() {
	if (stateChanged_) {
		stateChanged_();
	}
}
