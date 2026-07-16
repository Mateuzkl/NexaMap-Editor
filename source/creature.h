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

#ifndef RME_CREATURE_H_
#define RME_CREATURE_H_

#include "creatures.h"
#include "position.h"
#include "spawn_format.h"

enum Direction {
	NORTH = 0,
	EAST = 1,
	SOUTH = 2,
	WEST = 3,

	DIRECTION_FIRST = NORTH,
	DIRECTION_LAST = WEST
};

// NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) - Direction(LAST+1) is the loop end-sentinel, never indexed.
IMPLEMENT_INCREMENT_OP(Direction)

class Creature {
public:
	Creature(const CreatureType* ctype);
	Creature(std::string type_name);
	~Creature();

	// Static conversions
	static std::string DirID2Name(uint16_t id);

	Creature* deepCopy() const;

	const Outfit& getLookType() const;

	bool isSaved();
	void save();
	void reset();

	bool isSelected() const {
		return selected;
	}
	void deselect() {
		selected = false;
	}
	void select() {
		selected = true;
	}

	bool isNpc() const;

	std::string getName() const;
	CreatureBrush* getBrush() const;

	int getSpawnTime() const {
		return spawntime;
	}
	void setSpawnTime(int spawntime_) {
		this->spawntime = spawntime_;
	}

	uint32_t getWeight() const {
		return weight;
	}
	void setWeight(int weight_) {
		this->weight = static_cast<uint32_t>(std::max(0, weight_));
		spawn_weight_explicit = true;
	}
	void setSpawnWeight(uint32_t weight_, bool explicitWeight) {
		weight = weight_;
		spawn_weight_explicit = explicitWeight;
	}
	bool hasSpawnWeight() const {
		return spawn_weight_explicit;
	}

	Direction getDirection() const {
		return direction;
	}
	void setDirection(Direction direction_) {
		this->direction = direction_;
		spawn_direction_explicit = true;
	}
	void setSpawnDirection(Direction direction_, bool explicitDirection) {
		direction = direction_;
		spawn_direction_explicit = explicitDirection;
	}
	bool hasSpawnDirection() const {
		return spawn_direction_explicit;
	}

	void setSpawnType(bool npc) {
		spawn_type_override = true;
		spawn_is_npc = npc;
	}
	void setSpawnAttributes(const SpawnAttributeMap& attributes) {
		spawn_attributes = attributes;
	}
	const SpawnAttributeMap& getSpawnAttributes() const {
		return spawn_attributes;
	}
	void setAlternativeKind(SpawnAlternativeKind kind) {
		alternative_kind = kind;
	}
	SpawnAlternativeKind getAlternativeKind() const {
		return alternative_kind;
	}
	void addSpawnAlternative(const SpawnVariantData& alternative) {
		spawn_alternatives.push_back(alternative);
	}
	const std::vector<SpawnVariantData>& getSpawnAlternatives() const {
		return spawn_alternatives;
	}
	void setSpawnSource(const Position& position) {
		spawn_source = position;
		has_spawn_source = true;
	}
	bool hasSpawnSource() const {
		return has_spawn_source;
	}
	const Position& getSpawnSource() const {
		return spawn_source;
	}

protected:
	std::string type_name;
	Direction direction;
	int spawntime;
	uint32_t weight;
	bool saved;
	bool selected;
	bool spawn_weight_explicit = false;
	bool spawn_direction_explicit = true;
	bool spawn_type_override = false;
	bool spawn_is_npc = false;
	bool has_spawn_source = false;
	Position spawn_source;
	SpawnAttributeMap spawn_attributes;
	SpawnAlternativeKind alternative_kind = SpawnAlternativeKind::None;
	std::vector<SpawnVariantData> spawn_alternatives;
};

inline void Creature::save() {
	saved = true;
}

inline void Creature::reset() {
	saved = false;
}

inline bool Creature::isSaved() {
	return saved;
}

inline bool Creature::isNpc() const {
	if (spawn_type_override) {
		return spawn_is_npc;
	}
	CreatureType const* type = g_creatures[type_name];
	if (type) {
		return type->isNpc;
	}
	return false;
}

inline std::string Creature::getName() const {
	CreatureType const* type = g_creatures[type_name];
	if (type) {
		return type->name;
	}
	return "";
}
inline CreatureBrush* Creature::getBrush() const {
	CreatureType const* type = g_creatures[type_name];
	if (type) {
		return type->brush;
	}
	return nullptr;
}

typedef std::vector<Creature*> CreatureVector;
typedef std::list<Creature*> CreatureList;

#endif
