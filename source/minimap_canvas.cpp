#include "main.h"

#include "minimap_canvas.h"

#include "editor.h"
#include "graphics.h"
#include "gui.h"
#include "map.h"
#include "map_region.h"
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
	constexpr uint64_t SpecialChunkRetentionPasses = 4;
}

MinimapCanvas::MinimapCanvas(wxWindow* parent) :
	wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS) {
	SetMinSize(FROM_DIP(parent, wxSize(110, 100)));
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	navigationTooltip_ = "Click or drag to navigate. Use the mouse wheel to zoom.";
	displayedTooltip_ = navigationTooltip_;
	SetToolTip(navigationTooltip_);
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
	Bind(wxEVT_LEAVE_WINDOW, &MinimapCanvas::OnMouseLeave, this);
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

void MinimapCanvas::SetControlsVisible(bool visible) {
	if (controlsVisible_ == visible) {
		return;
	}
	controlsVisible_ = visible;
	InvalidateMap();
}

void MinimapCanvas::SetShowActionIds(bool visible) {
	if (showActionIds_ == visible) {
		return;
	}
	showActionIds_ = visible;
	visibleSpecialMarkers_.clear();
	ResetSpecialMarkerTooltip();
	Refresh(false);
}

bool MinimapCanvas::IsShowingActionIds() const {
	return showActionIds_;
}

void MinimapCanvas::SetShowUniqueIds(bool visible) {
	if (showUniqueIds_ == visible) {
		return;
	}
	showUniqueIds_ = visible;
	visibleSpecialMarkers_.clear();
	ResetSpecialMarkerTooltip();
	Refresh(false);
}

bool MinimapCanvas::IsShowingUniqueIds() const {
	return showUniqueIds_;
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
		visibleSpecialMarkers_.clear();
		ResetSpecialMarkerTooltip();
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
	UpdateSpecialMarkers(*editor, mapRect, floor);
	DrawSpecialMarkers(dc, mapRect);
	if (compassVisible_ && GetClientSize().x >= FROM_DIP(this, 110) && GetClientSize().y >= FROM_DIP(this, 100)) {
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
	const int specialMarker = FindSpecialMarker(event.GetPosition());
	dragging_ = true;
	SetFocus();
	if (!HasCapture()) {
		CaptureMouse();
	}
	if (specialMarker >= 0) {
		const VisibleSpecialMarker& marker = visibleSpecialMarkers_[specialMarker];
		NavigateTo(marker.x, marker.y, marker.floor);
	} else {
		NavigateAt(event.GetPosition());
	}
}

void MinimapCanvas::OnLeftUp(wxMouseEvent&) {
	if (!dragging_) {
		return;
	}
	dragging_ = false;
	if (HasCapture()) {
		ReleaseMouse();
	}
}

void MinimapCanvas::OnMotion(wxMouseEvent& event) {
	if (dragging_ && event.Dragging() && event.LeftIsDown()) {
		NavigateAt(event.GetPosition());
		return;
	}
	UpdateSpecialMarkerTooltip(event.GetPosition());
}

void MinimapCanvas::OnMouseLeave(wxMouseEvent& event) {
	ResetSpecialMarkerTooltip();
	event.Skip();
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
	const int sideMargin = FROM_DIP(this, controlsVisible_ ? 36 : (compassVisible_ ? 27 : 6));
	const int topMargin = FROM_DIP(this, compassVisible_ ? 25 : 6);
	const int bottomMargin = FROM_DIP(this, controlsVisible_ ? 36 : (compassVisible_ ? 25 : 6));
	rect.x += sideMargin;
	rect.y += topMargin;
	rect.width -= sideMargin * 2;
	rect.height -= topMargin + bottomMargin;
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
	const uint64_t mapSessionId = editor.map.getSessionId();
	if (!mapDirty_ && mapBitmap_.IsOk() && cachedMapSessionId_ == mapSessionId && cachedFloor_ == floor
		&& cachedCenterX_ == centerX && cachedCenterY_ == centerY && cachedWidth_ == mapRect.width && cachedHeight_ == mapRect.height) {
		return;
	}

	cachedMapSessionId_ = mapSessionId;
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

void MinimapCanvas::UpdateSpecialMarkers(Editor& editor, const wxRect& mapRect, int floor) {
	visibleSpecialMarkers_.clear();
	if (!showActionIds_ && !showUniqueIds_) {
		return;
	}

	const uint64_t mapSessionId = editor.map.getSessionId();
	if (specialMapSessionId_ != mapSessionId) {
		specialChunks_.clear();
		specialMapSessionId_ = mapSessionId;
		specialMarkerPass_ = 0;
		ResetSpecialMarkerTooltip();
	}
	++specialMarkerPass_;

	const double pixelsPerTile = GetPixelsPerTile();
	const int firstX = std::max(0, renderedStartX_);
	const int firstY = std::max(0, renderedStartY_);
	const int lastX = std::min(editor.map.getWidth(), renderedStartX_ + static_cast<int>(std::ceil(mapRect.width / pixelsPerTile)));
	const int lastY = std::min(editor.map.getHeight(), renderedStartY_ + static_cast<int>(std::ceil(mapRect.height / pixelsPerTile)));
	if (firstX > lastX || firstY > lastY || floor < 0 || floor >= MAP_LAYERS) {
		return;
	}

	const int firstChunkX = firstX & ~3;
	const int firstChunkY = firstY & ~3;
	for (int chunkX = firstChunkX; chunkX <= lastX; chunkX += 4) {
		for (int chunkY = firstChunkY; chunkY <= lastY; chunkY += 4) {
			QTreeNode* leaf = editor.map.getLeaf(chunkX, chunkY);
			Floor* floorData = leaf ? leaf->getFloor(static_cast<uint32_t>(floor)) : nullptr;
			const bool hasFloor = floorData != nullptr;
			const MapChunkRevision revision = hasFloor ? floorData->getRenderRevision() : MapChunkRevision {};
			const uint32_t key = MakeMapChunkKey(static_cast<uint16_t>(chunkX), static_cast<uint16_t>(chunkY), static_cast<uint8_t>(floor));
			auto found = specialChunks_.find(key);
			if (found == specialChunks_.end() || found->second.hasFloor != hasFloor || found->second.revision.content != revision.content) {
				ScanSpecialChunk(floorData, chunkX, chunkY, floor, revision);
				found = specialChunks_.find(key);
			}
			CachedSpecialChunk& chunk = found->second;
			chunk.lastVisiblePass = specialMarkerPass_;
			for (const CachedSpecialMarker& cachedMarker : chunk.markers) {
				VisibleSpecialMarker marker;
				marker.x = cachedMarker.x;
				marker.y = cachedMarker.y;
				marker.floor = cachedMarker.floor;
				for (const SpecialItemData& item : cachedMarker.items) {
					marker.hasVisibleActionId |= showActionIds_ && item.actionId != 0;
					marker.hasVisibleUniqueId |= showUniqueIds_ && item.uniqueId != 0;
					if ((showActionIds_ && item.actionId != 0) || (showUniqueIds_ && item.uniqueId != 0)) {
						marker.items.push_back(item);
					}
				}
				if (!marker.items.empty()) {
					visibleSpecialMarkers_.push_back(std::move(marker));
				}
			}
		}
	}

	for (auto it = specialChunks_.begin(); it != specialChunks_.end();) {
		if (specialMarkerPass_ - it->second.lastVisiblePass > SpecialChunkRetentionPasses) {
			it = specialChunks_.erase(it);
		} else {
			++it;
		}
	}
}

void MinimapCanvas::ScanSpecialChunk(Floor* floorData, int chunkX, int chunkY, int floor, const MapChunkRevision& revision) {
	const uint32_t key = MakeMapChunkKey(static_cast<uint16_t>(chunkX), static_cast<uint16_t>(chunkY), static_cast<uint8_t>(floor));
	CachedSpecialChunk& chunk = specialChunks_[key];
	chunk.revision = revision;
	chunk.hasFloor = floorData != nullptr;
	chunk.markers.clear();
	if (!floorData) {
		return;
	}

	for (const TileLocation& location : floorData->locs) {
		const Tile* tile = location.get();
		if (!tile) {
			continue;
		}
		CachedSpecialMarker marker;
		marker.x = location.getX();
		marker.y = location.getY();
		marker.floor = location.getZ();
		const auto inspectItem = [&marker](const Item* item) {
			if (!item) {
				return;
			}
			const uint16_t actionId = item->getActionID();
			const uint16_t uniqueId = item->getUniqueID();
			if (actionId == 0 && uniqueId == 0) {
				return;
			}
			marker.items.push_back({ item->getID(), actionId, uniqueId });
		};
		inspectItem(tile->ground);
		for (const Item* item : tile->items) {
			inspectItem(item);
		}
		if (!marker.items.empty()) {
			chunk.markers.push_back(std::move(marker));
		}
	}
}

void MinimapCanvas::DrawSpecialMarkers(wxDC& dc, const wxRect& mapRect) {
	if (visibleSpecialMarkers_.empty()) {
		return;
	}
	const MinimapColours colours = MinimapStyle::GetColours();
	const double pixelsPerTile = GetPixelsPerTile();
	dc.SetClippingRegion(mapRect);
	dc.SetFont(wxFontInfo(std::max(7, GetFont().GetPointSize() - 2)).Bold());
	for (VisibleSpecialMarker& marker : visibleSpecialMarkers_) {
		const wxString label = marker.hasVisibleActionId && marker.hasVisibleUniqueId ? "A/U" : (marker.hasVisibleActionId ? "A" : "U");
		const wxSize textSize = dc.GetTextExtent(label);
		const int width = std::max(FROM_DIP(this, label == "A/U" ? 25 : 18), textSize.x + FROM_DIP(this, 8));
		const int height = std::max(FROM_DIP(this, 16), textSize.y + FROM_DIP(this, 4));
		const int tileCenterX = mapRect.x + static_cast<int>(std::round((marker.x + 0.5 - renderedStartX_) * pixelsPerTile));
		const int tileCenterY = mapRect.y + static_cast<int>(std::round((marker.y + 0.5 - renderedStartY_) * pixelsPerTile));
		marker.hitBounds = wxRect(tileCenterX - width / 2, tileCenterY - height / 2, width, height);
		const wxColour markerColour = marker.hasVisibleActionId && !marker.hasVisibleUniqueId ? colours.viewport : colours.accent;
		dc.SetBrush(wxBrush(colours.panel));
		dc.SetPen(wxPen(markerColour, FROM_DIP(this, 1)));
		dc.DrawRoundedRectangle(marker.hitBounds, FROM_DIP(this, 4));
		dc.SetTextForeground(markerColour);
		dc.DrawText(label, marker.hitBounds.x + (marker.hitBounds.width - textSize.x) / 2, marker.hitBounds.y + (marker.hitBounds.height - textSize.y) / 2);
	}
	dc.DestroyClippingRegion();
}

void MinimapCanvas::DrawCompass(wxDC& dc, const wxRect& mapRect) const {
	const MinimapColours colours = MinimapStyle::GetColours();
	dc.SetFont(wxFontInfo(std::max(7, GetFont().GetPointSize() - 2)).Bold());
	const auto drawDirection = [&](const wxString& label, int centerX, int centerY, bool primary) {
		const wxSize textSize = dc.GetTextExtent(label);
		const int paddingX = FROM_DIP(this, 5);
		const int paddingY = FROM_DIP(this, 2);
		const wxRect badge(
			centerX - textSize.x / 2 - paddingX,
			centerY - textSize.y / 2 - paddingY,
			textSize.x + paddingX * 2,
			textSize.y + paddingY * 2
		);
		dc.SetBrush(wxBrush(primary ? colours.accentSoft : colours.raised));
		dc.SetPen(wxPen(primary ? colours.accent : colours.border, FROM_DIP(this, 1)));
		dc.DrawRoundedRectangle(badge, FROM_DIP(this, 4));
		dc.SetTextForeground(primary ? colours.accent : colours.textSubtle);
		dc.DrawText(label, centerX - textSize.x / 2, centerY - textSize.y / 2);
	};

	const wxRect client = GetClientRect();
	drawDirection("N", mapRect.x + mapRect.width / 2, std::max(FROM_DIP(this, 10), mapRect.y / 2), true);
	drawDirection("W", std::max(FROM_DIP(this, 10), mapRect.x / 2), mapRect.y + mapRect.height / 2, false);
	drawDirection("E", std::min(client.GetRight() - FROM_DIP(this, 10), mapRect.GetRight() + (client.GetRight() - mapRect.GetRight()) / 2), mapRect.y + mapRect.height / 2, false);
	drawDirection("S", mapRect.x + mapRect.width / 2, std::min(client.GetBottom() - FROM_DIP(this, 10), mapRect.GetBottom() + (client.GetBottom() - mapRect.GetBottom()) / 2), false);
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

int MinimapCanvas::FindSpecialMarker(const wxPoint& point) const {
	for (size_t index = visibleSpecialMarkers_.size(); index > 0; --index) {
		if (visibleSpecialMarkers_[index - 1].hitBounds.Contains(point)) {
			return static_cast<int>(index - 1);
		}
	}
	return -1;
}

wxString MinimapCanvas::BuildSpecialMarkerTooltip(size_t markerIndex) const {
	if (markerIndex >= visibleSpecialMarkers_.size()) {
		return navigationTooltip_;
	}
	const VisibleSpecialMarker& marker = visibleSpecialMarkers_[markerIndex];
	wxString tooltip = wxString::Format("Position: %d, %d, %d", marker.x, marker.y, marker.floor);
	for (size_t index = 0; index < marker.items.size(); ++index) {
		const SpecialItemData& item = marker.items[index];
		tooltip += index == 0 ? "\n" : "\n----------------\n";
		tooltip += wxString::Format("Item ID: %u\n", static_cast<unsigned int>(item.itemId));
		tooltip += item.actionId != 0 ? wxString::Format("ActionID: %u\n", static_cast<unsigned int>(item.actionId)) : wxString("ActionID: -\n");
		tooltip += item.uniqueId != 0 ? wxString::Format("UniqueID: %u", static_cast<unsigned int>(item.uniqueId)) : wxString("UniqueID: -");
	}
	return tooltip;
}

void MinimapCanvas::UpdateSpecialMarkerTooltip(const wxPoint& point) {
	const int markerIndex = FindSpecialMarker(point);
	const wxString tooltip = markerIndex >= 0 ? BuildSpecialMarkerTooltip(static_cast<size_t>(markerIndex)) : navigationTooltip_;
	if (tooltip == displayedTooltip_) {
		return;
	}
	displayedTooltip_ = tooltip;
	SetToolTip(displayedTooltip_);
}

void MinimapCanvas::ResetSpecialMarkerTooltip() {
	if (displayedTooltip_ == navigationTooltip_) {
		return;
	}
	displayedTooltip_ = navigationTooltip_;
	SetToolTip(navigationTooltip_);
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
	NavigateTo(mapX, mapY, tab->GetCanvas()->GetFloor());
}

void MinimapCanvas::NavigateTo(int x, int y, int floor) {
	g_gui.SetScreenCenterPosition(Position(x, y, floor), true);
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
