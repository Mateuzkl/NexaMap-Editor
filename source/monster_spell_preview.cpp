//////////////////////////////////////////////////////////////////////
// Native wxWidgets preview for normalized monster attacks.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "monster_spell_preview.h"

#include "monster_spell_area.h"
#include "graphics.h"
#include "gui.h"
#include "theme.h"

#include <algorithm>

#include <wx/dcbuffer.h>
#include <wx/image.h>
#include <wx/timer.h>

namespace {
	wxBitmap BitmapFromRgba(const std::vector<uint8_t>& rgba, int width, int height) {
		if (width <= 0 || height <= 0 || rgba.size() != static_cast<std::size_t>(width) * height * 4) {
			return {};
		}
		auto* rgb = new unsigned char[static_cast<std::size_t>(width) * height * 3];
		auto* alpha = new unsigned char[static_cast<std::size_t>(width) * height];
		for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(width) * height; ++pixel) {
			rgb[pixel * 3] = rgba[pixel * 4];
			rgb[pixel * 3 + 1] = rgba[pixel * 4 + 1];
			rgb[pixel * 3 + 2] = rgba[pixel * 4 + 2];
			alpha[pixel] = rgba[pixel * 4 + 3];
		}
		return wxBitmap(wxImage(width, height, rgb, alpha, false));
	}

	std::pair<int, int> DirectionPattern(int direction, const GameSprite& sprite) {
		static constexpr int offsets[8][2] {
			{ 0, -1 },
			{ 1, -1 },
			{ 1, 0 },
			{ 1, 1 },
			{ 0, 1 },
			{ -1, 1 },
			{ -1, 0 },
			{ -1, -1 },
		};
		const int selected = std::clamp(direction, 0, 7);
		return {
			sprite.pattern_x >= 3 ? offsets[selected][0] + 1 : std::min<int>(selected, std::max<int>(0, sprite.pattern_x - 1)),
			sprite.pattern_y >= 3 ? offsets[selected][1] + 1 : 0,
		};
	}

	wxBitmap SpriteBitmap(GameSprite* sprite, int frame, int direction) {
		if (!sprite) {
			return {};
		}
		std::vector<uint8_t> pixels;
		int width = 0;
		int height = 0;
		bool pending = false;
		const auto [patternX, patternY] = DirectionPattern(direction, *sprite);
		if (!sprite->getVisualPreviewRGBA(pixels, width, height, pending, true, nullptr, 0, frame, 0, patternX, patternY)) {
			return {};
		}
		return BitmapFromRgba(pixels, width, height);
	}

	wxBitmap FitBitmap(wxBitmap bitmap, int maximum) {
		if (!bitmap.IsOk() || maximum <= 0) {
			return bitmap;
		}
		const int largest = std::max(bitmap.GetWidth(), bitmap.GetHeight());
		if (largest <= maximum) {
			return bitmap;
		}
		const double scale = static_cast<double>(maximum) / largest;
		return wxBitmap(bitmap.ConvertToImage().Scale(
			std::max(1, static_cast<int>(bitmap.GetWidth() * scale)),
			std::max(1, static_cast<int>(bitmap.GetHeight() * scale)),
			wxIMAGE_QUALITY_HIGH
		));
	}
}

MonsterSpellPreview::MonsterSpellPreview(wxWindow* parent) :
	wxPanel(parent, wxID_ANY) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(FromDIP(wxSize(300, 320)));
	Bind(wxEVT_PAINT, &MonsterSpellPreview::OnPaint, this);
	timer = std::make_unique<wxTimer>(this);
	Bind(wxEVT_TIMER, &MonsterSpellPreview::OnTimer, this, timer->GetId());
}

void MonsterSpellPreview::SetAttack(const MonsterAttackDefinition* attack) {
	hasAttack = attack != nullptr;
	if (attack) {
		current = *attack;
	}
	customTiles.clear();
	customDescription.clear();
	customAreaMode = false;
	Refresh();
}

void MonsterSpellPreview::SetCustomArea(const MonsterAttackDefinition* attack, std::vector<MonsterAreaTile> tiles, std::string description) {
	hasAttack = attack != nullptr;
	if (attack) {
		current = *attack;
	}
	customTiles = std::move(tiles);
	customDescription = std::move(description);
	customAreaMode = true;
	Refresh();
}

void MonsterSpellPreview::SetUnavailableArea(const MonsterAttackDefinition* attack, std::string description) {
	SetCustomArea(attack, {}, std::move(description));
}

