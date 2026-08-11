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

#include "replace_rule_manager.h"

#include "../file_transaction.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <utility>

namespace {
	constexpr int RuleSetSchemaVersion = 1;

	bool IsWindowsDeviceName(std::string_view name) {
		std::string stem(name.substr(0, name.find('.')));
		std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char character) {
			return static_cast<char>(std::toupper(character));
		});
		static const std::unordered_set<std::string> ReservedNames = {
			"CON",
			"PRN",
			"AUX",
			"NUL",
			"COM1",
			"COM2",
			"COM3",
			"COM4",
			"COM5",
			"COM6",
			"COM7",
			"COM8",
			"COM9",
			"LPT1",
			"LPT2",
			"LPT3",
			"LPT4",
			"LPT5",
			"LPT6",
			"LPT7",
			"LPT8",
			"LPT9",
		};
		return ReservedNames.contains(stem);
	}

	nlohmann::json ToJson(const RuleSet& ruleSet) {
		nlohmann::json rules = nlohmann::json::array();
		for (const ReplacementRule& rule : ruleSet.rules) {
			nlohmann::json targets = nlohmann::json::array();
			for (const ReplacementTarget& target : rule.targets) {
				nlohmann::json targetJson = {
					{ "type", target.isTrash() ? "trash" : "item" },
					{ "probability", target.probability },
				};
				if (!target.isTrash()) {
					targetJson["serverId"] = target.serverId.value;
				}
				targets.push_back(std::move(targetJson));
			}
			rules.push_back({
				{ "sourceServerId", rule.sourceServerId.value },
				{ "targets", std::move(targets) },
			});
		}

		return {
			{ "schemaVersion", RuleSetSchemaVersion },
			{ "name", ruleSet.name },
			{ "rules", std::move(rules) },
		};
	}

	bool ReadUInt16(const nlohmann::json& value, uint16_t& output) {
		if (!value.is_number_unsigned()) {
			return false;
		}
		const uint64_t raw = value.get<uint64_t>();
		if (raw > std::numeric_limits<uint16_t>::max()) {
			return false;
		}
		output = static_cast<uint16_t>(raw);
		return true;
	}

	std::optional<RuleSet> FromJson(const nlohmann::json& document, std::string& error) {
		if (!document.is_object() || document.value("schemaVersion", 0) != RuleSetSchemaVersion || !document.contains("name") || !document["name"].is_string() || !document.contains("rules") || !document["rules"].is_array()) {
			error = "Malformed or unsupported replacement rule document.";
			return std::nullopt;
		}

		RuleSet ruleSet;
		ruleSet.name = document["name"].get<std::string>();
		for (const nlohmann::json& ruleJson : document["rules"]) {
			if (!ruleJson.is_object() || !ruleJson.contains("sourceServerId") || !ruleJson.contains("targets") || !ruleJson["targets"].is_array()) {
				error = "Malformed replacement rule.";
				return std::nullopt;
			}

			uint16_t sourceServerId = 0;
			if (!ReadUInt16(ruleJson["sourceServerId"], sourceServerId)) {
				error = "Replacement source ServerID is invalid.";
				return std::nullopt;
			}

			ReplacementRule rule;
			rule.sourceServerId = ServerItemId(sourceServerId);
			for (const nlohmann::json& targetJson : ruleJson["targets"]) {
				if (!targetJson.is_object() || !targetJson.contains("type") || !targetJson["type"].is_string() || !targetJson.contains("probability")) {
					error = "Malformed replacement target.";
					return std::nullopt;
				}

				uint16_t probability = 0;
				if (!ReadUInt16(targetJson["probability"], probability)) {
					error = "Replacement probability is invalid.";
					return std::nullopt;
				}

				const std::string type = targetJson["type"].get<std::string>();
				if (type == "trash") {
					rule.targets.push_back(ReplacementTarget::ForTrash(probability));
				} else if (type == "item" && targetJson.contains("serverId")) {
					uint16_t targetServerId = 0;
					if (!ReadUInt16(targetJson["serverId"], targetServerId)) {
						error = "Replacement target ServerID is invalid.";
						return std::nullopt;
					}
					rule.targets.push_back(ReplacementTarget::ForItem(ServerItemId(targetServerId), probability));
				} else {
					error = "Replacement target type is invalid.";
					return std::nullopt;
				}
			}
			ruleSet.rules.push_back(std::move(rule));
		}

		const ReplacementValidationResult validation = ValidateRuleSet(ruleSet);
		if (!validation.isValid()) {
			error = "Replacement rule document contains invalid rules.";
			return std::nullopt;
		}
		return ruleSet;
	}
}

ReplaceRuleManager::ReplaceRuleManager(std::filesystem::path rulesDirectory) :
	rulesDirectory(std::move(rulesDirectory)) { }

bool ReplaceRuleManager::IsValidRuleSetName(std::string_view name) {
	if (name.empty() || name.size() > 80 || name == "." || name == ".." || name.back() == ' ' || name.back() == '.') {
		return false;
	}

	bool hasVisibleCharacter = false;
	for (const unsigned char character : name) {
		if (character < 0x20 || std::string_view("<>:\"/\\|?*").find(static_cast<char>(character)) != std::string_view::npos) {
			return false;
		}
		hasVisibleCharacter = hasVisibleCharacter || std::isspace(character) == 0;
	}
	return hasVisibleCharacter && !IsWindowsDeviceName(name);
}

