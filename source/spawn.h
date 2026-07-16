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

#ifndef RME_SPAWN_H_
#define RME_SPAWN_H_

#include "spawn_format.h"

class Tile;

class Spawn {
public:
	Spawn(int size = 3) :
		size(0), selected(false) {
		setSize(size);
	}
	~Spawn() { }

	Spawn* deepCopy() {
		Spawn* copy = newd Spawn(size);
		copy->selected = selected;
		copy->mixedAttributes = mixedAttributes;
		copy->monsterAttributes = monsterAttributes;
		copy->npcAttributes = npcAttributes;
		copy->hasMixedSource = hasMixedSource;
		copy->hasMonsterSource = hasMonsterSource;
		copy->hasNpcSource = hasNpcSource;
		return copy;
	}

	bool isSelected() const {
		return selected;
	}
	void select() {
		selected = true;
	}
	void deselect() {
		selected = false;
	}

	// Does not compare selection!
	bool operator==(const Spawn& other) {
		return size == other.size;
	}
	bool operator!=(const Spawn& other) {
		return size != other.size;
	}

	void setSize(int newsize) {
		ASSERT(newsize >= 0 && newsize <= 255);
		size = newsize;
	}
	int getSize() const {
		return size;
	}

	void setSourceAttributes(SpawnAreaKind kind, const SpawnAttributeMap& attributes) {
		switch (kind) {
			case SpawnAreaKind::Monsters: monsterAttributes = attributes; hasMonsterSource = true; break;
			case SpawnAreaKind::Npcs: npcAttributes = attributes; hasNpcSource = true; break;
			case SpawnAreaKind::Mixed: mixedAttributes = attributes; hasMixedSource = true; break;
		}
	}
	bool hasSourceKind(SpawnAreaKind kind) const {
		if (kind == SpawnAreaKind::Monsters) {
			return hasMonsterSource;
		}
		if (kind == SpawnAreaKind::Npcs) {
			return hasNpcSource;
		}
		return hasMixedSource;
	}

	SpawnAttributeMap getSourceAttributes(SpawnAreaKind kind) const {
		if (kind == SpawnAreaKind::Monsters) {
			return monsterAttributes;
		}
		if (kind == SpawnAreaKind::Npcs) {
			return npcAttributes;
		}
		SpawnAttributeMap attributes = mixedAttributes;
		for (const auto& [name, value] : monsterAttributes) {
			attributes.try_emplace(name, value);
		}
		for (const auto& [name, value] : npcAttributes) {
			attributes.try_emplace(name, value);
		}
		return attributes;
	}

protected:
	int size;
	bool selected;
	SpawnAttributeMap mixedAttributes;
	SpawnAttributeMap monsterAttributes;
	SpawnAttributeMap npcAttributes;
	bool hasMixedSource = false;
	bool hasMonsterSource = false;
	bool hasNpcSource = false;
};

typedef std::set<Position> SpawnPositionList;
typedef std::list<Spawn*> SpawnList;

class Spawns {
public:
	Spawns();
	~Spawns();

	void addSpawn(Tile* tile);
	void removeSpawn(Tile* tile);

	SpawnPositionList::iterator begin() {
		return spawns.begin();
	}
	SpawnPositionList::const_iterator begin() const {
		return spawns.begin();
	}
	SpawnPositionList::iterator end() {
		return spawns.end();
	}
	SpawnPositionList::const_iterator end() const {
		return spawns.end();
	}
	void erase(SpawnPositionList::iterator iter) {
		spawns.erase(iter);
	}
	SpawnPositionList::iterator find(Position& pos) {
		return spawns.find(pos);
	}

private:
	SpawnPositionList spawns;
};

#endif
