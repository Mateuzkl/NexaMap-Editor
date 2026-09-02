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

#include "action.h"
#include "settings.h"
#include "map.h"
#include "editor.h"
#include "gui.h"
#include "house_paste_transaction.h"
#include "multiplayer_session.h"

namespace {
	struct HouseRegistryChange {
		HouseSnapshot snapshot;
		bool add = true;
		SessionId activeHouseSessionId = InvalidSessionId;
	};
}

Change::Change() :
	type(CHANGE_NONE), data(nullptr) {
	////
}

Change::Change(Tile* t) :
	type(CHANGE_TILE) {
	ASSERT(t);
	data = t;
}

Change* Change::Create(House* house, const Position& where) {
	auto* c = newd Change();
	c->type = CHANGE_MOVE_HOUSE_EXIT;
	auto* p = newd std::pair<uint32_t, Position>;
	p->first = house->getID();
	p->second = where;
	c->data = p;
	return c;
}

Change* Change::CreateHouse(const HouseSnapshot& snapshot) {
	auto* c = newd Change();
	c->type = CHANGE_HOUSE_REGISTRY;
	c->data = newd HouseRegistryChange { snapshot, true, InvalidSessionId };
	return c;
}

Change* Change::Create(Waypoint* wp, const Position& where) {
	auto* c = newd Change();
	c->type = CHANGE_MOVE_WAYPOINT;
	auto* p = newd std::pair<std::string, Position>;
	p->first = wp->name;
	p->second = where;
	c->data = p;
	return c;
}

Change* Change::CreateZone(const std::string& name, unsigned int id, bool add) {
	auto* c = newd Change();
	c->type = CHANGE_ZONE_REGISTRY;
	c->data = newd ZoneRegistryChange { name, id, add };
	return c;
}

Change* Change::RenameZone(const std::string& oldName, const std::string& newName) {
	auto* c = newd Change();
	c->type = CHANGE_RENAME_ZONE;
	c->data = newd ZoneRenameChange { oldName, newName };
	return c;
}

Change::~Change() {
	clear();
}

void Change::clear() {
	switch (type) {
		case CHANGE_TILE:
			ASSERT(data);
			delete reinterpret_cast<Tile*>(data);
			break;
		case CHANGE_MOVE_HOUSE_EXIT:
			ASSERT(data);
			delete reinterpret_cast<std::pair<uint32_t, Position>*>(data);
			break;
		case CHANGE_MOVE_WAYPOINT:
			ASSERT(data);
			delete reinterpret_cast<std::pair<std::string, Position>*>(data);
			break;
		case CHANGE_ZONE_REGISTRY:
			ASSERT(data);
			delete reinterpret_cast<ZoneRegistryChange*>(data);
			break;
		case CHANGE_RENAME_ZONE:
			ASSERT(data);
			delete reinterpret_cast<ZoneRenameChange*>(data);
			break;
		case CHANGE_HOUSE_REGISTRY:
			ASSERT(data);
			delete reinterpret_cast<HouseRegistryChange*>(data);
			break;
		case CHANGE_NONE:
			break;
		default:
#ifdef __DEBUG_MODE__
			if (data) {
				printf("UNHANDLED CHANGE TYPE! Leak!");
			}
#endif
			break;
	}
	type = CHANGE_NONE;
	data = nullptr;
}

uint32_t Change::memsize() const {
	uint32_t mem = sizeof(*this);
	switch (type) {
		case CHANGE_TILE:
			ASSERT(data);
			mem += reinterpret_cast<Tile*>(data)->memsize();
			break;
		case CHANGE_ZONE_REGISTRY: {
			ASSERT(data);
			const auto* change = reinterpret_cast<ZoneRegistryChange*>(data);
			mem += sizeof(ZoneRegistryChange) + change->name.capacity();
			break;
		}
		case CHANGE_RENAME_ZONE: {
			ASSERT(data);
			const auto* change = reinterpret_cast<ZoneRenameChange*>(data);
			mem += sizeof(ZoneRenameChange) + change->from.capacity() + change->to.capacity();
			break;
		}
		case CHANGE_HOUSE_REGISTRY: {
			ASSERT(data);
			const auto* change = reinterpret_cast<HouseRegistryChange*>(data);
			mem += sizeof(HouseRegistryChange) + change->snapshot.name.capacity();
			break;
		}
		default:
			break;
	}
	return mem;
}

Action::Action(Editor& editor, ActionIdentifier ident) :
	commited(false),
	editor(editor),
	type(ident) {
}

