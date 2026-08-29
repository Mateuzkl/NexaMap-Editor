#include <array>
#include <iostream>
#include <string>

#include "house_paste_transaction.h"
#include "session_id.h"

namespace {
	int failures = 0;

	void check(bool condition, const std::string& message) {
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	}
}

int main() {
	const SessionId sourceMapSession = CreateSessionId();
	const SessionId destinationMapSession = CreateSessionId();
	check(sourceMapSession != InvalidSessionId, "map session ids must never use the invalid sentinel");
	check(sourceMapSession != destinationMapSession, "different map sessions must have different identities");

	// A clipboard retains the numeric identity by value. Destroying/reopening the
	// source map cannot turn a new object into the original map by address reuse.
	const SessionId retainedClipboardSource = sourceMapSession;
	const SessionId reopenedSourceMapSession = CreateSessionId();
	check(retainedClipboardSource == sourceMapSession, "clipboard source identity must remain stable by value");
	check(retainedClipboardSource != reopenedSourceMapSession, "a reopened map must be a distinct session");

	using HousePasteTransaction::RedoState;
	using HousePasteTransaction::UndoState;

	const std::array validUndo { UndoState { true, true, true, 3, 3 } };
	check(HousePasteTransaction::CanUndo(validUndo), "an unchanged pasted House must be undoable");

	const std::array changedId { UndoState { false, false, false, 0, 0 } };
	check(!HousePasteTransaction::CanUndo(changedId), "changing the pasted House id must cancel the whole undo");

	const std::array removedHouse { UndoState { false, false, false, 0, 0 } };
	check(!HousePasteTransaction::CanUndo(removedHouse), "removing the pasted House must cancel the whole undo");

	const std::array replacedInstance { UndoState { true, false, true, 0, 0 } };
	check(!HousePasteTransaction::CanUndo(replacedInstance), "an unrelated House reusing the id must not be removed by undo");

	const std::array editedHouse { UndoState { true, true, false, 3, 3 } };
	check(!HousePasteTransaction::CanUndo(editedHouse), "editing pasted House metadata must cancel the whole undo");

	const std::array extraTile { UndoState { true, true, true, 4, 3 } };
	check(!HousePasteTransaction::CanUndo(extraTile), "House tiles outside the paste must cancel the whole undo");

	const std::array multiHouseUndo {
		UndoState { true, true, true, 2, 2 },
		UndoState { true, true, false, 1, 1 },
	};
	check(!HousePasteTransaction::CanUndo(multiHouseUndo), "one invalid House must cancel a multi-House undo atomically");

	const std::array validRedo { RedoState { false }, RedoState { false } };
	check(HousePasteTransaction::CanRedo(validRedo), "redo must proceed when all required House ids are free");

	const std::array conflictingRedo { RedoState { false }, RedoState { true } };
	check(!HousePasteTransaction::CanRedo(conflictingRedo), "one reused House id must cancel the whole redo");

	for (int cycle = 0; cycle < 5; ++cycle) {
		check(HousePasteTransaction::CanUndo(validUndo), "repeated undo validation must remain deterministic");
		check(HousePasteTransaction::CanRedo(validRedo), "repeated redo validation must remain deterministic");
	}

	if (failures != 0) {
		std::cerr << failures << " House paste hardening test(s) failed.\n";
		return 1;
	}

	std::cout << "All House paste hardening tests passed.\n";
	return 0;
}
