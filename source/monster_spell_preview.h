//////////////////////////////////////////////////////////////////////
// Native wxWidgets preview for normalized monster attacks.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_MONSTER_SPELL_PREVIEW_H_
#define NEXAMAP_MONSTER_SPELL_PREVIEW_H_

#include "monster_definition.h"

#include <wx/panel.h>

class MonsterSpellPreview final : public wxPanel {
public:
	explicit MonsterSpellPreview(wxWindow* parent);

	void SetAttack(const MonsterAttackDefinition* attack);
	void SetDirection(int direction);

private:
	void OnPaint(wxPaintEvent& event);

	MonsterAttackDefinition current;
	bool hasAttack = false;
	int direction = 0;
};

#endif // NEXAMAP_MONSTER_SPELL_PREVIEW_H_
