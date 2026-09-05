#include "main.h"

#include "map_diagnostics_scanner.h"

#include "complexitem.h"
#include "creature.h"
#include "creatures.h"
#include "house.h"
#include "items.h"
#include "map.h"
#include "spawn.h"
#include "tile.h"
#include "town.h"
#include "waypoints.h"

#include <memory>
#include <queue>
#include <set>
#include <utility>

namespace {
	using DoorKey = std::pair<uint32_t, uint8_t>;

	std::string PositionText(const Position& position) {
		return std::to_string(position.x) + ", " + std::to_string(position.y) + ", " + std::to_string(position.z);
	}

	std::string ItemLabel(const Item& item) {
		if (!item.typeExists()) {
			return "item " + std::to_string(item.getID());
		}
		const std::string name = item.getName();
		return name.empty() ? "item " + std::to_string(item.getID()) : name + " (" + std::to_string(item.getID()) + ")";
	}

	MapDiagnosticIssue MakeIssue(MapDiagnosticSeverity severity, MapDiagnosticCategory category, MapDiagnosticKind kind, const Position& position, std::string summary, std::string detail = {}) {
		MapDiagnosticIssue issue;
		issue.severity = severity;
		issue.category = category;
		issue.kind = kind;
		issue.position = position;
		issue.summary = std::move(summary);
		issue.detail = std::move(detail);
		return issue;
	}
}

class MapDiagnosticsScanner::Impl {
public:
	explicit Impl(Map& map) :
		map_(map), mapSessionId_(map.getSessionId()) { }

	void Start() {
		issues_.clear();
		uidOccurrences_.clear();
		aidOccurrences_.clear();
		actualHouseTiles_.clear();
		declaredHouseTiles_.clear();
		doorsByHouseId_.clear();
		lockedDoorsByActionId_.clear();
		keysByActionId_.clear();
		houses_.clear();
		houseStarted_ = false;
		actualHouseStarted_ = false;
		processedTiles_ = 0;
		totalTiles_ = map_.getTileCount();
		cancelled_ = false;
		complete_ = false;
		running_ = true;
		phase_ = Phase::Tiles;
		tileIterator_ = std::make_unique<MapIterator>(map_.begin());
		tileEnd_ = std::make_unique<MapIterator>(map_.end());
	}

	void Cancel() {
		if (!running_) {
			return;
		}
		running_ = false;
		cancelled_ = true;
		tileIterator_.reset();
		tileEnd_.reset();
	}

	void Step(size_t workUnits) {
		if (!running_ || workUnits == 0) {
			return;
		}

		size_t completedUnits = 0;
		while (running_ && completedUnits < workUnits) {
			switch (phase_) {
				case Phase::Tiles:
					if (*tileIterator_ == *tileEnd_) {
						BeginFinalization();
						break;
					}
					ScanTile((*(*tileIterator_))->get());
					++(*tileIterator_);
					++processedTiles_;
					++completedUnits;
					break;
				case Phase::UniqueIds:
					if (uidIterator_ == uidOccurrences_.end()) {
						phase_ = Phase::ActionIds;
						aidIterator_ = aidOccurrences_.begin();
						break;
					}
					AppendDuplicateGroup(uidIterator_->first, uidIterator_->second, true);
					++uidIterator_;
					++completedUnits;
					break;
				case Phase::ActionIds:
					if (aidIterator_ == aidOccurrences_.end()) {
						phase_ = Phase::Houses;
						break;
					}
					AppendDuplicateGroup(aidIterator_->first, aidIterator_->second, false);
					++aidIterator_;
					++completedUnits;
					break;
				case Phase::Houses:
					if (!StepHouse()) {
						phase_ = Phase::ActualHouseTiles;
						actualHouseIterator_ = actualHouseTiles_.begin();
						actualHouseStarted_ = false;
						break;
					}
					++completedUnits;
					break;
				case Phase::ActualHouseTiles:
					if (!StepActualHouseTile()) {
						phase_ = Phase::SpawnRegistry;
						spawnIterator_ = map_.spawns.begin();
						break;
					}
					++completedUnits;
					break;
				case Phase::SpawnRegistry:
					if (spawnIterator_ == map_.spawns.end()) {
						phase_ = Phase::Waypoints;
						waypointIterator_ = map_.waypoints.begin();
						break;
					}
					CheckSpawnRegistryEntry(*spawnIterator_++);
					++completedUnits;
					break;
				case Phase::Waypoints:
					if (waypointIterator_ == map_.waypoints.end()) {
						phase_ = Phase::DoorDuplicates;
						doorIterator_ = doorsByHouseId_.begin();
						break;
					}
					CheckWaypoint(*waypointIterator_++);
					++completedUnits;
					break;
				case Phase::DoorDuplicates:
					if (doorIterator_ == doorsByHouseId_.end()) {
						phase_ = Phase::Keys;
						keyIterator_ = keysByActionId_.begin();
						lockedDoorIterator_ = lockedDoorsByActionId_.begin();
						checkingKeys_ = true;
						break;
					}
					CheckDoorDuplicates(*doorIterator_++);
					++completedUnits;
					break;
				case Phase::Keys:
					if (!StepKeyChecks()) {
						Finish();
						break;
					}
					++completedUnits;
					break;
				case Phase::Done:
					Finish();
					break;
			}
		}
	}

