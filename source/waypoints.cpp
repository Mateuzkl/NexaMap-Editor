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
#include <utility>

#include "map.h"
#include "waypoints.h"

namespace {
	void removeFromVector(std::vector<std::string>& values, std::string_view name) {
		const std::string key = as_lower_str(std::string(name));
		values.erase(
			std::remove_if(values.begin(), values.end(), [&](const std::string& value) {
				return as_lower_str(value) == key;
			}),
			values.end()
		);
	}

	bool insertCaseInsensitive(std::set<std::string>& values, std::string_view value) {
		return values.insert(as_lower_str(std::string(value))).second;
	}

	const std::string* findCategory(
		const std::vector<std::string>& categories,
		std::string_view category
	) {
		const std::string key = as_lower_str(std::string(category));
		const auto found = std::find_if(categories.begin(), categories.end(), [&](const std::string& candidate) {
			return as_lower_str(candidate) == key;
		});
		return found == categories.end() ? nullptr : &*found;
	}
} // namespace

void Waypoints::incrementTileCount(const Position& position) {
	if (!position.isValid()) {
		return;
	}
	Tile* tile = map_.getTile(position);
	if (!tile) {
		tile = map_.allocator(map_.createTileL(position));
		map_.setTile(position, tile);
	}
	tile->getLocation()->increaseWaypointCount();
}

void Waypoints::decrementTileCount(const Position& position) {
	if (!position.isValid()) {
		return;
	}
	if (TileLocation* location = map_.getTileL(position); location && location->getWaypointCount() > 0) {
		location->decreaseWaypointCount();
	}
}

void Waypoints::removeWaypointFromPositionIndex(Waypoint* waypoint, const Position& position) {
	if (!waypoint || !position.isValid()) {
		return;
	}
	const auto found = waypointByPosition_.find(position);
	if (found == waypointByPosition_.end()) {
		return;
	}
	auto& bucket = found->second;
	bucket.erase(std::remove(bucket.begin(), bucket.end(), waypoint), bucket.end());
	if (bucket.empty()) {
		waypointByPosition_.erase(found);
	}
}

void Waypoints::indexWaypointPosition(Waypoint* waypoint) {
	if (!waypoint || !waypoint->pos.isValid()) {
		return;
	}
	auto& bucket = waypointByPosition_[waypoint->pos];
	if (std::find(bucket.begin(), bucket.end(), waypoint) == bucket.end()) {
		bucket.push_back(waypoint);
	}
}

bool Waypoints::addWaypoint(std::unique_ptr<Waypoint> waypoint, std::optional<size_t> orderIndex) {
	if (!waypoint || waypoint->name.empty() || !waypoint->pos.isValid()) {
		return false;
	}
	const std::string key = as_lower_str(waypoint->name);
	if (waypoints_.contains(key)) {
		return false;
	}
	if (!waypoint->category.empty()) {
		if (const std::string* category = findCategory(categories_, waypoint->category)) {
			waypoint->category = *category;
		} else {
			addCategory(waypoint->category);
		}
	}
	Waypoint* observer = waypoint.get();
	waypoints_.emplace(key, std::move(waypoint));
	incrementTileCount(observer->pos);
	indexWaypointPosition(observer);
	registerWaypointOrder(*observer, orderIndex);
	return true;
}

std::unique_ptr<Waypoint> Waypoints::extractWaypoint(std::string_view name) {
	const std::string key = as_lower_str(std::string(name));
	const auto found = waypoints_.find(key);
	if (found == waypoints_.end()) {
		return nullptr;
	}
	Waypoint* waypoint = found->second.get();
	unregisterWaypointOrder(waypoint->name);
	removeWaypointFromPositionIndex(waypoint, waypoint->pos);
	decrementTileCount(waypoint->pos);
	auto owned = std::move(found->second);
	waypoints_.erase(found);
	return owned;
}

bool Waypoints::removeWaypoint(std::string_view name) {
	return static_cast<bool>(extractWaypoint(name));
}

