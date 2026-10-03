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
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

class Waypoint {
public:
	std::string name;
	Position pos;
	std::string category;
};

using WaypointMap = std::map<std::string, std::unique_ptr<Waypoint>>;

class Waypoints {
public:
	explicit Waypoints(Map& map) :
		map_(map) { }

	bool addWaypoint(std::unique_ptr<Waypoint> waypoint, std::optional<size_t> orderIndex = std::nullopt);
	bool removeWaypoint(std::string_view name);
	bool renameWaypoint(std::string_view oldName, std::string newName);
	bool moveWaypoint(std::string_view name, const Position& newPosition);
	void clear();
	void importWaypointsFrom(Waypoints& source, const Position& offset);

	Waypoint* getWaypoint(std::string_view name);
	const Waypoint* getWaypoint(std::string_view name) const;
	Waypoint* getWaypoint(const TileLocation* location);
	const Waypoint* getWaypoint(const TileLocation* location) const;

	void clearGroups();
	bool addCategory(std::string name);
	bool removeCategory(std::string_view name);
	bool renameCategory(std::string_view oldName, std::string newName);
	bool setWaypointCategory(std::string_view waypointName, std::string category);
	bool hasGroups() const;
	void normalizeOrders();
	bool moveCategoryRelative(std::string_view category, std::string_view anchorCategory, bool insertBefore);
	bool moveWaypointRelative(std::string_view waypoint, std::string_view anchorWaypoint, bool insertBefore);
	bool moveWaypointIntoCategory(std::string_view waypoint, std::string_view category, bool atEnd);
	bool nudgeCategory(std::string_view category, bool moveUp);
	std::vector<std::string> orderedWaypointsInCategory(std::string_view category) const;
	std::optional<size_t> waypointOrderIndex(std::string_view waypointName) const;
	bool applyOrderingFromMetadata(
		const std::vector<std::string>& categoryOrder,
		const std::vector<std::string>& uncategorizedOrder,
		const std::map<std::string, std::vector<std::string>>& categoryOrders,
		const std::map<std::string, std::string>& waypointCategoryByName
	);
	bool validateInvariants(std::string* error = nullptr) const;

	size_t size() const noexcept {
		return waypoints_.size();
	}
	bool empty() const noexcept {
		return waypoints_.empty();
	}
	const std::vector<std::string>& categories() const noexcept {
		return categories_;
	}
	const std::vector<std::string>& uncategorizedOrder() const noexcept {
		return uncategorizedOrder_;
	}
	const std::map<std::string, std::vector<std::string>>& categoryWaypointOrders() const noexcept {
		return categoryWaypointOrder_;
	}

	WaypointMap::const_iterator begin() const noexcept {
		return waypoints_.begin();
	}
	WaypointMap::const_iterator end() const noexcept {
		return waypoints_.end();
	}
	WaypointMap::const_iterator begin() noexcept {
		return waypoints_.cbegin();
	}
	WaypointMap::const_iterator end() noexcept {
		return waypoints_.cend();
	}

private:
	using PositionIndex = std::map<Position, std::vector<Waypoint*>>;

	void decrementTileCount(const Position& position);
	void incrementTileCount(const Position& position);
	void removeWaypointFromPositionIndex(Waypoint* waypoint, const Position& position);
	void indexWaypointPosition(Waypoint* waypoint);
	void unregisterWaypointOrder(std::string_view name);
	void registerWaypointOrder(Waypoint& waypoint, std::optional<size_t> orderIndex = std::nullopt);
	std::unique_ptr<Waypoint> extractWaypoint(std::string_view name);
	bool validateMetadata(
		const std::vector<std::string>& categoryOrder,
		const std::vector<std::string>& uncategorizedOrder,
		const std::map<std::string, std::vector<std::string>>& categoryOrders,
		const std::map<std::string, std::string>& waypointCategoryByName,
		std::string* error
	) const;

	Map& map_;
	WaypointMap waypoints_;
	PositionIndex waypointByPosition_;
	std::vector<std::string> categories_;
	std::vector<std::string> uncategorizedOrder_;
	std::map<std::string, std::vector<std::string>> categoryWaypointOrder_;
};

#endif
