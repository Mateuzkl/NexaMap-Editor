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

#ifndef RME_WAYPOINTS_H_
#define RME_WAYPOINTS_H_

#include "position.h"

#include <map>
#include <set>
#include <vector>

class Waypoint {
public:
	std::string name;
	Position pos;
	std::string category;
};

typedef std::map<std::string, Waypoint*> WaypointMap;

class Waypoints {
	Map& map;

public:
	Waypoints(Map& map) :
		map(map) { }
	~Waypoints() {
		for (WaypointMap::iterator iter = waypoints.begin(); iter != waypoints.end(); ++iter) {
			delete iter->second;
		}
	}

	void addWaypoint(Waypoint* wp);
	Waypoint* getWaypoint(std::string name);
	const Waypoint* getWaypoint(std::string name) const;
	Waypoint* getWaypoint(const TileLocation* location);
	void removeWaypoint(std::string name);

	void clearGroups();
	void addCategory(const std::string& name);
	void removeCategory(const std::string& name);
	bool renameCategory(const std::string& oldName, const std::string& newName);
	void setWaypointCategory(const std::string& waypointName, const std::string& category);
	bool hasGroups() const;
	void syncWaypointOrders();
	void registerWaypointOrder(const Waypoint* wp);
	void unregisterWaypointOrder(const std::string& name);
	void renameInOrders(const std::string& oldName, const std::string& newName);
	bool moveCategoryRelative(const std::string& category, const std::string& anchorCategory, bool insertBefore);
	bool moveWaypointRelative(const std::string& waypoint, const std::string& anchorWaypoint, bool insertBefore);
	bool moveWaypointIntoCategory(const std::string& waypoint, const std::string& category, bool atEnd);
	bool nudgeCategory(const std::string& category, bool moveUp);
	std::vector<std::string> orderedWaypointsInCategory(const std::string& category) const;
	void notifyWaypointPositionChanged(Waypoint* wp, const Position& oldPos);
	bool applyOrderingFromMetadata(
		const std::vector<std::string>& categoryOrder,
		const std::vector<std::string>& uncategorizedOrder,
		const std::map<std::string, std::vector<std::string>>& categoryOrders,
		const std::map<std::string, std::string>& waypointCategoryByName
	);
	bool validateInvariants(std::string* error = nullptr) const;

	WaypointMap waypoints;
	std::map<Position, Waypoint*> waypoint_by_position;
	std::vector<std::string> categories;
	std::vector<std::string> uncategorized_order;
	std::map<std::string, std::vector<std::string>> category_waypoint_order;

	WaypointMap::iterator begin() {
		return waypoints.begin();
	}
	WaypointMap::const_iterator begin() const {
		return waypoints.begin();
	}
	WaypointMap::iterator end() {
		return waypoints.end();
	}
	WaypointMap::const_iterator end() const {
		return waypoints.end();
	}

private:
	void removeWaypointFromPositionIndex(Waypoint* wp);
	void indexWaypointPosition(Waypoint* wp);
};

#endif
