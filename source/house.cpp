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

#include "complexitem.h" // Door
#include "house.h"
#include "tile.h"
#include "map.h"

Houses::Houses(Map& map) :
	map(map),
	max_house_id(0) {
	////
}

Houses::~Houses() {
	for (auto it = houses.begin(); it != houses.end(); ++it) {
		delete it->second;
	}
}

uint32_t Houses::getEmptyID() {
	// Never use operator[] for a membership test: it inserts a nullptr entry and
	// makes the registry look as if the id were occupied.
	uint32_t candidate = max_house_id == std::numeric_limits<uint32_t>::max() ? 1 : max_house_id + 1;
	while (houses.find(candidate) != houses.end()) {
		candidate = candidate == std::numeric_limits<uint32_t>::max() ? 1 : candidate + 1;
	}
	max_house_id = candidate;
	return candidate;
}

bool Houses::addHouse(House* new_house) {
	if (!new_house || new_house->id == 0) {
		return false;
	}
	auto it = houses.find(new_house->id);
	if (it != houses.end()) {
		return false;
	}
	new_house->map = &map;
	if (new_house->id > max_house_id) {
		max_house_id = new_house->id;
	}
	houses.emplace(new_house->id, new_house);
	return true;
}

void Houses::removeHouse(House* house_to_remove) {
	if (!house_to_remove) {
		return;
	}
	auto it = houses.find(house_to_remove->id);
	if (it == houses.end() || it->second != house_to_remove) {
		return;
	}
	houses.erase(it);

	house_to_remove->clean();
	delete house_to_remove;
}

bool Houses::changeId(House* house, uint32_t newID) {
	if (!house || newID == 0) {
		return false;
	}
	auto it = houses.find(house->id);
	if (it == houses.end() || it->second != house) {
		return false;
	}
	if (newID == house->id) {
		return true;
	}
	if (houses.find(newID) != houses.end()) {
		return false;
	}

	Tile* exit_tile = map.getTile(house->exit);
	if (exit_tile) {
		exit_tile->removeHouseExit(house);
	}
	houses.erase(it);
	house->setID(newID);
	houses.emplace(newID, house);
	if (exit_tile) {
		exit_tile->addHouseExit(house);
	}

	// id list structure changed, prepare search for new free slot
	max_house_id = 0;
	return true;
}

House* Houses::getHouse(uint32_t houseid) {
	auto it = houses.find(houseid);
	if (it != houses.end()) {
		return it->second;
	}
	return nullptr;
}

const House* Houses::getHouse(uint32_t houseid) const {
	auto it = houses.find(houseid);
	if (it != houses.end()) {
		return it->second;
	}

	return nullptr;
}

House::House(Map& map) :
	rent(0),
	requiredReset(0),
	clientid(0),
	beds(0),
	townid(0),
	guildhall(false),
	id(0),
	map(&map),
	exit(0, 0, 0) {
	////
}

House::~House() {
	////
}

HouseSnapshot House::getSnapshot() const {
	HouseSnapshot snapshot;
	snapshot.id = id;
	snapshot.rent = rent;
	snapshot.requiredReset = requiredReset;
	snapshot.clientid = clientid;
	snapshot.beds = beds;
	snapshot.name = name;
	snapshot.townid = townid;
	snapshot.guildhall = guildhall;
	snapshot.exit = exit;
	return snapshot;
}

void House::applySnapshot(const HouseSnapshot& snapshot) {
	id = snapshot.id;
	rent = snapshot.rent;
	requiredReset = snapshot.requiredReset;
	clientid = snapshot.clientid;
	beds = snapshot.beds;
	name = snapshot.name;
	townid = snapshot.townid;
	guildhall = snapshot.guildhall;
	exit = Position();
}

void House::clean() {
	for (PositionList::const_iterator pos_iter = tiles.begin(); pos_iter != tiles.end(); ++pos_iter) {
		Tile* tile = map->getTile(*pos_iter);
		if (tile && tile->getHouseID() == id) {
			tile->setHouse(nullptr);
		}
	}
	tiles.clear();

	Tile* tile = map->getTile(exit);
	if (tile) {
		tile->removeHouseExit(this);
	}
	exit = Position();
}

size_t House::size() const {
	size_t count = 0;
	for (auto pos_iter = tiles.begin(); pos_iter != tiles.end(); ++pos_iter) {
		const Tile* tile = map->getTile(*pos_iter);
		if (tile && tile->getHouseID() == id && !tile->isBlocking()) {
			++count;
		}
	}
	return count;
}

void House::addTile(Tile* tile) {
	if (!tile) {
		return;
	}
	tile->setHouse(this);
	const Position position = tile->getPosition();
	if (std::find(tiles.begin(), tiles.end(), position) == tiles.end()) {
		tiles.push_back(position);
	}
}

void House::removeTile(Tile* tile) {
	if (!tile) {
		return;
	}
	auto it = std::find_if(tiles.begin(), tiles.end(), [&](const Position& pos) {
		return pos == tile->getPosition();
	});
	if (it != tiles.end()) {
		tiles.erase(it);
		if (tile->getHouseID() == id) {
			tile->setHouse(nullptr);
		}
	}
}

uint8_t House::getEmptyDoorID() const {
	std::set<uint8_t> taken;
	for (auto tile_iter = tiles.begin(); tile_iter != tiles.end(); ++tile_iter) {
		if (const Tile* tile = map->getTile(*tile_iter)) {
			if (tile->getHouseID() != id) {
				continue;
			}
			for (auto item_iter = tile->items.begin(); item_iter != tile->items.end(); ++item_iter) {
				if (const auto* door = dynamic_cast<Door*>(*item_iter)) {
					taken.insert(door->getDoorID());
				}
			}
		}
	}

	for (int i = 1; i < 256; ++i) {
		auto it = taken.find(uint8_t(i));
		if (it == taken.end()) {
			// Free ID!
			return i;
		}
	}
	return 255;
}

Position House::getDoorPositionByID(uint8_t id_) const {
	for (auto tile_iter = tiles.begin(); tile_iter != tiles.end(); ++tile_iter) {
		if (const Tile* tile = map->getTile(*tile_iter)) {
			if (tile->getHouseID() != id) {
				continue;
			}
			for (auto item_iter = tile->items.begin(); item_iter != tile->items.end(); ++item_iter) {
				if (const auto* door = dynamic_cast<Door*>(*item_iter)) {
					if (door->getDoorID() == id_) {
						return *tile_iter;
					}
				}
			}
		}
	}
	return Position();
}

std::string House::getDescription() const {
	std::ostringstream os;
	os << name;
	os << " (ID:" << id << "; Rent: " << rent << ")";
	return os.str();
}

void House::setExit(Map* targetmap, const Position& pos) {
	if (pos == exit) {
		return;
	}
	if (!targetmap) {
		targetmap = map;
	}

	if (exit != Position()) {
		Tile* oldexit = targetmap->getTile(exit);
		if (oldexit) {
			oldexit->removeHouseExit(this);
		}
	}
	exit = Position();

	if (pos == Position()) {
		return;
	}

	Tile* newexit = targetmap->getTile(pos);
	if (!newexit) {
		newexit = targetmap->allocator(targetmap->createTileL(pos));
		targetmap->setTile(pos, newexit);
	}

	newexit->addHouseExit(this);
	exit = pos;
}

void House::setExit(const Position& pos) {
	setExit(map, pos);
}
