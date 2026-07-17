//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//////////////////////////////////////////////////////////////////////

#include "depot_conversion_validation.h"

#include <sstream>

namespace {
	std::string DescribeTown(const DepotConversionState* depot) {
		if (!depot) {
			return "<missing or no longer a Depot>";
		}
		if (depot->depotId == 0) {
			return "No Town (ID 0)";
		}
		if (!depot->townExists) {
			return "Undefined Town (ID " + std::to_string(depot->depotId) + ')';
		}
		return depot->townName + " (ID " + std::to_string(depot->depotId) + ')';
	}
}

bool ValidateDepotConversionStates(const std::vector<DepotConversionState>& original, const std::vector<DepotConversionState>& converted, std::string& error) {
	if (original == converted) {
		error.clear();
		return true;
	}

	std::size_t index = 0;
	while (index < original.size() && index < converted.size() && original[index] == converted[index]) {
		++index;
	}
	const DepotConversionState* before = index < original.size() ? &original[index] : nullptr;
	const DepotConversionState* after = index < converted.size() ? &converted[index] : nullptr;
	const DepotConversionState* located = before ? before : after;
	const bool sameLocation = before && after && before->x == after->x && before->y == after->y && before->z == after->z && before->itemOrdinal == after->itemOrdinal;
	const DepotConversionState* reopened = (sameLocation || !before) ? after : nullptr;

	std::ostringstream message;
	message << "Depot/Town association changed at {x = " << located->x << ", y = " << located->y << ", z = " << located->z << "}. Original item ID: ";
	message << (before ? std::to_string(before->itemId) : "<missing>");
	message << "; output item ID: " << (reopened ? std::to_string(reopened->outputItemId) : "<missing>");
	message << "; original Town: " << DescribeTown(before);
	message << "; after conversion: " << DescribeTown(reopened);
	if (before && reopened) {
		message << "; original AID/UID: " << before->actionId << '/' << before->uniqueId;
		message << "; reopened AID/UID: " << reopened->actionId << '/' << reopened->uniqueId;
	}
	error = message.str();
	return false;
}
