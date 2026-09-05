#include "map_diagnostics.h"

#include <sstream>

std::string_view MapDiagnosticCategoryName(MapDiagnosticCategory category) {
	switch (category) {
		case MapDiagnosticCategory::UniqueIds:
			return "Unique IDs";
		case MapDiagnosticCategory::ActionIds:
			return "Action IDs";
		case MapDiagnosticCategory::Items:
			return "Items and Ground";
		case MapDiagnosticCategory::Teleports:
			return "Teleports";
		case MapDiagnosticCategory::Houses:
			return "Houses";
		case MapDiagnosticCategory::Spawns:
			return "Spawns and Creatures";
		case MapDiagnosticCategory::DoorsAndKeys:
			return "Doors and Keys";
		case MapDiagnosticCategory::Waypoints:
			return "Waypoints";
	}
	return "Other";
}

std::string_view MapDiagnosticSeverityName(MapDiagnosticSeverity severity) {
	return severity == MapDiagnosticSeverity::Error ? "Error" : "Warning";
}

bool MatchesMapDiagnosticFilter(const MapDiagnosticIssue& issue, const MapDiagnosticFilter& filter) {
	if (issue.severity == MapDiagnosticSeverity::Error ? !filter.errors : !filter.warnings) {
		return false;
	}

	switch (issue.category) {
		case MapDiagnosticCategory::UniqueIds:
			return filter.uniqueIds;
		case MapDiagnosticCategory::ActionIds:
			return filter.actionIds;
		case MapDiagnosticCategory::Items:
			return filter.items;
		case MapDiagnosticCategory::Teleports:
			return filter.teleports;
		case MapDiagnosticCategory::Houses:
			return filter.houses;
		case MapDiagnosticCategory::Spawns:
			return filter.spawns;
		case MapDiagnosticCategory::DoorsAndKeys:
			return filter.doorsAndKeys;
		case MapDiagnosticCategory::Waypoints:
			return filter.waypoints;
	}
	return true;
}

void AppendDuplicateIdIssues(const MapDiagnosticOccurrences& occurrences, bool uniqueIds, std::vector<MapDiagnosticIssue>& issues) {
	for (const auto& [id, entries] : occurrences) {
		if (id == 0 || entries.size() < 2) {
			continue;
		}

		for (const MapDiagnosticOccurrence& entry : entries) {
			MapDiagnosticIssue issue;
			issue.severity = uniqueIds ? MapDiagnosticSeverity::Error : MapDiagnosticSeverity::Warning;
			issue.category = uniqueIds ? MapDiagnosticCategory::UniqueIds : MapDiagnosticCategory::ActionIds;
			issue.kind = uniqueIds ? MapDiagnosticKind::DuplicateUniqueId : MapDiagnosticKind::DuplicateActionId;
			issue.position = entry.position;
			issue.itemId = entry.itemId;
			if (uniqueIds) {
				issue.uniqueId = id;
				issue.summary = "Duplicate UniqueID " + std::to_string(id);
			} else {
				issue.actionId = id;
				issue.summary = "Duplicate ActionID " + std::to_string(id);
			}
			std::ostringstream detail;
			detail << (uniqueIds ? "UniqueID " : "ActionID ") << id << " is used by " << entries.size() << " items.";
			issue.detail = detail.str();
			issues.push_back(std::move(issue));
		}
	}
}
