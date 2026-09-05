// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_PLAYTEST_HUD_H_
#define NEXAMAP_PLAYTEST_HUD_H_
#include <wx/panel.h>
#include "../position.h"
class PlaytestHud final : public wxPanel {
public:
	explicit PlaytestHud(wxWindow* parent);
	void SetPosition(Position value);

private:
	void OnPaint(wxPaintEvent& event);
	Position position { -1, -1, -1 };
};
#endif
