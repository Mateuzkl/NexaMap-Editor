#include <iostream>
#include <string>

#include "zones.h"

namespace {
	int failures = 0;

	void check(bool condition, const std::string& message) {
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	}
}

int main() {
	Zones zones;

	check(!Zones::isValidName(""), "empty zone names must be rejected");
	check(!Zones::isValidName("   "), "whitespace-only zone names must be rejected");
	check(Zones::isValidName("Zone 1"), "non-empty zone names must be accepted");
	check(!Zones::isValidID(0), "zone id zero must be rejected");
	check(!Zones::isValidID(65536), "zone ids must fit the OTBM uint16 field");
	check(Zones::isValidID(65535), "the largest OTBM zone id must be accepted");
	check(!zones.addZone("Invalid", 0), "zone id zero must be rejected");
	check(!zones.addZone("Too Large", 65536), "zone ids must fit the OTBM uint16 field");
	check(zones.addZone("Zone 1", 1), "first zone should be added");
	check(!zones.addZone("Zone 1", 2), "duplicate names must be rejected");
	check(!zones.addZone("Zone 2", 1), "duplicate ids must be rejected");
	check(zones.addZone("Zone 2", 2), "second zone should be added");
	check(!zones.empty() && zones.size() == 2, "zone registry size must track successful inserts");

	check(zones.renameZone("Zone 1", "Protection Area"), "rename should succeed");
	check(!zones.hasZone("Zone 1"), "old name must disappear after rename");
	check(zones.hasZone("Protection Area", 1), "name and id lookup must match the renamed zone");
	check(!zones.hasZone("Protection Area", 2), "name and id lookup must reject mismatched pairs");
	check(zones.getZoneID("Protection Area") == 1, "rename must preserve the id");
	check(zones.getZoneName(1) == "Protection Area", "id lookup must use the new name");
	check(!zones.renameZone("Protection Area", ""), "rename must reject an empty name");
	check(!zones.renameZone("Protection Area", " \t"), "rename must reject a whitespace-only name");
	check(!zones.renameZone("Protection Area", "Zone 2"), "rename must reject duplicate names");
	check(zones.getZoneID("Protection Area") == 1, "failed rename must preserve the original mapping");
	check(zones.getEmptyID() == 3, "renamed ids must remain reserved");

	check(zones.removeZone("Protection Area"), "existing zone should be removable");
	check(!zones.removeZone("Protection Area"), "removing a missing zone should fail");
	check(!zones.hasZone(1) && zones.size() == 1, "removing a zone must release its id and update the size");
	check(zones.getEmptyID() == 1, "deleted ids may be reused");

	if (failures != 0) {
		std::cerr << failures << " zone test(s) failed.\n";
		return 1;
	}

	std::cout << "All zone tests passed.\n";
	return 0;
}
