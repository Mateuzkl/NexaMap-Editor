// SPDX-License-Identifier: GPL-3.0-or-later
#include "../main.h"
#include "playtest_hud.h"
#include "../theme.h"
#include <wx/dcbuffer.h>

PlaytestHud::PlaytestHud(wxWindow* parent) :
	wxPanel(parent, wxID_ANY) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(FromDIP(wxSize(-1, 66)));
	SetToolTip("Visual HUD only. This local playtest does not simulate combat, health or server scripts.");
	Bind(wxEVT_PAINT, &PlaytestHud::OnPaint, this);
}
void PlaytestHud::SetPosition(Position value) {
	if (position != value) {
		position = value;
		Refresh(false);
	}
}
void PlaytestHud::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	dc.SetBackground(wxBrush(Theme::Get(Theme::Role::RaisedSurface)));
	dc.Clear();
	dc.SetFont(GetFont());
	dc.SetTextForeground(Theme::Get(Theme::Role::Text));
	const int pad = FromDIP(10), gap = FromDIP(14);
	const int width = std::max(1, (GetClientSize().x - 2 * pad - gap) / 2);
	const wxString location = position.isValid() ? wxString::Format("X %d   Y %d   Z %d", position.x, position.y, position.z) : wxString("No active map");
	dc.DrawText(location, pad, FromDIP(7));
	const wxString floor = position.isValid() ? wxString::Format("FLOOR %d", position.z) : wxString("PLAYTEST");
	dc.SetTextForeground(Theme::Get(Theme::Role::Accent));
	dc.DrawText(floor, GetClientSize().x - pad - dc.GetTextExtent(floor).x, FromDIP(7));
	const int y = FromDIP(34), height = FromDIP(20);
	for (int i = 0; i < 2; ++i) {
		const int x = pad + i * (width + gap);
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(i == 0 ? wxColour(31, 119, 94) : wxColour(49, 87, 164)));
		dc.DrawRoundedRectangle(x, y, width, height, FromDIP(4));
		dc.SetTextForeground(*wxWHITE);
		const wxString label = i == 0 ? "HP 100 / 100" : "MP 100 / 100";
		dc.DrawText(label, x + FromDIP(8), y + (height - dc.GetTextExtent(label).y) / 2);
	}
}
