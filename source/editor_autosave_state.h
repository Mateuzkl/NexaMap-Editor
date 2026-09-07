//////////////////////////////////////////////////////////////////////
// Small UI-independent state machine for debounced source autosave.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_EDITOR_AUTOSAVE_STATE_H_
#define NEXAMAP_EDITOR_AUTOSAVE_STATE_H_

#include <chrono>
#include <string>

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
