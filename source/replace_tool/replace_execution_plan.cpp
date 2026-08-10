//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "replace_execution_plan.h"

ReplacementChoice SelectReplacementTarget(const ReplacementRule& rule, uint32_t roll) {
	if (roll == 0 || roll > 100) {
		return {};
	}

	uint32_t cumulativeProbability = 0;
	for (const ReplacementTarget& target : rule.targets) {
		cumulativeProbability += target.probability;
		if (roll <= cumulativeProbability) {
			return { &target };
		}
	}
	return {};
}
