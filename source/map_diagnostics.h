#ifndef RME_MAP_DIAGNOSTICS_H_
#define RME_MAP_DIAGNOSTICS_H_

#include "definitions.h"

#include <istream>

#include "position.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

enum class MapDiagnosticSeverity {
	Error,
	Warning,
};

enum class MapDiagnosticCategory {
	UniqueIds,
	ActionIds,
	Items,
	Teleports,
	Houses,
	Spawns,
	DoorsAndKeys,
	Waypoints,
};

enum class MapDiagnosticKind {
	DuplicateUniqueId,
	DuplicateActionId,
	InvalidItemId,
	BrokenTeleport,
	TileWithoutGround,
	InvalidCreature,
	InvalidSpawn,
	InconsistentHouse,
	InconsistentDoor,
	InconsistentKey,
	InvalidWaypoint,
};

struct MapDiagnosticIssue {
	MapDiagnosticSeverity severity = MapDiagnosticSeverity::Warning;
	MapDiagnosticCategory category = MapDiagnosticCategory::Items;
	MapDiagnosticKind kind = MapDiagnosticKind::InvalidItemId;
	Position position;
	std::string summary;
	std::string detail;
	uint16_t itemId = 0;
	uint16_t actionId = 0;
	uint16_t uniqueId = 0;
};

struct MapDiagnosticFilter {
	bool errors = true;
	bool warnings = true;
	bool uniqueIds = true;
	bool actionIds = true;
	bool items = true;
	bool teleports = true;
	bool houses = true;
	bool spawns = true;
	bool doorsAndKeys = true;
	bool waypoints = true;
};

struct MapDiagnosticOccurrence {
	Position position;
	uint16_t itemId = 0;
};

using MapDiagnosticOccurrences = std::map<uint16_t, std::vector<MapDiagnosticOccurrence>>;

std::string_view MapDiagnosticCategoryName(MapDiagnosticCategory category);
std::string_view MapDiagnosticSeverityName(MapDiagnosticSeverity severity);
bool MatchesMapDiagnosticFilter(const MapDiagnosticIssue& issue, const MapDiagnosticFilter& filter);
void AppendDuplicateIdIssues(const MapDiagnosticOccurrences& occurrences, bool uniqueIds, std::vector<MapDiagnosticIssue>& issues);

#endif
