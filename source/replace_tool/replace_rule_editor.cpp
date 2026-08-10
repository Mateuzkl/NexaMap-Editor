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

#include "replace_rule_editor.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <utility>

namespace {
	constexpr std::string_view DragPayloadPrefix = "NEXAMAP_SERVER_ITEM_ID:";
}

void ReplaceRuleEditor::SetRules(std::vector<ReplacementRule> newRules) {
	rules = std::move(newRules);
}

void ReplaceRuleEditor::Clear() {
	rules.clear();
}

bool ReplaceRuleEditor::AddRule(ServerItemId sourceServerId) {
	if (!sourceServerId.isValid() || HasSource(sourceServerId, rules.size())) {
		return false;
	}
	rules.push_back({ sourceServerId, {} });
	return true;
}

bool ReplaceRuleEditor::RemoveRule(size_t ruleIndex) {
	if (ruleIndex >= rules.size()) {
		return false;
	}
	rules.erase(rules.begin() + static_cast<std::ptrdiff_t>(ruleIndex));
	return true;
}

bool ReplaceRuleEditor::SetSource(size_t ruleIndex, ServerItemId sourceServerId) {
	if (ruleIndex >= rules.size() || !sourceServerId.isValid() || HasSource(sourceServerId, ruleIndex)) {
		return false;
	}
	const ReplacementRule& rule = rules[ruleIndex];
	if (std::any_of(rule.targets.begin(), rule.targets.end(), [sourceServerId](const ReplacementTarget& target) {
			return !target.isTrash() && target.serverId == sourceServerId;
		})) {
		return false;
	}
	rules[ruleIndex].sourceServerId = sourceServerId;
	return true;
}

bool ReplaceRuleEditor::AddItemTarget(size_t ruleIndex, ServerItemId targetServerId) {
	if (ruleIndex >= rules.size() || !targetServerId.isValid() || targetServerId == rules[ruleIndex].sourceServerId) {
		return false;
	}
	ReplacementRule& rule = rules[ruleIndex];
	if (std::any_of(rule.targets.begin(), rule.targets.end(), [targetServerId](const ReplacementTarget& target) {
			return target.isTrash() || target.serverId == targetServerId;
		})) {
		return false;
	}
	rule.targets.push_back(ReplacementTarget::ForItem(targetServerId, 1));
	DistributeProbabilities(ruleIndex);
	return true;
}

bool ReplaceRuleEditor::AddTrashTarget(size_t ruleIndex) {
	if (ruleIndex >= rules.size() || !rules[ruleIndex].targets.empty()) {
		return false;
	}
	rules[ruleIndex].targets.push_back(ReplacementTarget::ForTrash(100));
	return true;
}

bool ReplaceRuleEditor::ReplaceItemTarget(size_t ruleIndex, size_t targetIndex, ServerItemId targetServerId) {
	if (ruleIndex >= rules.size() || targetIndex >= rules[ruleIndex].targets.size() || !targetServerId.isValid() || targetServerId == rules[ruleIndex].sourceServerId) {
		return false;
	}
	ReplacementRule& rule = rules[ruleIndex];
	for (size_t index = 0; index < rule.targets.size(); ++index) {
		if (index != targetIndex && !rule.targets[index].isTrash() && rule.targets[index].serverId == targetServerId) {
			return false;
		}
	}
	rule.targets[targetIndex].type = ReplacementTargetType::Item;
	rule.targets[targetIndex].serverId = targetServerId;
	return true;
}

bool ReplaceRuleEditor::RemoveTarget(size_t ruleIndex, size_t targetIndex) {
	if (ruleIndex >= rules.size() || targetIndex >= rules[ruleIndex].targets.size()) {
		return false;
	}
	auto& targets = rules[ruleIndex].targets;
	targets.erase(targets.begin() + static_cast<std::ptrdiff_t>(targetIndex));
	if (!targets.empty()) {
		DistributeProbabilities(ruleIndex);
	}
	return true;
}

bool ReplaceRuleEditor::SetTargetProbability(size_t ruleIndex, size_t targetIndex, uint16_t probability) {
	if (ruleIndex >= rules.size() || targetIndex >= rules[ruleIndex].targets.size() || probability == 0 || probability > 100) {
		return false;
	}
	rules[ruleIndex].targets[targetIndex].probability = probability;
	return true;
}

std::optional<ServerItemId> ReplaceRuleEditor::ParseDragPayload(std::string_view payload) {
	if (!payload.starts_with(DragPayloadPrefix)) {
		return std::nullopt;
	}
	payload.remove_prefix(DragPayloadPrefix.size());
	uint32_t value = 0;
	const auto [end, error] = std::from_chars(payload.data(), payload.data() + payload.size(), value);
	if (error != std::errc() || end != payload.data() + payload.size() || value == 0 || value > std::numeric_limits<uint16_t>::max()) {
		return std::nullopt;
	}
	return ServerItemId(static_cast<uint16_t>(value));
}

void ReplaceRuleEditor::DistributeProbabilities(size_t ruleIndex) {
	auto& targets = rules[ruleIndex].targets;
	if (targets.empty()) {
		return;
	}
	const uint16_t count = static_cast<uint16_t>(targets.size());
	const uint16_t base = static_cast<uint16_t>(100 / count);
	const uint16_t remainder = static_cast<uint16_t>(100 % count);
	for (uint16_t index = 0; index < count; ++index) {
		targets[index].probability = static_cast<uint16_t>(base + (index < remainder ? 1 : 0));
	}
}

bool ReplaceRuleEditor::HasSource(ServerItemId sourceServerId, size_t exceptRuleIndex) const {
	for (size_t index = 0; index < rules.size(); ++index) {
		if (index != exceptRuleIndex && rules[index].sourceServerId == sourceServerId) {
			return true;
		}
	}
	return false;
}