	bool IsRunning() const {
		return running_;
	}
	bool IsComplete() const {
		return complete_;
	}
	bool WasCancelled() const {
		return cancelled_;
	}
	uint64_t GetProcessedTileCount() const {
		return processedTiles_;
	}
	uint64_t GetTotalTileCount() const {
		return totalTiles_;
	}
	SessionId GetMapSessionId() const {
		return mapSessionId_;
	}
	const std::vector<MapDiagnosticIssue>& GetIssues() const {
		return issues_;
	}

	int GetProgressPercent() const {
		if (complete_) {
			return 100;
		}
		if (totalTiles_ == 0 || phase_ != Phase::Tiles) {
			return running_ ? 99 : 0;
		}
		return static_cast<int>(std::min<uint64_t>(98, processedTiles_ * 98 / totalTiles_));
	}

private:
	enum class Phase {
		Tiles,
		UniqueIds,
		ActionIds,
		Houses,
		ActualHouseTiles,
		SpawnRegistry,
		Waypoints,
		DoorDuplicates,
		Keys,
		Done,
	};

	void AddIssue(MapDiagnosticIssue issue) {
		issues_.push_back(std::move(issue));
	}

	void ScanTile(Tile* tile) {
		if (!tile) {
			return;
		}
		const Position position = tile->getPosition();

		if (!tile->hasGround() && tile->size() > 0) {
			AddIssue(MakeIssue(MapDiagnosticSeverity::Warning, MapDiagnosticCategory::Items, MapDiagnosticKind::TileWithoutGround, position, "Tile without ground", "The tile contains map data but has no ground item."));
		}

		if (tile->ground) {
			ScanItem(tile->ground, position, tile->getHouseID());
		}
		std::queue<Item*> pending;
		for (Item* item : tile->items) {
			pending.push(item);
		}
		while (!pending.empty()) {
			Item* item = pending.front();
			pending.pop();
			if (!item) {
				continue;
			}
			ScanItem(item, position, tile->getHouseID());
			if (auto* container = dynamic_cast<Container*>(item)) {
				for (Item* child : container->getVector()) {
					pending.push(child);
				}
			}
		}

		if (tile->getHouseID() != 0) {
			actualHouseTiles_[tile->getHouseID()].insert(position);
			if (!map_.houses.getHouse(tile->getHouseID())) {
				AddIssue(MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Houses, MapDiagnosticKind::InconsistentHouse, position, "House tile references a missing house", "House ID " + std::to_string(tile->getHouseID()) + " is not present in the house registry."));
			}
		}

