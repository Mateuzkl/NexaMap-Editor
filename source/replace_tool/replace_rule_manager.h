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

#ifndef RME_REPLACE_TOOL_REPLACE_RULE_MANAGER_H_
#define RME_REPLACE_TOOL_REPLACE_RULE_MANAGER_H_

#include "replace_rule.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ReplaceRuleManager {
public:
	explicit ReplaceRuleManager(std::filesystem::path rulesDirectory);

	[[nodiscard]] static bool IsValidRuleSetName(std::string_view name);

	bool Save(const RuleSet& ruleSet, std::string& error) const;
	[[nodiscard]] std::optional<RuleSet> Load(std::string_view name, std::string& error) const;
	bool Rename(std::string_view oldName, std::string_view newName, std::string& error) const;
	bool Delete(std::string_view name, std::string& error) const;
	[[nodiscard]] std::vector<std::string> List(std::string& error) const;

	[[nodiscard]] const std::filesystem::path& GetRulesDirectory() const {
		return rulesDirectory;
	}

private:
	[[nodiscard]] std::filesystem::path GetRuleSetPath(std::string_view name) const;

	std::filesystem::path rulesDirectory;
};

#endif