bool Waypoints::renameWaypoint(std::string_view oldName, std::string newName) {
	if (newName.empty()) {
		return false;
	}
	const std::string oldKey = as_lower_str(std::string(oldName));
	const std::string newKey = as_lower_str(newName);
	auto found = waypoints_.find(oldKey);
	if (found == waypoints_.end()) {
		return false;
	}
	if (oldKey != newKey && waypoints_.contains(newKey)) {
		return false;
	}

	Waypoint* waypoint = found->second.get();
	const std::string previousName = waypoint->name;
	for (auto& name : uncategorizedOrder_) {
		if (as_lower_str(name) == oldKey) {
			name = newName;
		}
	}
	for (auto& [category, order] : categoryWaypointOrder_) {
		for (auto& name : order) {
			if (as_lower_str(name) == oldKey) {
				name = newName;
			}
		}
	}
	waypoint->name = std::move(newName);
	if (oldKey != newKey) {
		auto node = waypoints_.extract(found);
		node.key() = newKey;
		waypoints_.insert(std::move(node));
	}
	return waypoint->name != previousName;
}

bool Waypoints::moveWaypoint(std::string_view name, const Position& newPosition) {
	Waypoint* waypoint = getWaypoint(name);
	if (!waypoint || !newPosition.isValid() || waypoint->pos == newPosition) {
		return false;
	}
	const Position oldPosition = waypoint->pos;
	removeWaypointFromPositionIndex(waypoint, oldPosition);
	decrementTileCount(oldPosition);
	waypoint->pos = newPosition;
	incrementTileCount(newPosition);
	indexWaypointPosition(waypoint);
	return true;
}

void Waypoints::clear() {
	while (!waypoints_.empty()) {
		extractWaypoint(waypoints_.begin()->first);
	}
	waypointByPosition_.clear();
	categories_.clear();
	uncategorizedOrder_.clear();
	categoryWaypointOrder_.clear();
}

void Waypoints::importWaypointsFrom(Waypoints& source, const Position& offset) {
	if (&source == this) {
		return;
	}
	source.normalizeOrders();
	for (const auto& category : source.categories_) {
		addCategory(category);
	}

	std::vector<std::string> orderedNames = source.uncategorizedOrder_;
	for (const auto& category : source.categories_) {
		if (const auto found = source.categoryWaypointOrder_.find(category); found != source.categoryWaypointOrder_.end()) {
			orderedNames.insert(orderedNames.end(), found->second.begin(), found->second.end());
		}
	}
	for (const auto& [key, waypoint] : source.waypoints_) {
		if (std::find_if(orderedNames.begin(), orderedNames.end(), [&](const std::string& name) {
				return as_lower_str(name) == key;
			})
			== orderedNames.end()) {
			orderedNames.push_back(waypoint->name);
		}
	}

	for (const auto& name : orderedNames) {
		auto waypoint = source.extractWaypoint(name);
		if (!waypoint || getWaypoint(waypoint->name)) {
			continue;
		}
		waypoint->pos += offset;
		if (waypoint->pos.isValid()) {
			addWaypoint(std::move(waypoint));
		}
	}
	source.clearGroups();
	normalizeOrders();
}

Waypoint* Waypoints::getWaypoint(std::string_view name) {
	const auto found = waypoints_.find(as_lower_str(std::string(name)));
	return found == waypoints_.end() ? nullptr : found->second.get();
}

const Waypoint* Waypoints::getWaypoint(std::string_view name) const {
	const auto found = waypoints_.find(as_lower_str(std::string(name)));
	return found == waypoints_.end() ? nullptr : found->second.get();
}

Waypoint* Waypoints::getWaypoint(const TileLocation* location) {
	return const_cast<Waypoint*>(std::as_const(*this).getWaypoint(location));
}

const Waypoint* Waypoints::getWaypoint(const TileLocation* location) const {
	if (!location) {
		return nullptr;
	}
	const auto found = waypointByPosition_.find(location->position);
	if (found == waypointByPosition_.end()) {
		return nullptr;
	}
	const auto observer = std::find_if(found->second.begin(), found->second.end(), [](const Waypoint* waypoint) {
		return waypoint != nullptr;
	});
	return observer == found->second.end() ? nullptr : *observer;
}

void Waypoints::clearGroups() {
	categories_.clear();
	categoryWaypointOrder_.clear();
	uncategorizedOrder_.clear();
	for (auto& [key, waypoint] : waypoints_) {
		waypoint->category.clear();
		uncategorizedOrder_.push_back(waypoint->name);
	}
}

bool Waypoints::addCategory(std::string name) {
	if (name.empty() || findCategory(categories_, name)) {
		return false;
	}
	categories_.push_back(std::move(name));
	categoryWaypointOrder_[categories_.back()];
	return true;
}

bool Waypoints::removeCategory(std::string_view name) {
	const std::string* actual = findCategory(categories_, name);
	if (!actual) {
		return false;
	}
	const std::string category = *actual;
	categories_.erase(std::remove(categories_.begin(), categories_.end(), category), categories_.end());
	categoryWaypointOrder_.erase(category);
	for (auto& [key, waypoint] : waypoints_) {
		if (waypoint->category == category) {
			waypoint->category.clear();
		}
	}
	normalizeOrders();
	return true;
}

