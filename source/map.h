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

#ifndef RME_MAP_H_
#define RME_MAP_H_

#include "basemap.h"
#include "tile.h"
#include "town.h"
#include "house.h"
#include "spawn.h"
#include "complexitem.h"
#include "waypoints.h"
#include "zones.h"
#include "templates.h"

#include <unordered_set>

inline bool IsRemovableDuplicatedItem(const Item* item) {
	if (!item || item->isGroundTile()) {
		return false;
	}

	if (item->isMoveable() && item->hasElevation()) {
		return false;
	}

	if (item->getActionID() > 0 || item->getUniqueID() > 0) {
		return false;
	}

	return true;
}

class Map : public BaseMap {
public:
	// ctor and dtor
	Map();
	~Map() override;

	// Operations on the entire map
	void cleanInvalidTiles(bool showdialog = false);
	void cleanDeletedZones(bool showdialog = false);
	Position getZonePosition(unsigned int zoneId);
	void convertHouseTiles(uint32_t fromId, uint32_t toId);

	bool convert(MapVersion to, bool showdialog = false);
	bool convert(const ConversionMap& cm, bool showdialog = false);

	// Query information about the map

	MapVersion getVersion() const;
	// Returns true if any change has been done since last save
	bool hasChanged() const;
	// Makes a change, doesn't matter what. Just so that it asks when saving (Also adds a * to the window title)
	bool doChange();
	// Clears any changes
	bool clearChanges();

	// Errors/warnings
	bool hasWarnings() const {
		return warnings.size() != 0;
	}
	const wxArrayString& getWarnings() const {
		return warnings;
	}
	bool hasError() const {
		return error.size() != 0;
	}
	const wxString& getError() const {
		return error;
	}

	// Mess with spawns
	bool addSpawn(Tile* spawn);
	void removeSpawn(Tile* tile);
	void removeSpawn(const Position& position) {
		removeSpawn(getTile(position));
	}

	// Returns all possible spawns on the target tile
	SpawnList getSpawnList(Tile* t);
	SpawnList getSpawnList(const Position& position) {
		return getSpawnList(getTile(position));
	}
	SpawnList getSpawnList(int32_t x, int32_t y, int32_t z) {
		return getSpawnList(getTile(x, y, z));
	}

	// Returns true if the map has been saved
	// ie. it knows which file it should be saved to
	bool hasFile() const;
	const std::string& getFilename() const {
		return filename;
	}
	const std::string& getName() const {
		return name;
	}
	void setName(const std::string& n) {
		name = n;
	}

	// Get map data
	int getWidth() const {
		return width;
	}
	int getHeight() const {
		return height;
	}
	const std::string& getMapDescription() const {
		return description;
	}
	const std::string& getHouseFilename() const {
		return housefile;
	}
	const std::string& getSpawnFilename() const {
		return spawnfile;
	}
	const std::string& getZoneFilename() const {
		return zonefile;
	}

	// Set some map data
	void setWidth(int new_width);
	void setHeight(int new_height);
	void setMapDescription(const std::string& new_description);
	void setHouseFilename(const std::string& new_housefile);
	void setSpawnFilename(const std::string& new_spawnfile);
	void setZoneFilename(const std::string& new_zonefile);

	void flagAsNamed() {
		unnamed = false;
	}

	// Returns true if the given (non-zero) unique id is already present somewhere on the map.
	bool hasUniqueId(uint16_t uid) const;

protected:
	// Loads a map
	bool open(const std::string identifier);

protected:
	void updateUniqueIds(Tile* old_tile, Tile* new_tile) override;
	void addUniqueId(uint16_t uid);
	void removeUniqueId(uint16_t uid);

	void removeSpawnInternal(Tile* tile);

	wxArrayString warnings;
	wxString error;

	std::string name; // The map name, NOT the same as filename
	std::string filename; // the maps filename
	std::string description; // The description of the map

	MapVersion mapVersion;

	// Map Width and Height - for info purposes
	uint16_t width, height;

	std::string spawnfile; // The maps spawnfile
	std::string housefile; // The housefile
	std::string waypointfile; // The waypoints file (stores extended waypoint information such as id, preferred icon and matching town)
	std::string zonefile; // The zonefile

public:
	Towns towns;
	Houses houses;
	Spawns spawns;

protected:
	bool has_changed; // If the map has changed
	bool unnamed; // If the map has yet to receive a name

