//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "../main.h"
#include "replace_engine.h"

#include "replace_execution_plan.h"

#include "../action.h"
#include "../complexitem.h"
#include "../editor.h"
#include "../item.h"
#include "../tile.h"

#include <cassert>
#include <memory>
#include <random>
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
		ExecutionState(const RuleLookup& rules, uint32_t seed, ReplaceExecutionResult& result) :
			rules(rules),
			random(seed),
			result(result) { }

		PlannedItem PlanItem(Item* item) {
			PlannedItem plan;
			if (!item) {
				return plan;
			}
			++result.itemsScanned;
			const auto found = rules.find(item->getID());
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
					plan.action = PlannedAction::Replace;
					plan.targetServerId = choice.target->serverId.value;
					plan.changed = true;
				}
			}

			if (auto* container = dynamic_cast<Container*>(item)) {
				plan.contents = PlanItems(container->getVector());
				for (const PlannedItem& child : plan.contents) {
					plan.changed = plan.changed || child.changed;
				}
			}
			return plan;
		}

		std::vector<PlannedItem> PlanItems(const ItemVector& items) {
			std::vector<PlannedItem> plans;
			plans.reserve(items.size());
			for (Item* item : items) {
				plans.push_back(PlanItem(item));
			}
			return plans;
		}

		void ApplyItem(Item*& item, const PlannedItem& plan, ItemVector& promotedContents) const {
			if (!item) {
				return;
			}

			if (auto* container = dynamic_cast<Container*>(item)) {
				ApplyItems(container->getVector(), plan.contents);
			}

			if (plan.action == PlannedAction::Delete) {
				delete item;
				item = nullptr;
				return;
			}
			if (plan.action != PlannedAction::Replace) {
				return;
			}

			ItemVector preservedContents;
			if (auto* container = dynamic_cast<Container*>(item)) {
				preservedContents.swap(container->getVector());
			}

			item->setID(plan.targetServerId);
			// Bypass virtual dispatch once so the factory selects the target type.
			std::unique_ptr<Item> replacement(item->Item::deepCopy());
			if (replacement && typeid(*replacement) == typeid(*item)) {
				// Same-type replacements can retain their specialized OTBM state.
				replacement.reset(item->deepCopy());
			}
			delete item;
			item = replacement.release();

			if (auto* replacementContainer = dynamic_cast<Container*>(item)) {
				replacementContainer->getVector().swap(preservedContents);
			} else {
				promotedContents.insert(promotedContents.end(), preservedContents.begin(), preservedContents.end());
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
}

ReplaceExecutionResult ReplaceEngine::Run(Editor& editor, const std::vector<Tile*>& tiles, const std::vector<ReplacementRule>& rules, ReplaceExecutionOptions options) {
	ReplaceExecutionResult result;
	result.validation = ValidateRuleSet({ "Execution", rules });
	if (!result.validation.isValid()) {
		return result;
	}

	result.randomSeed = ResolveSeed(options.randomSeed);
	const RuleLookup lookup = BuildRuleLookup(rules);
	ExecutionState state(lookup, result.randomSeed, result);
	std::unique_ptr<Action> action;
	if (!options.dryRun) {
		action.reset(editor.actionQueue->createAction(ACTION_REPLACE_ITEMS));
	}

	for (Tile* sourceTile : tiles) {
		if (!sourceTile) {
			continue;
		}
		++result.tilesScanned;
		PlannedTile plan;
		if (sourceTile->ground) {
			plan.hasGround = true;
			plan.ground = state.PlanItem(sourceTile->ground);
			plan.changed = plan.ground.changed;
		}
		plan.items = state.PlanItems(sourceTile->items);
		for (const PlannedItem& itemPlan : plan.items) {
			plan.changed = plan.changed || itemPlan.changed;
		}

		if (plan.changed) {
			++result.changedTiles;
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

	if (!options.dryRun && action && action->size() != 0) {
		editor.actionQueue->addAction(action.release());
		result.committed = true;
	}
	return result;
}
