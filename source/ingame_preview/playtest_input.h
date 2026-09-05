// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_PLAYTEST_INPUT_H_
#define NEXAMAP_PLAYTEST_INPUT_H_
#include "playtest_controller.h"
#include <array>
namespace Playtest {
	inline std::optional<Facing> LetterDirection(int key) {
		switch (key) {
			case 'W':
			case 'w':
				return Facing::North;
			case 'D':
			case 'd':
				return Facing::East;
			case 'S':
			case 's':
				return Facing::South;
			case 'A':
			case 'a':
				return Facing::West;
			default:
				return {};
		}
	}
	class Input {
	public:
		void press(Facing facing) {
			held[static_cast<size_t>(facing)] = ++sequence;
		}
		void release(Facing facing) {
			held[static_cast<size_t>(facing)] = 0;
		}
		void clear() {
			held = {};
			sequence = 0;
		}
		std::optional<Facing> direction() const {
			size_t latest = 0;
			for (size_t i = 1; i < held.size(); ++i) {
				if (held[i] > held[latest]) {
					latest = i;
				}
			}
			return held[latest] ? std::optional<Facing>(static_cast<Facing>(latest)) : std::nullopt;
		}

	private:
		std::array<uint64_t, 4> held {};
		uint64_t sequence = 0;
	};
}
#endif
