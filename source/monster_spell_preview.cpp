//////////////////////////////////////////////////////////////////////
// Native wxWidgets preview for normalized monster attacks.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "monster_spell_preview.h"

#include "monster_spell_area.h"
#include "theme.h"

#include <algorithm>

#include <wx/dcbuffer.h>

MonsterSpellPreview::MonsterSpellPreview(wxWindow* parent) : wxPanel(parent, wxID_ANY) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(FromDIP(wxSize(300, 320)));
	Bind(wxEVT_PAINT, &MonsterSpellPreview::OnPaint, this);
}

void MonsterSpellPreview::SetAttack(const MonsterAttackDefinition* attack) {
	hasAttack = attack != nullptr;
	if (attack) {
		current = *attack;
	}
	customTiles.clear();
	customDescription.clear();
	Refresh();
}

void MonsterSpellPreview::SetCustomArea(const MonsterAttackDefinition* attack, std::vector<MonsterAreaTile> tiles, std::string description) {
	hasAttack = attack != nullptr;
	if (attack) {
		current = *attack;
	}
	customTiles = std::move(tiles);
	customDescription = std::move(description);
	Refresh();
}

void MonsterSpellPreview::SetDirection(int newDirection) {
	direction = newDirection;
	Refresh();
}

void MonsterSpellPreview::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Surface)));
	dc.Clear();
	const wxSize size = GetClientSize();
	if (!hasAttack) {
		dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
		dc.DrawLabel("Select an attack to preview its affected tiles.", wxRect(FromDIP(16), FromDIP(16), size.x - FromDIP(32), size.y - FromDIP(32)), wxALIGN_CENTER);
		return;
	}

	const std::vector<MonsterAreaTile> tiles = customTiles.empty() ? BuildMonsterAreaTiles(current.area, direction) : customTiles;
	int extent = 4;
	for (const MonsterAreaTile& tile : tiles) {
		extent = std::max({ extent, std::abs(tile.x) + 1, std::abs(tile.y) + 1 });
	}
	const int available = std::max(80, std::min(size.x - FromDIP(28), size.y - FromDIP(96)));
	const int cell = std::clamp(available / (extent * 2 + 1), FromDIP(10), FromDIP(28));
	const wxPoint center(size.x / 2, FromDIP(18) + available / 2);
	const wxColour grid = Theme::Get(Theme::Role::Border);
	dc.SetPen(wxPen(grid));
	for (int coordinate = -extent; coordinate <= extent; ++coordinate) {
		const int offset = coordinate * cell;
		dc.DrawLine(center.x - extent * cell, center.y + offset, center.x + (extent + 1) * cell, center.y + offset);
		dc.DrawLine(center.x + offset, center.y - extent * cell, center.x + offset, center.y + (extent + 1) * cell);
	}

	for (const MonsterAreaTile& tile : tiles) {
		const wxRect rectangle(center.x + tile.x * cell + 1, center.y + tile.y * cell + 1, cell - 1, cell - 1);
		dc.SetBrush(wxBrush(wxColour(210, 74, 62, 180)));
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.DrawRectangle(rectangle);
	}
	const wxRect caster(center.x + 1, center.y + 1, cell - 1, cell - 1);
	dc.SetBrush(wxBrush(wxColour(67, 148, 230)));
	dc.DrawRectangle(caster);
	dc.SetTextForeground(Theme::Get(Theme::Role::Text));
	dc.DrawLabel("C", caster, wxALIGN_CENTER);

	const int detailsTop = size.y - FromDIP(62);
	dc.SetTextForeground(Theme::Get(Theme::Role::Text));
	dc.DrawText(wxString::FromUTF8(customDescription.empty() ? DescribeMonsterArea(current.area) : customDescription), FromDIP(10), detailsTop);
	dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
	dc.DrawText("Effect: " + wxString::FromUTF8(current.effect.empty() ? "none" : current.effect), FromDIP(10), detailsTop + FromDIP(20));
	dc.DrawText("Projectile: " + wxString::FromUTF8(current.projectile.empty() ? "none" : current.projectile), FromDIP(10), detailsTop + FromDIP(38));
}
