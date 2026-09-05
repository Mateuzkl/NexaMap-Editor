#ifndef NEXAMAP_MINIMAP_STYLE_H_
#define NEXAMAP_MINIMAP_STYLE_H_

#include <wx/colour.h>
#include <wx/control.h>

struct MinimapColours {
	wxColour panel;
	wxColour header;
	wxColour canvas;
	wxColour raised;
	wxColour raisedHover;
	wxColour border;
	wxColour text;
	wxColour textSubtle;
	wxColour accent;
	wxColour accentSoft;
	wxColour viewport;
};

class MinimapStyle {
public:
	static MinimapColours GetColours();
};

enum class MinimapGlyph {
	ZoomIn,
	ZoomOut,
	Center,
	FloorUp,
	FloorDown,
	Options,
	Go,
};

class MinimapToolButton final : public wxControl {
public:
	MinimapToolButton(wxWindow* parent, MinimapGlyph glyph, const wxString& tooltip, const wxSize& dipSize = wxSize(30, 30));

	void SetSelected(bool selected);

private:
	void OnPaint(wxPaintEvent& event);
	void OnMouseEnter(wxMouseEvent& event);
	void OnMouseLeave(wxMouseEvent& event);
	void OnMouseDown(wxMouseEvent& event);
	void OnMouseUp(wxMouseEvent& event);
	void OnCaptureLost(wxMouseCaptureLostEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void Activate();
	void DrawGlyph(wxDC& dc, const wxRect& bounds, const wxColour& colour) const;

	MinimapGlyph glyph_;
	bool hovered_ = false;
	bool pressed_ = false;
	bool selected_ = false;
};

#endif