void Action::applyZoneChange(Change* c) {
	switch (c->type) {
		case CHANGE_ZONE_REGISTRY: {
			auto* change = reinterpret_cast<Change::ZoneRegistryChange*>(c->data);
			ASSERT(change);
			const bool changed = change->add ? editor.map.zones.addZone(change->name, change->id) : editor.map.zones.removeZone(change->name);
			if (changed) {
				change->add = !change->add;
			}
			break;
		}

		case CHANGE_RENAME_ZONE: {
			auto* change = reinterpret_cast<Change::ZoneRenameChange*>(c->data);
			ASSERT(change);
			if (editor.map.zones.renameZone(change->from, change->to)) {
				std::swap(change->from, change->to);
			}
			break;
		}

		default:
			ASSERT(false);
			break;
	}
}

bool Action::canApplyHouseChanges() const {
	for (const Change* c : changes) {
		if (!c || c->type != CHANGE_HOUSE_REGISTRY) {
			continue;
		}

		const auto* change = reinterpret_cast<const HouseRegistryChange*>(c->data);
		ASSERT(change);
		if (!change) {
			return false;
		}

		const House* house = editor.map.houses.getHouse(change->snapshot.id);
		if (change->add) {
			if (house) {
				return false;
			}
			continue;
		}

		if (!house || house->getSessionId() != change->activeHouseSessionId || house->getSnapshot() != change->snapshot || house->tileCount() != 0) {
			return false;
		}
	}
	return true;
}

bool Action::applyHouseChange(Change* c) {
	auto* change = reinterpret_cast<HouseRegistryChange*>(c->data);
	ASSERT(change);
	if (!change) {
		return false;
	}

	if (change->add) {
		// A paste must never attach its tiles to an unrelated house that appeared
		// under the same id. Leave the change unapplied if the invariant is broken.
		if (editor.map.houses.getHouse(change->snapshot.id)) {
			return false;
		}

		auto* house = newd House(editor.map);
		house->applySnapshot(change->snapshot);
		if (!editor.map.houses.addHouse(house)) {
			delete house;
			return false;
		}
		if (change->snapshot.exit != Position() && change->snapshot.exit.isValid()) {
			house->setExit(change->snapshot.exit);
		}
		change->activeHouseSessionId = house->getSessionId();
		change->add = false;
		return true;
	}

	House* house = editor.map.houses.getHouse(change->snapshot.id);
	if (!house || house->getSessionId() != change->activeHouseSessionId || house->getSnapshot() != change->snapshot || house->tileCount() != 0) {
		return false;
	}
	editor.map.houses.removeHouse(house);
	change->activeHouseSessionId = InvalidSessionId;
	change->add = true;
	return true;
}

Action::~Action() {
	auto it = changes.rbegin();
	while (it != changes.rend()) {
		delete *it;
		++it;
	}
}

size_t Action::approx_memsize() const {
	uint32_t mem = sizeof(*this);
	mem += changes.size() * (sizeof(Change) + sizeof(Tile) + sizeof(Item) + 6 /* approx overhead*/);
	return mem;
}

size_t Action::memsize() const {
	uint32_t mem = sizeof(*this);
	mem += sizeof(Change*) * 3 * changes.size();
	auto it = changes.begin();
	while (it != changes.end()) {
		Change* c = *it;
		switch (c->type) {
			case CHANGE_TILE: {
				ASSERT(c->data);
				mem += reinterpret_cast<Tile*>(c->data)->memsize();
				break;
			}

			case CHANGE_ZONE_REGISTRY:
			case CHANGE_RENAME_ZONE:
			case CHANGE_HOUSE_REGISTRY:
				mem += c->memsize();
				break;

			default:
				break;
		}
		++it;
	}
	return mem;
}

