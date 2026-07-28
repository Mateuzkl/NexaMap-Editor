//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include <cstdint>
#include <limits>

#include "zones.h"

bool Zones::isValidName(const std::string& name) {
	return !name.empty() && name.find_first_not_of(" \t\r\n") != std::string::npos;
}

bool Zones::isValidID(unsigned int id) {
	return id != 0 && id <= std::numeric_limits<uint16_t>::max();
}

bool Zones::addZone(const std::string& name, unsigned int id) {
	if (!isValidName(name) || !isValidID(id) || hasZone(name)) {
		return false;
	}
	if (used_ids.find(id) != used_ids.end()) {
		return false;
	}
	zones.emplace(name, id);
	used_ids.insert(id);
	return true;
}

bool Zones::renameZone(const std::string& oldName, const std::string& newName) {
	if (!isValidName(oldName) || !isValidName(newName)) {
		return false;
	}

	auto oldIt = zones.find(oldName);
	if (oldIt == zones.end()) {
		return false;
	}
	if (oldName == newName) {
		return true;
	}
	if (zones.find(newName) != zones.end()) {
		return false;
	}

	const unsigned int id = oldIt->second;
	zones.erase(oldIt);
	zones.emplace(newName, id);
	return true;
}

bool Zones::hasZone(const std::string& name) const {
	return zones.find(name) != zones.end();
}

bool Zones::hasZone(unsigned int id) const {
	return used_ids.find(id) != used_ids.end();
}

bool Zones::hasZone(const std::string& name, unsigned int id) const {
	auto it = zones.find(name);
	return it != zones.end() && it->second == id;
}

bool Zones::removeZone(const std::string& name) {
	auto it = zones.find(name);
	if (it == zones.end()) {
		return false;
	}
	used_ids.erase(it->second);
	zones.erase(it);
	return true;
}

std::string Zones::getZoneName(unsigned int id) const {
	for (const auto& zone : zones) {
		if (zone.second == id) {
			return zone.first;
		}
	}
	return {};
}

unsigned int Zones::getEmptyID() const {
	return generateID();
}

unsigned int Zones::generateID() const {
	for (unsigned int id = 1; id <= std::numeric_limits<uint16_t>::max(); ++id) {
		if (used_ids.find(id) == used_ids.end()) {
			return id;
		}
	}
	return 0;
}
