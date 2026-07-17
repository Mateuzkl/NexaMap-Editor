//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//////////////////////////////////////////////////////////////////////

#ifndef RME_DEPOT_CONVERSION_VALIDATION_H_
#define RME_DEPOT_CONVERSION_VALIDATION_H_

#include <cstdint>
#include <string>
#include <vector>

struct DepotConversionState {
	int32_t x = 0;
	int32_t y = 0;
	int32_t z = 0;
	uint32_t itemOrdinal = 0;
	uint16_t itemId = 0;
	uint16_t outputItemId = 0;
	uint16_t depotId = 0;
	uint16_t actionId = 0;
	uint16_t uniqueId = 0;
	bool townExists = false;
	std::string townName;

	[[nodiscard]] bool operator==(const DepotConversionState& other) const = default;
};

bool ValidateDepotConversionStates(const std::vector<DepotConversionState>& original, const std::vector<DepotConversionState>& converted, std::string& error);

#endif