bool ReplaceRuleManager::Save(const RuleSet& ruleSet, std::string& error) const {
	error.clear();
	if (!IsValidRuleSetName(ruleSet.name)) {
		error = "Invalid replacement rule set name.";
		return false;
	}
	if (!ValidateRuleSet(ruleSet).isValid()) {
		error = "Cannot save an invalid replacement rule set.";
		return false;
	}

	std::error_code filesystemError;
	std::filesystem::create_directories(rulesDirectory, filesystemError);
	if (filesystemError) {
		error = "Could not create replacement rule directory: " + filesystemError.message();
		return false;
	}

	FileSaveTransaction transaction;
	const std::filesystem::path stagedPath = transaction.Stage(GetRuleSetPath(ruleSet.name));
	std::ofstream output(stagedPath, std::ios::binary | std::ios::trunc);
	if (!output.is_open()) {
		error = "Could not open replacement rule file for writing.";
		return false;
	}
	output << ToJson(ruleSet).dump(2) << '\n';
	output.close();
	if (!output) {
		error = "Could not write replacement rule file.";
		return false;
	}
	return transaction.Commit(error);
}

std::optional<RuleSet> ReplaceRuleManager::Load(std::string_view name, std::string& error) const {
	error.clear();
	if (!IsValidRuleSetName(name)) {
		error = "Invalid replacement rule set name.";
		return std::nullopt;
	}

	std::ifstream input(GetRuleSetPath(name), std::ios::binary);
	if (!input.is_open()) {
		error = "Replacement rule set was not found.";
		return std::nullopt;
	}

	try {
		nlohmann::json document;
		input >> document;
		std::optional<RuleSet> ruleSet = FromJson(document, error);
		if (ruleSet && ruleSet->name != name) {
			error = "Replacement rule set name does not match its file name.";
			return std::nullopt;
		}
		return ruleSet;
	} catch (const nlohmann::json::exception& exception) {
		error = "Could not parse replacement rule JSON: " + std::string(exception.what());
		return std::nullopt;
	}
}

bool ReplaceRuleManager::Rename(std::string_view oldName, std::string_view newName, std::string& error) const {
	error.clear();
	if (!IsValidRuleSetName(oldName) || !IsValidRuleSetName(newName)) {
		error = "Invalid replacement rule set name.";
		return false;
	}
	if (oldName == newName) {
		return true;
	}

	std::optional<RuleSet> ruleSet = Load(oldName, error);
	if (!ruleSet) {
		return false;
	}

	const std::filesystem::path oldPath = GetRuleSetPath(oldName);
	const std::filesystem::path newPath = GetRuleSetPath(newName);
	std::error_code filesystemError;
	if (std::filesystem::exists(newPath, filesystemError) || filesystemError) {
		error = filesystemError ? "Could not inspect replacement rule destination: " + filesystemError.message() : "A replacement rule set with the new name already exists.";
		return false;
	}

	std::filesystem::rename(oldPath, newPath, filesystemError);
	if (filesystemError) {
		error = "Could not rename replacement rule set: " + filesystemError.message();
		return false;
	}

	ruleSet->name = std::string(newName);
	if (Save(*ruleSet, error)) {
		return true;
	}

	std::error_code rollbackError;
	std::filesystem::rename(newPath, oldPath, rollbackError);
	if (rollbackError) {
		error += " The original file remains at the new path because rollback failed: " + rollbackError.message();
	}
	return false;
}

bool ReplaceRuleManager::Delete(std::string_view name, std::string& error) const {
	error.clear();
	if (!IsValidRuleSetName(name)) {
		error = "Invalid replacement rule set name.";
		return false;
	}

	std::error_code filesystemError;
	const bool removed = std::filesystem::remove(GetRuleSetPath(name), filesystemError);
	if (filesystemError) {
		error = "Could not delete replacement rule set: " + filesystemError.message();
	} else if (!removed) {
		error = "Replacement rule set was not found.";
	}
	return removed && !filesystemError;
}

std::vector<std::string> ReplaceRuleManager::List(std::string& error) const {
	error.clear();
	std::vector<std::string> names;
	std::error_code filesystemError;
	if (!std::filesystem::exists(rulesDirectory, filesystemError)) {
		if (filesystemError) {
			error = "Could not inspect replacement rule directory: " + filesystemError.message();
		}
		return names;
	}

	for (std::filesystem::directory_iterator iterator(rulesDirectory, filesystemError); !filesystemError && iterator != std::filesystem::directory_iterator(); iterator.increment(filesystemError)) {
		if (!iterator->is_regular_file() || iterator->path().extension() != ".json") {
			continue;
		}
		const std::string name = iterator->path().stem().string();
		if (IsValidRuleSetName(name)) {
			names.push_back(name);
		}
	}
	if (filesystemError) {
		error = "Could not list replacement rule sets: " + filesystemError.message();
		return {};
	}

	std::sort(names.begin(), names.end());
	return names;
}

std::filesystem::path ReplaceRuleManager::GetRuleSetPath(std::string_view name) const {
	return rulesDirectory / (std::string(name) + ".json");
}
