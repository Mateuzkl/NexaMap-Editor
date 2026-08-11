#include "replace_tool/replace_rule_editor.h"

#include <iostream>
#include <string_view>

namespace {
	int failures = 0;

	void check(bool condition, std::string_view name) {
		if (!condition) {
			std::cerr << "FAILED: " << name << '\n';
			++failures;
		}
	}
}

int main() {
	ReplaceRuleEditor editor;
	check(editor.AddRule(ServerItemId(100)), "source item must create a rule");
	check(!editor.AddRule(ServerItemId(100)), "duplicate source item must be rejected");
	check(!editor.AddRule(ServerItemId(0)), "invalid source item must be rejected");

	check(editor.AddItemTarget(0, ServerItemId(200)), "first item target must be accepted");
	check(editor.GetRules()[0].targets[0].probability == 100, "single target must receive 100 percent");
	check(editor.AddItemTarget(0, ServerItemId(201)), "second item target must be accepted");
	check(editor.GetRules()[0].targets[0].probability == 50 && editor.GetRules()[0].targets[1].probability == 50, "two targets must split probability evenly");
	check(!editor.AddItemTarget(0, ServerItemId(201)), "duplicate target must be rejected");
	check(!editor.AddItemTarget(0, ServerItemId(100)), "source item cannot be its own target");

	check(editor.SetTargetProbability(0, 0, 60) && editor.SetTargetProbability(0, 1, 41), "manual probabilities must be editable");
	const RuleSet invalidTotal = { "Draft", editor.GetRules() };
	check(ValidateRuleSet(invalidTotal).error == ReplacementValidationError::ProbabilityTotalAbove100, "builder must expose totals above 100 to model validation");
	check(editor.RemoveTarget(0, 1) && editor.GetRules()[0].targets[0].probability == 100, "removing a target must redistribute probability");

	check(editor.RemoveTarget(0, 0), "last target must be removable");
	check(editor.AddTrashTarget(0), "trash target must be accepted for an empty rule");
	check(!editor.AddItemTarget(0, ServerItemId(200)), "trash target must remain exclusive");
	check(editor.RemoveTarget(0, 0), "trash target must be removable");

	const std::optional<ServerItemId> parsed = ReplaceRuleEditor::ParseDragPayload("NEXAMAP_SERVER_ITEM_ID:2377");
	check(parsed == ServerItemId(2377), "ServerID drag payload must parse");
	check(!ReplaceRuleEditor::ParseDragPayload("RME_ITEM:2377"), "ambiguous legacy drag payload must be rejected");
	check(!ReplaceRuleEditor::ParseDragPayload("NEXAMAP_SERVER_ITEM_ID:0"), "invalid ServerID drag payload must be rejected");

	check(editor.RemoveRule(0) && editor.GetRules().empty(), "rule must be removable");
	check(!editor.RemoveRule(0), "missing rule removal must fail");

	if (failures != 0) {
		std::cerr << failures << " replacement rule editor test(s) failed.\n";
		return 1;
	}

	std::cout << "All replacement rule editor tests passed.\n";
	return 0;
}
