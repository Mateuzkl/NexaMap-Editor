//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_HOUSE_PASTE_TRANSACTION_H_
#define RME_HOUSE_PASTE_TRANSACTION_H_

#include <algorithm>
#include <span>

namespace HousePasteTransaction {
	struct UndoState {
		bool exists = false;
		bool sessionMatches = false;
		bool snapshotMatches = false;
		size_t houseTileCount = 0;
		size_t affectedHouseTileCount = 0;
	};

	struct RedoState {
		bool idOccupied = false;
	};

	inline bool CanUndo(std::span<const UndoState> states) {
		return std::ranges::all_of(states, [](const UndoState& state) {
			return state.exists && state.sessionMatches && state.snapshotMatches && state.houseTileCount == state.affectedHouseTileCount;
		});
	}

	inline bool CanRedo(std::span<const RedoState> states) {
		return std::ranges::all_of(states, [](const RedoState& state) {
			return !state.idOccupied;
		});
	}
}

#endif
