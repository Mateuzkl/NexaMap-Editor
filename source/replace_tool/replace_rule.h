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

#ifndef RME_REPLACE_TOOL_REPLACE_RULE_H_
#define RME_REPLACE_TOOL_REPLACE_RULE_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// Advanced Replace always stores map-facing ServerIDs. ClientIDs must be
// converted by the UI or asset layer before constructing a rule.
struct ServerItemId {
	constexpr ServerItemId() = default;
	explicit constexpr ServerItemId(uint16_t id) :
		value(id) { }

	[[nodiscard]] constexpr bool isValid() const {
		return value != 0;
	}

	bool operator==(const ServerItemId&) const = default;

	uint16_t value = 0;
};

enum class ReplacementTargetType : uint8_t {
	Item,
	Trash,
};

struct ReplacementTarget {
	static ReplacementTarget ForItem(ServerItemId serverId, uint16_t probability);
	static ReplacementTarget ForTrash(uint16_t probability);

	[[nodiscard]] bool isTrash() const {
		return type == ReplacementTargetType::Trash;
	}

	ReplacementTargetType type = ReplacementTargetType::Item;
	ServerItemId serverId;
	uint16_t probability = 0;
};

struct ReplacementRule {
	ServerItemId sourceServerId;
	std::vector<ReplacementTarget> targets;
};

struct RuleSet {
	std::string name;
	std::vector<ReplacementRule> rules;
};

enum class ReplaceScope : uint8_t {
	Selection,
	Viewport,
	AllMap,
};

enum class ReplacementValidationError : uint8_t {
	None,
	InvalidSourceServerId,
	MissingTargets,
	InvalidTargetServerId,
	SourceEqualsTarget,
	InvalidProbability,
	ProbabilityTotalAbove100,
	TrashMixedWithItemTargets,
	DuplicateSourceServerId,
};

struct ReplacementValidationResult {
	static constexpr size_t NoIndex = std::numeric_limits<size_t>::max();

	[[nodiscard]] bool isValid() const {
		return error == ReplacementValidationError::None;
	}

	ReplacementValidationError error = ReplacementValidationError::None;
	size_t ruleIndex = NoIndex;
	size_t targetIndex = NoIndex;
};

// A total below 100 is valid: the remaining percentage means no replacement.
// A total of 100 always selects one target. Totals above 100 are invalid.
[[nodiscard]] ReplacementValidationResult ValidateReplacementRule(const ReplacementRule& rule);
[[nodiscard]] ReplacementValidationResult ValidateRuleSet(const RuleSet& ruleSet);

#endif
