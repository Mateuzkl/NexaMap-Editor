//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "zones.h"
#include "map.h"

Zones::~Zones() {
	zones.clear();
}

bool Zones::addZone(const std::string& name, unsigned int id) {
	if (hasZone(name)) {
		return false;
	}
	if (used_ids.find(id) != used_ids.end()) {
		return false;
	}
	zones.emplace(name, id);
	used_ids.insert(id);
	return true;
}

bool Zones::addZone(const std::string& name) {
	return addZone(name, generateID());
}

bool Zones::hasZone(const std::string& name) {
	return zones.find(name) != zones.end();
}

bool Zones::hasZone(unsigned int id) {
	return used_ids.find(id) != used_ids.end();
}

void Zones::removeZone(const std::string& name) {
	if (!hasZone(name)) {
		return;
	}
	used_ids.erase(zones[name]);
	zones.erase(name);
}

unsigned int Zones::generateID() {
	unsigned int id = 1;
	while (used_ids.find(id) != used_ids.end()) {
		++id;
	}
	return id;
}
