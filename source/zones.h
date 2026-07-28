//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_ZONES_H_
#define RME_ZONES_H_

#include <map>
#include <string>
#include <unordered_set>

typedef std::map<std::string, unsigned int> ZoneMap;

class Zones {
public:
	Zones() = default;
	virtual ~Zones() = default;

	unsigned int getZoneID(const std::string& name) const {
		auto it = zones.find(name);
		if (it == zones.end()) {
			return 0;
		}
		return it->second;
	}
	std::string getZoneName(unsigned int id) const;
	unsigned int getEmptyID() const;

	static bool isValidName(const std::string& name);
	static bool isValidID(unsigned int id);

	bool addZone(const std::string& name, unsigned int id);
	bool renameZone(const std::string& oldName, const std::string& newName);
	bool hasZone(const std::string& name) const;
	bool hasZone(unsigned int id) const;
	bool hasZone(const std::string& name, unsigned int id) const;
	bool removeZone(const std::string& name);

	bool empty() const {
		return zones.empty();
	}
	ZoneMap::size_type size() const {
		return zones.size();
	}

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
	ZoneMap zones;
	std::unordered_set<unsigned int> used_ids;

	unsigned int generateID() const;
};

#endif
