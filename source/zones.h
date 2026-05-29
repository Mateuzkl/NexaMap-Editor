//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_ZONES_H_
#define RME_ZONES_H_

#include <map>
#include <unordered_set>

class Map;

typedef std::map<std::string, unsigned int> ZoneMap;

class Zones {
public:
	Zones(Map& map) :
		map(map) { }
	virtual ~Zones();

	unsigned int getZoneID(std::string name) const {
		auto it = zones.find(name);
		if (it == zones.end()) {
			return 0;
		}
		return it->second;
	}
	bool addZone(const std::string& name);
	bool addZone(const std::string& name, unsigned int id);
	bool hasZone(const std::string& name);
	bool hasZone(unsigned int id);
	void removeZone(const std::string& name);

	ZoneMap zones;

	ZoneMap::iterator begin() {
		return zones.begin();
	}
	ZoneMap::const_iterator begin() const {
		return zones.begin();
	}
	ZoneMap::iterator end() {
		return zones.end();
	}
	ZoneMap::const_iterator end() const {
		return zones.end();
	}

private:
	Map& map;
	std::unordered_set<unsigned int> used_ids;

	unsigned int generateID();
};

#endif
