// SPDX-License-Identifier: GPL-3.0-or-later
#include "reference_style_store.h"

ReferenceStyleStore& ReferenceStyleStore::Instance() {
	static ReferenceStyleStore instance;
	return instance;
}

void ReferenceStyleStore::set(ReferenceStyleSnapshot snapshot) {
	std::lock_guard lock(mutex_);
	currentSnapshot_ = std::move(snapshot);
}

void ReferenceStyleStore::clear() {
	std::lock_guard lock(mutex_);
	currentSnapshot_.reset();
}

bool ReferenceStyleStore::hasReference() const noexcept {
	std::lock_guard lock(mutex_);
	return currentSnapshot_.has_value();
}

const ReferenceStyleSnapshot* ReferenceStyleStore::get() const noexcept {
	std::lock_guard lock(mutex_);
	return currentSnapshot_.has_value() ? &*currentSnapshot_ : nullptr;
}

std::optional<ReferenceStyleSnapshot> ReferenceStyleStore::getSnapshot() const {
	std::lock_guard lock(mutex_);
	return currentSnapshot_;
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
