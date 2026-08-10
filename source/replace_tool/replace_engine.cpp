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

#include <memory>
#include <random>
#include <unordered_map>

namespace {
	using RuleLookup = std::unordered_map<uint16_t, const ReplacementRule*>;

	class ExecutionState {
	public:
		ExecutionState(const RuleLookup& rules, uint32_t seed, ReplaceExecutionResult& result) :
			rules(rules),
			random(seed),
			result(result) { }

		bool ProcessItem(Item*& item, bool mutate) {
			if (!item) {
				return false;
			}
			++result.itemsScanned;
			const auto found = rules.find(item->getID());
			if (found == rules.end()) {
				if (auto* container = dynamic_cast<Container*>(item)) {
					return ProcessItems(container->getVector(), mutate);
				}
				return false;
			}

			++result.matchedItems;
			const ReplacementChoice choice = SelectReplacementTarget(*found->second, distribution(random));
			if (!choice.HasReplacement()) {
				++result.unchangedByProbability;
				if (auto* container = dynamic_cast<Container*>(item)) {
					return ProcessItems(container->getVector(), mutate);
				}
				return false;
			}

			if (choice.target->isTrash()) {
				++result.deletions;
				if (mutate) {
					delete item;
					item = nullptr;
				}
				return true;
			}

			++result.replacements;
			if (mutate) {
				item->setID(choice.target->serverId.value);
				Item* replacement = item->deepCopy();
				delete item;
				item = replacement;
			}

			if (item) {
				if (auto* container = dynamic_cast<Container*>(item)) {
					ProcessItems(container->getVector(), mutate);
				}
			}
			return true;
		}

		bool ProcessItems(ItemVector& items, bool mutate) {
			bool changed = false;
			for (auto iterator = items.begin(); iterator != items.end();) {
				Item*& item = *iterator;
				const bool itemChanged = ProcessItem(item, mutate);
				changed = changed || itemChanged;
				if (mutate && !item) {
					iterator = items.erase(iterator);
				} else {
					++iterator;
				}
			}
			return changed;
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
		Tile* workingTile = options.dryRun ? sourceTile : sourceTile->deepCopy(editor.map);
		bool changed = false;
		if (workingTile->ground) {
			changed = state.ProcessItem(workingTile->ground, !options.dryRun) || changed;
		}
		changed = state.ProcessItems(workingTile->items, !options.dryRun) || changed;
		if (changed) {
			++result.changedTiles;
			if (!options.dryRun) {
				action->addChange(new Change(workingTile));
			}
		} else if (!options.dryRun) {
			delete workingTile;
		}
	}

	if (!options.dryRun && action && action->size() != 0) {
		editor.actionQueue->addAction(action.release());
		result.committed = true;
	}
	return result;
}
