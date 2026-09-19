//////////////////////////////////////////////////////////////////////
// This file is part of NexaMap Editor
//////////////////////////////////////////////////////////////////////
// NexaMap Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// NexaMap Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_PALETTE_MODEL_H_
#define NEXAMAP_PALETTE_MODEL_H_

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <map>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

class Brush;
class Tileset;
using TilesetContainer = std::map<std::string, Tileset*>;

enum class TilesetSortKey {
	ID = 0,
	Name = 1,
};

enum class TilesetSortDirection {
	Ascending = 0,
	Descending = 1,
};

struct PaletteSearchResult {
	Brush* brush = nullptr;
	int category = 0; // TilesetCategoryType
	std::string tilesetName;
};

namespace PaletteModel {

	inline int SanitizeTileSize(int sizePx) {
		if (sizePx != 16 && sizePx != 32 && sizePx != 64 && sizePx != 128) {
			return 32;
		}
		return sizePx;
	}

	inline int DeriveInitialTileSize(int explicitOverride, bool isSmallIcons) {
		if (explicitOverride == 16 || explicitOverride == 32 || explicitOverride == 64 || explicitOverride == 128) {
			return explicitOverride;
		}
		return isSmallIcons ? 16 : 32;
	}

	inline std::string NormalizeQuery(std::string_view query) {
		std::string result;
		result.reserve(query.size());
		for (size_t i = 0; i < query.size(); ++i) {
			unsigned char c = static_cast<unsigned char>(query[i]);
			if (c < 0x80) {
				result.push_back(static_cast<char>(std::tolower(c)));
			} else {
				// Preserve UTF-8 multibyte sequences without byte corruption
				result.push_back(static_cast<char>(c));
			}
		}
		return result;
	}

	inline bool MatchesSearch(std::string_view name, const std::vector<uint32_t>& ids, const std::string& queryLower) {
		if (queryLower.empty()) {
			return true;
		}

		std::string nameLower = NormalizeQuery(name);
		if (nameLower.find(queryLower) != std::string::npos) {
			return true;
		}

		for (uint32_t id : ids) {
			std::string idStr = std::to_string(id);
			if (idStr.find(queryLower) != std::string::npos) {
				return true;
			}
		}

		return false;
	}

	template <typename ItemPtr, typename GetNameFn, typename GetIdFn>
	void SortItems(std::vector<ItemPtr>& items, TilesetSortKey key, TilesetSortDirection dir, GetNameFn&& getName, GetIdFn&& getId) {
		std::stable_sort(items.begin(), items.end(), [&](const ItemPtr& a, const ItemPtr& b) {
			if (!a && !b) {
				return false;
			}
			if (!a) {
				return false;
			}
			if (!b) {
				return true;
			}

			if (key == TilesetSortKey::ID) {
				uint32_t idA = getId(a);
				uint32_t idB = getId(b);
				if (idA != idB) {
					return dir == TilesetSortDirection::Ascending ? (idA < idB) : (idA > idB);
				}
				std::string na = NormalizeQuery(getName(a));
				std::string nb = NormalizeQuery(getName(b));
				if (na != nb) {
					return dir == TilesetSortDirection::Ascending ? (na < nb) : (na > nb);
				}
			} else {
				std::string na = NormalizeQuery(getName(a));
				std::string nb = NormalizeQuery(getName(b));
				if (na != nb) {
					return dir == TilesetSortDirection::Ascending ? (na < nb) : (na > nb);
				}
				uint32_t idA = getId(a);
				uint32_t idB = getId(b);
				if (idA != idB) {
					return dir == TilesetSortDirection::Ascending ? (idA < idB) : (idA > idB);
				}
			}
			return getId(a) < getId(b);
		});
	}

	template <typename ItemPtr, typename GetNameFn, typename GetIdsFn>
	std::vector<ItemPtr> FilterItems(const std::vector<ItemPtr>& items, const std::string& query, GetNameFn&& getName, GetIdsFn&& getIds) {
		std::string queryLower = NormalizeQuery(query);
		if (queryLower.empty()) {
			return items;
		}

		std::vector<ItemPtr> result;
		result.reserve(items.size());
		for (const auto& item : items) {
			if (!item) {
				continue;
			}
			std::string name = getName(item);
			std::vector<uint32_t> ids = getIds(item);
			if (MatchesSearch(name, ids, queryLower)) {
				result.push_back(item);
			}
		}
		return result;
	}

	template <typename ItemPtr>
	int RestoreSelection(const std::vector<ItemPtr>& items, const ItemPtr& selectedBefore) {
		if (!selectedBefore) {
			return -1;
		}
		for (size_t i = 0; i < items.size(); ++i) {
			if (items[i] == selectedBefore) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	template <typename ItemPtr>
	std::vector<ItemPtr> DeduplicateItems(const std::vector<ItemPtr>& items) {
		std::vector<ItemPtr> result;
		result.reserve(items.size());
		std::unordered_set<ItemPtr> seen;
		for (const auto& it : items) {
			if (it && seen.insert(it).second) {
				result.push_back(it);
			}
		}
		return result;
	}

	// Concrete functions for Brush* instances, resolved in palette_model.cpp
	uint32_t GetBrushSortID(const Brush* brush);
	std::vector<uint32_t> GetSearchableIDs(const Brush* brush);
	bool BrushMatchesQuery(const Brush* brush, const std::string& queryLower);
	void SortBrushes(std::vector<Brush*>& brushes, TilesetSortKey key, TilesetSortDirection dir);
	std::vector<Brush*> FilterBrushes(const std::vector<Brush*>& brushes, const std::string& query);
	int FindBrushIndex(const std::vector<Brush*>& brushes, const Brush* target);
	int RestoreSelectionIndex(const std::vector<Brush*>& brushes, const Brush* selectedBefore);
	std::vector<PaletteSearchResult> AggregateGlobalBrushes(const TilesetContainer& tilesets, int defaultCategory);

} // namespace PaletteModel

#endif // NEXAMAP_PALETTE_MODEL_H_
