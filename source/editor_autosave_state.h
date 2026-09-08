//////////////////////////////////////////////////////////////////////
// Small UI-independent state machine for debounced source autosave.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_EDITOR_AUTOSAVE_STATE_H_
#define NEXAMAP_EDITOR_AUTOSAVE_STATE_H_

#include <chrono>
#include <string>
#include <utility>

enum class EditorChangeImpact : unsigned int {
	None = 0,
	Dirty = 1,
	Preview = 2,
};

constexpr EditorChangeImpact operator|(EditorChangeImpact left, EditorChangeImpact right) {
	return static_cast<EditorChangeImpact>(static_cast<unsigned int>(left) | static_cast<unsigned int>(right));
}

constexpr bool HasEditorChangeImpact(EditorChangeImpact value, EditorChangeImpact expected) {
	return (static_cast<unsigned int>(value) & static_cast<unsigned int>(expected)) != 0;
}

class EditorChangeCoalescer {
public:
	void changed(EditorChangeImpact impact) {
		dirtyPending = dirtyPending || HasEditorChangeImpact(impact, EditorChangeImpact::Dirty);
		previewPending = previewPending || HasEditorChangeImpact(impact, EditorChangeImpact::Preview);
	}

	[[nodiscard]] bool takeDirty() {
		return std::exchange(dirtyPending, false);
	}
	[[nodiscard]] bool takePreview() {
		return std::exchange(previewPending, false);
	}
	[[nodiscard]] bool hasDirty() const {
		return dirtyPending;
	}
	[[nodiscard]] bool hasPreview() const {
		return previewPending;
	}

private:
	bool dirtyPending = false;
	bool previewPending = false;
};

class EditorAutosaveState {
public:
	using Clock = std::chrono::steady_clock;

	explicit EditorAutosaveState(std::chrono::milliseconds delay = std::chrono::milliseconds(650)) :
		delay(delay) { }

	void changed(Clock::time_point now = Clock::now()) {
		dirtyValue = true;
		errorValue.clear();
		deadline = now + delay;
	}

	[[nodiscard]] bool ready(Clock::time_point now = Clock::now()) const {
		return dirtyValue && errorValue.empty() && now >= deadline;
	}

	void saved() {
		dirtyValue = false;
		errorValue.clear();
	}

	void failed(std::string message) {
		dirtyValue = true;
		errorValue = std::move(message);
	}

	[[nodiscard]] bool dirty() const {
		return dirtyValue;
	}
	[[nodiscard]] bool hasError() const {
		return !errorValue.empty();
	}
	[[nodiscard]] const std::string& error() const {
		return errorValue;
	}

private:
	std::chrono::milliseconds delay;
	Clock::time_point deadline {};
	std::string errorValue;
	bool dirtyValue = false;
};

#endif // NEXAMAP_EDITOR_AUTOSAVE_STATE_H_