		if (tile->spawn) {
			Position lookup = position;
			if (map_.spawns.find(lookup) == map_.spawns.end()) {
				AddIssue(MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Spawns, MapDiagnosticKind::InvalidSpawn, position, "Spawn is missing from the map registry", "The tile owns a spawn object, but its position is absent from the spawn registry."));
			}
			if (tile->spawn->getSize() <= 0) {
				AddIssue(MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Spawns, MapDiagnosticKind::InvalidSpawn, position, "Spawn has an invalid radius", "Spawn radius must be greater than zero."));
			}
			if (!tile->hasGround()) {
				AddIssue(MakeIssue(MapDiagnosticSeverity::Warning, MapDiagnosticCategory::Spawns, MapDiagnosticKind::InvalidSpawn, position, "Spawn center has no ground", "The spawn center cannot be placed on a walkable ground tile."));
			}
		}

		if (tile->creature) {
			const std::string name = tile->creature->getName();
			CreatureType* type = name.empty() ? nullptr : g_creatures[name];
			if (!type || type->missing) {
				AddIssue(MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Spawns, MapDiagnosticKind::InvalidCreature, position, "Creature definition is missing", name.empty() ? "The creature cannot be resolved in the active resource session." : "Creature '" + name + "' is marked as missing in the active resource session."));
			}
			if (tile->creature->getSpawnTime() <= 0) {
				AddIssue(MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Spawns, MapDiagnosticKind::InvalidSpawn, position, "Creature has an invalid spawn time", "Spawn time must be greater than zero."));
			}
			if (tile->getLocation()->getSpawnCount() == 0) {
				AddIssue(MakeIssue(MapDiagnosticSeverity::Warning, MapDiagnosticCategory::Spawns, MapDiagnosticKind::InvalidSpawn, position, "Creature is outside every spawn area", "No registered spawn covers this creature position."));
			}
		}
	}

	void ScanItem(Item* item, const Position& position, uint32_t houseId) {
		const uint16_t itemId = item->getID();
		const uint16_t actionId = item->getActionID();
		const uint16_t uniqueId = item->getUniqueID();
		if (uniqueId != 0) {
			uidOccurrences_[uniqueId].push_back({ position, itemId });
		}
		if (actionId != 0) {
			aidOccurrences_[actionId].push_back({ position, itemId });
		}

		if (!item->typeExists()) {
			auto issue = MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Items, MapDiagnosticKind::InvalidItemId, position, "Invalid Item ID " + std::to_string(itemId), "Item ID " + std::to_string(itemId) + " does not exist in the active resource session.");
			issue.itemId = itemId;
			issue.actionId = actionId;
			issue.uniqueId = uniqueId;
			AddIssue(std::move(issue));
			return;
		}

		if (auto* teleport = dynamic_cast<Teleport*>(item)) {
			const Position destination = teleport->getDestination();
			if (!teleport->hasDestination()) {
				AddItemIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Teleports, MapDiagnosticKind::BrokenTeleport, position, *item, "Broken teleport", "The teleport has no destination.");
			} else if (!destination.isValid()) {
				AddItemIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Teleports, MapDiagnosticKind::BrokenTeleport, position, *item, "Broken teleport destination", "Destination " + PositionText(destination) + " is outside the supported map coordinate range.");
			} else if (!map_.getTile(destination)) {
				AddItemIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Teleports, MapDiagnosticKind::BrokenTeleport, position, *item, "Broken teleport destination", "Destination " + PositionText(destination) + " does not contain a map tile.");
			}
		}

		const ItemType& type = g_items.getItemType(itemId);
		if (type.isKey()) {
			if (actionId == 0) {
				AddItemIssue(MapDiagnosticSeverity::Warning, MapDiagnosticCategory::DoorsAndKeys, MapDiagnosticKind::InconsistentKey, position, *item, "Key has no ActionID", "A key needs an ActionID to identify the locked door it opens.");
			} else {
				keysByActionId_[actionId].push_back({ position, itemId });
			}
		}

		if (auto* door = dynamic_cast<Door*>(item); door && door->isRealDoor()) {
			const uint8_t doorId = door->getDoorID();
			if (doorId != 0 && houseId == 0) {
				AddItemIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::DoorsAndKeys, MapDiagnosticKind::InconsistentDoor, position, *item, "Door ID outside a house", "Door ID " + std::to_string(doorId) + " is only meaningful on a house tile.");
			} else if (doorId == 0 && houseId != 0) {
				AddItemIssue(MapDiagnosticSeverity::Warning, MapDiagnosticCategory::DoorsAndKeys, MapDiagnosticKind::InconsistentDoor, position, *item, "House door has no Door ID", "The real door is on house " + std::to_string(houseId) + " but has Door ID 0.");
			} else if (doorId != 0) {
				doorsByHouseId_[{ houseId, doorId }].push_back({ position, itemId });
			}

			if (door->getDoorType() == WALL_DOOR_LOCKED) {
				if (actionId == 0) {
					AddItemIssue(MapDiagnosticSeverity::Warning, MapDiagnosticCategory::DoorsAndKeys, MapDiagnosticKind::InconsistentDoor, position, *item, "Locked door has no ActionID", "The locked door cannot be matched to a key without an ActionID.");
				} else {
					lockedDoorsByActionId_[actionId].push_back({ position, itemId });
				}
			}
		}
	}

	void AddItemIssue(MapDiagnosticSeverity severity, MapDiagnosticCategory category, MapDiagnosticKind kind, const Position& position, const Item& item, std::string summary, std::string detail) {
		auto issue = MakeIssue(severity, category, kind, position, std::move(summary), ItemLabel(item) + ": " + std::move(detail));
		issue.itemId = item.getID();
		issue.actionId = item.getActionID();
		issue.uniqueId = item.getUniqueID();
		AddIssue(std::move(issue));
	}

	void BeginFinalization() {
		tileIterator_.reset();
		tileEnd_.reset();
		for (auto& [id, house] : map_.houses) {
			houses_.push_back({ id, house });
		}
		houseIndex_ = 0;
		houseStarted_ = false;
		phase_ = Phase::UniqueIds;
		uidIterator_ = uidOccurrences_.begin();
	}

	void AppendDuplicateGroup(uint16_t id, const std::vector<MapDiagnosticOccurrence>& entries, bool unique) {
		if (id == 0 || entries.size() < 2) {
			return;
		}
		for (const MapDiagnosticOccurrence& entry : entries) {
			auto issue = MakeIssue(unique ? MapDiagnosticSeverity::Error : MapDiagnosticSeverity::Warning, unique ? MapDiagnosticCategory::UniqueIds : MapDiagnosticCategory::ActionIds, unique ? MapDiagnosticKind::DuplicateUniqueId : MapDiagnosticKind::DuplicateActionId, entry.position, std::string(unique ? "Duplicate UniqueID " : "Duplicate ActionID ") + std::to_string(id), std::string(unique ? "UniqueID " : "ActionID ") + std::to_string(id) + " is used by " + std::to_string(entries.size()) + " items.");
			issue.itemId = entry.itemId;
			if (unique) {
				issue.uniqueId = id;
			} else {
				issue.actionId = id;
			}
			AddIssue(std::move(issue));
		}
	}

	bool StepHouse() {
		if (houseIndex_ >= houses_.size()) {
			return false;
		}
		const auto [registryId, house] = houses_[houseIndex_];
		if (!house) {
			AddIssue(MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Houses, MapDiagnosticKind::InconsistentHouse, Position(), "House registry contains an empty entry", "House ID " + std::to_string(registryId) + " has no house object."));
			++houseIndex_;
			return true;
		}

		if (!houseStarted_) {
			CheckHouseMetadata(registryId, *house);
			houseTileIterator_ = house->getTilePositions().begin();
			houseStarted_ = true;
		}
		const PositionList& positions = house->getTilePositions();
		if (houseTileIterator_ != positions.end()) {
			const Position position = *houseTileIterator_++;
			declaredHouseTiles_[registryId].insert(position);
			Tile* tile = map_.getTile(position);
			if (!tile) {
				AddIssue(MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Houses, MapDiagnosticKind::InconsistentHouse, position, "House references a missing tile", "House " + std::to_string(registryId) + " lists this position, but no tile exists there."));
			} else if (tile->getHouseID() != registryId) {
				AddIssue(MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Houses, MapDiagnosticKind::InconsistentHouse, position, "House tile ownership disagrees with registry", "House " + std::to_string(registryId) + " lists this tile, but the tile stores house ID " + std::to_string(tile->getHouseID()) + "."));
			}
			return true;
		}

		++houseIndex_;
		houseStarted_ = false;
		return true;
	}

	void CheckHouseMetadata(uint32_t registryId, const House& house) {
		const Position anchor = house.getExit();
		if (house.getID() != registryId) {
			AddIssue(MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Houses, MapDiagnosticKind::InconsistentHouse, anchor, "House ID disagrees with registry key", "Registry key " + std::to_string(registryId) + " points to a house storing ID " + std::to_string(house.getID()) + "."));
		}
		if (house.name.empty()) {
			AddIssue(MakeIssue(MapDiagnosticSeverity::Warning, MapDiagnosticCategory::Houses, MapDiagnosticKind::InconsistentHouse, anchor, "House has no name", "House " + std::to_string(registryId) + " has an empty name."));
		}
		if (house.townid == 0 || !map_.towns.getTown(house.townid)) {
			AddIssue(MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Houses, MapDiagnosticKind::InconsistentHouse, anchor, "House references an invalid town", "House " + std::to_string(registryId) + " references town ID " + std::to_string(house.townid) + "."));
		}
		if (anchor == Position()) {
			AddIssue(MakeIssue(MapDiagnosticSeverity::Warning, MapDiagnosticCategory::Houses, MapDiagnosticKind::InconsistentHouse, anchor, "House has no exit", "House " + std::to_string(registryId) + " does not define an exit position."));
		} else if (!anchor.isValid() || !map_.getTile(anchor)) {
			AddIssue(MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Houses, MapDiagnosticKind::InconsistentHouse, anchor, "House exit is invalid", "House " + std::to_string(registryId) + " points to an unavailable exit tile."));
		} else if (!map_.getTile(anchor)->hasHouseExit(registryId)) {
			AddIssue(MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Houses, MapDiagnosticKind::InconsistentHouse, anchor, "House exit registry is inconsistent", "The exit tile does not reference house " + std::to_string(registryId) + "."));
		}
	}

	bool StepActualHouseTile() {
		if (actualHouseIterator_ == actualHouseTiles_.end()) {
			return false;
		}
		const uint32_t houseId = actualHouseIterator_->first;
		const auto& positions = actualHouseIterator_->second;
		if (!actualHouseStarted_) {
			actualHouseTileIterator_ = positions.begin();
			actualHouseStarted_ = true;
		}
		if (actualHouseTileIterator_ == positions.end()) {
			++actualHouseIterator_;
			actualHouseStarted_ = false;
			return true;
		}
		const Position position = *actualHouseTileIterator_++;
		if (map_.houses.getHouse(houseId) && declaredHouseTiles_[houseId].find(position) == declaredHouseTiles_[houseId].end()) {
			AddIssue(MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Houses, MapDiagnosticKind::InconsistentHouse, position, "House tile is absent from the house registry", "The tile stores house ID " + std::to_string(houseId) + ", but the house does not list this position."));
		}
		return true;
	}

	void CheckSpawnRegistryEntry(const Position& position) {
		Tile* tile = map_.getTile(position);
		if (!tile || !tile->spawn) {
			AddIssue(MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Spawns, MapDiagnosticKind::InvalidSpawn, position, "Spawn registry points to a missing spawn", tile ? "The registry position has a tile but no spawn object." : "The registry position does not contain a map tile."));
		}
	}

	void CheckWaypoint(const WaypointMap::value_type& entry) {
		const std::string& key = entry.first;
		const Waypoint* waypoint = entry.second;
		if (!waypoint) {
			AddIssue(MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Waypoints, MapDiagnosticKind::InvalidWaypoint, Position(), "Waypoint registry contains an empty entry", "Waypoint key '" + key + "' has no waypoint object."));
			return;
		}
		const Position position = waypoint->pos;
		if (waypoint->name.empty()) {
			AddIssue(MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Waypoints, MapDiagnosticKind::InvalidWaypoint, position, "Waypoint has no name", "The waypoint registry key is '" + key + "'."));
		}
		if (key != as_lower_str(waypoint->name)) {
			AddIssue(MakeIssue(MapDiagnosticSeverity::Warning, MapDiagnosticCategory::Waypoints, MapDiagnosticKind::InvalidWaypoint, position, "Waypoint name disagrees with registry key", "Registry key '" + key + "' refers to waypoint '" + waypoint->name + "'."));
		}
		if (position == Position() || !position.isValid()) {
			AddIssue(MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Waypoints, MapDiagnosticKind::InvalidWaypoint, position, "Waypoint position is invalid", "Waypoint '" + waypoint->name + "' has no valid map position."));
			return;
		}
		Tile* tile = map_.getTile(position);
		if (!tile) {
			AddIssue(MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Waypoints, MapDiagnosticKind::InvalidWaypoint, position, "Waypoint points to a missing tile", "Waypoint '" + waypoint->name + "' cannot be resolved to a map tile."));
		} else if (tile->getLocation()->getWaypointCount() == 0) {
			AddIssue(MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::Waypoints, MapDiagnosticKind::InvalidWaypoint, position, "Waypoint tile index is inconsistent", "The target tile does not record waypoint '" + waypoint->name + "'."));
		}
	}

	void CheckDoorDuplicates(const std::pair<const DoorKey, std::vector<MapDiagnosticOccurrence>>& entry) {
		if (entry.second.size() < 2) {
			return;
		}
		for (const MapDiagnosticOccurrence& occurrence : entry.second) {
			auto issue = MakeIssue(MapDiagnosticSeverity::Error, MapDiagnosticCategory::DoorsAndKeys, MapDiagnosticKind::InconsistentDoor, occurrence.position, "Duplicate Door ID " + std::to_string(entry.first.second), "House " + std::to_string(entry.first.first) + " uses Door ID " + std::to_string(entry.first.second) + " at " + std::to_string(entry.second.size()) + " positions.");
			issue.itemId = occurrence.itemId;
			AddIssue(std::move(issue));
		}
	}

	bool StepKeyChecks() {
		if (checkingKeys_) {
			if (keyIterator_ != keysByActionId_.end()) {
				if (lockedDoorsByActionId_.find(keyIterator_->first) == lockedDoorsByActionId_.end()) {
					for (const MapDiagnosticOccurrence& occurrence : keyIterator_->second) {
						auto issue = MakeIssue(MapDiagnosticSeverity::Warning, MapDiagnosticCategory::DoorsAndKeys, MapDiagnosticKind::InconsistentKey, occurrence.position, "Key has no matching locked door", "No locked door uses ActionID " + std::to_string(keyIterator_->first) + ".");
						issue.itemId = occurrence.itemId;
						issue.actionId = keyIterator_->first;
						AddIssue(std::move(issue));
					}
				}
				++keyIterator_;
				return true;
			}
			checkingKeys_ = false;
		}
		if (lockedDoorIterator_ != lockedDoorsByActionId_.end()) {
			if (keysByActionId_.find(lockedDoorIterator_->first) == keysByActionId_.end()) {
				for (const MapDiagnosticOccurrence& occurrence : lockedDoorIterator_->second) {
					auto issue = MakeIssue(MapDiagnosticSeverity::Warning, MapDiagnosticCategory::DoorsAndKeys, MapDiagnosticKind::InconsistentDoor, occurrence.position, "Locked door has no matching key", "No key uses ActionID " + std::to_string(lockedDoorIterator_->first) + ".");
					issue.itemId = occurrence.itemId;
					issue.actionId = lockedDoorIterator_->first;
					AddIssue(std::move(issue));
				}
			}
			++lockedDoorIterator_;
			return true;
		}
		return false;
	}

	void Finish() {
		phase_ = Phase::Done;
		running_ = false;
		complete_ = true;
		std::stable_sort(issues_.begin(), issues_.end(), [](const MapDiagnosticIssue& left, const MapDiagnosticIssue& right) {
			if (left.severity != right.severity) {
				return left.severity == MapDiagnosticSeverity::Error;
			}
			if (left.category != right.category) {
				return left.category < right.category;
			}
			if (left.summary != right.summary) {
				return left.summary < right.summary;
			}
			return left.position < right.position;
		});
	}

	Map& map_;
	SessionId mapSessionId_;
	std::unique_ptr<MapIterator> tileIterator_;
	std::unique_ptr<MapIterator> tileEnd_;
	uint64_t processedTiles_ = 0;
	uint64_t totalTiles_ = 0;
	bool running_ = false;
	bool complete_ = false;
	bool cancelled_ = false;
	Phase phase_ = Phase::Done;
	std::vector<MapDiagnosticIssue> issues_;
	MapDiagnosticOccurrences uidOccurrences_;
	MapDiagnosticOccurrences aidOccurrences_;
	MapDiagnosticOccurrences::iterator uidIterator_;
	MapDiagnosticOccurrences::iterator aidIterator_;
	std::map<uint32_t, std::set<Position>> actualHouseTiles_;
	std::map<uint32_t, std::set<Position>> declaredHouseTiles_;
	std::vector<std::pair<uint32_t, House*>> houses_;
	size_t houseIndex_ = 0;
	bool houseStarted_ = false;
	PositionList::const_iterator houseTileIterator_;
	std::map<uint32_t, std::set<Position>>::iterator actualHouseIterator_;
	bool actualHouseStarted_ = false;
	std::set<Position>::const_iterator actualHouseTileIterator_;
	SpawnPositionList::iterator spawnIterator_;
	WaypointMap::iterator waypointIterator_;
	std::map<DoorKey, std::vector<MapDiagnosticOccurrence>> doorsByHouseId_;
	std::map<DoorKey, std::vector<MapDiagnosticOccurrence>>::iterator doorIterator_;
	MapDiagnosticOccurrences lockedDoorsByActionId_;
	MapDiagnosticOccurrences keysByActionId_;
	MapDiagnosticOccurrences::iterator keyIterator_;
	MapDiagnosticOccurrences::iterator lockedDoorIterator_;
	bool checkingKeys_ = true;
};

