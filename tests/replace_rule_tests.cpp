#include "replace_tool/replace_rule.h"

#include <initializer_list>
#include <iostream>
#include <string_view>
#include <type_traits>

namespace {
	int failures = 0;

	void check(bool condition, std::string_view name) {
		if (!condition) {
			std::cerr << "FAILED: " << name << '\n';
			++failures;
		}
	}

	ReplacementRule makeRule(uint16_t sourceServerId, std::initializer_list<ReplacementTarget> targets) {
		return { ServerItemId(sourceServerId), targets };
	}
}

int main() {
	static_assert(!std::is_convertible_v<uint16_t, ServerItemId>, "ClientID/ServerID intent must be explicit");

	const ReplacementRule oneToOne = makeRule(2377, {
														ReplacementTarget::ForItem(ServerItemId(2393), 100),
													});
	check(ValidateReplacementRule(oneToOne).isValid(), "1:1 replacement must be valid");

	const ReplacementRule oneToMany = makeRule(2377, {
														 ReplacementTarget::ForItem(ServerItemId(2393), 60),
														 ReplacementTarget::ForItem(ServerItemId(2394), 40),
													 });
	check(ValidateReplacementRule(oneToMany).isValid(), "1:N replacement must be valid");

	const ReplacementRule trash = makeRule(2377, {
													 ReplacementTarget::ForTrash(100),
												 });
	check(ValidateReplacementRule(trash).isValid() && trash.targets.front().isTrash(), "trash target must be explicit and valid");

	const ReplacementRule invalidSource = makeRule(0, {
														  ReplacementTarget::ForItem(ServerItemId(2393), 100),
													  });
	check(ValidateReplacementRule(invalidSource).error == ReplacementValidationError::InvalidSourceServerId, "ServerID zero must be rejected as a source");

	const ReplacementRule invalidTarget = makeRule(2377, {
															 ReplacementTarget::ForItem(ServerItemId(0), 100),
														 });
	check(ValidateReplacementRule(invalidTarget).error == ReplacementValidationError::InvalidTargetServerId, "ServerID zero must be rejected as a target");

	const ReplacementRule below100 = makeRule(2377, {
														ReplacementTarget::ForItem(ServerItemId(2393), 75),
													});
	check(ValidateReplacementRule(below100).isValid(), "probability total below 100 must leave a no-replacement chance");

	const ReplacementRule equal100 = makeRule(2377, {
														ReplacementTarget::ForItem(ServerItemId(2393), 50),
														ReplacementTarget::ForItem(ServerItemId(2394), 50),
													});
	check(ValidateReplacementRule(equal100).isValid(), "probability total equal to 100 must be valid");

	const ReplacementRule above100 = makeRule(2377, {
														ReplacementTarget::ForItem(ServerItemId(2393), 60),
														ReplacementTarget::ForItem(ServerItemId(2394), 41),
													});
	check(ValidateReplacementRule(above100).error == ReplacementValidationError::ProbabilityTotalAbove100, "probability total above 100 must be rejected");

	const ReplacementRule mixedTrash = makeRule(2377, {
														  ReplacementTarget::ForTrash(50),
														  ReplacementTarget::ForItem(ServerItemId(2393), 50),
													  });
	check(ValidateReplacementRule(mixedTrash).error == ReplacementValidationError::TrashMixedWithItemTargets, "trash must be the only target");

	const RuleSet duplicateSources = { "Duplicates", { oneToOne, oneToMany } };
	const ReplacementValidationResult duplicateResult = ValidateRuleSet(duplicateSources);
	check(duplicateResult.error == ReplacementValidationError::DuplicateSourceServerId && duplicateResult.ruleIndex == 1, "rule sets must reject duplicate source ServerIDs");

	check(ReplaceScope::Selection != ReplaceScope::Viewport && ReplaceScope::Viewport != ReplaceScope::AllMap, "replace scopes must remain distinct");

	if (failures != 0) {
		std::cerr << failures << " replacement rule test(s) failed.\n";
		return 1;
	}

	std::cout << "All replacement rule tests passed.\n";
	return 0;
}
