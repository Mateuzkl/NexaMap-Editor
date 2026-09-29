//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "../main.h"
#include "replace_engine.h"

#include "replace_execution_plan.h"

#include "../action.h"
#include "../complexitem.h"
#include "../doodad_brush.h"
#include "../editor.h"
#include "../item.h"
#include "../items.h"
#include "../tile.h"

#include <algorithm>
#include <cassert>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <typeinfo>
#include <unordered_map>

namespace {
	using RuleLookup = std::unordered_map<uint16_t, const ReplacementRule*>;

	enum class PlannedAction {
		None,
		Replace,
		Delete,
	};

	struct PlannedItem {
		PlannedAction action = PlannedAction::None;
		uint16_t targetServerId = 0;
		std::vector<PlannedItem> contents;
		bool contentsPlanned = false;
		bool doodadAnchor = false;
		bool changed = false;
	};

	struct PlannedTile {
		PlannedItem ground;
		bool hasGround = false;
		std::vector<PlannedItem> items;
		bool changed = false;
	};

	class ExecutionState {
	public:
		ExecutionState(const RuleLookup& rules, uint32_t seed, const ReplaceExecutionOptions& options, ReplaceExecutionResult& result) :
			rules(rules),
			random(seed),
			includeContainerContents(options.includeContainerContents),
			doodadReplacement(options.doodadReplacementBrush != nullptr),
			matchFilter(options.matchFilter),
			result(result) { }

		PlannedItem PlanItem(Item* item, const Tile* placementTile = nullptr) {
			PlannedItem plan;
			if (!item) {
				return plan;
			}
			++result.itemsScanned;
			const bool placementMatches = !matchFilter || (placementTile && matchFilter(*placementTile, *item));
			const auto found = placementMatches ? rules.find(item->getID()) : rules.end();
			if (found != rules.end()) {
				++result.matchedItems;
				const ReplacementChoice choice = SelectReplacementTarget(*found->second, distribution(random));
				if (!choice.HasReplacement()) {
					++result.unchangedByProbability;
				} else if (choice.target->isTrash()) {
					++result.deletions;
					plan.action = PlannedAction::Delete;
					plan.changed = true;
					return plan;
				} else {
					++result.replacements;
					plan.action = doodadReplacement ? PlannedAction::Delete : PlannedAction::Replace;
					plan.doodadAnchor = doodadReplacement;
					if (!doodadReplacement) {
						plan.targetServerId = choice.target->serverId.value;
					}
					plan.changed = true;
				}
			}

			if (includeContainerContents) {
				if (auto* container = dynamic_cast<Container*>(item)) {
					plan.contentsPlanned = true;
					plan.contents = PlanItems(container->getVector(), nullptr);
					for (const PlannedItem& child : plan.contents) {
						plan.changed = plan.changed || child.changed;
					}
				}
			}
			return plan;
		}

		std::vector<PlannedItem> PlanItems(const ItemVector& items, const Tile* placementTile) {
			std::vector<PlannedItem> plans;
			plans.reserve(items.size());
			for (Item* item : items) {
				plans.push_back(PlanItem(item, placementTile));
			}
			return plans;
		}

		void ApplyItem(Item*& item, const PlannedItem& plan, ItemVector& promotedContents) const {
			if (!item) {
				return;
			}

			if (plan.contentsPlanned) {
				if (auto* container = dynamic_cast<Container*>(item)) {
					ApplyItems(container->getVector(), plan.contents);
				}
			}

			if (plan.action == PlannedAction::Delete) {
				delete item;
				item = nullptr;
				return;
			}
			if (plan.action != PlannedAction::Replace) {
				return;
			}

			if (!ReplaceItemPreservingState(item, plan.targetServerId, promotedContents)) {
				return;
			}
		}

		void ApplyItems(ItemVector& items, const std::vector<PlannedItem>& plans) const {
			assert(items.size() == plans.size());
			ItemVector promotedContents;
			auto iterator = items.begin();
			for (const PlannedItem& plan : plans) {
				if (iterator == items.end()) {
					break;
				}
				Item*& item = *iterator;
				ApplyItem(item, plan, promotedContents);
				if (!item) {
					iterator = items.erase(iterator);
				} else {
					++iterator;
				}
			}
			items.insert(items.end(), promotedContents.begin(), promotedContents.end());
		}

	private:
		const RuleLookup& rules;
		std::mt19937 random;
		std::uniform_int_distribution<uint32_t> distribution { 1, 100 };
		bool includeContainerContents;
		bool doodadReplacement;
		const std::function<bool(const Tile&, const Item&)>& matchFilter;
		ReplaceExecutionResult& result;
	};

	RuleLookup BuildRuleLookup(const std::vector<ReplacementRule>& rules) {
		RuleLookup lookup;
		lookup.reserve(rules.size());
		for (const ReplacementRule& rule : rules) {
			lookup.emplace(rule.sourceServerId.value, &rule);
		}
		return lookup;
	}

