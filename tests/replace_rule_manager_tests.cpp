#include "replace_tool/replace_rule_manager.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <utility>

namespace {
	int failures = 0;

	void check(bool condition, std::string_view name) {
		if (!condition) {
			std::cerr << "FAILED: " << name << '\n';
			++failures;
		}
	}

	RuleSet makeRuleSet(std::string name) {
		return {
			std::move(name),
			{
				{
					ServerItemId(2377),
					{
						ReplacementTarget::ForItem(ServerItemId(2393), 70),
						ReplacementTarget::ForItem(ServerItemId(2394), 30),
					},
				},
			},
		};
	}
}

int main() {
	const std::filesystem::path testDirectory = std::filesystem::temp_directory_path() / ("nexamap-replace-rules-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	ReplaceRuleManager manager(testDirectory);
	std::string error;

	const RuleSet original = makeRuleSet("City Floors");
	check(manager.Save(original, error), "rule set must save");
	const std::optional<RuleSet> loaded = manager.Load(original.name, error);
	check(loaded.has_value() && loaded->name == original.name && loaded->rules.size() == 1 && loaded->rules.front().targets.size() == 2, "saved rule set must load intact");

	check(manager.Rename("City Floors", "Cave Floors", error), "rule set must rename");
	check(!manager.Load("City Floors", error).has_value(), "old rule set name must disappear after rename");
	check(manager.Load("Cave Floors", error).has_value(), "renamed rule set must load by its new name");

	const RuleSet second = makeRuleSet("Borders");
	check(manager.Save(second, error), "second rule set must save");
	const std::vector<std::string> names = manager.List(error);
	check(names == std::vector<std::string>({ "Borders", "Cave Floors" }), "rule set listing must be sorted");

	check(manager.Delete("Cave Floors", error), "rule set must delete");
	check(!manager.Delete("Cave Floors", error), "deleting a missing rule set must fail");

	check(!ReplaceRuleManager::IsValidRuleSetName("../escape"), "path traversal name must be rejected");
	check(!ReplaceRuleManager::IsValidRuleSetName("bad/name"), "path separator must be rejected");
	check(!ReplaceRuleManager::IsValidRuleSetName("   "), "whitespace-only name must be rejected");
	check(!ReplaceRuleManager::IsValidRuleSetName("CON"), "Windows device name must be rejected");

	std::filesystem::create_directories(testDirectory);
	{
		std::ofstream malformed(testDirectory / "Malformed.json");
		malformed << "{ this is not json";
	}
	check(!manager.Load("Malformed", error).has_value() && !error.empty(), "malformed JSON must be rejected with an error");

	std::error_code cleanupError;
	std::filesystem::remove_all(testDirectory, cleanupError);
	if (cleanupError) {
		std::cerr << "FAILED: could not clean test directory: " << cleanupError.message() << '\n';
		++failures;
	}

	if (failures != 0) {
		std::cerr << failures << " replacement rule manager test(s) failed.\n";
		return 1;
	}

	std::cout << "All replacement rule manager tests passed.\n";
	return 0;
}
