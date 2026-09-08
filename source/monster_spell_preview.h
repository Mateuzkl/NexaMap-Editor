//////////////////////////////////////////////////////////////////////
// Native wxWidgets preview for normalized monster attacks.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_MONSTER_SPELL_PREVIEW_H_
#define NEXAMAP_MONSTER_SPELL_PREVIEW_H_

#include "monster_definition.h"
#include "monster_spell_area.h"

#include <wx/panel.h>
#include <wx/bitmap.h>
#include <wx/timer.h>

#include <string>
#include <memory>
#include <vector>

class MonsterSpellPreview final : public wxPanel {
public:
	explicit MonsterSpellPreview(wxWindow* parent);

	void SetAttack(const MonsterAttackDefinition* attack);
	void SetCustomArea(const MonsterAttackDefinition* attack, std::vector<MonsterAreaTile> tiles, std::string description);
	void SetUnavailableArea(const MonsterAttackDefinition* attack, std::string description);
	void SetDirection(int direction);
	void SetVisualIds(int effectId, int projectileId);
	void SetPlaying(bool playing);
	void SetAnimationInterval(int milliseconds);

private:
	void OnPaint(wxPaintEvent& event);
	void OnTimer(wxTimerEvent& event);

	MonsterAttackDefinition current;
	std::vector<MonsterAreaTile> customTiles;
	std::string customDescription;
	bool customAreaMode = false;
	bool hasAttack = false;
	int direction = 0;
	int effectId = 0;
	int projectileId = 0;
	int animationTick = 0;
	int flightStep = 0;
	int animationInterval = 140;
	wxBitmap cachedEffectBitmap;
	wxBitmap cachedProjectileBitmap;
	int cachedEffectId = -1;
	int cachedEffectFrame = -1;
	int cachedProjectileId = -1;
	int cachedProjectileFrame = -1;
	int cachedProjectileDirection = -1;
	std::unique_ptr<wxTimer> timer;
};

#endif // NEXAMAP_MONSTER_SPELL_PREVIEW_H_
