// SPDX-License-Identifier: GPL-3.0-or-later
#include "ingame_preview/playtest_controller.h"
#include "ingame_preview/playtest_input.h"
#include "ingame_preview/playtest_weather.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <chrono>
using namespace Playtest;
namespace {
	void check(bool value, const char* message) {
		if (!value) {
			throw std::runtime_error(message);
		}
	}
	struct TestWorld : World {
		std::map<Position, TileInfo> tiles;
		mutable int reads = 0;
		TileInfo read(const Position& p) const override {
			++reads;
			const auto it = tiles.find(p);
			return it == tiles.end() ? TileInfo {} : it->second;
		}
		TileInfo& ground(Position p) {
			auto& t = tiles[p];
			t.exists = t.ground = true;
			return t;
		}
	};
	void finish(Controller& c) {
		for (int i = 0; i < 20 && c.moving(); ++i) {
			c.advance(100);
		}
		check(!c.moving(), "Walking never completed");
	}
}
int main() {
	try {
		TestWorld world;
		const Position start(100, 100, 7), east(101, 100, 7);
		world.ground(start);
		world.ground(east);
		Controller c;
		c.reset(start);
		check(c.move(world, Facing::East) && c.position() == east && c.offsetX() == -32, "East walk/interpolation");
		check(!c.move(world, Facing::East), "Overlapping walk accepted");
		c.advance(100);
		check(c.offsetX() > -32 && c.offsetX() < 0, "Walk interpolation did not advance");
		finish(c);
		check(c.offsetX() == 0 && c.animationFrame() == 0, "Walk did not settle");
		check(!c.move(world, Facing::North) && c.position() == east, "Walked onto missing ground");
		world.ground(start).blocked = true;
		check(!c.move(world, Facing::West), "Walked through blocking metadata");
		world.ground(start).blocked = false;
		c.reset({ 0, 0, 0 });
		check(!c.move(world, Facing::West), "Negative coordinates accepted");
		std::cout << "PASS walking, interpolation, collision and map boundaries\n";
		for (auto [flag, delta] : { std::pair<unsigned, Position>(North, { 0, -1, -1 }), { East, { 1, 0, -1 } }, { South, { 0, 1, -1 } }, { West, { -1, 0, -1 } } }) {
			TestWorld stairs;
			stairs.ground(start);
			stairs.ground(east).floorFlags = flag;
			stairs.ground(east + delta);
			c.reset(start);
			check(c.move(stairs, Facing::East) && c.position() == east + delta && !c.moving(), "Directional stairs");
		}
		TestWorld down;
		down.ground(start);
		down.ground(east).floorFlags = Down;
		down.ground({ 101, 100, 8 }).floorFlags = North;
		down.ground({ 101, 101, 8 });
		c.reset(start);
		check(c.move(down, Facing::East) && c.position() == Position(101, 101, 8), "Down stairs reverse offset");
		down.tiles.erase({ 101, 101, 8 });
		c.reset(start);
		check(!c.move(down, Facing::East) && c.position() == start, "Invalid stairs changed position");
		for (const auto kind : { UseKind::Rope, UseKind::Ladder }) {
			TestWorld rope;
			rope.ground(start).use = kind;
			rope.ground({ 100, 101, 6 });
			c.reset(start);
			check(c.use(rope) && c.position() == Position(100, 101, 6), "Rope/ladder interaction");
			rope.tiles.erase({ 100, 101, 6 });
			c.reset(start);
			check(!c.use(rope), "Missing rope exit accepted");
		}
		std::cout << "PASS stairs in four directions, holes/down offsets, rope and ladder destinations\n";
		TestWorld teleport;
		teleport.ground(start);
		auto& tp = teleport.ground(east);
		tp.hasTeleport = true;
		tp.destination = Position(200, 200, 5);
		teleport.ground(*tp.destination);
		c.reset(start);
		check(c.move(teleport, Facing::East) && c.position() == *tp.destination, "Teleport destination");
		teleport.tiles[*tp.destination].hasTeleport = true;
		teleport.tiles[*tp.destination].destination = east;
		c.reset(start);
		check(!c.move(teleport, Facing::East) && c.position() == start, "Teleport cycle was not cancelled atomically");
		tp.destination.reset();
		c.reset(start);
		check(!c.move(teleport, Facing::East), "Unset teleport accepted");
		tp.destination = Position(0, 0, 0);
		teleport.ground(*tp.destination);
		c.reset(start);
		check(c.move(teleport, Facing::East) && c.position() == Position(0, 0, 0), "Explicit origin destination rejected");
		std::cout << "PASS teleports, unset destinations, origin and cycles\n";
		auto& door = world.ground(east);
		door.doorId = 500;
		door.doorAlternate = 501;
		door.doorOpen = false;
		c.reset(start);
		check(!c.move(world, Facing::East), "Closed door walk allowed");
		check(c.use(world, east) && c.doorOverrides().size() == 1 && !door.doorOpen && door.doorId == 500, "Door modified world definition");
		check(c.move(world, Facing::East), "Opened door blocked movement");
		finish(c);
		check(!c.use(world, east), "Door closed on player");
		c.invalidateInteractions();
		check(c.doorOverrides().empty(), "Map change left stale doors");
		c.reset(start);
		check(c.use(world, east), "Door open after reset");
		TestWorld other;
		other.ground(start);
		other.ground(east).blocked = true;
		c.reset(start);
		check(c.doorOverrides().empty() && !c.move(other, Facing::East), "Cross-map override survived reset");
		check(!c.use(world, Position(200, 200, 7)), "Distant use accepted");
		world.ground({ 100, 100, 6 });
		c.reset(start);
		check(c.changeFloor(world, -1), "Valid manual floor rejected");
		check(!c.changeFloor(world, -1), "Missing manual floor accepted");
		std::cout << "PASS local doors, map/session reset, nearby use and floor inspection\n";
		Input input;
		for (int key : { 'W', 'A', 'S', 'D', 'w', 'a', 's', 'd' }) {
			check(LetterDirection(key).has_value(), "WASD mapping");
		}
		check(!LetterDirection('X'), "Unexpected movement key");
		input.press(Facing::North);
		input.press(Facing::East);
		check(input.direction() == Facing::East, "Latest held key lost");
		input.release(Facing::East);
		check(input.direction() == Facing::North, "Held key did not resume");
		input.clear();
		check(!input.direction(), "Focus loss left stuck movement");
		std::cout << "PASS WASD, held/released keys and focus reset\n";
		for (auto weather : { Weather::Off, Weather::Rain, Weather::Storm, Weather::Fog, Weather::Snow, Weather::DesertHeat }) {
			for (float scale : { 1.f, 1.25f, 1.5f, 2.f }) {
				auto frame = BuildWeather(weather, 480 * scale, 352 * scale, 12345.67, scale);
				check(frame.count <= frame.quads.size(), "Unbounded weather work");
				check((weather == Weather::Off) == (frame.count == 0), "Weather off/preset failed");
				for (size_t i = 0; i < frame.count; ++i) {
					check(std::isfinite(frame.quads[i].x) && std::isfinite(frame.quads[i].y), "Invalid weather coordinates");
				}
			}
		}
		check(BuildWeather(Weather::Rain, 0, 0, 1, 1).count == 0, "Zero-sized weather viewport");
		const auto lightning = BuildWeather(Weather::Storm, 480, 352, 6.2, 1);
		check(lightning.count > BuildWeather(Weather::Storm, 480, 352, 5.0, 1).count && lightning.count <= lightning.quads.size(), "Storm lightning missing or unbounded");
		check(BuildWeather(Weather::Rain, 480, 352, 6.2, 1).count == 64, "Lightning leaked into rain");
		const auto began = std::chrono::steady_clock::now();
		size_t quads = 0;
		for (int i = 0; i < 10000; ++i) {
			quads += BuildWeather(Weather::Storm, 1920, 1080, i / 30.0, 2).count;
		}
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
		std::cout << "PASS weather presets/DPI/lightning: " << quads << " quads in 10000 synthetic frames; CPU " << ms << " ms\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << "FAIL " << e.what() << '\n';
		return 1;
	}
}
