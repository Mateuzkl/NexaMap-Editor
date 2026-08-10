//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_REPLACE_TOOL_REPLACE_EXECUTION_PLAN_H_
#define RME_REPLACE_TOOL_REPLACE_EXECUTION_PLAN_H_

#include "replace_rule.h"

#include <cstdint>

struct ReplacementChoice {
	const ReplacementTarget* target = nullptr;

	[[nodiscard]] bool HasReplacement() const {
		return target != nullptr;
	}
};

// roll is an inclusive percentage in [1, 100]. A rule whose probabilities
// total less than 100 intentionally produces no replacement above that total.
[[nodiscard]] ReplacementChoice SelectReplacementTarget(const ReplacementRule& rule, uint32_t roll);

#endif
