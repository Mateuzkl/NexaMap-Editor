//////////////////////////////////////////////////////////////////////
// Deterministic normalized monster spell area model and preview data.
//////////////////////////////////////////////////////////////////////

#include "monster_spell_area.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace {
	MonsterAreaTile Rotate(MonsterAreaTile tile, int direction) {
		switch ((direction % 4 + 4) % 4) {
			case 1:
				return { -tile.y, tile.x };
			case 2:
				return { -tile.x, -tile.y };
			case 3:
				return { tile.y, -tile.x };
			default:
				return tile;
		}
	}

	void AddUnique(std::vector<MonsterAreaTile>& tiles, MonsterAreaTile tile) {
		if (std::find(tiles.begin(), tiles.end(), tile) == tiles.end()) {
			tiles.push_back(tile);
		}
	}
}

MonsterAreaShape ResolveMonsterAreaShape(const MonsterAttackArea& area) {
	if (area.length > 0) {
		return MonsterAreaShape::Beam;
	}
	if (area.ring > 0) {
		return MonsterAreaShape::Ring;
	}
	if (area.radius > 0) {
		return MonsterAreaShape::Radius;
	}
	if (area.target || area.range > 0) {
		return MonsterAreaShape::Target;
	}
	return MonsterAreaShape::Single;
}

const char* MonsterAreaShapeName(MonsterAreaShape shape) {
	switch (shape) {
		case MonsterAreaShape::Target:
			return "Target";
		case MonsterAreaShape::Radius:
			return "Radius";
		case MonsterAreaShape::Ring:
			return "Ring";
		case MonsterAreaShape::Beam:
			return "Beam";
		default:
			return "Single";
	}
}

std::string DescribeMonsterArea(const MonsterAttackArea& area) {
	std::ostringstream description;
	const MonsterAreaShape shape = ResolveMonsterAreaShape(area);
	description << MonsterAreaShapeName(shape);
	if (shape == MonsterAreaShape::Beam) {
		description << " " << area.length;
		if (area.spread > 0) {
			description << " x " << area.spread;
		}
	} else if (shape == MonsterAreaShape::Radius) {
		description << " " << area.radius;
	} else if (shape == MonsterAreaShape::Ring) {
		description << " " << area.ring;
	}
	if (area.range > 0) {
		description << " | range " << area.range;
	}
	return description.str();
}

std::vector<MonsterAreaTile> BuildMonsterAreaTiles(const MonsterAttackArea& area, int direction) {
	std::vector<MonsterAreaTile> tiles;
	const MonsterAreaShape shape = ResolveMonsterAreaShape(area);
	const int targetDistance = area.target ? std::max(1, area.range) : 0;
	const MonsterAreaTile center { 0, -targetDistance };
	if (shape == MonsterAreaShape::Single) {
		tiles.push_back({ 0, -1 });
	} else if (shape == MonsterAreaShape::Target) {
		tiles.push_back({ 0, -std::max(1, area.range) });
	} else if (shape == MonsterAreaShape::Beam) {
		const int length = std::max(1, area.length);
		for (int step = 1; step <= length; ++step) {
			const int halfWidth = area.spread <= 0 ? 0 : (step * area.spread) / (2 * length);
			for (int x = -halfWidth; x <= halfWidth; ++x) {
				AddUnique(tiles, { x, -step });
			}
		}
	} else {
		const int outer = std::max(1, shape == MonsterAreaShape::Ring ? area.ring : area.radius);
		const int inner = shape == MonsterAreaShape::Ring ? std::max(0, outer - 1) : 0;
		for (int y = -outer; y <= outer; ++y) {
			for (int x = -outer; x <= outer; ++x) {
				const double distance = std::sqrt(static_cast<double>(x * x + y * y));
				if (distance <= outer + 0.25 && distance >= inner - 0.25) {
					AddUnique(tiles, { center.x + x, center.y + y });
				}
			}
		}
	}
	for (MonsterAreaTile& tile : tiles) {
		tile = Rotate(tile, direction);
	}
	std::sort(tiles.begin(), tiles.end(), [](const MonsterAreaTile& left, const MonsterAreaTile& right) {
		return left.y == right.y ? left.x < right.x : left.y < right.y;
	});
	return tiles;
}
