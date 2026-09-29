#include "main.h"
#include "map.h"

#include <memory>

namespace {
	int failures = 0;

	void check(bool condition, const char* message) {
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	}

	std::unique_ptr<Waypoint> waypoint(std::string name, const Position& position, std::string category = {}) {
		auto result = std::make_unique<Waypoint>();
		result->name = std::move(name);
		result->pos = position;
		result->category = std::move(category);
		return result;
	}

	void testPositionIndexAndCounts() {
		Map map;
		map.setWidth(256);
		map.setHeight(256);
		const Position shared(50, 50, 7);
		const Position moved(51, 50, 7);

		check(map.waypoints.addWaypoint(waypoint("alpha", shared)), "first waypoint should be added");
		check(map.waypoints.addWaypoint(waypoint("beta", shared)), "second waypoint at the same position should be added");
		check(map.getTileL(shared)->getWaypointCount() == 2, "two colocated waypoints should increment the tile count twice");
		check(map.waypoints.getWaypoint(map.getTileL(shared)) != nullptr, "shared position should resolve a waypoint");

		check(map.waypoints.moveWaypoint("alpha", moved), "moving a waypoint should succeed");
		check(map.getTileL(shared)->getWaypointCount() == 1, "move should decrement the old tile exactly once");
		check(map.getTileL(moved)->getWaypointCount() == 1, "move should increment the new tile exactly once");
		check(map.waypoints.getWaypoint(map.getTileL(shared))->name == "beta", "the remaining colocated waypoint should stay reachable");
		check(map.waypoints.getWaypoint(map.getTileL(moved))->name == "alpha", "the moved waypoint should be indexed at its destination");

		check(map.waypoints.removeWaypoint("alpha"), "moved waypoint should be removed");
		check(map.getTileL(moved)->getWaypointCount() == 0, "delete should decrement the destination exactly once");
		check(map.waypoints.removeWaypoint("beta"), "remaining waypoint should be removed");
		check(map.getTileL(shared)->getWaypointCount() == 0, "delete should clear the shared tile count");
		check(map.waypoints.validateInvariants(nullptr), "empty registry invariants should hold");
	}

	void testCategoriesRenameAndNormalization() {
		Map map;
		map.setWidth(256);
		map.setHeight(256);
		check(map.waypoints.addWaypoint(waypoint("spawn", Position(70, 70, 7))), "waypoint should be added");
		Waypoint* identity = map.waypoints.getWaypoint("spawn");
		const int count = map.getTileL(identity->pos)->getWaypointCount();

		check(map.waypoints.addCategory("Routes"), "category should be added");
		check(map.waypoints.setWaypointCategory("spawn", "Routes"), "waypoint should be categorized");
		check(map.getTileL(identity->pos)->getWaypointCount() == count, "category edit must not change tile count");
		check(map.waypoints.renameCategory("Routes", "Travel"), "category should be renamed");
		check(map.waypoints.renameWaypoint("spawn", "arrival"), "waypoint should be renamed in place");
		check(map.waypoints.getWaypoint("arrival") == identity, "rename should preserve object identity");
		check(map.waypoints.removeCategory("Travel"), "category should be removed");
		check(identity->category.empty(), "removed category should make the waypoint uncategorized");
		check(map.waypoints.orderedWaypointsInCategory("") == std::vector<std::string> { "arrival" }, "removed category waypoint should be in uncategorized order");
		check(map.waypoints.validateInvariants(nullptr), "category mutations should preserve invariants");
	}

	void testAtomicMetadataValidation() {
		Map map;
		map.setWidth(256);
		map.setHeight(256);
		map.waypoints.addWaypoint(waypoint("one", Position(80, 80, 7)));
		map.waypoints.addWaypoint(waypoint("two", Position(81, 80, 7)));

		const std::vector<std::string> categories { "Routes" };
		const std::vector<std::string> uncategorized { "two" };
		const std::map<std::string, std::vector<std::string>> orders { { "Routes", { "one" } } };
		const std::map<std::string, std::string> assignments { { "one", "Routes" }, { "two", "" } };
		check(map.waypoints.applyOrderingFromMetadata(categories, uncategorized, orders, assignments), "valid metadata should apply");

		const auto beforeCategories = map.waypoints.categories();
		const auto beforeUncategorized = map.waypoints.uncategorizedOrder();
		const std::map<std::string, std::vector<std::string>> invalidOrders { { "Routes", { "one", "two" } } };
		check(!map.waypoints.applyOrderingFromMetadata(categories, uncategorized, invalidOrders, assignments), "duplicate/mismatched metadata should fail");
		check(map.waypoints.categories() == beforeCategories, "failed metadata must not change categories");
		check(map.waypoints.uncategorizedOrder() == beforeUncategorized, "failed metadata must not partially change ordering");
		check(map.waypoints.getWaypoint("one")->category == "Routes", "failed metadata must not change waypoint categories");
		check(map.waypoints.validateInvariants(nullptr), "metadata failure should leave valid state");
	}

	void testImportTransfersOwnershipAndIndexes() {
		Map destination;
		Map source;
		destination.setWidth(512);
		destination.setHeight(512);
		source.setWidth(256);
		source.setHeight(256);
		source.waypoints.addWaypoint(waypoint("first", Position(20, 20, 7), "Imported"));
		source.waypoints.addWaypoint(waypoint("second", Position(21, 20, 7), "Imported"));
		source.waypoints.moveWaypointRelative("second", "first", true);

		destination.waypoints.importWaypointsFrom(source.waypoints, Position(100, 100, 0));
		check(source.waypoints.empty(), "import should transfer ownership out of the source registry");
		check(destination.waypoints.size() == 2, "import should preserve all unique waypoints");
		check(destination.waypoints.getWaypoint(destination.getTileL(Position(120, 120, 7))) != nullptr, "first imported waypoint should be indexed with offset");
		check(destination.waypoints.getWaypoint(destination.getTileL(Position(121, 120, 7))) != nullptr, "second imported waypoint should be indexed with offset");
		check(destination.waypoints.orderedWaypointsInCategory("Imported") == std::vector<std::string>({ "second", "first" }), "import should preserve category order");
		check(destination.waypoints.validateInvariants(nullptr), "import should preserve destination invariants");
	}
} // namespace

int RunMultiplayerSessionTests(int, char**) {
	testPositionIndexAndCounts();
	testCategoriesRenameAndNormalization();
	testAtomicMetadataValidation();
	testImportTransfersOwnershipAndIndexes();

	if (failures != 0) {
		std::cerr << failures << " waypoint group test(s) failed.\n";
		return 1;
	}
	std::cout << "All waypoint group tests passed.\n";
	return 0;
}
