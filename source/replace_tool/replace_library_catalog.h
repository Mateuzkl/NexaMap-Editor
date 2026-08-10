//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#ifndef RME_REPLACE_TOOL_REPLACE_LIBRARY_CATALOG_H_
#define RME_REPLACE_TOOL_REPLACE_LIBRARY_CATALOG_H_

#include "replace_rule.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct ReplaceLibraryItem {
	uint32_t key = 0;
	ServerItemId serverId;
	uint16_t clientId = 0;
	std::string name;

	bool operator==(const ReplaceLibraryItem&) const = default;
};

struct ReplaceLibraryBrush {
	uint32_t key = 0;
	std::string name;
	ReplaceLibraryItem look;
	std::vector<ReplaceLibraryItem> relatedItems;
};

class ReplaceLibraryCatalog {
public:
	void SetItems(std::vector<ReplaceLibraryItem> items);
	void SetBrushes(std::vector<ReplaceLibraryBrush> brushes);

	[[nodiscard]] const std::vector<ReplaceLibraryItem>& GetItems() const {
		return items;
	}
	[[nodiscard]] const std::vector<ReplaceLibraryBrush>& GetBrushes() const {
		return brushes;
	}

	[[nodiscard]] std::vector<ReplaceLibraryItem> FilterItems(std::string_view query) const;
	[[nodiscard]] std::vector<ReplaceLibraryItem> FilterBrushes(std::string_view query) const;
	[[nodiscard]] const ReplaceLibraryBrush* FindBrush(uint32_t key) const;

private:
	std::vector<ReplaceLibraryItem> items;
	std::vector<ReplaceLibraryBrush> brushes;
};

#endif
