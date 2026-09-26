// SPDX-License-Identifier: GPL-3.0-or-later
// Full editor fixtures for selected-area candidate discovery and replacement.
#include "main.h"
#include "complexitem.h"
#include "copybuffer.h"
#include "editor.h"
#include "items.h"
#include "map.h"
#include "replace_tool/quick_replace_selection_model.h"
#include "replace_tool/replace_engine.h"
#include "tile.h"

#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace {
	void QuickReplaceCheck(bool value, const char* message) {
		if (!value) {
			throw std::runtime_error(message);
		}
	}

	struct Definitions {
		ItemDatabase saved;

		Definitions() {
			g_items.swap(saved);
		}

		~Definitions() {
			g_items.clear();
			g_items.swap(saved);
		}

		ItemType& Add(uint16_t id, const char* name) {
			auto* type = new ItemType();
			type->id = id;
			type->clientID = static_cast<uint16_t>(id + 1000);
			type->name = name;
			g_items.items.set(id, type);
			return *type;
		}
	};

	Tile* AddTile(Map& map, const Position& position, uint16_t groundId) {
		auto* tile = map.allocator(map.createTileL(position));
		tile->addItem(Item::Create(groundId));
		map.setTile(position, tile);
		return tile;
	}

	void AddCopies(Tile& tile, uint16_t id, size_t count) {
		for (size_t index = 0; index < count; ++index) {
			tile.addItem(Item::Create(id));
		}
	}

	void CandidateCollection() {
		Definitions definitions;
		definitions.Add(100, "Wooden floor").group = ITEM_GROUP_GROUND;
		definitions.Add(102, "Stone floor").group = ITEM_GROUP_GROUND;
		definitions.Add(621, "Stone border").isBorder = true;
		definitions.Add(6894, "Stone wall").isWall = true;
		auto& door = definitions.Add(6900, "Closed door");
		door.type = ITEM_TYPE_DOOR;
		door.isWall = true;
		door.isBrushDoor = true;
		auto& carpet = definitions.Add(1157, "Red carpet");
		carpet.isCarpet = true;
		carpet.isWall = true; // Some real clients expose overlapping material flags.
		auto& table = definitions.Add(1298, "Wooden table");
		table.isTable = true;
		table.isWall = true;
		definitions.Add(1949, "Magic forcefield");
		definitions.Add(65000, "Editor marker").is_metaitem = true;

		Map map;
		Tile* first = AddTile(map, { 100, 100, 7 }, 100);
		Tile* second = AddTile(map, { 101, 100, 7 }, 100);
		Tile* third = AddTile(map, { 102, 100, 7 }, 102);
		AddCopies(*first, 621, 2);
		AddCopies(*second, 621, 1);
		AddCopies(*first, 6894, 4);
		AddCopies(*second, 6900, 2);
		AddCopies(*third, 1157, 1);
		AddCopies(*third, 1298, 1);
		AddCopies(*third, 1949, 2);
		AddCopies(*first, 65000, 5);

		const auto candidates = CollectQuickReplaceCandidates({ first, second, third });
		QuickReplaceCheck(candidates.size() == 8, "Quick Replace must omit meta items and keep each visible ID once");
		std::unordered_map<uint16_t, QuickReplaceCandidate> byId;
		for (const auto& candidate : candidates) {
			byId.emplace(candidate.mapItemId, candidate);
		}
		QuickReplaceCheck(byId.at(100).category == QuickReplaceCategory::Ground && byId.at(100).count == 2, "Ground count/category mismatch");
		QuickReplaceCheck(byId.at(102).category == QuickReplaceCategory::Ground && byId.at(102).count == 1, "Second ground ID missing");
		QuickReplaceCheck(byId.at(621).category == QuickReplaceCategory::Border && byId.at(621).count == 3, "Border count/category mismatch");
		QuickReplaceCheck(byId.at(6894).category == QuickReplaceCategory::Wall && byId.at(6894).count == 4, "Wall count/category mismatch");
		QuickReplaceCheck(byId.at(6900).category == QuickReplaceCategory::Door && byId.at(6900).count == 2, "Door must classify before wall");
		QuickReplaceCheck(byId.at(1157).category == QuickReplaceCategory::Carpet, "Carpet category mismatch");
		QuickReplaceCheck(byId.at(1298).category == QuickReplaceCategory::Table, "Table category mismatch");
		QuickReplaceCheck(byId.at(1949).category == QuickReplaceCategory::Item && byId.at(1949).count == 2, "Generic item count/category mismatch");
		QuickReplaceCheck(candidates.front().category == QuickReplaceCategory::Ground && candidates.front().mapItemId == 100, "Candidate ordering must start with the most frequent ground");
		std::cout << "PASS quick replace candidate collection, classification, counting and order\n";
	}

	void ExactSelectionVisibleOnlyAndUndo() {
		Definitions definitions;
		definitions.Add(100, "Wooden floor").group = ITEM_GROUP_GROUND;
		definitions.Add(102, "Stone floor").group = ITEM_GROUP_GROUND;
		definitions.Add(200, "Chest").type = ITEM_TYPE_CONTAINER;

		CopyBuffer copyBuffer;
		Editor editor(copyBuffer, nullptr);
		const Position selectedLeft(100, 100, 7);
		const Position unselectedMiddle(101, 100, 7);
		const Position selectedRight(102, 100, 7);
		Tile* left = AddTile(editor.map, selectedLeft, 100);
		AddTile(editor.map, unselectedMiddle, 100);
		Tile* right = AddTile(editor.map, selectedRight, 100);
		auto* chest = dynamic_cast<Container*>(Item::Create(200));
		QuickReplaceCheck(chest != nullptr, "Container fixture creation failed");
		for (size_t index = 0; index < 20; ++index) {
			chest->getVector().push_back(Item::Create(100));
		}
		left->addItem(chest);

		ReplacementRule rule;
		rule.sourceServerId = ServerItemId(100);
		rule.targets.push_back(ReplacementTarget::ForItem(ServerItemId(102), 100));
		ReplaceExecutionOptions options;
		options.dryRun = false;
		options.includeContainerContents = false;
		const ReplaceExecutionResult result = ReplaceEngine::Run(editor, { left, right }, { rule }, options);
		QuickReplaceCheck(result.committed && result.replacements == 2 && result.changedTiles == 2, "Quick Replace must commit selected visible items as one action");
		QuickReplaceCheck(editor.map.getTile(selectedLeft)->ground->getID() == 102 && editor.map.getTile(selectedRight)->ground->getID() == 102, "Selected ground was not replaced");
		QuickReplaceCheck(editor.map.getTile(unselectedMiddle)->ground->getID() == 100, "Unselected tile inside bounding rectangle changed");
		auto* replacedChest = dynamic_cast<Container*>(editor.map.getTile(selectedLeft)->items.front());
		QuickReplaceCheck(replacedChest && replacedChest->getItemCount() == 20 && replacedChest->getItem(0)->getID() == 100, "Quick Replace changed nested container contents");
		QuickReplaceCheck(editor.actionQueue->canUndo() && editor.actionQueue->getUndoType() == ACTION_REPLACE_ITEMS, "Quick Replace did not create one undo action");
		QuickReplaceCheck(editor.actionQueue->undo(), "Quick Replace undo failed");
		QuickReplaceCheck(editor.map.getTile(selectedLeft)->ground->getID() == 100 && editor.map.getTile(selectedRight)->ground->getID() == 100, "Undo did not restore all selected ground items");
		QuickReplaceCheck(editor.map.getTile(unselectedMiddle)->ground->getID() == 100, "Undo affected the unselected tile");
		QuickReplaceCheck(editor.actionQueue->redo(), "Quick Replace redo failed");
		QuickReplaceCheck(editor.map.getTile(selectedLeft)->ground->getID() == 102 && editor.map.getTile(selectedRight)->ground->getID() == 102, "Redo did not reapply all selected ground items");

		ReplaceExecutionOptions advancedDefaults;
		const ReplaceExecutionResult recursiveDryRun = ReplaceEngine::Run(editor, { editor.map.getTile(selectedLeft) }, { rule }, advancedDefaults);
		QuickReplaceCheck(recursiveDryRun.matchedItems == 20, "Advanced Replace default must continue scanning container contents");
		std::cout << "PASS exact non-rectangular selection, visible-only containers, ground safety and undo/redo\n";
	}
}

void RunQuickReplaceSelectionTests() {
	CandidateCollection();
	ExactSelectionVisibleOnlyAndUndo();
}
