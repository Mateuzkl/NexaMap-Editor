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

#ifndef RME_SPAWN_BRUSH_H
#define RME_SPAWN_BRUSH_H

#include "brush.h"

//=============================================================================
// SpawnBrush, place spawns

class SpawnBrush : public Brush {
public:
	struct CreatureEntry {
		CreatureBrush* brush = nullptr;
		uint8_t weight = 0;
	};

	struct CreaturePlacement {
		Position position;
		CreatureBrush* brush = nullptr;
		uint8_t weight = 0;
	};

	SpawnBrush(); // Create a RAWBrush of the specified type
	~SpawnBrush() override;

	bool isSpawn() const override {
		return true;
	}
	SpawnBrush* asSpawn() override {
		return static_cast<SpawnBrush*>(this);
	}

	bool canDraw(BaseMap* map, const Position& position) const override;
	void draw(BaseMap* map, Tile* tile, void* parameter) override; // parameter is brush size
	void undraw(BaseMap* map, Tile* tile) override;

	int getLookID() const override; // We don't have a look, sorry!
	std::string getName() const override;
	bool canDrag() const override {
		return true;
	}
	bool canSmear() const override {
		return false;
	}
	bool oneSizeFitsAll() const override {
		return true;
	}

	void setCreatures(const std::vector<CreatureEntry>& creatures);
	bool hasCreatures() const;
	std::vector<CreaturePlacement> getCreaturePlacements(BaseMap* map, const Position& center, int size) const;
	bool canDrawCreature(Tile* tile, CreatureBrush* brush) const;
	void drawCreature(Tile* tile, CreatureBrush* brush, uint8_t weight) const;

private:
	CreatureEntry pickCreatureForTile(Tile* tile) const;

	std::vector<CreatureEntry> creatures;
};

#endif
