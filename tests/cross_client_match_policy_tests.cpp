#include "cross_client_match_policy.h"
#include <iostream>
#include <string_view>

int main() {
	int failures = 0;
	auto check = [&](bool result, std::string_view label) {
		if (!result) {
			std::cerr << "FAILED: " << label << '\n';
			++failures;
		}
	};
	using namespace CrossClientMatchPolicy;
	constexpr uint64_t Ground = uint64_t(1) << 14;
	constexpr uint64_t WallBrushHint = uint64_t(1) << 27;
	constexpr uint64_t FloorChange = uint64_t(1) << 36;
	check(StructurallyCompatible(1, 0, Ground, 1, 0, Ground), "ground with same kind");
	check(!StructurallyCompatible(1, 0, Ground, 0, 0, 0), "ground vs non-ground rejected");
	check(!StructurallyCompatible(0, 2, 0, 0, 3, 0), "special type mismatch rejected");
	check(StructurallyCompatible(0, 0, WallBrushHint, 0, 0, 0), "missing brush labels are not fatal");
	check(!StructurallyCompatible(0, 0, FloorChange, 0, 0, 0), "floor change direction preserved");
	check(!StructurallyCompatible(0, 0, uint64_t(1) << 31, 0, 0, 0), "open vs closed door rejected");
	check(!StructurallyCompatible(0, 0, uint64_t(1) << 32, 0, 0, 0), "locked door preserved");
	check(!StructurallyCompatible(0, 0, 1, 0, 0, 0), "stack count semantics preserved");
	check(CanApplySuggestion(100, true, true, 1.0, -1, -1.0, true), "unique complete visual proof");
	check(!CanApplySuggestion(99, true, true, 1.0, -1), "preview alone cannot prove equivalence");
	check(!CanApplySuggestion(99, true, true, 1.0, 97, 0.8, true), "near confidence tie blocked");
	check(!CanApplySuggestion(100, true, true, 1.0, 85, 1.0, true), "equal visuals cannot be broken by names");
	check(!CanApplySuggestion(100, false, true, 1.0, -1, -1.0, true), "ambiguous complete graphics need review");
	check(!CanApplySuggestion(95, true, false, 0.0, -1), "name alone blocked");
	check(!CanApplySuggestion(91, true, true, 0.90, -1), "weak visual blocked");
	check(!CanApplySuggestion(80, false, true, 0.99, -1), "weak score blocked");
	std::cout << (failures ? "Failed" : "All cross-client matching policy tests passed") << '\n';
	return failures == 0 ? 0 : 1;
}
