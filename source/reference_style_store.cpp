// SPDX-License-Identifier: GPL-3.0-or-later
#include "reference_style_store.h"

#include <algorithm>

namespace {

	bool BoundsOverlap(const Position& firstMin, const Position& firstMax, const Position& secondMin, const Position& secondMax) {
		return firstMin.x <= secondMax.x && firstMax.x >= secondMin.x && firstMin.y <= secondMax.y && firstMax.y >= secondMin.y;
	}

	bool HasExactPosition(const std::vector<Position>& positions, const Position& position) {
		return std::binary_search(positions.begin(), positions.end(), position);
	}

} // namespace

ReferenceStyleStore& ReferenceStyleStore::Instance() {
	static ReferenceStyleStore instance;
	return instance;
}

void ReferenceStyleStore::set(ReferenceStyleSnapshot snapshot) {
	std::lock_guard lock(mutex_);
	currentSnapshot_ = std::move(snapshot);
	// A target only makes sense for the exact source that was captured with it.
	currentTarget_.reset();
}

void ReferenceStyleStore::clear() {
	std::lock_guard lock(mutex_);
	currentSnapshot_.reset();
	currentTarget_.reset();
}

bool ReferenceStyleStore::hasReference() const noexcept {
	std::lock_guard lock(mutex_);
	return currentSnapshot_.has_value();
}

std::optional<ReferenceStyleSnapshot> ReferenceStyleStore::getSnapshot() const {
	std::lock_guard lock(mutex_);
	return currentSnapshot_;
}

void ReferenceStyleStore::setTarget(TargetAreaSnapshot snapshot) {
	std::lock_guard lock(mutex_);
	currentTarget_ = std::move(snapshot);
}

void ReferenceStyleStore::clearTarget() {
	std::lock_guard lock(mutex_);
	currentTarget_.reset();
}

bool ReferenceStyleStore::hasTarget() const noexcept {
	std::lock_guard lock(mutex_);
	return currentTarget_.has_value();
}

std::optional<TargetAreaSnapshot> ReferenceStyleStore::getTargetSnapshot() const {
	std::lock_guard lock(mutex_);
	return currentTarget_;
}

bool ReferenceStyleStore::isCompatibleWith(ReferenceAssetMode activeAssetMode, uint64_t activeWorkspaceGen) const noexcept {
	std::lock_guard lock(mutex_);
	if (!currentSnapshot_.has_value()) {
		return false;
	}
	if (currentSnapshot_->assetMode != activeAssetMode) {
		return false;
	}
	if (currentSnapshot_->workspaceGeneration != activeWorkspaceGen) {
		return false;
	}
	return true;
}

void ReferenceStyleStore::swapWith(std::optional<ReferenceStyleSnapshot>& other) {
	std::lock_guard lock(mutex_);
	currentSnapshot_.swap(other);
}

void ReferenceStyleStore::swapTargetWith(std::optional<TargetAreaSnapshot>& other) {
	std::lock_guard lock(mutex_);
	currentTarget_.swap(other);
}

bool ReferenceSourceOverlapsTarget(const ReferenceStyleSnapshot& reference, const TargetAreaSnapshot& target) {
	if (reference.floor != target.floor || target.positions.empty()) {
		return false;
	}

	// Bounds are protected as well as the exact mask. This prevents a target
	// from overwriting an unselected hole inside the captured source region.
	return BoundsOverlap(reference.sourceMin, reference.sourceMax, target.targetMin, target.targetMax);
}

std::optional<std::string> ValidateReferenceTargetWrite(
	const ReferenceStyleSnapshot* reference,
	const TargetAreaSnapshot* target,
	SessionId activeMapSessionId,
	uint64_t activeWorkspaceGeneration,
	const std::vector<Position>& writePositions,
	bool allowReferenceSourceOverwrite
) {
	if (!reference) {
		return std::nullopt;
	}
	if (!target) {
		return "No target area is selected. Select a target area and call target_capture first.";
	}
	if (target->mapSessionId != activeMapSessionId) {
		return "Target area belongs to a stale map session. Capture the target again.";
	}
	if (target->workspaceGeneration != activeWorkspaceGeneration) {
		return "Target area belongs to a stale resource workspace. Capture the target again.";
	}

	const bool overwriteAllowed = allowReferenceSourceOverwrite || target->allowReferenceSourceOverwrite;
	if (!overwriteAllowed && ReferenceSourceOverlapsTarget(*reference, *target)) {
		return "Target area overlaps the captured reference source. Select a different target area.";
	}

	for (const Position& position : writePositions) {
		if (position.z != target->floor || !HasExactPosition(target->positions, position)) {
			return "Write position is outside the captured target area. Use target_get and restrict writes to its exact positions.";
		}
	}

	return std::nullopt;
}
