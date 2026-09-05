// SPDX-License-Identifier: GPL-3.0-or-later
#include "playtest_controller.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace Playtest {
	Position Controller::offset(Facing facing) {
		switch (facing) {
			case Facing::North:
				return { 0, -1, 0 };
			case Facing::East:
				return { 1, 0, 0 };
			case Facing::South:
				return { 0, 1, 0 };
			case Facing::West:
				return { -1, 0, 0 };
		}
		return {};
	}
	void Controller::reset(Position position) {
		player = position;
		direction = Facing::South;
		remaining = duration = 0;
		walkDelta = {};
		doors.clear();
		message = "Ready. Click the view and use WASD to walk.";
	}
	void Controller::invalidateInteractions() {
		doors.clear();
		remaining = 0;
		message = "Map changed. Local door states were reset.";
	}
	bool Controller::blocked(const TileInfo& tile, const Position& position) const {
		bool doorOpen = tile.doorOpen;
		if (const auto it = doors.find(position); it != doors.end() && it->second.original == tile.doorId) {
			doorOpen = it->second.open;
		}
		return tile.blocked || (tile.doorId != 0 && !doorOpen);
	}
	bool Controller::canStand(const World& world, const Position& position) const {
		if (!position.isValid()) {
			return false;
		}
		const auto tile = world.read(position);
		return tile.exists && tile.ground && !blocked(tile, position);
	}
	bool Controller::resolve(const World& world, Position& target) {
		auto tile = world.read(target);
		if (tile.floorFlags != 0) {
			Position destination = target;
			if (tile.floorFlags & Down) {
				++destination.z;
				if (!destination.isValid()) {
					message = "No floor below this transition.";
					return false;
				}
				const unsigned lower = world.read(destination).floorFlags;
				if (lower & North) {
					++destination.y;
				}
				if (lower & South) {
					--destination.y;
				}
				if (lower & East) {
					--destination.x;
				}
				if (lower & West) {
					++destination.x;
				}
			} else {
				--destination.z;
				if (tile.floorFlags & North) {
					--destination.y;
				}
				if (tile.floorFlags & South) {
					++destination.y;
				}
				if (tile.floorFlags & East) {
					++destination.x;
				}
				if (tile.floorFlags & West) {
					--destination.x;
				}
			}
			if (!canStand(world, destination)) {
				message = "Floor transition has no walkable destination.";
				return false;
			}
			target = destination;
		}
		std::set<Position> visited;
		for (int hop = 0; hop < 8; ++hop) {
			if (!visited.insert(target).second) {
				message = "Teleport cycle detected. Movement cancelled.";
				return false;
			}
			tile = world.read(target);
			if (!tile.hasTeleport) {
				return canStand(world, target);
			}
			if (!tile.destination || !canStand(world, *tile.destination)) {
				message = "Teleport has no valid walkable destination.";
				return false;
			}
			target = *tile.destination;
		}
		message = "Teleport chain limit reached. Movement cancelled.";
		return false;
	}
	bool Controller::move(const World& world, Facing facing) {
		if (moving() || !player.isValid()) {
			return false;
		}
		direction = facing;
		const Position delta = offset(facing);
		Position target = player + delta;
		if (!target.isValid()) {
			message = "Map boundary reached.";
			return false;
		}
		const auto tile = world.read(target);
		if (!tile.exists || (!tile.ground && !tile.floorFlags) || blocked(tile, target)) {
			message = "Blocked. Use a door with right-click or Space.";
			return false;
		}
		if (!resolve(world, target)) {
			return false;
		}
		const Position previous = player;
		player = target;
		if (player == previous + delta) {
			walkDelta = delta;
			duration = remaining = std::clamp(1000.0 * std::max(1, tile.groundSpeed) / 220.0, 80.0, 1000.0);
			message = "Walking";
		} else {
			remaining = 0;
			message = "Transition complete";
		}
		return true;
	}
	bool Controller::use(const World& world, std::optional<Position> target) {
		if (moving() || !player.isValid()) {
			return false;
		}
		Position at = target.value_or(player + offset(direction));
		if (!target && world.read(player).use != UseKind::None) {
			at = player;
		}
		if (at.z != player.z || std::abs(at.x - player.x) + std::abs(at.y - player.y) > 1) {
			message = "Move next to the object to use it.";
			return false;
		}
		if (!at.isValid()) {
			return false;
		}
		const auto tile = world.read(at);
		if (tile.doorId) {
			if (!tile.doorAlternate) {
				message = "This door has no paired variant in the active resources.";
				return false;
			}
			if (const auto found = doors.find(at); found != doors.end()) {
				if (at == player && !tile.doorOpen) {
					message = "Step away before closing the door.";
					return false;
				}
				doors.erase(found);
				message = "Door restored (playtest only).";
			} else {
				if (at == player && tile.doorOpen) {
					message = "Step away before closing the door.";
					return false;
				}
				if (doors.size() >= 128) {
					message = "Local door limit reached. Reset playtest to clear it.";
					return false;
				}
				doors.emplace(at, DoorVisual { tile.doorId, tile.doorAlternate, !tile.doorOpen });
				message = tile.doorOpen ? "Door closed (playtest only)." : "Door opened (playtest only).";
			}
			return true;
		}
		Position destination = at;
		if (tile.floorFlags || tile.hasTeleport) {
			if (!resolve(world, destination)) {
				return false;
			}
		} else if (tile.use == UseKind::Rope || tile.use == UseKind::Ladder) {
			// Conventional rope/ladder exit; custom server scripts are not run.
			--destination.z;
			++destination.y;
			if (!canStand(world, destination)) {
				message = "No walkable rope/ladder exit. Custom server actions are not simulated.";
				return false;
			}
		} else {
			message = "No supported interaction. Server scripts are not simulated.";
			return false;
		}
		player = destination;
		remaining = 0;
		message = "Interaction complete (playtest only).";
		return true;
	}
	bool Controller::changeFloor(const World& world, int delta) {
		if (moving() || (delta != -1 && delta != 1)) {
			return false;
		}
		Position target = player;
		target.z += delta;
		if (!canStand(world, target)) {
			message = "No walkable tile on that floor.";
			return false;
		}
		player = target;
		message = "Floor inspection changed. No map tiles were modified.";
		return true;
	}
	bool Controller::advance(double milliseconds) {
		if (!moving()) {
			return false;
		}
		remaining = std::max(0.0, remaining - std::clamp(milliseconds, 0.0, 100.0));
		if (!moving()) {
			message = "Ready";
		}
		return true;
	}
	int Controller::offsetX() const {
		return duration > 0 ? static_cast<int>(std::lround(-walkDelta.x * 32.0 * remaining / duration)) : 0;
	}
	int Controller::offsetY() const {
		return duration > 0 ? static_cast<int>(std::lround(-walkDelta.y * 32.0 * remaining / duration)) : 0;
	}
	int Controller::animationFrame() const {
		return moving() ? 1 + (static_cast<int>((duration - remaining) / 100.0) % 2) : 0;
	}
}
