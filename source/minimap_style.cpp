#include "main.h"

#include "minimap_style.h"

#include "theme.h"

#include <wx/dcbuffer.h>

namespace {
	wxColour Blend(const wxColour& first, const wxColour& second, int secondWeight) {
		const int firstWeight = 100 - secondWeight;
		return wxColour(
			(first.Red() * firstWeight + second.Red() * secondWeight) / 100,
			(first.Green() * firstWeight + second.Green() * secondWeight) / 100,
			(first.Blue() * firstWeight + second.Blue() * secondWeight) / 100
		);
	}
}

MinimapColours MinimapStyle::GetColours() {
	const wxColour surface = Theme::Get(Theme::Role::Surface);
	const wxColour background = Theme::Get(Theme::Role::Background);
	const wxColour raised = Theme::Get(Theme::Role::RaisedSurface);
	const wxColour accent = Theme::Get(Theme::Role::Accent);
	return {
		surface,
		Blend(surface, raised, 38),
		Theme::IsDark() ? wxColour(3, 11, 16) : wxColour(225, 232, 235),
		raised,
		Blend(raised, accent, Theme::IsDark() ? 16 : 9),
		Theme::Get(Theme::Role::Border),
		Theme::Get(Theme::Role::Text),
		Theme::Get(Theme::Role::TextSubtle),
		accent,
		Blend(background, accent, Theme::IsDark() ? 23 : 13),
		Theme::IsDark() ? wxColour(248, 205, 92) : wxColour(173, 112, 0),
	};
}

MinimapToolButton::MinimapToolButton(wxWindow* parent, MinimapGlyph glyph, const wxString& tooltip, const wxSize& dipSize) :
	wxControl(parent, wxID_ANY, wxDefaultPosition, FROM_DIP(parent, dipSize), wxBORDER_NONE | wxWANTS_CHARS),
	glyph_(glyph) {
	SetMinSize(FROM_DIP(parent, dipSize));
	SetToolTip(tooltip);
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	Bind(wxEVT_PAINT, &MinimapToolButton::OnPaint, this);
	Bind(wxEVT_ENTER_WINDOW, &MinimapToolButton::OnMouseEnter, this);
	Bind(wxEVT_LEAVE_WINDOW, &MinimapToolButton::OnMouseLeave, this);
	Bind(wxEVT_LEFT_DOWN, &MinimapToolButton::OnMouseDown, this);
	Bind(wxEVT_LEFT_UP, &MinimapToolButton::OnMouseUp, this);
	Bind(wxEVT_MOUSE_CAPTURE_LOST, &MinimapToolButton::OnCaptureLost, this);
	Bind(wxEVT_KEY_DOWN, &MinimapToolButton::OnKeyDown, this);
}

void MinimapToolButton::SetSelected(bool selected) {
	if (selected_ == selected) {
		return;
	}
	selected_ = selected;
	Refresh();
}

void MinimapToolButton::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	const MinimapColours colours = MinimapStyle::GetColours();
	dc.SetBackground(wxBrush(colours.panel));
	dc.Clear();

	wxRect bounds = GetClientRect();
	bounds.Deflate(FROM_DIP(this, 1));
	const wxColour fill = selected_ ? colours.accentSoft : (hovered_ || pressed_ ? colours.raisedHover : colours.raised);
	const wxColour border = selected_ ? colours.accent : colours.border;
	dc.SetBrush(wxBrush(fill));
	dc.SetPen(wxPen(border, FROM_DIP(this, pressed_ ? 2 : 1)));
	dc.DrawRoundedRectangle(bounds, FROM_DIP(this, 5));

	DrawGlyph(dc, bounds, IsEnabled() ? (selected_ ? colours.accent : colours.text) : colours.textSubtle);
	if (HasFocus()) {
		wxRect focusBounds = bounds;
		focusBounds.Deflate(FROM_DIP(this, 3));
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.SetPen(wxPen(colours.accent, FROM_DIP(this, 1), wxPENSTYLE_DOT));
		dc.DrawRoundedRectangle(focusBounds, FROM_DIP(this, 3));
	}
}

