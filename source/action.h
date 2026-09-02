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

#ifndef RME_ACTION_H_
#define RME_ACTION_H_

#include "position.h"

#include <deque>
#include <string>

class Editor;
class Tile;
class House;
struct HouseSnapshot;
class Waypoint;
class Change;
class Action;
class BatchAction;
class ActionQueue;
class MultiplayerSession;

enum ChangeType {
	CHANGE_NONE,
	CHANGE_TILE,
	CHANGE_MOVE_HOUSE_EXIT,
	CHANGE_MOVE_WAYPOINT,
	CHANGE_ZONE_REGISTRY,
	CHANGE_RENAME_ZONE,
	CHANGE_HOUSE_REGISTRY,
};

class Change {
private:
	struct ZoneRegistryChange {
		std::string name;
		unsigned int id;
		bool add;
	};

	struct ZoneRenameChange {
		std::string from;
		std::string to;
	};

	ChangeType type;
	void* data;

	Change();

public:
	Change(Tile* tile);
	static Change* Create(House* house, const Position& where);
	static Change* CreateHouse(const HouseSnapshot& snapshot);
	static Change* Create(Waypoint* wp, const Position& where);
	static Change* CreateZone(const std::string& name, unsigned int id, bool add);
	static Change* RenameZone(const std::string& oldName, const std::string& newName);
	~Change();
	void clear();

	ChangeType getType() const {
		return type;
	}
	void* getData() const {
		return data;
	}

	// Get memory footprint
	uint32_t memsize() const;

	friend class Action;
	friend class BatchAction;
};

typedef std::vector<Change*> ChangeList;

enum ActionIdentifier {
	ACTION_NONE,
	ACTION_MOVE,
	ACTION_REMOTE,
	ACTION_SELECT,
	ACTION_DELETE_TILES,
	ACTION_CUT_TILES,
	ACTION_PASTE_TILES,
	ACTION_RANDOMIZE,
	ACTION_BORDERIZE,
	ACTION_DRAW,
	ACTION_SWITCHDOOR,
	ACTION_ROTATE_ITEM,
	ACTION_REPLACE_ITEMS,
	ACTION_CHANGE_PROPERTIES,
	ACTION_GENERATE_AREA,
	ACTION_IMPORT_MINIMAP,
	ACTION_IMPORT_PNG,
	ACTION_ZONE_EDIT,
};

class Action {
public:
	virtual ~Action();

	void addChange(Change* t) {
		changes.push_back(t);
	}

	// Get memory footprint
	size_t approx_memsize() const;
	size_t memsize() const;
	size_t size() const {
		return changes.size();
	}
	ActionIdentifier getType() const {
		return type;
	}

	bool commit();
	bool isCommited() const {
		return commited;
	}
	bool undo();
	bool redo() {
		return commit();
	}

protected:
	Action(Editor& editor, ActionIdentifier ident);
	void applyZoneChange(Change* change);
	bool applyHouseChange(Change* change);
	bool canApplyHouseChanges() const;

	bool commited;
	ChangeList changes;
	Editor& editor;
	ActionIdentifier type;

	friend class ActionQueue;
	friend class BatchAction;
	friend class MultiplayerSession;
};

typedef std::vector<Action*> ActionVector;

class BatchAction {
public:
	virtual ~BatchAction();

	void resetTimer() {
		timestamp = 0;
	}

	// Get memory footprint
	size_t memsize(bool resize = false) const;
	size_t size() const {
		return batch.size();
	}
	ActionIdentifier getType() const {
		return type;
	}

	virtual void addAction(Action* action);
	virtual bool addAndCommitAction(Action* action);
	// Revert all already-committed child actions. Intended for transactional
	// producers that must recover if a later construction phase fails.
	void rollback();

protected:
	BatchAction(Editor& editor, ActionIdentifier ident);

	virtual bool commit();
	virtual bool undo();
	virtual bool redo();
	bool canUndoHouseChanges() const;
	bool canRedoHouseChanges() const;

	void merge(BatchAction* other);

	Editor& editor;
	int timestamp;
	uint32_t memory_size;
	ActionIdentifier type;
	ActionVector batch;
	MultiplayerSession* multiplayerGroup = nullptr;

	friend class ActionQueue;
	friend class MultiplayerSession;
};

class ActionQueue {
public:
	ActionQueue(Editor& editor);
	virtual ~ActionQueue();

	typedef std::deque<BatchAction*> ActionList;

	void resetTimer();

	virtual Action* createAction(ActionIdentifier ident);
	virtual Action* createAction(BatchAction* parent);
	virtual BatchAction* createBatch(ActionIdentifier ident);

	void addBatch(BatchAction* action, int stacking_delay = 0);
	void addAction(Action* action, int stacking_delay = 0);

	bool undo();
	bool redo();
	void clear();

	bool canUndo();
	bool canRedo();
	ActionIdentifier getUndoType() const;
	ActionIdentifier getRedoType() const;

protected:
	size_t current;
	size_t memory_size;
	Editor& editor;
	ActionList actions;
};

#endif
