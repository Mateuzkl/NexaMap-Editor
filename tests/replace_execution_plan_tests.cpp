#include "replace_tool/replace_execution_plan.h"

#include <cassert>
#include <iostream>

namespace {
	ReplacementRule MakeRule() {
		return {
			ServerItemId(100),
			{
				ReplacementTarget::ForItem(ServerItemId(200), 60),
				ReplacementTarget::ForItem(ServerItemId(300), 30),
			},
		};
	}

	void TestProbabilityBoundaries() {
		const ReplacementRule rule = MakeRule();
		assert(SelectReplacementTarget(rule, 1).target == &rule.targets[0]);
		assert(SelectReplacementTarget(rule, 60).target == &rule.targets[0]);
		assert(SelectReplacementTarget(rule, 61).target == &rule.targets[1]);
		assert(SelectReplacementTarget(rule, 90).target == &rule.targets[1]);
		assert(!SelectReplacementTarget(rule, 91).HasReplacement());
		assert(!SelectReplacementTarget(rule, 100).HasReplacement());
	}

	void TestTrashAndInvalidRolls() {
		const ReplacementRule trashRule { ServerItemId(100), { ReplacementTarget::ForTrash(100) } };
		const ReplacementChoice choice = SelectReplacementTarget(trashRule, 42);
		assert(choice.HasReplacement());
		assert(choice.target->isTrash());
		assert(!SelectReplacementTarget(trashRule, 0).HasReplacement());
		assert(!SelectReplacementTarget(trashRule, 101).HasReplacement());
	}
}

int main() {
	TestProbabilityBoundaries();
	TestTrashAndInvalidRolls();
	std::cout << "replace execution plan tests passed\n";
	return 0;
}