void MonsterSpellPreview::SetDirection(int newDirection) {
	direction = newDirection;
	cachedProjectileDirection = -1;
	Refresh();
}

void MonsterSpellPreview::SetVisualIds(int newEffectId, int newProjectileId) {
	effectId = std::max(0, newEffectId);
	projectileId = std::max(0, newProjectileId);
	animationTick = 0;
	flightStep = 0;
	cachedEffectId = -1;
	cachedProjectileId = -1;
	Refresh();
}

void MonsterSpellPreview::SetPlaying(bool playing) {
	if (playing) {
		timer->Start(animationInterval);
	} else {
		timer->Stop();
	}
	Refresh();
}

void MonsterSpellPreview::SetAnimationInterval(int milliseconds) {
	animationInterval = std::clamp(milliseconds, 50, 1000);
	if (timer->IsRunning()) {
		timer->Start(animationInterval);
	}
}

void MonsterSpellPreview::OnTimer(wxTimerEvent&) {
	++animationTick;
	flightStep = (flightStep + 1) % 13;
	Refresh(false);
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

	const std::vector<MonsterAreaTile> tiles = customAreaMode ? customTiles : BuildMonsterAreaTiles(current.area, direction / 2);
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

	GameSprite* effectSprite = g_gui.gfx.getEffectSprite(effectId);
	if (effectSprite) {
		const int effectFrame = animationTick % std::max<int>(1, effectSprite->frames);
		if (cachedEffectId != effectId || cachedEffectFrame != effectFrame) {
			cachedEffectBitmap = SpriteBitmap(effectSprite, effectFrame, 0);
			cachedEffectId = effectId;
			cachedEffectFrame = effectFrame;
		}
		wxBitmap bitmap = FitBitmap(cachedEffectBitmap, std::max(cell, cell * 2));
		if (bitmap.IsOk()) {
			if (tiles.empty()) {
				dc.DrawBitmap(bitmap, center.x + cell / 2 - bitmap.GetWidth() / 2, center.y + cell / 2 - bitmap.GetHeight() / 2, true);
			} else {
				for (const MonsterAreaTile& tile : tiles) {
					const int x = center.x + tile.x * cell + cell / 2 - bitmap.GetWidth() / 2;
					const int y = center.y + tile.y * cell + cell / 2 - bitmap.GetHeight() / 2;
					dc.DrawBitmap(bitmap, x, y, true);
				}
			}
		}
	}

	GameSprite* projectileSprite = g_gui.gfx.getDistanceSprite(projectileId);
	if (projectileSprite) {
		const int projectileFrame = animationTick % std::max<int>(1, projectileSprite->frames);
		if (cachedProjectileId != projectileId || cachedProjectileFrame != projectileFrame || cachedProjectileDirection != direction) {
			cachedProjectileBitmap = SpriteBitmap(projectileSprite, projectileFrame, direction);
			cachedProjectileId = projectileId;
			cachedProjectileFrame = projectileFrame;
			cachedProjectileDirection = direction;
		}
		wxBitmap bitmap = FitBitmap(cachedProjectileBitmap, std::max(cell, cell * 2));
		if (bitmap.IsOk()) {
			MonsterAreaTile destination { 0, -std::max(2, current.area.range) };
			if (!tiles.empty()) {
				destination = *std::max_element(tiles.begin(), tiles.end(), [](const MonsterAreaTile& left, const MonsterAreaTile& right) {
					return std::abs(left.x) + std::abs(left.y) < std::abs(right.x) + std::abs(right.y);
				});
			}
			const double progress = timer->IsRunning() ? static_cast<double>(flightStep) / 12.0 : 1.0;
			const int x = center.x + static_cast<int>(destination.x * cell * progress) + cell / 2 - bitmap.GetWidth() / 2;
			const int y = center.y + static_cast<int>(destination.y * cell * progress) + cell / 2 - bitmap.GetHeight() / 2;
			dc.DrawBitmap(bitmap, x, y, true);
		}
	}

	const int detailsTop = size.y - FromDIP(62);
	dc.SetTextForeground(Theme::Get(Theme::Role::Text));
	dc.DrawText(wxString::FromUTF8(customDescription.empty() ? DescribeMonsterArea(current.area) : customDescription), FromDIP(10), detailsTop);
	dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
	dc.DrawText("Effect: " + wxString::FromUTF8(current.effect.empty() ? "none" : current.effect), FromDIP(10), detailsTop + FromDIP(20));
	dc.DrawText("Projectile: " + wxString::FromUTF8(current.projectile.empty() ? "none" : current.projectile), FromDIP(10), detailsTop + FromDIP(38));
}