bool Action::commit() {
	if (commited) {
		return true;
	}
	if (editor.multiplayer && editor.multiplayer->active() && !editor.multiplayer->internalChange() && type != ACTION_SELECT && type != ACTION_REMOTE && !editor.multiplayer->canEdit()) {
		return false;
	}
	if (!canApplyHouseChanges()) {
		return false;
	}

	editor.selection.start(Selection::INTERNAL);
	ChangeList::const_iterator it = changes.begin();
	while (it != changes.end()) {
		Change* c = *it;
		switch (c->type) {
			case CHANGE_TILE: {
				void** data = &c->data;
				Tile* newtile = reinterpret_cast<Tile*>(*data);
				ASSERT(newtile);
				Position pos = newtile->getPosition();

				Tile* oldtile = editor.map.swapTile(pos, newtile);
				TileLocation* location = newtile->getLocation();

				newtile->update();

				// std::cout << "\tSwitched tile at " << pos.x << ";" << pos.y << ";" << pos.z << " from " << (void*)oldtile << " to " << *data <<  std::endl;
				if (newtile->isSelected()) {
					editor.selection.addInternal(newtile);
				}

				if (oldtile) {
					if (newtile->getHouseID() != oldtile->getHouseID()) {
						// oooooomggzzz we need to add it to the appropriate house!
						House* house = editor.map.houses.getHouse(oldtile->getHouseID());
						if (house) {
							house->removeTile(oldtile);
						}

						house = editor.map.houses.getHouse(newtile->getHouseID());
						if (house) {
							house->addTile(newtile);
						}
					}
					if (oldtile->spawn) {
						if (newtile->spawn) {
							if (*oldtile->spawn != *newtile->spawn) {
								editor.map.removeSpawn(oldtile);
								editor.map.addSpawn(newtile);
							}
						} else {
							// Spawn has been removed
							editor.map.removeSpawn(oldtile);
						}
					} else if (newtile->spawn) {
						editor.map.addSpawn(newtile);
					}

					// oldtile->update();
					if (oldtile->isSelected()) {
						editor.selection.removeInternal(oldtile);
					}

					*data = oldtile;
				} else {
					*data = editor.map.allocator(location);
					if (newtile->getHouseID() != 0) {
						// oooooomggzzz we need to add it to the appropriate house!
						House* house = editor.map.houses.getHouse(newtile->getHouseID());
						if (house) {
							house->addTile(newtile);
						}
					}

					if (newtile->spawn) {
						editor.map.addSpawn(newtile);
					}
				}
				// Mark the tile as modified
				newtile->modify();

				break;
			}

			case CHANGE_MOVE_HOUSE_EXIT: {
				auto* p = reinterpret_cast<std::pair<uint32_t, Position>*>(c->data);
				ASSERT(p);
				House* whathouse = editor.map.houses.getHouse(p->first);

				if (whathouse) {
					Position oldpos = whathouse->getExit();
					whathouse->setExit(p->second);
					p->second = oldpos;
				}
				break;
			}

			case CHANGE_MOVE_WAYPOINT: {
				auto* p = reinterpret_cast<std::pair<std::string, Position>*>(c->data);
				ASSERT(p);
				Waypoint* wp = editor.map.waypoints.getWaypoint(p->first);

				if (wp) {
					// Change the tiles
					TileLocation* oldtile = editor.map.getTileL(wp->pos);
					TileLocation* newtile = editor.map.getTileL(p->second);

					// Only need to remove from old if it actually exists
					if (p->second != Position()) {
						if (oldtile && oldtile->getWaypointCount() > 0) {
							oldtile->decreaseWaypointCount();
						}
					}

					if (newtile) {
						newtile->increaseWaypointCount();
					}

					// Update shit
					Position oldpos = wp->pos;
					wp->pos = p->second;
					p->second = oldpos;
				}
				break;
			}

			case CHANGE_ZONE_REGISTRY:
			case CHANGE_RENAME_ZONE:
				applyZoneChange(c);
				break;

			case CHANGE_HOUSE_REGISTRY:
				if (!applyHouseChange(c)) {
					editor.selection.finish(Selection::INTERNAL);
					return false;
				}
				break;

			default:
				break;
		}
		++it;
	}
	editor.selection.finish(Selection::INTERNAL);
	commited = true;
	return true;
}