bool Waypoints::renameCategory(std::string_view oldName, std::string newName) {
	if (newName.empty()) {
		return false;
	}
	const std::string* actual = findCategory(categories_, oldName);
	if (!actual) {
		return false;
	}
	if (const std::string* duplicate = findCategory(categories_, newName); duplicate && duplicate != actual) {
		return false;
	}
	const std::string oldCategory = *actual;
	if (oldCategory == newName) {
		return true;
	}
	for (auto& category : categories_) {
		if (category == oldCategory) {
			category = newName;
			break;
		}
	}
	if (auto found = categoryWaypointOrder_.find(oldCategory); found != categoryWaypointOrder_.end()) {
		categoryWaypointOrder_[newName] = std::move(found->second);
		categoryWaypointOrder_.erase(found);
	}
	for (auto& [key, waypoint] : waypoints_) {
		if (waypoint->category == oldCategory) {
			waypoint->category = newName;
		}
	}
	normalizeOrders();
	return true;
}

bool Waypoints::setWaypointCategory(std::string_view waypointName, std::string category) {
	Waypoint* waypoint = getWaypoint(waypointName);
	if (!waypoint) {
		return false;
	}
	if (!category.empty()) {
		if (const std::string* existing = findCategory(categories_, category)) {
			category = *existing;
		} else {
			addCategory(category);
		}
	}
	if (waypoint->category == category) {
		return false;
	}
	unregisterWaypointOrder(waypoint->name);
	waypoint->category = std::move(category);
	registerWaypointOrder(*waypoint);
	return true;
}

bool Waypoints::hasGroups() const {
	if (!categories_.empty() || !uncategorizedOrder_.empty() || !categoryWaypointOrder_.empty()) {
		return true;
	}
	return std::any_of(waypoints_.begin(), waypoints_.end(), [](const auto& entry) {
		return !entry.second->category.empty();
	});
}

void Waypoints::unregisterWaypointOrder(std::string_view name) {
	removeFromVector(uncategorizedOrder_, name);
	for (auto& [category, order] : categoryWaypointOrder_) {
		removeFromVector(order, name);
	}
}

void Waypoints::registerWaypointOrder(Waypoint& waypoint, std::optional<size_t> orderIndex) {
	unregisterWaypointOrder(waypoint.name);
	std::vector<std::string>* order = &uncategorizedOrder_;
	if (!waypoint.category.empty()) {
		if (const std::string* category = findCategory(categories_, waypoint.category)) {
			waypoint.category = *category;
		} else {
			addCategory(waypoint.category);
		}
		order = &categoryWaypointOrder_[waypoint.category];
	}
	const size_t index = std::min(orderIndex.value_or(order->size()), order->size());
	order->insert(order->begin() + index, waypoint.name);
}

void Waypoints::normalizeOrders() {
	std::vector<std::string> normalizedCategories;
	std::set<std::string> seenCategories;
	for (const auto& category : categories_) {
		if (!category.empty() && insertCaseInsensitive(seenCategories, category)) {
			normalizedCategories.push_back(category);
		}
	}
	for (const auto& [key, waypoint] : waypoints_) {
		if (!waypoint->category.empty() && insertCaseInsensitive(seenCategories, waypoint->category)) {
			normalizedCategories.push_back(waypoint->category);
		}
	}

	std::vector<std::string> normalizedUncategorized;
	std::map<std::string, std::vector<std::string>> normalizedCategoryOrders;
	for (const auto& category : normalizedCategories) {
		normalizedCategoryOrders[category];
	}
	std::set<std::string> listed;
	auto preserve = [&](const std::vector<std::string>& order, const std::string& category) {
		auto& destination = category.empty() ? normalizedUncategorized : normalizedCategoryOrders[category];
		for (const auto& name : order) {
			const Waypoint* waypoint = getWaypoint(name);
			if (!waypoint || waypoint->category != category || !insertCaseInsensitive(listed, waypoint->name)) {
				continue;
			}
			destination.push_back(waypoint->name);
		}
	};
	preserve(uncategorizedOrder_, "");
	for (const auto& category : normalizedCategories) {
		if (const auto found = categoryWaypointOrder_.find(category); found != categoryWaypointOrder_.end()) {
			preserve(found->second, category);
		}
	}
	for (const auto& [key, waypoint] : waypoints_) {
		if (!insertCaseInsensitive(listed, waypoint->name)) {
			continue;
		}
		if (waypoint->category.empty()) {
			normalizedUncategorized.push_back(waypoint->name);
		} else {
			normalizedCategoryOrders[waypoint->category].push_back(waypoint->name);
		}
	}
	categories_ = std::move(normalizedCategories);
	uncategorizedOrder_ = std::move(normalizedUncategorized);
	categoryWaypointOrder_ = std::move(normalizedCategoryOrders);
}

