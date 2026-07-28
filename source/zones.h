//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_ZONES_H_
#define RME_ZONES_H_

#include <cstdint>
#include <limits>
#include <map>
#include <unordered_set>

class Map;

typedef std::map<std::string, unsigned int> ZoneMap;

class Zones {
public:
	explicit Zones(Map&) { }
	virtual ~Zones();

	unsigned int getZoneID(const std::string& name) const {
		auto it = zones.find(name);
		if (it == zones.end()) {
			return 0;
		}
		return it->second;
	}
	std::string getZoneName(unsigned int id) const;
	unsigned int getEmptyID() const;

	bool addZone(const std::string& name);
	bool addZone(const std::string& name, unsigned int id);
	bool renameZone(const std::string& oldName, const std::string& newName);
	bool hasZone(const std::string& name) const;
	bool hasZone(unsigned int id) const;
	bool removeZone(const std::string& name);

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
	std::unordered_set<unsigned int> used_ids;

	unsigned int generateID() const;
};

#endif
