// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_REFERENCE_STYLE_STORE_H_
#define NEXAMAP_REFERENCE_STYLE_STORE_H_

#include "reference_style.h"

#include <memory>
#include <mutex>
#include <optional>

/// Thread-safe store for the active AI style reference snapshot.
/// Associates ephemeral reference state with the active EditorResourceSession
/// so references are isolated across sessions and safely swapped during session changes.
class ReferenceStyleStore final {
public:
	static ReferenceStyleStore& Instance();

	void set(ReferenceStyleSnapshot snapshot);
	void clear();
	[[nodiscard]] bool hasReference() const noexcept;
	/// Return an owned copy so callers can safely read it after the lock ends.
	[[nodiscard]] std::optional<ReferenceStyleSnapshot> getSnapshot() const;

	void setTarget(TargetAreaSnapshot snapshot);
	void clearTarget();
	[[nodiscard]] bool hasTarget() const noexcept;
	[[nodiscard]] std::optional<TargetAreaSnapshot> getTargetSnapshot() const;

	/// Validates that the stored reference matches the currently active resource session parameters.
	[[nodiscard]] bool isCompatibleWith(ReferenceAssetMode activeAssetMode, uint64_t activeWorkspaceGen) const noexcept;

	/// Swaps the active snapshot with another optional snapshot (used by EditorResourceSession::swapWithGlobals).
	void swapWith(std::optional<ReferenceStyleSnapshot>& other);
	/// Swaps the active writable target with the session-local target.
	void swapTargetWith(std::optional<TargetAreaSnapshot>& other);

private:
	mutable std::mutex mutex_;
	std::optional<ReferenceStyleSnapshot> currentSnapshot_;
	std::optional<TargetAreaSnapshot> currentTarget_;
};

#endif // NEXAMAP_REFERENCE_STYLE_STORE_H_
