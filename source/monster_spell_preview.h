//////////////////////////////////////////////////////////////////////
// Native wxWidgets preview for normalized monster attacks.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_MONSTER_SPELL_PREVIEW_H_
#define NEXAMAP_MONSTER_SPELL_PREVIEW_H_

#include "monster_definition.h"
#include "monster_spell_area.h"

#include <wx/panel.h>

#include <string>
#include <vector>

class MonsterSpellPreview final : public wxPanel {
public:
	explicit MonsterSpellPreview(wxWindow* parent);

	void SetAttack(const MonsterAttackDefinition* attack);
	void SetCustomArea(const MonsterAttackDefinition* attack, std::vector<MonsterAreaTile> tiles, std::string description);
	void SetDirection(int direction);

private:
	void OnPaint(wxPaintEvent& event);

	MonsterAttackDefinition current;
	std::vector<MonsterAreaTile> customTiles;
	std::string customDescription;
	bool hasAttack = false;
	int direction = 0;
};

#endif // NEXAMAP_MONSTER_SPELL_PREVIEW_H_
