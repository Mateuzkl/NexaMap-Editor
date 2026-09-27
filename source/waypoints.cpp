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

#include <algorithm>
#include <set>

#include "waypoints.h"
#include "map.h"

namespace {
void removeFromVector(std::vector<std::string>& values, const std::string& name) {
	values.erase(std::remove(values.begin(), values.end(), name), values.end());
}
} // namespace

void Waypoints::addWaypoint(Waypoint* wp) {
	removeWaypoint(wp->name);
	if (wp->pos != Position()) {
		Tile* t = map.getTile(wp->pos);
		if (!t) {
			map.setTile(wp->pos, t = map.allocator(map.createTileL(wp->pos)));
		}
		t->getLocation()->increaseWaypointCount();
	}
	waypoints.insert(std::make_pair(as_lower_str(wp->name), wp));
	registerWaypointOrder(wp);
}

Waypoint* Waypoints::getWaypoint(std::string name) {
	to_lower_str(name);
	auto iter = waypoints.find(name);
	if (iter == waypoints.end()) {
		return nullptr;
	}
	return iter->second;
}

Waypoint* Waypoints::getWaypoint(const TileLocation* location) {
	if (!location) {
		return nullptr;
	}
	// TODO find waypoint by position hash.
	for (auto it = waypoints.begin(); it != waypoints.end(); it++) {
		Waypoint* waypoint = it->second;
		if (waypoint && waypoint->pos == location->position) {
			return waypoint;
		}
	}
	return nullptr;
}

void Waypoints::removeWaypoint(std::string name) {
	to_lower_str(name);
	auto iter = waypoints.find(name);
	if (iter == waypoints.end()) {
		return;
	}
	if (iter->second) {
		unregisterWaypointOrder(iter->second->name);
	}
	delete iter->second;
	waypoints.erase(iter);
}

void Waypoints::clearGroups() {
	categories.clear();
	uncategorized_order.clear();
	category_waypoint_order.clear();
	for (auto& entry : waypoints) {
		if (entry.second) {
			entry.second->category.clear();
		}
	}
}

void Waypoints::addCategory(const std::string& name) {
	if (name.empty()) {
		return;
	}
	for (const auto& existing : categories) {
		if (existing == name) {
			return;
		}
	}
	categories.push_back(name);
	category_waypoint_order[name];
}

void Waypoints::removeCategory(const std::string& name) {
	categories.erase(
		std::remove(categories.begin(), categories.end(), name),
		categories.end()
	);
	const auto found = category_waypoint_order.find(name);
	if (found != category_waypoint_order.end()) {
		for (const auto& waypointName : found->second) {
			if (Waypoint* wp = getWaypoint(waypointName)) {
				wp->category.clear();
				uncategorized_order.push_back(wp->name);
			}
		}
		category_waypoint_order.erase(found);
	}
	for (auto& entry : waypoints) {
		if (entry.second && entry.second->category == name) {
			entry.second->category.clear();
		}
	}
}

bool Waypoints::renameCategory(const std::string& oldName, const std::string& newName) {
	if (oldName.empty() || newName.empty() || oldName == newName) {
		return false;
	}
	for (const auto& existing : categories) {
		if (existing == newName) {
			return false;
		}
	}
	for (auto& category : categories) {
		if (category == oldName) {
			category = newName;
			for (auto& entry : waypoints) {
				if (entry.second && entry.second->category == oldName) {
					entry.second->category = newName;
				}
			}
			if (auto found = category_waypoint_order.find(oldName); found != category_waypoint_order.end()) {
				category_waypoint_order[newName] = std::move(found->second);
				category_waypoint_order.erase(found);
			}
			return true;
		}
	}
	return false;
}

void Waypoints::setWaypointCategory(const std::string& waypointName, const std::string& category) {
	if (Waypoint* wp = getWaypoint(waypointName)) {
		unregisterWaypointOrder(wp->name);
		wp->category = category;
		if (!category.empty()) {
			addCategory(category);
		}
		registerWaypointOrder(wp);
	}
}

bool Waypoints::hasGroups() const {
	if (!categories.empty() || !uncategorized_order.empty() || !category_waypoint_order.empty()) {
		return true;
	}
	for (const auto& entry : waypoints) {
		if (entry.second && !entry.second->category.empty()) {
			return true;
		}
	}
	return false;
}

void Waypoints::unregisterWaypointOrder(const std::string& name) {
	removeFromVector(uncategorized_order, name);
	for (auto& entry : category_waypoint_order) {
		removeFromVector(entry.second, name);
	}
}

