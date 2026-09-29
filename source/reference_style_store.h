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
	[[nodiscard]] const ReferenceStyleSnapshot* get() const noexcept;
	[[nodiscard]] std::optional<ReferenceStyleSnapshot> getSnapshot() const;

	/// Validates that the stored reference matches the currently active resource session parameters.
	[[nodiscard]] bool isCompatibleWith(ReferenceAssetMode activeAssetMode, uint64_t activeWorkspaceGen) const noexcept;

	/// Swaps the active snapshot with another optional snapshot (used by EditorResourceSession::swapWithGlobals).
	void swapWith(std::optional<ReferenceStyleSnapshot>& other);

private:
	mutable std::mutex mutex_;
	std::optional<ReferenceStyleSnapshot> currentSnapshot_;
};

#endif // NEXAMAP_REFERENCE_STYLE_STORE_H_