bool Action::undo() {
	if (!commited) {
		return true;
	}
	if (changes.empty()) {
		commited = false;
		return true;
	}
	if (!canApplyHouseChanges()) {
		return false;
	}

	editor.selection.start(Selection::INTERNAL);
	auto it = changes.rbegin();

	while (it != changes.rend()) {
		Change* c = *it;
		switch (c->type) {
			case CHANGE_TILE: {
				void** data = &c->data;
				Tile* oldtile = reinterpret_cast<Tile*>(*data);
				ASSERT(oldtile);
				Position pos = oldtile->getPosition();

				Tile* newtile = editor.map.swapTile(pos, oldtile);

				if (oldtile->isSelected()) {
					editor.selection.addInternal(oldtile);
				}
				if (newtile->isSelected()) {
					editor.selection.removeInternal(newtile);
				}

				if (newtile->getHouseID() != oldtile->getHouseID()) {
					// oooooomggzzz we need to remove it from the appropriate house!
					House* house = editor.map.houses.getHouse(newtile->getHouseID());
					if (house) {
						house->removeTile(newtile);
					} else {
						// Set tile house to 0, house has been removed
						newtile->setHouse(nullptr);
					}

					house = editor.map.houses.getHouse(oldtile->getHouseID());
					if (house) {
						house->addTile(oldtile);
					}
				}

				if (oldtile->spawn) {
					if (newtile->spawn) {
						if (*oldtile->spawn != *newtile->spawn) {
							editor.map.removeSpawn(newtile);
							editor.map.addSpawn(oldtile);
						}
					} else {
						editor.map.addSpawn(oldtile);
					}
				} else if (newtile->spawn) {
					editor.map.removeSpawn(newtile);
				}
				*data = newtile;

				break;
			}

			case CHANGE_MOVE_HOUSE_EXIT: {
				auto* p = reinterpret_cast<std::pair<uint32_t, Position>*>(c->data);
				ASSERT(p);
				House* whathouse = editor.map.houses.getHouse(p->first);
				if (whathouse) {
					Position oldpos = whathouse->getExit();
					whathouse->setExit(p->second);
					p->second = oldpos;
				}
				break;
			}

			case CHANGE_MOVE_WAYPOINT: {
				auto* p = reinterpret_cast<std::pair<std::string, Position>*>(c->data);
				ASSERT(p);
				Waypoint* wp = editor.map.waypoints.getWaypoint(p->first);

				if (wp) {
					// Change the tiles
					TileLocation* oldtile = editor.map.getTileL(wp->pos);
					TileLocation* newtile = editor.map.getTileL(p->second);

					// Only need to remove from old if it actually exists
					if (p->second != Position()) {
						if (oldtile && oldtile->getWaypointCount() > 0) {
							oldtile->decreaseWaypointCount();
						}
					}

					if (newtile) {
						newtile->increaseWaypointCount();
					}

					// Update shit
					Position oldpos = wp->pos;
					wp->pos = p->second;
					p->second = oldpos;
				}
				break;
			}

			case CHANGE_ZONE_REGISTRY:
			case CHANGE_RENAME_ZONE:
				applyZoneChange(c);
				break;

			case CHANGE_HOUSE_REGISTRY:
				if (!applyHouseChange(c)) {
					editor.selection.finish(Selection::INTERNAL);
					return false;
				}
				break;

			default:
				break;
		}
		++it;
	}
	editor.selection.finish(Selection::INTERNAL);
	commited = false;
	return true;
}

BatchAction::BatchAction(Editor& editor, ActionIdentifier ident) :
	editor(editor),
	timestamp(0),
	memory_size(0),
	type(ident) {
	if (editor.multiplayer && editor.multiplayer->active() && !editor.multiplayer->internalChange()) {
		multiplayerGroup = editor.multiplayer.get();
		multiplayerGroup->beginActionGroup();
	}
}

BatchAction::~BatchAction() {
	for (Action* action : batch) {
		delete action;
	}
	batch.clear();
	if (multiplayerGroup) multiplayerGroup->endActionGroup();
}

size_t BatchAction::memsize(bool recalc) const {
	// Expensive operation, only evaluate once (won't change anyways)
	if (!recalc && memory_size > 0) {
		return memory_size;
	}

	uint32_t mem = sizeof(*this);
	mem += sizeof(Action*) * 3 * batch.size();

	for (Action* action : batch) {
#ifdef __USE_EXACT_MEMSIZE__
		mem += action->memsize();
#else
		// Less exact but MUCH faster
		mem += action->approx_memsize();
#endif
	}

	const_cast<BatchAction*>(this)->memory_size = mem;
	return mem;
}

void BatchAction::addAction(Action* action) {
	if (!action) {
		return;
	}

	// If empty, do nothing.
	if (action->size() == 0) {
		delete action;
		return;
	}

	ASSERT(action->getType() == type);

	if (!editor.CanEdit()) {
		delete action;
		return;
	}

	// Add it!
	batch.push_back(action);
	timestamp = time(nullptr);
}

