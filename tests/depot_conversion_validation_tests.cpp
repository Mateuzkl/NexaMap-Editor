#include "depot_conversion_validation.h"

#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

namespace {
	int failures = 0;

	void check(bool condition, std::string_view test) {
		if (!condition) {
			std::cerr << "FAILED: " << test << '\n';
			++failures;
		}
	}

	DepotConversionState depot(uint16_t itemId, uint16_t outputItemId, uint16_t depotId, std::string town, int32_t x, uint32_t ordinal = 0) {
		return { x, 200, 7, ordinal, itemId, outputItemId, depotId, 0, 0, true, std::move(town) };
	}
}

int main() {
	std::string error;
	std::vector<DepotConversionState> serverToClient {
		depot(2589, 3497, 1, "Thais", 100),
		depot(2590, 3498, 1, "Thais", 101),
		depot(2591, 3499, 4, "Carlin", 102),
		depot(2592, 3500, 300, "Kazordoon", 103),
	};
	check(ValidateDepotConversionStates(serverToClient, serverToClient, error), "2589-2592 preserve Towns during ServerID to ClientID conversion");

	auto clientToServer = serverToClient;
	for (DepotConversionState& state : clientToServer) {
		state.outputItemId = state.itemId;
	}
	check(ValidateDepotConversionStates(clientToServer, clientToServer, error), "2589-2592 preserve Towns during ClientID to ServerID conversion");

	auto repeatedTown = serverToClient;
	repeatedTown.push_back(depot(2589, 3497, 1, "Thais", 104, 1));
	check(ValidateDepotConversionStates(repeatedTown, repeatedTown, error), "multiple Depots may share one Town");

	auto noTown = serverToClient;
	noTown[0].depotId = 0;
	noTown[0].townExists = false;
	noTown[0].townName.clear();
	check(!ValidateDepotConversionStates(serverToClient, noTown, error), "configured Depot becoming No Town is rejected");
	check(error.find("{x = 100, y = 200, z = 7}") != std::string::npos && error.find("Thais (ID 1)") != std::string::npos && error.find("No Town (ID 0)") != std::string::npos, "No Town diagnostic identifies position and Towns");

	auto changedTown = serverToClient;
	changedTown[2].depotId = 1;
	changedTown[2].townName = "Thais";
	check(!ValidateDepotConversionStates(serverToClient, changedTown, error), "changing a Depot to another Town is rejected");

	auto changedAttributes = serverToClient;
	changedAttributes[3].actionId = 100;
	changedAttributes[3].uniqueId = 200;
	check(!ValidateDepotConversionStates(serverToClient, changedAttributes, error), "Depot AID and UID changes are rejected");

	auto missingDepot = serverToClient;
	missingDepot.pop_back();
	check(!ValidateDepotConversionStates(serverToClient, missingDepot, error), "missing Depot is rejected");

	if (failures != 0) {
		std::cerr << failures << " Depot conversion validation test(s) failed.\n";
		return 1;
	}
	std::cout << "8 Depot conversion validation tests passed.\n";
	return 0;
}
