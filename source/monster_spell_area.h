//////////////////////////////////////////////////////////////////////
// Deterministic normalized monster spell area model and preview data.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_MONSTER_SPELL_AREA_H_
#define NEXAMAP_MONSTER_SPELL_AREA_H_

#include "monster_definition.h"

#include <string>
#include <vector>

struct MonsterAreaTile {
	int x = 0;
	int y = 0;

	friend bool operator==(const MonsterAreaTile&, const MonsterAreaTile&) = default;
};

[[nodiscard]] MonsterAreaShape ResolveMonsterAreaShape(const MonsterAttackArea& area);
[[nodiscard]] const char* MonsterAreaShapeName(MonsterAreaShape shape);
[[nodiscard]] std::string DescribeMonsterArea(const MonsterAttackArea& area);
[[nodiscard]] std::vector<MonsterAreaTile> BuildMonsterAreaTiles(const MonsterAttackArea& area, int direction = 0);

#endif // NEXAMAP_MONSTER_SPELL_AREA_H_
