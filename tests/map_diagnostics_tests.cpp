#include "map_diagnostics.h"

#include <iostream>

namespace {
	int failures = 0;

	void check(bool condition, const char* message) {
		if (!condition) {
			std::cerr << "FAILED: " << message << '\n';
			++failures;
		}
	}
}

int main() {
	MapDiagnosticOccurrences occurrences;
	occurrences[5001] = { { Position(100, 200, 7), 2160 }, { Position(101, 200, 7), 1945 } };
	occurrences[5002] = { { Position(102, 200, 7), 2160 } };

	std::vector<MapDiagnosticIssue> issues;
	AppendDuplicateIdIssues(occurrences, true, issues);
	check(issues.size() == 2, "duplicate UID reports every conflicting occurrence");
	check(issues[0].severity == MapDiagnosticSeverity::Error, "duplicate UID is an error");
	check(issues[0].category == MapDiagnosticCategory::UniqueIds && issues[0].uniqueId == 5001, "UID identity is retained");
	check(issues[0].itemId == 2160 && issues[1].itemId == 1945, "item IDs are retained for tooltips");

	issues.clear();
	AppendDuplicateIdIssues(occurrences, false, issues);
	check(issues.size() == 2, "duplicate AID reports every conflicting occurrence");
	check(issues[0].severity == MapDiagnosticSeverity::Warning, "duplicate AID is a warning because shared actions can be intentional");
	check(issues[0].category == MapDiagnosticCategory::ActionIds && issues[0].actionId == 5001, "AID identity is retained");

	MapDiagnosticFilter filter;
	filter.warnings = false;
	check(!MatchesMapDiagnosticFilter(issues[0], filter), "severity filter hides warnings");
	filter.warnings = true;
	filter.actionIds = false;
	check(!MatchesMapDiagnosticFilter(issues[0], filter), "category filter hides AID findings");
	filter.actionIds = true;
	check(MatchesMapDiagnosticFilter(issues[0], filter), "enabled severity and category show finding");

	MapDiagnosticIssue waypoint;
	waypoint.category = MapDiagnosticCategory::Waypoints;
	waypoint.severity = MapDiagnosticSeverity::Error;
	filter.waypoints = false;
	check(!MatchesMapDiagnosticFilter(waypoint, filter), "waypoint filter is independent");

	std::cout << (failures == 0 ? "Map diagnostics model tests passed\n" : "Map diagnostics model tests failed\n");
	return failures == 0 ? 0 : 1;
}