bool BatchAction::addAndCommitAction(Action* action) {
	if (!action) {
		return false;
	}

	// If empty, do nothing.
	if (action->size() == 0) {
		delete action;
		return false;
	}

	if (!editor.CanEdit()) {
		delete action;
		return false;
	}

	if (!action->commit()) {
		delete action;
		return false;
	}
	batch.push_back(action);
	timestamp = time(nullptr);
	return true;
}

void BatchAction::rollback() {
	undo();
}

bool BatchAction::commit() {
	std::vector<Action*> committedActions;
	for (Action* action : batch) {
		if (action && !action->isCommited()) {
			if (!action->commit()) {
				for (Action* committedAction : std::views::reverse(committedActions)) {
					committedAction->undo();
				}
				return false;
			}
			committedActions.push_back(action);
		}
	}
	return true;
}

bool BatchAction::canUndoHouseChanges() const {
	if (type != ACTION_PASTE_TILES) {
		return true;
	}

	std::set<Position> affectedPositions;
	for (const Action* action : batch) {
		if (!action) {
			continue;
		}
		for (const Change* change : action->changes) {
			if (change && change->type == CHANGE_TILE) {
				const auto* tile = reinterpret_cast<const Tile*>(change->data);
				if (tile) {
					affectedPositions.insert(tile->getPosition());
				}
			}
		}
	}

	std::vector<HousePasteTransaction::UndoState> states;
	for (const Action* action : batch) {
		if (!action) {
			continue;
		}
		for (const Change* change : action->changes) {
			if (!change || change->type != CHANGE_HOUSE_REGISTRY) {
				continue;
			}

			const auto* registryChange = reinterpret_cast<const HouseRegistryChange*>(change->data);
			if (!registryChange || registryChange->add || registryChange->activeHouseSessionId == InvalidSessionId) {
				return false;
			}

			const House* house = editor.map.houses.getHouse(registryChange->snapshot.id);
			HousePasteTransaction::UndoState state;
			state.exists = house != nullptr;
			if (house) {
				state.sessionMatches = house->getSessionId() == registryChange->activeHouseSessionId;
				state.snapshotMatches = house->getSnapshot() == registryChange->snapshot;
				state.houseTileCount = house->tileCount();
				for (const Position& position : affectedPositions) {
					const Tile* tile = editor.map.getTile(position);
					if (tile && tile->getHouseID() == registryChange->snapshot.id) {
						++state.affectedHouseTileCount;
					}
				}
			}
			states.push_back(state);
		}
	}

	return HousePasteTransaction::CanUndo(states);
}

bool BatchAction::canRedoHouseChanges() const {
	if (type != ACTION_PASTE_TILES) {
		return true;
	}

	std::vector<HousePasteTransaction::RedoState> states;
	for (const Action* action : batch) {
		if (!action) {
			continue;
		}
		for (const Change* change : action->changes) {
			if (!change || change->type != CHANGE_HOUSE_REGISTRY) {
				continue;
			}

			const auto* registryChange = reinterpret_cast<const HouseRegistryChange*>(change->data);
			if (!registryChange || !registryChange->add || registryChange->activeHouseSessionId != InvalidSessionId) {
				return false;
			}
			states.push_back({ editor.map.houses.getHouse(registryChange->snapshot.id) != nullptr });
		}
	}

	return HousePasteTransaction::CanRedo(states);
}

bool BatchAction::undo() {
	if (!canUndoHouseChanges()) {
		return false;
	}

	std::vector<Action*> undoneActions;
	for (Action* action : std::views::reverse(batch)) {
		if (action) {
			if (!action->undo()) {
				for (Action* undoneAction : std::views::reverse(undoneActions)) {
					undoneAction->redo();
				}
				return false;
			}
			undoneActions.push_back(action);
		}
	}
	return true;
}

bool BatchAction::redo() {
	if (!canRedoHouseChanges()) {
		return false;
	}

	std::vector<Action*> redoneActions;
	for (Action* action : batch) {
		if (action) {
			if (!action->redo()) {
				for (Action* redoneAction : std::views::reverse(redoneActions)) {
					redoneAction->undo();
				}
				return false;
			}
			redoneActions.push_back(action);
		}
	}
	return true;
}

void BatchAction::merge(BatchAction* other) {
	batch.insert(batch.end(), other->batch.begin(), other->batch.end());
	other->batch.clear();
}

ActionQueue::ActionQueue(Editor& editor) :
	current(0), memory_size(0), editor(editor) {
	////
}

ActionQueue::~ActionQueue() {
	for (auto it = actions.begin(); it != actions.end(); it = actions.erase(it)) {
		delete *it;
	}
}

