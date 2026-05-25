//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "spawn_brush.h"
#include "basemap.h"
#include "creature.h"
#include "creature_brush.h"
#include "gui.h"
#include "settings.h"
#include "spawn.h"

#include <cmath>

//=============================================================================
// Spawn brush

SpawnBrush::SpawnBrush() :
	Brush() {
	////
}

SpawnBrush::~SpawnBrush() {
	////
}

int SpawnBrush::getLookID() const {
	return 0;
}

std::string SpawnBrush::getName() const {
	return "Spawn Brush";
}

bool SpawnBrush::canDraw(BaseMap* map, const Position& position) const {
	Tile* tile = map->getTile(position);
	if (tile) {
		if (tile->spawn) {
			return false;
		}
	}
	return true;
}

void SpawnBrush::undraw(BaseMap* map, Tile* tile) {
	delete tile->spawn;
	tile->spawn = nullptr;
}

void SpawnBrush::draw(BaseMap* map, Tile* tile, void* parameter) {
	ASSERT(tile);
	ASSERT(parameter); // Should contain an int which is the size of the newd spawn
	if (tile->spawn == nullptr) {
		tile->spawn = newd Spawn(max(1, *(int*)parameter));
	}
}

void SpawnBrush::setCreatures(const std::vector<CreatureEntry>& creatures) {
	this->creatures = creatures;
}

bool SpawnBrush::hasCreatures() const {
	return !creatures.empty();
}

std::vector<SpawnBrush::CreaturePlacement> SpawnBrush::getCreaturePlacements(BaseMap* map, const Position& center, int size) const {
	std::vector<CreaturePlacement> placements;
	if (!map || creatures.empty()) {
		return placements;
	}

	size = std::max(1, size);
	const int side = size * 2 + 1;
	const int density = g_settings.getInteger(Config::SPAWN_MONSTER_DENSITY);
	const int toSpawn = std::min(side * side, static_cast<int>(std::ceil((side * side) * (density / 100.0))));
	if (toSpawn <= 0) {
		return placements;
	}

	std::vector<Position> positions;
	positions.reserve(side * side);
	for (int x = -size; x <= size; ++x) {
		for (int y = -size; y <= size; ++y) {
			positions.push_back(center + Position(x, y, 0));
		}
	}

	while (!positions.empty() && static_cast<int>(placements.size()) < toSpawn) {
		const size_t index = static_cast<size_t>(rand()) % positions.size();
		const Position position = positions[index];
		positions[index] = positions.back();
		positions.pop_back();

		Tile* tile = map->getTile(position);
		if (!tile) {
			continue;
		}

		const CreatureEntry entry = pickCreatureForTile(tile);
		if (!entry.brush) {
			continue;
		}

		placements.push_back({ position, entry.brush, entry.weight });
	}

	return placements;
}

bool SpawnBrush::canDrawCreature(Tile* tile, CreatureBrush* brush) const {
	if (!tile || !brush || !brush->getType() || tile->isBlocking()) {
		return false;
	}

	if (tile->isPZ() && !brush->getType()->isNpc) {
		return false;
	}

	return true;
}

void SpawnBrush::drawCreature(Tile* tile, CreatureBrush* brush, uint8_t weight) const {
	if (!canDrawCreature(tile, brush)) {
		return;
	}

	delete tile->creature;
	tile->creature = newd Creature(brush->getType());
	tile->creature->setSpawnTime(g_gui.GetSpawnTime());
	tile->creature->setWeight(weight);
}

SpawnBrush::CreatureEntry SpawnBrush::pickCreatureForTile(Tile* tile) const {
	std::vector<CreatureEntry> validCreatures;
	int totalWeight = 0;

	for (const CreatureEntry& entry : creatures) {
		if (!canDrawCreature(tile, entry.brush)) {
			continue;
		}

		validCreatures.push_back(entry);
		totalWeight += entry.weight;
	}

	if (validCreatures.empty()) {
		return CreatureEntry();
	}

	if (totalWeight <= 0) {
		return validCreatures[static_cast<size_t>(rand()) % validCreatures.size()];
	}

	int roll = rand() % totalWeight;
	for (const CreatureEntry& entry : validCreatures) {
		if (roll < entry.weight) {
			return entry;
		}
		roll -= entry.weight;
	}

	return validCreatures.back();
}