void Waypoints::registerWaypointOrder(const Waypoint* wp) {
	if (!wp) {
		return;
	}
	std::vector<std::string>* order = nullptr;
	if (wp->category.empty()) {
		order = &uncategorized_order;
	} else {
		addCategory(wp->category);
		order = &category_waypoint_order[wp->category];
	}
	if (!wp->name.empty()) {
		if (std::find(order->begin(), order->end(), wp->name) != order->end()) {
			return;
		}
		unregisterWaypointOrder(wp->name);
	} else {
		removeFromVector(*order, std::string());
	}
	order->push_back(wp->name);
}

void Waypoints::renameInOrders(const std::string& oldName, const std::string& newName) {
	for (auto& name : uncategorized_order) {
		if (name == oldName) {
			name = newName;
		}
	}
	for (auto& entry : category_waypoint_order) {
		for (auto& name : entry.second) {
			if (name == oldName) {
				name = newName;
			}
		}
	}
}

void Waypoints::syncWaypointOrders() {
	std::set<std::string> listed;
	for (const auto& name : uncategorized_order) {
		listed.insert(as_lower_str(name));
	}
	for (const auto& entry : category_waypoint_order) {
		for (const auto& name : entry.second) {
			listed.insert(as_lower_str(name));
		}
	}
	for (const auto& entry : waypoints) {
		if (!entry.second) {
			continue;
		}
		if (listed.count(as_lower_str(entry.second->name)) != 0) {
			continue;
		}
		registerWaypointOrder(entry.second);
	}
}

std::vector<std::string> Waypoints::orderedWaypointsInCategory(const std::string& category) const {
	if (category.empty()) {
		return uncategorized_order;
	}
	const auto found = category_waypoint_order.find(category);
	if (found != category_waypoint_order.end()) {
		return found->second;
	}
	return {};
}

bool Waypoints::moveCategoryRelative(const std::string& category, const std::string& anchorCategory, bool insertBefore) {
	if (category.empty() || anchorCategory.empty() || category == anchorCategory) {
		return false;
	}
	auto src = std::find(categories.begin(), categories.end(), category);
	auto anchor = std::find(categories.begin(), categories.end(), anchorCategory);
	if (src == categories.end() || anchor == categories.end()) {
		return false;
	}
	categories.erase(src);
	anchor = std::find(categories.begin(), categories.end(), anchorCategory);
	if (insertBefore) {
		categories.insert(anchor, category);
	} else {
		categories.insert(std::next(anchor), category);
	}
	return true;
}

bool Waypoints::moveWaypointRelative(const std::string& waypoint, const std::string& anchorWaypoint, bool insertBefore) {
	Waypoint* wp = getWaypoint(waypoint);
	Waypoint* anchor = getWaypoint(anchorWaypoint);
	if (!wp || !anchor || wp == anchor) {
		return false;
	}
	unregisterWaypointOrder(wp->name);
	wp->category = anchor->category;
	auto& order = wp->category.empty() ? uncategorized_order : category_waypoint_order[wp->category];
	auto position = std::find(order.begin(), order.end(), anchor->name);
	if (position == order.end()) {
		order.push_back(wp->name);
		return true;
	}
	if (insertBefore) {
		order.insert(position, wp->name);
	} else {
		order.insert(std::next(position), wp->name);
	}
	return true;
}

bool Waypoints::moveWaypointIntoCategory(const std::string& waypoint, const std::string& category, bool atEnd) {
	Waypoint* wp = getWaypoint(waypoint);
	if (!wp) {
		return false;
	}
	unregisterWaypointOrder(wp->name);
	wp->category = category;
	if (!category.empty()) {
		addCategory(category);
	}
	auto& order = category.empty() ? uncategorized_order : category_waypoint_order[category];
	if (atEnd) {
		order.push_back(wp->name);
	} else {
		order.insert(order.begin(), wp->name);
	}
	return true;
}

bool Waypoints::nudgeCategory(const std::string& category, bool moveUp) {
	if (category.empty()) {
		return false;
	}
	auto iter = std::find(categories.begin(), categories.end(), category);
	if (iter == categories.end()) {
		return false;
	}
	if (moveUp) {
		if (iter == categories.begin()) {
			return false;
		}
		std::iter_swap(iter, std::prev(iter));
	} else {
		if (std::next(iter) == categories.end()) {
			return false;
		}
		std::iter_swap(iter, std::next(iter));
	}
	return true;
}