	uint32_t ResolveSeed(uint32_t requestedSeed) {
		if (requestedSeed != 0) {
			return requestedSeed;
		}
		std::random_device device;
		const uint32_t generated = device();
		return generated == 0 ? 1 : generated;
	}

	void AddBorderNeighborhood(std::set<Position>& positions, const Position& center) {
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				const Position position(center.x + dx, center.y + dy, center.z);
				if (position.isValid()) {
					positions.insert(position);
				}
			}
		}
	}

	void RemoveDuplicateWalls(const ItemVector& incomingItems, Tile& destination) {
		for (const Item* item : incomingItems) {
			if (item && item->getWallBrush()) {
				destination.cleanWalls(item->getWallBrush());
			}
		}
	}
}

bool ReplaceItemPreservingState(Item*& item, uint16_t targetServerId, ItemVector& promotedContents) {
	if (!item) {
		return false;
	}

	ItemVector preservedContents;
	if (auto* container = dynamic_cast<Container*>(item)) {
		preservedContents.swap(container->getVector());
	}

	const uint16_t originalId = item->getID();
	auto restoreOriginal = [&]() {
		item->setID(originalId);
		if (auto* originalContainer = dynamic_cast<Container*>(item)) {
			originalContainer->getVector().swap(preservedContents);
		}
	};
	item->setID(targetServerId);
	// Bypass virtual dispatch once so the factory selects the target type.
	std::unique_ptr<Item> replacement(item->Item::deepCopy());
	if (!replacement) {
		restoreOriginal();
		return false;
	}
	if (typeid(*replacement) == typeid(*item)) {
		// Same-type replacements can retain their specialized OTBM state.
		replacement.reset(item->deepCopy());
		if (!replacement) {
			restoreOriginal();
			return false;
		}
	}
	delete item;
	item = replacement.release();

	if (auto* replacementContainer = dynamic_cast<Container*>(item)) {
		replacementContainer->getVector().swap(preservedContents);
	} else {
		promotedContents.insert(promotedContents.end(), preservedContents.begin(), preservedContents.end());
	}
	return true;
}

