#include "monster_spell_area.h"

#include <algorithm>
#include <iostream>

namespace {
	int failures = 0;

	void Check(bool condition, const char* message) {
		if (!condition) {
			std::cerr << "FAILED: " << message << '\n';
			++failures;
		}
	}

	bool Contains(const std::vector<MonsterAreaTile>& tiles, MonsterAreaTile tile) {
		return std::find(tiles.begin(), tiles.end(), tile) != tiles.end();
	}
}

int main() {
	MonsterAttackArea target;
	target.target = true;
	target.range = 7;
	auto tiles = BuildMonsterAreaTiles(target);
	Check(ResolveMonsterAreaShape(target) == MonsterAreaShape::Target, "target shape is inferred");
	Check(tiles.size() == 1 && tiles.front() == MonsterAreaTile { 0, -7 }, "target range is represented exactly");

	MonsterAttackArea beam;
	beam.length = 8;
	beam.spread = 3;
	tiles = BuildMonsterAreaTiles(beam);
	Check(ResolveMonsterAreaShape(beam) == MonsterAreaShape::Beam, "beam takes precedence over other area fields");
	Check(Contains(tiles, { 0, -1 }) && Contains(tiles, { 1, -8 }) && !Contains(tiles, { 3, -1 }), "beam length and spread generate a directional cone");
	auto east = BuildMonsterAreaTiles(beam, 1);
	Check(Contains(east, { 8, 1 }), "area rotates with caster direction");

	MonsterAttackArea radius;
	radius.radius = 3;
	radius.target = true;
	radius.range = 5;
	tiles = BuildMonsterAreaTiles(radius);
	Check(ResolveMonsterAreaShape(radius) == MonsterAreaShape::Radius, "radius takes precedence over target");
	Check(Contains(tiles, { 0, -5 }) && Contains(tiles, { 3, -5 }) && !Contains(tiles, { 4, -5 }), "targeted radius is centered at target range");

	MonsterAttackArea ring;
	ring.ring = 4;
	tiles = BuildMonsterAreaTiles(ring);
	Check(ResolveMonsterAreaShape(ring) == MonsterAreaShape::Ring, "ring shape is inferred");
	Check(Contains(tiles, { 4, 0 }) && !Contains(tiles, { 0, 0 }), "ring excludes its center");

	return failures == 0 ? 0 : 1;
}