Action* ActionQueue::createAction(ActionIdentifier ident) {
	return newd Action(editor, ident);
}

Action* ActionQueue::createAction(BatchAction* batch) {
	return newd Action(editor, batch->getType());
}

BatchAction* ActionQueue::createBatch(ActionIdentifier ident) {
	return newd BatchAction(editor, ident);
}

void ActionQueue::resetTimer() {
	if (!actions.empty()) {
		actions.back()->resetTimer();
	}
}

void ActionQueue::addBatch(BatchAction* batch, int stacking_delay) {
	ASSERT(batch);
	ASSERT(current <= actions.size());

	if (!batch) {
		return;
	}

	if (batch->size() == 0) {
		delete batch;
		return;
	}

	// Commit any uncommited actions. A failed batch must not enter history.
	if (!batch->commit()) {
		batch->rollback();
		delete batch;
		return;
	}

	// Update title
	if (editor.multiplayer && editor.multiplayer->active() && !editor.multiplayer->internalChange()) {
		editor.multiplayer->consumeBatch(batch);
		return;
	}

	// Update title
	if (editor.map.doChange()) {
		g_gui.UpdateTitle();
	}

	while (current != actions.size()) {
		memory_size -= actions.back()->memsize();
		BatchAction* todelete = actions.back();
		actions.pop_back();
		delete todelete;
	}

	bool merged = false;
	if (!actions.empty()) {
		BatchAction* lastAction = actions.back();
		if (lastAction->type == batch->type && g_settings.getInteger(Config::GROUP_ACTIONS) && time(nullptr) - stacking_delay < lastAction->timestamp) {
			lastAction->merge(batch);
			lastAction->timestamp = time(nullptr);
			memory_size -= lastAction->memsize();
			memory_size += lastAction->memsize(true);
			delete batch;
			merged = true;
		}
	}

	if (!merged) {
		memory_size += batch->memsize();
		actions.push_back(batch);
		batch->timestamp = time(nullptr);
		current++;
	}

	const size_t max_undo_memory = static_cast<size_t>(std::max(0, g_settings.getInteger(Config::UNDO_MEM_SIZE))) * 1024ULL * 1024ULL;
	while (memory_size > max_undo_memory && !actions.empty()) {
		memory_size -= actions.front()->memsize();
		delete actions.front();
		actions.pop_front();
		current--;
	}

	const size_t max_undo_size = static_cast<size_t>(std::max(0, g_settings.getInteger(Config::UNDO_SIZE)));
	while (actions.size() > max_undo_size && !actions.empty()) {
		memory_size -= actions.front()->memsize();
		BatchAction* todelete = actions.front();
		actions.pop_front();
		delete todelete;
		current--;
	}
}

void ActionQueue::addAction(Action* action, int stacking_delay) {
	if (!action) {
		return;
	}

	BatchAction* batch = createBatch(action->getType());
	batch->addAndCommitAction(action);
	if (batch->size() == 0) {
		delete batch;
		return;
	}

	addBatch(batch, stacking_delay);
}

bool ActionQueue::undo() {
	if (editor.multiplayer && editor.multiplayer->active()) return editor.multiplayer->undo();
	if (current > 0) {
		BatchAction* batch = actions[current - 1];
		if (!batch->undo()) {
			return false;
		}
		--current;
		return true;
	}
	return false;
}

bool ActionQueue::redo() {
	if (editor.multiplayer && editor.multiplayer->active()) return editor.multiplayer->redo();
	if (current < actions.size()) {
		BatchAction* batch = actions[current];
		if (!batch->redo()) {
			return false;
		}
		++current;
		return true;
	}
	return false;
}

void ActionQueue::clear() {
	for (auto it = actions.begin(); it != actions.end();) {
		delete *it;
		it = actions.erase(it);
	}
	current = 0;
	memory_size = 0;
}

bool ActionQueue::canUndo() {
	return editor.multiplayer && editor.multiplayer->active() ? editor.multiplayer->canUndo() : current > 0;
}
bool ActionQueue::canRedo() {
	return editor.multiplayer && editor.multiplayer->active() ? editor.multiplayer->canRedo() : current < actions.size();
}

ActionIdentifier ActionQueue::getUndoType() const {
	return current > 0 ? actions[current - 1]->getType() : ACTION_NONE;
}

ActionIdentifier ActionQueue::getRedoType() const {
	return current < actions.size() ? actions[current]->getType() : ACTION_NONE;
}