ReplaceExecutionResult ReplaceEngine::Run(Editor& editor, const std::vector<Tile*>& tiles, const std::vector<ReplacementRule>& rules, ReplaceExecutionOptions options) {
	ReplaceExecutionResult result;
	result.validation = ValidateRuleSet({ "Execution", rules });
	if (!result.validation.isValid()) {
		return result;
	}
	for (size_t ruleIndex = 0; ruleIndex < rules.size(); ++ruleIndex) {
		for (size_t targetIndex = 0; targetIndex < rules[ruleIndex].targets.size(); ++targetIndex) {
			const ReplacementTarget& target = rules[ruleIndex].targets[targetIndex];
			if (!target.isTrash() && (!g_items.typeExists(target.serverId.value) || g_items.getItemType(target.serverId.value).id == 0)) {
				result.validation = { ReplacementValidationError::InvalidTargetServerId, ruleIndex, targetIndex };
				return result;
			}
		}
	}

	result.randomSeed = ResolveSeed(options.randomSeed);
	const RuleLookup lookup = BuildRuleLookup(rules);
	ExecutionState state(lookup, result.randomSeed, options, result);
	const bool needsBatch = !options.dryRun && (options.rebuildGroundBorders || options.doodadReplacementBrush != nullptr);
	std::unique_ptr<BatchAction> batch;
	std::unique_ptr<Action> action;
	if (!options.dryRun) {
		if (needsBatch) {
			batch.reset(editor.actionQueue->createBatch(ACTION_REPLACE_ITEMS));
			action.reset(editor.actionQueue->createAction(batch.get()));
		} else {
			action.reset(editor.actionQueue->createAction(ACTION_REPLACE_ITEMS));
		}
	}
	std::set<Position> changedGroundPositions;
	std::set<Position> doodadAnchors;
	const CompositeTileList* doodadComposite = options.doodadComposite;
	if (options.doodadReplacementBrush && !doodadComposite && options.doodadReplacementBrush->hasCompositeObjects(options.doodadVariation)) {
		doodadComposite = &options.doodadReplacementBrush->getComposite(options.doodadVariation);
	}

	for (Tile* sourceTile : tiles) {
		if (!sourceTile) {
			continue;
		}
		++result.tilesScanned;
		PlannedTile plan;
		if (sourceTile->ground) {
			plan.hasGround = true;
			plan.ground = state.PlanItem(sourceTile->ground, sourceTile);
			plan.changed = plan.ground.changed;
		}
		plan.items = state.PlanItems(sourceTile->items, sourceTile);
		for (const PlannedItem& itemPlan : plan.items) {
			plan.changed = plan.changed || itemPlan.changed;
		}

		if (plan.changed) {
			++result.changedTiles;
			if (plan.hasGround && plan.ground.changed) {
				changedGroundPositions.insert(sourceTile->getPosition());
			}
			if (plan.ground.doodadAnchor || std::any_of(plan.items.begin(), plan.items.end(), [](const PlannedItem& itemPlan) { return itemPlan.doodadAnchor; })) {
				doodadAnchors.insert(sourceTile->getPosition());
			}
			if (!options.dryRun) {
				Tile* workingTile = sourceTile->deepCopy(editor.map);
				ItemVector promotedGroundContents;
				if (plan.hasGround) {
					state.ApplyItem(workingTile->ground, plan.ground, promotedGroundContents);
				}
				state.ApplyItems(workingTile->items, plan.items);
				workingTile->items.insert(workingTile->items.end(), promotedGroundContents.begin(), promotedGroundContents.end());
				action->addChange(new Change(workingTile));
			}
		}
	}

	if (options.doodadReplacementBrush && options.enforceDoodadSelectionScope && !options.allowDoodadOutsideScope && !doodadAnchors.empty()) {
		const std::set<Position> allowedPositions(options.doodadAllowedPositions.begin(), options.doodadAllowedPositions.end());
		std::set<Position> outsidePositions;
		for (const Position& anchor : doodadAnchors) {
			if (doodadComposite && !doodadComposite->empty()) {
				for (const auto& [relativePosition, items] : *doodadComposite) {
					const Position destination = anchor + relativePosition;
					if (destination.isValid() && !allowedPositions.contains(destination)) {
						outsidePositions.insert(destination);
					}
				}
			} else if (!allowedPositions.contains(anchor)) {
				outsidePositions.insert(anchor);
			}
		}
		result.doodadOutsideScopeTiles = outsidePositions.size();
		if (!outsidePositions.empty()) {
			return result;
		}
	}

	if (options.dryRun || !action || action->size() == 0) {
		return result;
	}

	if (!needsBatch) {
		editor.actionQueue->addAction(action.release());
		result.committed = true;
		return result;
	}

	if (!batch->addAndCommitAction(action.release())) {
		return result;
	}

	std::set<Position> borderPositions;
	if (options.rebuildGroundBorders) {
		for (const Position& position : changedGroundPositions) {
			AddBorderNeighborhood(borderPositions, position);
		}
	}

	if (options.doodadReplacementBrush && !doodadAnchors.empty()) {
		std::unique_ptr<Action> doodadAction(editor.actionQueue->createAction(batch.get()));
		std::map<Position, Tile*> workingTiles;
		auto obtainWorkingTile = [&editor, &workingTiles](const Position& position) -> Tile* {
			auto found = workingTiles.find(position);
			if (found != workingTiles.end()) {
				return found->second;
			}
			TileLocation* location = editor.map.createTileL(position);
			Tile* current = location->get();
			Tile* working = current ? current->deepCopy(editor.map) : editor.map.allocator(location);
			workingTiles.emplace(position, working);
			return working;
		};

		for (const Position& anchor : doodadAnchors) {
			if (doodadComposite && !doodadComposite->empty()) {
				for (const auto& [relativePosition, items] : *doodadComposite) {
					const Position destination = anchor + relativePosition;
					if (!destination.isValid()) {
						continue;
					}
					Tile* working = obtainWorkingTile(destination);
					RemoveDuplicateWalls(items, *working);
					for (const Item* item : items) {
						working->addItem(item->deepCopy());
					}
					if (options.doodadReplacementBrush->doNewBorders()) {
						AddBorderNeighborhood(borderPositions, destination);
					}
				}
			} else if (options.doodadReplacementBrush->hasSingleObjects(options.doodadVariation)) {
				Tile* working = obtainWorkingTile(anchor);
				int variation = options.doodadVariation;
				options.doodadReplacementBrush->draw(&editor.map, working, &variation);
				if (options.doodadReplacementBrush->doNewBorders()) {
					AddBorderNeighborhood(borderPositions, anchor);
				}
			}
			++result.doodadPlacements;
		}

		for (const auto& [position, working] : workingTiles) {
			if (working->size() != 0 || editor.map.getTile(position)) {
				doodadAction->addChange(new Change(working));
				++result.doodadTilesChanged;
			} else {
				delete working;
			}
		}
		if (doodadAction->size() == 0 || !batch->addAndCommitAction(doodadAction.release())) {
			batch->rollback();
			return result;
		}
	}

	if (!borderPositions.empty()) {
		std::unique_ptr<Action> borderAction(editor.actionQueue->createAction(batch.get()));
		for (const Position& position : borderPositions) {
			TileLocation* location = editor.map.createTileL(position);
			Tile* current = location->get();
			Tile* working = current ? current->deepCopy(editor.map) : editor.map.allocator(location);
			working->borderize(&editor.map);
			if (options.doodadReplacementBrush && options.doodadReplacementBrush->doNewBorders()) {
				working->wallize(&editor.map);
			}
			if (current || working->size() != 0) {
				borderAction->addChange(new Change(working));
				++result.bordersRebuilt;
			} else {
				delete working;
			}
		}
		if (borderAction->size() != 0 && !batch->addAndCommitAction(borderAction.release())) {
			batch->rollback();
			return result;
		}
	}

	editor.actionQueue->addBatch(batch.release());
	result.committed = true;
	return result;
}