bool Waypoints::validateMetadata(
	const std::vector<std::string>& categoryOrder,
	const std::vector<std::string>& uncategorizedOrder,
	const std::map<std::string, std::vector<std::string>>& categoryOrders,
	const std::map<std::string, std::string>& waypointCategoryByName,
	std::string* error
) const {
	auto fail = [&](const char* message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	std::set<std::string> categoryNames;
	for (const auto& category : categoryOrder) {
		if (category.empty() || !insertCaseInsensitive(categoryNames, category)) {
			return fail("Waypoint categories contain an empty or duplicate name.");
		}
	}
	if (categoryOrders.size() != categoryOrder.size()) {
		return fail("Waypoint category ordering is incomplete.");
	}
	for (const auto& [category, order] : categoryOrders) {
		if (!findCategory(categoryOrder, category)) {
			return fail("Waypoint order references an unknown category.");
		}
	}
	if (waypointCategoryByName.size() != waypoints_.size()) {
		return fail("Waypoint category metadata is incomplete.");
	}

	std::set<std::string> listed;
	auto validateOrder = [&](const std::vector<std::string>& order, std::string_view category) {
		for (const auto& name : order) {
			const Waypoint* waypoint = getWaypoint(name);
			if (!waypoint || !insertCaseInsensitive(listed, name)) {
				return false;
			}
			const auto categoryFound = waypointCategoryByName.find(waypoint->name);
			if (categoryFound == waypointCategoryByName.end() || categoryFound->second != category) {
				return false;
			}
		}
		return true;
	};
	if (!validateOrder(uncategorizedOrder, "")) {
		return fail("Invalid or duplicate uncategorized waypoint reference.");
	}
	for (const auto& category : categoryOrder) {
		const auto found = categoryOrders.find(category);
		if (found == categoryOrders.end() || !validateOrder(found->second, category)) {
			return fail("Invalid, duplicate, or mismatched categorized waypoint reference.");
		}
	}
	for (const auto& [name, category] : waypointCategoryByName) {
		if (!getWaypoint(name)) {
			return fail("Waypoint category metadata references a missing waypoint.");
		}
		if (!category.empty() && !findCategory(categoryOrder, category)) {
			return fail("Waypoint references an unknown category.");
		}
		if (!listed.contains(as_lower_str(name))) {
			return fail("Waypoint ordering is incomplete.");
		}
	}
	return listed.size() == waypoints_.size() || fail("Waypoint ordering is incomplete.");
}

bool Waypoints::applyOrderingFromMetadata(
	const std::vector<std::string>& categoryOrder,
	const std::vector<std::string>& uncategorizedOrder,
	const std::map<std::string, std::vector<std::string>>& categoryOrders,
	const std::map<std::string, std::string>& waypointCategoryByName
) {
	if (!validateMetadata(categoryOrder, uncategorizedOrder, categoryOrders, waypointCategoryByName, nullptr)) {
		return false;
	}
	for (auto& [key, waypoint] : waypoints_) {
		waypoint->category = waypointCategoryByName.at(waypoint->name);
	}
	categories_ = categoryOrder;
	uncategorizedOrder_ = uncategorizedOrder;
	categoryWaypointOrder_ = categoryOrders;
	return validateInvariants(nullptr);
}

bool Waypoints::validateInvariants(std::string* error) const {
	std::map<std::string, std::string> categoriesByWaypoint;
	for (const auto& [key, waypoint] : waypoints_) {
		if (!waypoint || key != as_lower_str(waypoint->name)) {
			if (error) {
				*error = "Waypoint registry key does not match its object.";
			}
			return false;
		}
		categoriesByWaypoint.emplace(waypoint->name, waypoint->category);
		const auto index = waypointByPosition_.find(waypoint->pos);
		if (index == waypointByPosition_.end() || std::count(index->second.begin(), index->second.end(), waypoint.get()) != 1) {
			if (error) {
				*error = "Waypoint position index is missing or duplicated.";
			}
			return false;
		}
	}
	for (const auto& [position, bucket] : waypointByPosition_) {
		if (bucket.empty()) {
			if (error) {
				*error = "Waypoint position index contains an empty bucket.";
			}
			return false;
		}
		for (const Waypoint* waypoint : bucket) {
			if (!waypoint || waypoint->pos != position || getWaypoint(waypoint->name) != waypoint) {
				if (error) {
					*error = "Waypoint position index contains a stale observer.";
				}
				return false;
			}
		}
	}
	return validateMetadata(
		categories_,
		uncategorizedOrder_,
		categoryWaypointOrder_,
		categoriesByWaypoint,
		error
	);
}

std::vector<std::string> Waypoints::orderedWaypointsInCategory(std::string_view category) const {
	if (category.empty()) {
		return uncategorizedOrder_;
	}
	const auto found = categoryWaypointOrder_.find(std::string(category));
	return found == categoryWaypointOrder_.end() ? std::vector<std::string>() : found->second;
}

std::optional<size_t> Waypoints::waypointOrderIndex(std::string_view waypointName) const {
	const Waypoint* waypoint = getWaypoint(waypointName);
	if (!waypoint) {
		return std::nullopt;
	}
	const std::vector<std::string>* order = &uncategorizedOrder_;
	if (!waypoint->category.empty()) {
		const auto category = categoryWaypointOrder_.find(waypoint->category);
		if (category == categoryWaypointOrder_.end()) {
			return std::nullopt;
		}
		order = &category->second;
	}
	const std::string key = as_lower_str(waypoint->name);
	const auto position = std::find_if(order->begin(), order->end(), [&](const std::string& name) {
		return as_lower_str(name) == key;
	});
	return position == order->end() ? std::nullopt : std::optional<size_t>(std::distance(order->begin(), position));
}

bool Waypoints::moveCategoryRelative(std::string_view category, std::string_view anchorCategory, bool insertBefore) {
	const std::string* sourceName = findCategory(categories_, category);
	const std::string* anchorName = findCategory(categories_, anchorCategory);
	if (!sourceName || !anchorName || sourceName == anchorName) {
		return false;
	}
	const std::string source = *sourceName;
	const std::string anchor = *anchorName;
	categories_.erase(std::find(categories_.begin(), categories_.end(), source));
	auto anchorPosition = std::find(categories_.begin(), categories_.end(), anchor);
	categories_.insert(insertBefore ? anchorPosition : std::next(anchorPosition), source);
	return true;
}

bool Waypoints::moveWaypointRelative(std::string_view waypointName, std::string_view anchorName, bool insertBefore) {
	Waypoint* waypoint = getWaypoint(waypointName);
	Waypoint* anchor = getWaypoint(anchorName);
	if (!waypoint || !anchor || waypoint == anchor) {
		return false;
	}
	unregisterWaypointOrder(waypoint->name);
	waypoint->category = anchor->category;
	auto& order = waypoint->category.empty() ? uncategorizedOrder_ : categoryWaypointOrder_[waypoint->category];
	const auto anchorPosition = std::find(order.begin(), order.end(), anchor->name);
	order.insert(anchorPosition == order.end() ? order.end() : (insertBefore ? anchorPosition : std::next(anchorPosition)), waypoint->name);
	return true;
}

bool Waypoints::moveWaypointIntoCategory(std::string_view waypointName, std::string_view categoryName, bool atEnd) {
	Waypoint* waypoint = getWaypoint(waypointName);
	if (!waypoint) {
		return false;
	}
	std::string category(categoryName);
	if (!category.empty()) {
		if (const std::string* actual = findCategory(categories_, category)) {
			category = *actual;
		} else {
			addCategory(category);
		}
	}
	unregisterWaypointOrder(waypoint->name);
	waypoint->category = category;
	auto& order = category.empty() ? uncategorizedOrder_ : categoryWaypointOrder_[category];
	order.insert(atEnd ? order.end() : order.begin(), waypoint->name);
	return true;
}

bool Waypoints::nudgeCategory(std::string_view category, bool moveUp) {
	const std::string* actual = findCategory(categories_, category);
	if (!actual) {
		return false;
	}
	auto position = std::find(categories_.begin(), categories_.end(), *actual);
	if (moveUp) {
		if (position == categories_.begin()) {
			return false;
		}
		std::iter_swap(position, std::prev(position));
	} else {
		if (std::next(position) == categories_.end()) {
			return false;
		}
		std::iter_swap(position, std::next(position));
	}
	return true;
}
