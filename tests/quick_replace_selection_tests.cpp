// SPDX-License-Identifier: GPL-3.0-or-later
// Full editor fixtures for selected-area candidate discovery and replacement.
#include "main.h"
#include "complexitem.h"
#include "copybuffer.h"
#include "doodad_brush.h"
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

	bool HasItem(const Tile& tile, uint16_t id) {
		return std::any_of(tile.items.begin(), tile.items.end(), [id](const Item* item) { return item->getID() == id; });
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
		first->items.push_back(Item::Create(100));

		const auto candidates = CollectQuickReplaceCandidates({ first, second, third });
		QuickReplaceCheck(candidates.size() == 9, "Quick Replace must omit meta items and keep ID/category collisions separate");
		std::unordered_map<uint16_t, QuickReplaceCandidate> byId;
		for (const auto& candidate : candidates) {
			if (candidate.mapItemId != 100) {
				byId.emplace(candidate.mapItemId, candidate);
			}
		}
		const auto ground100 = std::find_if(candidates.begin(), candidates.end(), [](const QuickReplaceCandidate& candidate) { return candidate.mapItemId == 100 && candidate.category == QuickReplaceCategory::Ground; });
		const auto item100 = std::find_if(candidates.begin(), candidates.end(), [](const QuickReplaceCandidate& candidate) { return candidate.mapItemId == 100 && candidate.category == QuickReplaceCategory::Item; });
		QuickReplaceCheck(ground100 != candidates.end() && ground100->count == 2, "Ground ID/category candidate mismatch");
		QuickReplaceCheck(item100 != candidates.end() && item100->count == 1, "Stacked same-ID item must be a separate candidate");
		const std::optional<size_t> preferredItem = FindQuickReplaceCandidateIndex(candidates, { 100, QuickReplaceCategory::Item });
		QuickReplaceCheck(preferredItem && candidates[*preferredItem].category == QuickReplaceCategory::Item, "Category-aware auto-next confused same-ID candidates");
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
		// Deliberately model a malformed/mixed placement where the same ID also
		// appears as a stacked item. Quick Replace must honor the displayed category.
		left->items.push_back(Item::Create(100));

		ReplacementRule rule;
		rule.sourceServerId = ServerItemId(100);
		rule.targets.push_back(ReplacementTarget::ForItem(ServerItemId(102), 100));
		ReplaceExecutionOptions options;
		options.dryRun = false;
		options.includeContainerContents = false;
		options.matchFilter = [](const Tile& tile, const Item& item) { return ClassifyPlacedItem(tile, item) == QuickReplaceCategory::Ground; };
		const ReplaceExecutionResult result = ReplaceEngine::Run(editor, { left, right }, { rule }, options);
		QuickReplaceCheck(result.committed && result.replacements == 2 && result.changedTiles == 2, "Quick Replace must commit selected visible items as one action");
		QuickReplaceCheck(editor.map.getTile(selectedLeft)->ground->getID() == 102 && editor.map.getTile(selectedRight)->ground->getID() == 102, "Selected ground was not replaced");
		QuickReplaceCheck(editor.map.getTile(unselectedMiddle)->ground->getID() == 100, "Unselected tile inside bounding rectangle changed");
		auto* replacedChest = dynamic_cast<Container*>(editor.map.getTile(selectedLeft)->items.front());
		QuickReplaceCheck(replacedChest && replacedChest->getItemCount() == 20 && replacedChest->getItem(0)->getID() == 100, "Quick Replace changed nested container contents");
		QuickReplaceCheck(HasItem(*editor.map.getTile(selectedLeft), 100), "Quick Replace changed an occurrence outside the selected category");
		const auto remainingCandidates = CollectQuickReplaceCandidates({ editor.map.getTile(selectedLeft), editor.map.getTile(selectedRight) });
		QuickReplaceCheck(!FindQuickReplaceCandidateIndex(remainingCandidates, { 100, QuickReplaceCategory::Ground }), "Removed Ground candidate remained after replacement");
		QuickReplaceCheck(FindQuickReplaceCandidateIndex(remainingCandidates, { 100, QuickReplaceCategory::Item }).has_value(), "Auto-next lost the remaining same-ID Item candidate");
		QuickReplaceCheck(editor.actionQueue->canUndo() && editor.actionQueue->getUndoType() == ACTION_REPLACE_ITEMS, "Quick Replace did not create one undo action");
		QuickReplaceCheck(editor.actionQueue->undo(), "Quick Replace undo failed");
		QuickReplaceCheck(editor.map.getTile(selectedLeft)->ground->getID() == 100 && editor.map.getTile(selectedRight)->ground->getID() == 100, "Undo did not restore all selected ground items");
		QuickReplaceCheck(editor.map.getTile(unselectedMiddle)->ground->getID() == 100, "Undo affected the unselected tile");
		QuickReplaceCheck(editor.actionQueue->redo(), "Quick Replace redo failed");
		QuickReplaceCheck(editor.map.getTile(selectedLeft)->ground->getID() == 102 && editor.map.getTile(selectedRight)->ground->getID() == 102, "Redo did not reapply all selected ground items");

		ReplacementRule itemRule;
		itemRule.sourceServerId = ServerItemId(100);
		itemRule.targets.push_back(ReplacementTarget::ForItem(ServerItemId(200), 100));
		ReplaceExecutionOptions itemOptions;
		itemOptions.dryRun = false;
		itemOptions.includeContainerContents = false;
		itemOptions.matchFilter = [](const Tile& tile, const Item& item) { return ClassifyPlacedItem(tile, item) == QuickReplaceCategory::Item; };
		const ReplaceExecutionResult itemResult = ReplaceEngine::Run(editor, { editor.map.getTile(selectedLeft) }, { itemRule }, itemOptions);
		QuickReplaceCheck(itemResult.committed && itemResult.replacements == 1, "Item-category replacement did not isolate the stacked same-ID occurrence");
		QuickReplaceCheck(editor.map.getTile(selectedLeft)->ground->getID() == 102 && !HasItem(*editor.map.getTile(selectedLeft), 100), "Item-category replacement changed the ground or missed the stacked item");
		QuickReplaceCheck(editor.actionQueue->undo(), "Item-category replacement undo failed");
		QuickReplaceCheck(editor.map.getTile(selectedLeft)->ground->getID() == 102 && HasItem(*editor.map.getTile(selectedLeft), 100), "Item-category undo did not restore only the stacked item");

		ReplaceExecutionOptions advancedDefaults;
		const ReplaceExecutionResult recursiveDryRun = ReplaceEngine::Run(editor, { editor.map.getTile(selectedLeft) }, { rule }, advancedDefaults);
		QuickReplaceCheck(recursiveDryRun.matchedItems == 21, "Advanced Replace default must continue scanning container contents and visible items");
		std::cout << "PASS exact non-rectangular selection, visible-only containers, ground safety and undo/redo\n";
	}

	void AutomaticBordersAreAtomic() {
		Definitions definitions;
		definitions.Add(100, "Wooden floor").group = ITEM_GROUP_GROUND;
		definitions.Add(102, "Stone floor").group = ITEM_GROUP_GROUND;
		definitions.Add(300, "Old border").isBorder = true;

		CopyBuffer strictCopyBuffer;
		Editor strictEditor(strictCopyBuffer, nullptr);
		const Position strictChanged(90, 90, 7);
		const Position strictAdjacent(91, 90, 7);
		Tile* strictSource = AddTile(strictEditor.map, strictChanged, 100);
		AddTile(strictEditor.map, strictAdjacent, 100)->addItem(Item::Create(300));

		ReplacementRule rule;
		rule.sourceServerId = ServerItemId(100);
		rule.targets.push_back(ReplacementTarget::ForItem(ServerItemId(102), 100));
		ReplaceExecutionOptions strictOptions;
		strictOptions.dryRun = false;
		strictOptions.includeContainerContents = false;
		const ReplaceExecutionResult strictResult = ReplaceEngine::Run(strictEditor, { strictSource }, { rule }, strictOptions);
		QuickReplaceCheck(strictResult.committed && strictEditor.map.getTile(strictChanged)->ground->getID() == 102, "Strict-scope floor replacement failed");
		QuickReplaceCheck(HasItem(*strictEditor.map.getTile(strictAdjacent), 300), "Border rebuild OFF changed an adjacent unselected tile");

		CopyBuffer copyBuffer;
		Editor editor(copyBuffer, nullptr);
		const Position changed(100, 100, 7);
		const Position adjacent(101, 100, 7);
		const Position distant(102, 100, 7);
		Tile* source = AddTile(editor.map, changed, 100);
		AddTile(editor.map, adjacent, 100)->addItem(Item::Create(300));
		AddTile(editor.map, distant, 100)->addItem(Item::Create(300));

		ReplaceExecutionOptions options;
		options.dryRun = false;
		options.includeContainerContents = false;
		options.rebuildGroundBorders = true;
		const ReplaceExecutionResult result = ReplaceEngine::Run(editor, { source }, { rule }, options);
		QuickReplaceCheck(result.committed && result.bordersRebuilt >= 2, "Automatic border rebuild did not commit");
		QuickReplaceCheck(!HasItem(*editor.map.getTile(adjacent), 300), "Adjacent stale border was not rebuilt");
		QuickReplaceCheck(HasItem(*editor.map.getTile(distant), 300), "Border rebuild escaped the one-tile neighborhood");
		QuickReplaceCheck(editor.actionQueue->undo(), "Automatic border undo failed");
		QuickReplaceCheck(editor.map.getTile(changed)->ground->getID() == 100 && HasItem(*editor.map.getTile(adjacent), 300), "One undo did not restore floor and border together");
		QuickReplaceCheck(editor.actionQueue->redo(), "Automatic border redo failed");
		QuickReplaceCheck(editor.map.getTile(changed)->ground->getID() == 102 && !HasItem(*editor.map.getTile(adjacent), 300), "One redo did not reapply floor and border together");
		std::cout << "PASS automatic neighboring borders with atomic undo/redo\n";
	}

	void RemoveSelectedCategoryIsAtomic() {
		Definitions definitions;
		definitions.Add(100, "Floor and stacked fixture").group = ITEM_GROUP_GROUND;

		CopyBuffer copyBuffer;
		Editor editor(copyBuffer, nullptr);
		const Position firstPosition(150, 150, 7);
		const Position secondPosition(151, 150, 7);
		Tile* first = AddTile(editor.map, firstPosition, 100);
		Tile* second = AddTile(editor.map, secondPosition, 100);
		first->items.push_back(Item::Create(100));
		second->items.push_back(Item::Create(100));

		ReplacementRule rule;
		rule.sourceServerId = ServerItemId(100);
		rule.targets.push_back(ReplacementTarget::ForTrash(100));
		ReplaceExecutionOptions options;
		options.dryRun = false;
		options.includeContainerContents = false;
		options.matchFilter = [](const Tile& tile, const Item& item) { return ClassifyPlacedItem(tile, item) == QuickReplaceCategory::Item; };
		const ReplaceExecutionResult result = ReplaceEngine::Run(editor, { first, second }, { rule }, options);
		QuickReplaceCheck(result.committed && result.deletions == 2 && result.changedTiles == 2, "Quick Remove did not remove every matching selected item");
		QuickReplaceCheck(editor.map.getTile(firstPosition)->ground && editor.map.getTile(secondPosition)->ground, "Quick Remove deleted a same-ID item from the wrong category");
		QuickReplaceCheck(!HasItem(*editor.map.getTile(firstPosition), 100) && !HasItem(*editor.map.getTile(secondPosition), 100), "Quick Remove left a matching visible item behind");
		QuickReplaceCheck(editor.actionQueue->undo(), "Quick Remove undo failed");
		QuickReplaceCheck(HasItem(*editor.map.getTile(firstPosition), 100) && HasItem(*editor.map.getTile(secondPosition), 100), "One undo did not restore all removed items");
		QuickReplaceCheck(editor.actionQueue->redo(), "Quick Remove redo failed");
		QuickReplaceCheck(!HasItem(*editor.map.getTile(firstPosition), 100) && !HasItem(*editor.map.getTile(secondPosition), 100), "One redo did not remove all matching items again");
		std::cout << "PASS category-aware removal with atomic undo/redo\n";
	}

	void CompleteDoodadBrushIsAtomic() {
		Definitions definitions;
		definitions.Add(100, "Floor").group = ITEM_GROUP_GROUND;
		definitions.Add(500, "Old object");
		definitions.Add(600, "Statue base");
		definitions.Add(601, "Statue top");

		pugi::xml_document document;
		QuickReplaceCheck(
			document.load("<brush name='Complete statue' lookid='1600'><alternate><composite chance='100'><tile x='0' y='0'><item id='600'/></tile><tile x='1' y='0'><item id='601'/></tile></composite></alternate></brush>"),
			"Doodad brush XML fixture failed"
		);
		DoodadBrush statue;
		wxArrayString warnings;
		QuickReplaceCheck(statue.load(document.child("brush"), warnings), "Doodad brush fixture failed to load");

		CopyBuffer copyBuffer;
		Editor editor(copyBuffer, nullptr);
		const Position anchor(200, 200, 7);
		const Position secondPart(201, 200, 7);
		Tile* source = AddTile(editor.map, anchor, 100);
		source->addItem(Item::Create(500));
		AddTile(editor.map, secondPart, 100);

		ReplacementRule rule;
		rule.sourceServerId = ServerItemId(500);
		rule.targets.push_back(ReplacementTarget::ForItem(ServerItemId(600), 100));
		ReplaceExecutionOptions options;
		options.dryRun = false;
		options.includeContainerContents = false;
		options.doodadReplacementBrush = &statue;
		options.doodadComposite = &statue.getComposite(0);
		options.doodadAllowedPositions = { anchor };
		options.enforceDoodadSelectionScope = true;
		const ReplaceExecutionResult declined = ReplaceEngine::Run(editor, { source }, { rule }, options);
		QuickReplaceCheck(!declined.committed && declined.doodadOutsideScopeTiles == 1, "Outside-selection Doodad did not request confirmation");
		QuickReplaceCheck(HasItem(*editor.map.getTile(anchor), 500) && !HasItem(*editor.map.getTile(secondPart), 601), "Default-No Doodad preflight changed the map");

		options.allowDoodadOutsideScope = true;
		const ReplaceExecutionResult result = ReplaceEngine::Run(editor, { source }, { rule }, options);
		QuickReplaceCheck(result.committed && result.doodadPlacements == 1 && result.doodadTilesChanged == 2, "Complete Doodad brush was not applied");
		QuickReplaceCheck(!HasItem(*editor.map.getTile(anchor), 500) && HasItem(*editor.map.getTile(anchor), 600) && HasItem(*editor.map.getTile(secondPart), 601), "Doodad composite pieces were not assembled");
		QuickReplaceCheck(editor.actionQueue->undo(), "Complete Doodad undo failed");
		QuickReplaceCheck(HasItem(*editor.map.getTile(anchor), 500) && !HasItem(*editor.map.getTile(anchor), 600) && !HasItem(*editor.map.getTile(secondPart), 601), "One undo did not restore the replaced Doodad");
		QuickReplaceCheck(editor.actionQueue->redo(), "Complete Doodad redo failed");
		QuickReplaceCheck(!HasItem(*editor.map.getTile(anchor), 500) && HasItem(*editor.map.getTile(anchor), 600) && HasItem(*editor.map.getTile(secondPart), 601), "One redo did not restore the complete Doodad");

		CopyBuffer insideCopyBuffer;
		Editor insideEditor(insideCopyBuffer, nullptr);
		Tile* insideSource = AddTile(insideEditor.map, anchor, 100);
		insideSource->addItem(Item::Create(500));
		AddTile(insideEditor.map, secondPart, 100);
		options.allowDoodadOutsideScope = false;
		options.doodadAllowedPositions = { anchor, secondPart };
		const ReplaceExecutionResult inside = ReplaceEngine::Run(insideEditor, { insideSource }, { rule }, options);
		QuickReplaceCheck(inside.committed && inside.doodadOutsideScopeTiles == 0, "Fully selected Doodad incorrectly required outside-scope confirmation");
		std::cout << "PASS complete Doodad brush placement with atomic undo/redo\n";
	}

	void BulkUndoAndInvalidTargetSafety() {
		Definitions definitions;
		definitions.Add(100, "Old floor").group = ITEM_GROUP_GROUND;
		definitions.Add(102, "New floor").group = ITEM_GROUP_GROUND;

		CopyBuffer copyBuffer;
		Editor editor(copyBuffer, nullptr);
		std::vector<Tile*> tiles;
		PositionVector positions;
		for (int index = 0; index < 128; ++index) {
			positions.emplace_back(300 + index % 32, 300 + index / 32, 7);
			tiles.push_back(AddTile(editor.map, positions.back(), 100));
		}

		ReplacementRule invalidRule;
		invalidRule.sourceServerId = ServerItemId(100);
		invalidRule.targets.push_back(ReplacementTarget::ForItem(ServerItemId(65000), 100));
		ReplaceExecutionOptions options;
		options.dryRun = false;
		options.includeContainerContents = false;
		const ReplaceExecutionResult invalid = ReplaceEngine::Run(editor, tiles, { invalidRule }, options);
		QuickReplaceCheck(!invalid.validation.isValid() && !invalid.committed, "Invalid replacement target was not rejected");
		QuickReplaceCheck(tiles.front()->ground->getID() == 100, "Invalid replacement target changed the source item");

		ReplacementRule rule;
		rule.sourceServerId = ServerItemId(100);
		rule.targets.push_back(ReplacementTarget::ForItem(ServerItemId(102), 100));
		const ReplaceExecutionResult result = ReplaceEngine::Run(editor, tiles, { rule }, options);
		QuickReplaceCheck(result.committed && result.replacements == 128 && result.changedTiles == 128, "Bulk Quick Replace did not update every tile");
		QuickReplaceCheck(editor.actionQueue->undo(), "Bulk Quick Replace undo failed");
		QuickReplaceCheck(std::all_of(positions.begin(), positions.end(), [&editor](const Position& position) { return editor.map.getTile(position)->ground->getID() == 100; }), "One undo did not restore all bulk replacements");
		QuickReplaceCheck(editor.actionQueue->redo(), "Bulk Quick Replace redo failed");
		QuickReplaceCheck(std::all_of(positions.begin(), positions.end(), [&editor](const Position& position) { return editor.map.getTile(position)->ground->getID() == 102; }), "One redo did not reapply all bulk replacements");
		std::cout << "PASS invalid target safety and 128 replacements in one undo/redo action\n";
	}
}

void RunQuickReplaceSelectionTests() {
	CandidateCollection();
	ExactSelectionVisibleOnlyAndUndo();
	AutomaticBordersAreAtomic();
	RemoveSelectedCategoryIsAtomic();
	CompleteDoodadBrushIsAtomic();
	BulkUndoAndInvalidTargetSafety();
}
