#include "main.h"
#include "map.h"

namespace {
int failures = 0;

void check(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}
} // namespace

int main() {
	Map map;
	map.setWidth(256);
	map.setHeight(256);

	auto* wp = new Waypoint;
	wp->name = "spawn";
	wp->pos = Position(50, 50, 7);
	map.waypoints.addWaypoint(wp);

	check(map.waypoints.getWaypoint(map.getTileL(wp->pos)) == wp, "position index should resolve waypoint");

	map.waypoints.addCategory("Routes");
	map.waypoints.moveWaypointIntoCategory("spawn", "Routes", true);
	check(map.waypoints.validateInvariants(nullptr), "group invariants should hold after categorize");

	const Position oldPos = wp->pos;
	wp->pos = Position(51, 50, 7);
	map.waypoints.notifyWaypointPositionChanged(wp, oldPos);
	check(map.waypoints.getWaypoint(map.getTileL(Position(51, 50, 7))) == wp, "position index should update on move");
	check(map.waypoints.getWaypoint(map.getTileL(oldPos)) == nullptr, "old position index entry should be cleared");

	map.waypoints.removeWaypoint("spawn");
	check(map.waypoints.getWaypoint(map.getTileL(Position(51, 50, 7))) == nullptr, "position index should clear on delete");

	if (failures != 0) {
		std::cerr << failures << " waypoint group test(s) failed.\n";
		return 1;
	}
	std::cout << "All waypoint group tests passed.\n";
	return 0;
}
