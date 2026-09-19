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

#include "main.h"

#include "palette_model.h"

#include "brush.h"
#include "creature_brush.h"
#include "creatures.h"
#include "items.h"
#include "raw_brush.h"
#include "tileset.h"

namespace PaletteModel {

	uint32_t GetBrushSortID(const Brush* brush) {
		if (!brush) {
			return 0;
		}
		if (const auto* raw = dynamic_cast<const RAWBrush*>(brush)) {
			return raw->getItemID();
		}
		if (const auto* cb = dynamic_cast<const CreatureBrush*>(brush)) {
			if (cb->getType()) {
				if (cb->getType()->outfit.lookType != 0) {
					return static_cast<uint32_t>(cb->getType()->outfit.lookType);
				}
				if (cb->getType()->outfit.lookItem != 0) {
					return static_cast<uint32_t>(cb->getType()->outfit.lookItem);
				}
			}
		}
		if (brush->getLookID() != 0) {
			return static_cast<uint32_t>(brush->getLookID());
		}
		return brush->getID();
	}

	std::vector<uint32_t> GetSearchableIDs(const Brush* brush) {
		std::vector<uint32_t> ids;
		if (!brush) {
			return ids;
		}

		if (const auto* raw = dynamic_cast<const RAWBrush*>(brush)) {
			ids.push_back(raw->getItemID());
			if (raw->getItemType()) {
				if (raw->getItemType()->clientID != 0 && raw->getItemType()->clientID != raw->getItemID()) {
					ids.push_back(raw->getItemType()->clientID);
				}
			}
		} else if (const auto* cb = dynamic_cast<const CreatureBrush*>(brush)) {
			if (cb->getType()) {
				if (cb->getType()->outfit.lookType != 0) {
					ids.push_back(static_cast<uint32_t>(cb->getType()->outfit.lookType));
				}
				if (cb->getType()->outfit.lookItem != 0) {
					ids.push_back(static_cast<uint32_t>(cb->getType()->outfit.lookItem));
				}
			}
			ids.push_back(brush->getID());
		} else {
			ids.push_back(brush->getID());
			if (brush->getLookID() != 0 && static_cast<uint32_t>(brush->getLookID()) != brush->getID()) {
				ids.push_back(static_cast<uint32_t>(brush->getLookID()));
			}
		}

		return ids;
	}

	bool BrushMatchesQuery(const Brush* brush, const std::string& queryLower) {
		if (!brush) {
			return false;
		}
		if (queryLower.empty()) {
			return true;
		}
		std::string name = brush->getName();
		std::vector<uint32_t> ids = GetSearchableIDs(brush);
		return MatchesSearch(name, ids, queryLower);
	}

	void SortBrushes(std::vector<Brush*>& brushes, TilesetSortKey key, TilesetSortDirection dir) {
		SortItems(
			brushes,
			key,
			dir,
			[](const Brush* b) { return b ? b->getName() : std::string(); },
			[](const Brush* b) { return GetBrushSortID(b); }
		);
	}

	std::vector<Brush*> FilterBrushes(const std::vector<Brush*>& brushes, const std::string& query) {
		return FilterItems(
			brushes,
			query,
			[](const Brush* b) { return b ? b->getName() : std::string(); },
			[](const Brush* b) { return GetSearchableIDs(b); }
		);
	}

	int FindBrushIndex(const std::vector<Brush*>& brushes, const Brush* target) {
		return RestoreSelection(brushes, const_cast<Brush*>(target));
	}

	int RestoreSelectionIndex(const std::vector<Brush*>& brushes, const Brush* selectedBefore) {
		return RestoreSelection(brushes, const_cast<Brush*>(selectedBefore));
	}

	std::vector<PaletteSearchResult> AggregateGlobalBrushes(const TilesetContainer& tilesets, int defaultCategory) {
		std::vector<PaletteSearchResult> results;
		std::unordered_set<const Brush*> seen;

		for (const auto& [name, ts] : tilesets) {
			if (!ts) {
				continue;
			}
			for (int cat = TILESET_UNKNOWN; cat <= TILESET_HOUSE; ++cat) {
				const auto* tcg = ts->getCategory(static_cast<TilesetCategoryType>(cat));
				if (!tcg) {
					continue;
				}
				for (Brush* b : tcg->brushlist) {
					if (b && seen.insert(b).second) {
						results.push_back(PaletteSearchResult {
							.brush = b,
							.category = cat,
							.tilesetName = ts->name,
						});
					}
				}
			}
		}

		return results;
	}

} // namespace PaletteModel
