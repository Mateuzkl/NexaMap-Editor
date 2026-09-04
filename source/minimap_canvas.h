#ifndef NEXAMAP_MINIMAP_CANVAS_H_
#define NEXAMAP_MINIMAP_CANVAS_H_

#include <wx/bitmap.h>
#include <wx/panel.h>

#include <array>
#include <cstdint>
#include <functional>

class Editor;
class MapCanvas;

class MinimapCanvas final : public wxPanel {
public:
	explicit MinimapCanvas(wxWindow* parent);

	void InvalidateMap();
	void SetZoomLevel(int level);
	int GetZoomLevel() const;
	int GetZoomPercent() const;
	void SetCompassVisible(bool visible);
	bool IsCompassVisible() const;
	void CenterOnEditor();
	void SetStateChangedCallback(std::function<void()> callback);
	bool GetEditorCenter(int& x, int& y, int& z) const;

private:
	static constexpr int MinimumZoomLevel = 0;
	static constexpr int MaximumZoomLevel = 4;

	void OnPaint(wxPaintEvent& event);
	void OnEraseBackground(wxEraseEvent& event);
	void OnSize(wxSizeEvent& event);
	void OnLeftDown(wxMouseEvent& event);
	void OnLeftUp(wxMouseEvent& event);
	void OnMotion(wxMouseEvent& event);
	void OnCaptureLost(wxMouseCaptureLostEvent& event);
	void OnMouseWheel(wxMouseEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnContextMenu(wxContextMenuEvent& event);

	wxRect GetMapRect() const;
	double GetPixelsPerTile() const;
	void EnsureMapBitmap(Editor& editor, MapCanvas& mapCanvas, const wxRect& mapRect, int centerX, int centerY, int floor);
	void RenderMapBitmap(Editor& editor, const wxSize& size, double pixelsPerTile);
	void DrawViewport(wxDC& dc, MapCanvas& mapCanvas, const wxRect& mapRect, int centerX, int centerY, int floor) const;
	void DrawCompass(wxDC& dc, const wxRect& mapRect) const;
	void DrawEmptyState(wxDC& dc) const;
	void NavigateAt(const wxPoint& point);
	void NotifyStateChanged();

	std::array<wxPen, 256> pens_;
	std::array<wxBrush, 256> brushes_;
	wxBitmap mapBitmap_;
	std::function<void()> stateChanged_;
	uintptr_t cachedEditorIdentity_ = 0;
	int cachedFloor_ = -1;
	int cachedCenterX_ = 0;
	int cachedCenterY_ = 0;
	int cachedWidth_ = 0;
	int cachedHeight_ = 0;
	int renderedStartX_ = 0;
	int renderedStartY_ = 0;
	int zoomLevel_ = 2;
	bool compassVisible_ = true;
	bool mapDirty_ = true;
	bool dragging_ = false;
};

#endif