MapDiagnosticsScanner::MapDiagnosticsScanner(Map& map) :
	impl_(std::make_unique<Impl>(map)) { }

MapDiagnosticsScanner::~MapDiagnosticsScanner() = default;

void MapDiagnosticsScanner::Start() {
	impl_->Start();
}
void MapDiagnosticsScanner::Cancel() {
	impl_->Cancel();
}
void MapDiagnosticsScanner::Step(size_t workUnits) {
	impl_->Step(workUnits);
}
bool MapDiagnosticsScanner::IsRunning() const {
	return impl_->IsRunning();
}
bool MapDiagnosticsScanner::IsComplete() const {
	return impl_->IsComplete();
}
bool MapDiagnosticsScanner::WasCancelled() const {
	return impl_->WasCancelled();
}
int MapDiagnosticsScanner::GetProgressPercent() const {
	return impl_->GetProgressPercent();
}
uint64_t MapDiagnosticsScanner::GetProcessedTileCount() const {
	return impl_->GetProcessedTileCount();
}
uint64_t MapDiagnosticsScanner::GetTotalTileCount() const {
	return impl_->GetTotalTileCount();
}
SessionId MapDiagnosticsScanner::GetMapSessionId() const {
	return impl_->GetMapSessionId();
}
const std::vector<MapDiagnosticIssue>& MapDiagnosticsScanner::GetIssues() const {
	return impl_->GetIssues();
}
