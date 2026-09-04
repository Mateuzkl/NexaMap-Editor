#ifndef NEXAMAP_MINIMAP_CANVAS_H_
#define NEXAMAP_MINIMAP_CANVAS_H_

#include <wx/bitmap.h>
#include <wx/panel.h>

#include "map_chunk_revision.h"

#include <array>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

class Editor;
class Floor;
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
	void SetControlsVisible(bool visible);
	void SetShowActionIds(bool visible);
	bool IsShowingActionIds() const;
	void SetShowUniqueIds(bool visible);
	bool IsShowingUniqueIds() const;
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
	void OnMouseLeave(wxMouseEvent& event);
	void OnCaptureLost(wxMouseCaptureLostEvent& event);
	void OnMouseWheel(wxMouseEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnContextMenu(wxContextMenuEvent& event);

	wxRect GetMapRect() const;
	double GetPixelsPerTile() const;
	void EnsureMapBitmap(Editor& editor, MapCanvas& mapCanvas, const wxRect& mapRect, int centerX, int centerY, int floor);
	void RenderMapBitmap(Editor& editor, const wxSize& size, double pixelsPerTile);
	void DrawViewport(wxDC& dc, MapCanvas& mapCanvas, const wxRect& mapRect, int centerX, int centerY, int floor) const;
	void UpdateSpecialMarkers(Editor& editor, const wxRect& mapRect, int floor);
	void ScanSpecialChunk(Floor* floorData, int chunkX, int chunkY, int floor, const MapChunkRevision& revision);
	void DrawSpecialMarkers(wxDC& dc, const wxRect& mapRect);
	void DrawCompass(wxDC& dc, const wxRect& mapRect) const;
	void DrawEmptyState(wxDC& dc) const;
	int FindSpecialMarker(const wxPoint& point) const;
	wxString BuildSpecialMarkerTooltip(size_t markerIndex) const;
	void UpdateSpecialMarkerTooltip(const wxPoint& point);
	void ResetSpecialMarkerTooltip();
	void NavigateAt(const wxPoint& point);
	void NavigateTo(int x, int y, int floor);
	void NotifyStateChanged();

	struct SpecialItemData {
		uint16_t itemId = 0;
		uint16_t actionId = 0;
		uint16_t uniqueId = 0;
	};

	struct CachedSpecialMarker {
		int x = 0;
		int y = 0;
		int floor = 0;
		std::vector<SpecialItemData> items;
	};

	struct CachedSpecialChunk {
		MapChunkRevision revision;
		bool hasFloor = false;
		uint64_t lastVisiblePass = 0;
		std::vector<CachedSpecialMarker> markers;
	};

	struct VisibleSpecialMarker : CachedSpecialMarker {
		wxRect hitBounds;
		bool hasVisibleActionId = false;
		bool hasVisibleUniqueId = false;
	};

	std::array<wxPen, 256> pens_;
	std::array<wxBrush, 256> brushes_;
	wxBitmap mapBitmap_;
	std::function<void()> stateChanged_;
	std::unordered_map<uint32_t, CachedSpecialChunk> specialChunks_;
	std::vector<VisibleSpecialMarker> visibleSpecialMarkers_;
	wxString navigationTooltip_;
	wxString displayedTooltip_;
	uint64_t cachedMapSessionId_ = 0;
	uint64_t specialMapSessionId_ = 0;
	uint64_t specialMarkerPass_ = 0;
	int cachedFloor_ = -1;
	int cachedCenterX_ = 0;
	int cachedCenterY_ = 0;
	int cachedWidth_ = 0;
	int cachedHeight_ = 0;
	int renderedStartX_ = 0;
	int renderedStartY_ = 0;
	int zoomLevel_ = 2;
	bool compassVisible_ = true;
	bool controlsVisible_ = true;
	bool showActionIds_ = false;
	bool showUniqueIds_ = false;
	bool mapDirty_ = true;
	bool dragging_ = false;
};

#endif