void MinimapToolButton::DrawGlyph(wxDC& dc, const wxRect& bounds, const wxColour& colour) const {
	const int centerX = bounds.x + bounds.width / 2;
	const int centerY = bounds.y + bounds.height / 2;
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	dc.SetPen(wxPen(colour, std::max(1, FROM_DIP(this, 2)), wxPENSTYLE_SOLID));

	switch (glyph_) {
		case MinimapGlyph::ZoomIn:
		case MinimapGlyph::ZoomOut: {
			const wxString label = glyph_ == MinimapGlyph::ZoomIn ? wxString("+") : wxString::FromUTF8("\xE2\x88\x92");
			dc.SetFont(wxFontInfo(std::max(10, GetFont().GetPointSize() + 1)).Bold());
			dc.SetTextForeground(colour);
			const wxSize textSize = dc.GetTextExtent(label);
			dc.DrawText(label, centerX - textSize.x / 2, centerY - textSize.y / 2);
			break;
		}
		case MinimapGlyph::Center: {
			const int edge = FROM_DIP(this, 7);
			const int arm = FROM_DIP(this, 4);
			dc.DrawLine(centerX - edge, centerY - edge + arm, centerX - edge, centerY - edge);
			dc.DrawLine(centerX - edge, centerY - edge, centerX - edge + arm, centerY - edge);
			dc.DrawLine(centerX + edge - arm, centerY - edge, centerX + edge, centerY - edge);
			dc.DrawLine(centerX + edge, centerY - edge, centerX + edge, centerY - edge + arm);
			dc.DrawLine(centerX - edge, centerY + edge - arm, centerX - edge, centerY + edge);
			dc.DrawLine(centerX - edge, centerY + edge, centerX - edge + arm, centerY + edge);
			dc.DrawLine(centerX + edge - arm, centerY + edge, centerX + edge, centerY + edge);
			dc.DrawLine(centerX + edge, centerY + edge - arm, centerX + edge, centerY + edge);
			dc.SetBrush(wxBrush(colour));
			dc.DrawCircle(centerX, centerY, FROM_DIP(this, 2));
			break;
		}
		case MinimapGlyph::FloorUp:
		case MinimapGlyph::FloorDown: {
			const wxString label = glyph_ == MinimapGlyph::FloorUp ? "U" : "D";
			dc.SetFont(wxFontInfo(std::max(8, GetFont().GetPointSize() - 1)).Bold());
			dc.SetTextForeground(colour);
			const wxSize textSize = dc.GetTextExtent(label);
			dc.DrawText(label, centerX - textSize.x / 2, centerY - textSize.y / 2);
			break;
		}
		case MinimapGlyph::Options:
			dc.SetBrush(wxBrush(colour));
			for (int offset : { -5, 0, 5 }) {
				dc.DrawCircle(centerX + FROM_DIP(this, offset), centerY, FROM_DIP(this, 1));
			}
			break;
		case MinimapGlyph::Go:
			dc.DrawLine(centerX - FROM_DIP(this, 6), centerY, centerX + FROM_DIP(this, 5), centerY);
			dc.DrawLine(centerX + FROM_DIP(this, 1), centerY - FROM_DIP(this, 4), centerX + FROM_DIP(this, 5), centerY);
			dc.DrawLine(centerX + FROM_DIP(this, 5), centerY, centerX + FROM_DIP(this, 1), centerY + FROM_DIP(this, 4));
			break;
	}
}

void MinimapToolButton::OnMouseEnter(wxMouseEvent& event) {
	hovered_ = true;
	Refresh();
	event.Skip();
}

void MinimapToolButton::OnMouseLeave(wxMouseEvent& event) {
	hovered_ = false;
	Refresh();
	event.Skip();
}

void MinimapToolButton::OnMouseDown(wxMouseEvent&) {
	if (!IsEnabled()) {
		return;
	}
	pressed_ = true;
	SetFocus();
	if (!HasCapture()) {
		CaptureMouse();
	}
	Refresh();
}

void MinimapToolButton::OnMouseUp(wxMouseEvent& event) {
	const bool activate = pressed_ && GetClientRect().Contains(event.GetPosition()) && IsEnabled();
	pressed_ = false;
	if (HasCapture()) {
		ReleaseMouse();
	}
	Refresh();
	if (activate) {
		Activate();
	}
}

void MinimapToolButton::OnCaptureLost(wxMouseCaptureLostEvent&) {
	pressed_ = false;
	Refresh();
}

void MinimapToolButton::OnKeyDown(wxKeyEvent& event) {
	if (event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_SPACE) {
		Activate();
		return;
	}
	event.Skip();
}

void MinimapToolButton::Activate() {
	if (!IsEnabled()) {
		return;
	}
	wxCommandEvent event(wxEVT_BUTTON, GetId());
	event.SetEventObject(this);
	ProcessWindowEvent(event);
}
