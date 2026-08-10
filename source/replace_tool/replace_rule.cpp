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

#include "replace_rule.h"

#include <unordered_set>

ReplacementTarget ReplacementTarget::ForItem(ServerItemId serverId, uint16_t probability) {
	return { ReplacementTargetType::Item, serverId, probability };
}

ReplacementTarget ReplacementTarget::ForTrash(uint16_t probability) {
	return { ReplacementTargetType::Trash, ServerItemId(), probability };
}

ReplacementValidationResult ValidateReplacementRule(const ReplacementRule& rule) {
	if (!rule.sourceServerId.isValid()) {
		return { ReplacementValidationError::InvalidSourceServerId };
	}
	if (rule.targets.empty()) {
		return { ReplacementValidationError::MissingTargets };
	}

	uint32_t probabilityTotal = 0;
	bool hasTrashTarget = false;
	for (size_t index = 0; index < rule.targets.size(); ++index) {
		const ReplacementTarget& target = rule.targets[index];
		if (target.probability == 0 || target.probability > 100) {
			return { ReplacementValidationError::InvalidProbability, ReplacementValidationResult::NoIndex, index };
		}

		if (target.isTrash()) {
			if (target.serverId.isValid()) {
				return { ReplacementValidationError::InvalidTargetServerId, ReplacementValidationResult::NoIndex, index };
			}
			hasTrashTarget = true;
		} else {
			if (!target.serverId.isValid()) {
				return { ReplacementValidationError::InvalidTargetServerId, ReplacementValidationResult::NoIndex, index };
			}
			if (target.serverId == rule.sourceServerId) {
				return { ReplacementValidationError::SourceEqualsTarget, ReplacementValidationResult::NoIndex, index };
			}
		}

		probabilityTotal += target.probability;
	}

	if (hasTrashTarget && rule.targets.size() != 1) {
		return { ReplacementValidationError::TrashMixedWithItemTargets };
	}
	if (probabilityTotal > 100) {
		return { ReplacementValidationError::ProbabilityTotalAbove100 };
	}

	return {};
}

ReplacementValidationResult ValidateRuleSet(const RuleSet& ruleSet) {
	std::unordered_set<uint16_t> sourceServerIds;
	for (size_t index = 0; index < ruleSet.rules.size(); ++index) {
		const ReplacementRule& rule = ruleSet.rules[index];
		ReplacementValidationResult result = ValidateReplacementRule(rule);
		if (!result.isValid()) {
			result.ruleIndex = index;
			return result;
		}
		if (!sourceServerIds.insert(rule.sourceServerId.value).second) {
			return { ReplacementValidationError::DuplicateSourceServerId, index };
		}
	}

	return {};
}
