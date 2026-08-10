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

#ifndef RME_REPLACE_TOOL_REPLACE_RULE_EDITOR_H_
#define RME_REPLACE_TOOL_REPLACE_RULE_EDITOR_H_

#include "replace_rule.h"

#include <optional>
#include <string_view>
#include <vector>

class ReplaceRuleEditor {
public:
	[[nodiscard]] const std::vector<ReplacementRule>& GetRules() const {
		return rules;
	}

	void SetRules(std::vector<ReplacementRule> rules);
	void Clear();

	bool AddRule(ServerItemId sourceServerId);
	bool RemoveRule(size_t ruleIndex);
	bool SetSource(size_t ruleIndex, ServerItemId sourceServerId);
	bool AddItemTarget(size_t ruleIndex, ServerItemId targetServerId);
	bool AddTrashTarget(size_t ruleIndex);
	bool ReplaceItemTarget(size_t ruleIndex, size_t targetIndex, ServerItemId targetServerId);
	bool RemoveTarget(size_t ruleIndex, size_t targetIndex);
	bool SetTargetProbability(size_t ruleIndex, size_t targetIndex, uint16_t probability);

	[[nodiscard]] static std::optional<ServerItemId> ParseDragPayload(std::string_view payload);

private:
	void DistributeProbabilities(size_t ruleIndex);
	[[nodiscard]] bool HasSource(ServerItemId sourceServerId, size_t exceptRuleIndex) const;

	std::vector<ReplacementRule> rules;
};

#endif
