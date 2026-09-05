// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_PLAYTEST_CONTROLLER_H_
#define NEXAMAP_PLAYTEST_CONTROLLER_H_

#include <istream>
#include <cstdlib>
#include "../definitions.h"
#include "../position.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace Playtest {
	enum class Facing { North,
						East,
						South,
						West };
	enum FloorFlag : unsigned { Down = 1,
								North = 2,
								East = 4,
								South = 8,
								West = 16 };
	enum class UseKind { None,
						 Ladder,
						 Rope,
						 UnknownFloorChange };
	struct TileInfo {
		bool exists = false, ground = false, blocked = false;
		unsigned floorFlags = 0;
		int groundSpeed = 100;
		UseKind use = UseKind::None;
		bool hasTeleport = false;
		std::optional<Position> destination;
		uint16_t doorId = 0, doorAlternate = 0;
		bool doorOpen = false;
	};
	class World {
	public:
		virtual ~World() = default;
		virtual TileInfo read(const Position& position) const = 0;
	};
	struct DoorVisual {
		uint16_t original = 0, replacement = 0;
		bool open = false;
		bool operator==(const DoorVisual&) const = default;
	};
	class Controller {
	public:
		void reset(Position position);
		void invalidateInteractions();
		bool move(const World& world, Facing facing);
		bool use(const World& world, std::optional<Position> target = {});
		bool changeFloor(const World& world, int delta);
		bool advance(double milliseconds);
		void turn(Facing facing) {
			direction = facing;
		}
		Position position() const {
			return player;
		}
		Facing facing() const {
			return direction;
		}
		bool moving() const {
			return remaining > 0;
		}
		int offsetX() const;
		int offsetY() const;
		int animationFrame() const;
		const std::string& status() const {
			return message;
		}
		const std::map<Position, DoorVisual>& doorOverrides() const {
			return doors;
		}
		static Position offset(Facing facing);

	private:
		bool canStand(const World& world, const Position& position) const;
		bool blocked(const TileInfo& tile, const Position& position) const;
		bool resolve(const World& world, Position& target);
		Position player { -1, -1, -1 }, walkDelta;
		Facing direction = Facing::South;
		double duration = 0, remaining = 0;
		std::map<Position, DoorVisual> doors;
		std::string message = "Ready. Click the view and use WASD to walk.";
	};
}
#endif