	friend class IOMapOTBM;
	friend class Editor;

public:
	Waypoints waypoints;
	Zones zones;

private:
	std::vector<uint16_t> uniqueIds;
};

template <typename ForeachType>
inline void foreach_ItemOnMap(Map& map, ForeachType& foreach, bool selectedTiles) {
	MapIterator tileiter = map.begin();
	MapIterator const end = map.end();
	long long done = 0;

	while (tileiter != end) {
		++done;
		Tile* tile = (*tileiter)->get();
		if (selectedTiles && !tile->isSelected()) {
			++tileiter;
			continue;
		}

		if (tile->ground) {
			foreach (map, tile, tile->ground, done)
				;
		}

		std::queue<Container*> containers;
		for (ItemVector::iterator itemiter = tile->items.begin(); itemiter != tile->items.end(); ++itemiter) {
			Item* item = *itemiter;
			Container* container = dynamic_cast<Container*>(item);
			foreach (map, tile, item, done)
				;
			if (container) {
				containers.push(container);

				do {
					container = containers.front();
					ItemVector& v = container->getVector();
					for (ItemVector::iterator containeriter = v.begin(); containeriter != v.end(); ++containeriter) {
						Item* i = *containeriter;
						Container* c = dynamic_cast<Container*>(i);
						foreach (map, tile, i, done)
							;
						if (c) {
							containers.push(c);
						}
					}
					containers.pop();
				} while (containers.size());
			}
		}
		++tileiter;
	}
}

template <typename ForeachType>
inline void foreach_TileOnMap(Map& map, ForeachType& foreach) {
	MapIterator tileiter = map.begin();
	MapIterator const end = map.end();
	long long done = 0;

	while (tileiter != end) {
		foreach (map, (*tileiter++)->get(), ++done)
			;
	}
}

template <typename RemoveIfType>
inline long long remove_if_TileOnMap(Map& map, RemoveIfType& remove_if) {
	MapIterator tileiter = map.begin();
	MapIterator const end = map.end();
	long long done = 0;
	long long removed = 0;
	long long total = map.getTileCount();

	while (tileiter != end) {
		Tile* tile = (*tileiter)->get();
		if (remove_if(map, tile, removed, done, total)) {
			map.setTile(tile->getPosition(), nullptr, true);
			++removed;
		}
		++tileiter;
		++done;
	}

	return removed;
}

template <typename RemoveIfType>
inline int64_t RemoveItemOnMap(Map& map, RemoveIfType& condition, bool selectedOnly) {
	int64_t done = 0;
	int64_t removed = 0;

	MapIterator it = map.begin();
	MapIterator const end = map.end();

	while (it != end) {
		++done;
		Tile* tile = (*it)->get();
		if (selectedOnly && !tile->isSelected()) {
			++it;
			continue;
		}

		if (tile->ground) {
			if (condition(map, tile->ground, removed, done)) {
				delete tile->ground;
				tile->ground = nullptr;
				++removed;
			}
		}

		for (auto iit = tile->items.begin(); iit != tile->items.end();) {
			Item* item = *iit;
			if (condition(map, item, removed, done)) {
				iit = tile->items.erase(iit);
				delete item;
				++removed;
			} else {
				++iit;
			}
		}
		++it;
	}
	return removed;
}

template <typename RemoveIfType>
inline int64_t RemoveItemDuplicateOnMap(Map& map, RemoveIfType& condition, bool selectedOnly) {
	int64_t done = 0;
	int64_t removed = 0;

	MapIterator it = map.begin();
	MapIterator const end = map.end();

	while (it != end) {
		++done;
		Tile* tile = (*it)->get();
		if (selectedOnly && !tile->isSelected()) {
			++it;
			continue;
		}

		std::unordered_set<uint16_t> seenItemIDs;
		for (auto iit = tile->items.begin(); iit != tile->items.end();) {
			Item* item = *iit;
			const bool duplicated = !seenItemIDs.insert(item->getID()).second;
			if (duplicated && condition(map, tile, item, removed, done)) {
				iit = tile->items.erase(iit);
				delete item;
				++removed;
			} else {
				++iit;
			}
		}
		++it;
	}
	return removed;
}

int64_t RemoveMonstersOnMap(Map& map, bool selectedOnly);
std::pair<int64_t, std::unordered_map<std::string, int64_t>> CountMonstersOnMap(Map& map, bool selectedOnly);
int64_t EditMonsterSpawnTime(Map& map, bool selectedOnly, int32_t spawnTime);

#endif
