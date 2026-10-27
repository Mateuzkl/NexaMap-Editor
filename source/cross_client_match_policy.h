#ifndef NEXAMAP_CROSS_CLIENT_MATCH_POLICY_H_
#define NEXAMAP_CROSS_CLIENT_MATCH_POLICY_H_

#include <cstdint>

namespace CrossClientMatchPolicy {
	// Keep actual item kinds, hanging positions and floor-change directions strict.
	// Brush-provided wall/border/table labels may be unavailable without XML.
	inline constexpr uint64_t RequiredRoleMask = uint64_t(1) | (uint64_t(0x7) << 8) | (uint64_t(0xFF) << 14) | (uint64_t(0x3) << 31) | (uint64_t(0x3F) << 35);

	[[nodiscard]] constexpr bool StructurallyCompatible(
		uint16_t sourceGroup, uint16_t sourceType, uint64_t sourceFlags,
		uint16_t destinationGroup, uint16_t destinationType, uint64_t destinationFlags
	) noexcept {
		return sourceGroup == destinationGroup && sourceType == destinationType
			&& (sourceFlags & RequiredRoleMask) == (destinationFlags & RequiredRoleMask);
	}

	// Never select a visually unverified or nearly tied suggestion on behalf of the user.
	[[nodiscard]] constexpr bool CanApplySuggestion(
		int confidence, bool automatic, bool visualCompared, double visualSimilarity, int nextConfidence,
		double nextVisualSimilarity = -1., bool allVariantsVerified = false
	) noexcept {
		if (!automatic || !allVariantsVerified || !visualCompared || visualSimilarity < 0.999999) {
			return false;
		}
		if (confidence < 88) {
			return false;
		}
		return (nextConfidence < 0 || confidence - nextConfidence >= 6)
			&& (nextVisualSimilarity < 0. || visualSimilarity - nextVisualSimilarity >= 0.02);
	}
}

#endif // NEXAMAP_CROSS_CLIENT_MATCH_POLICY_H_
