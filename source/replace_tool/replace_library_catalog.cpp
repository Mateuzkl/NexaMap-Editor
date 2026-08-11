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

#include "replace_library_catalog.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace {
	std::string Normalize(std::string_view text) {
		std::string normalized(text);
		std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return normalized;
	}

	bool Matches(const ReplaceLibraryItem& item, const std::string& normalizedQuery) {
		if (normalizedQuery.empty()) {
			return true;
		}
		return Normalize(item.name).find(normalizedQuery) != std::string::npos || std::to_string(item.serverId.value).find(normalizedQuery) != std::string::npos || std::to_string(item.clientId).find(normalizedQuery) != std::string::npos;
	}
}

void ReplaceLibraryCatalog::SetItems(std::vector<ReplaceLibraryItem> newItems) {
	std::sort(newItems.begin(), newItems.end(), [](const ReplaceLibraryItem& left, const ReplaceLibraryItem& right) {
		return left.serverId.value < right.serverId.value;
	});
	newItems.erase(std::unique(newItems.begin(), newItems.end(), [](const ReplaceLibraryItem& left, const ReplaceLibraryItem& right) {
					   return left.serverId == right.serverId;
				   }),
				   newItems.end());
	items = std::move(newItems);
}

void ReplaceLibraryCatalog::SetBrushes(std::vector<ReplaceLibraryBrush> newBrushes) {
	std::sort(newBrushes.begin(), newBrushes.end(), [](const ReplaceLibraryBrush& left, const ReplaceLibraryBrush& right) {
		const std::string leftName = Normalize(left.name);
		const std::string rightName = Normalize(right.name);
		return leftName == rightName ? left.key < right.key : leftName < rightName;
	});
	newBrushes.erase(std::unique(newBrushes.begin(), newBrushes.end(), [](const ReplaceLibraryBrush& left, const ReplaceLibraryBrush& right) {
						 return left.key == right.key;
					 }),
					 newBrushes.end());
	brushes = std::move(newBrushes);
}

std::vector<ReplaceLibraryItem> ReplaceLibraryCatalog::FilterItems(std::string_view query) const {
	const std::string normalizedQuery = Normalize(query);
	std::vector<ReplaceLibraryItem> result;
	for (const ReplaceLibraryItem& item : items) {
		if (Matches(item, normalizedQuery)) {
			result.push_back(item);
		}
	}
	return result;
}

std::vector<ReplaceLibraryItem> ReplaceLibraryCatalog::FilterBrushes(std::string_view query) const {
	const std::string normalizedQuery = Normalize(query);
	std::vector<ReplaceLibraryItem> result;
	for (const ReplaceLibraryBrush& brush : brushes) {
		ReplaceLibraryItem item = brush.look;
		item.key = brush.key;
		item.name = brush.name;
		if (Matches(item, normalizedQuery)) {
			result.push_back(std::move(item));
		}
	}
	return result;
}

const ReplaceLibraryBrush* ReplaceLibraryCatalog::FindBrush(uint32_t key) const {
	const auto iterator = std::find_if(brushes.begin(), brushes.end(), [key](const ReplaceLibraryBrush& brush) {
		return brush.key == key;
	});
	return iterator == brushes.end() ? nullptr : &*iterator;
}
