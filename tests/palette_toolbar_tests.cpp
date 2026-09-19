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

#include "palette_model.h"

#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
	int failures = 0;

	void check(bool condition, const std::string& message) {
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	}

	struct TestItem {
		std::string name;
		uint32_t id;
		uint32_t clientId = 0;
		uint32_t lookType = 0;

		TestItem(std::string n, uint32_t i, uint32_t cid = 0, uint32_t lt = 0) :
			name(std::move(n)), id(i), clientId(cid), lookType(lt) { }

		const std::string& getName() const {
			return name;
		}
		uint32_t getID() const {
			return id;
		}
		std::vector<uint32_t> getSearchableIDs() const {
			std::vector<uint32_t> ids = { id };
			if (clientId != 0 && clientId != id) {
				ids.push_back(clientId);
			}
			if (lookType != 0 && lookType != id) {
				ids.push_back(lookType);
			}
			return ids;
		}
	};
} // namespace

int main() {
	std::cout << "Running palette model, search, sorting, and selection tests...\n";

	TestItem b1("Cobblestone", 101, 2101);
	TestItem b2("Grass", 102, 2102);
	TestItem b3("apple tree", 200, 2200);
	TestItem b4("Banana Tree", 201, 2201);
	TestItem b5("Water", 50, 2050);
	TestItem b6("ancient stone", 300, 2300);
	TestItem bCreature("Demon", 999, 0, 35); // LookType 35

	// -----------------------------------------------------------------------
	// 1. Sorting Tests (Using Production PaletteModel::SortItems)
	// -----------------------------------------------------------------------
	{
		std::vector<const TestItem*> list = { &b2, &b1, &b4, &b3, &b5 };

		// Sort by Name Ascending (case-insensitive)
		PaletteModel::SortItems(
			list,
			TilesetSortKey::Name,
			TilesetSortDirection::Ascending,
			[](const TestItem* i) { return i->getName(); },
			[](const TestItem* i) { return i->getID(); }
		);

		check(list.size() == 5, "list size should remain 5");
		check(list[0]->getName() == "apple tree", "1st should be 'apple tree'");
		check(list[1]->getName() == "Banana Tree", "2nd should be 'Banana Tree'");
		check(list[2]->getName() == "Cobblestone", "3rd should be 'Cobblestone'");
		check(list[3]->getName() == "Grass", "4th should be 'Grass'");
		check(list[4]->getName() == "Water", "5th should be 'Water'");

		// Sort by Name Descending (case-insensitive)
		PaletteModel::SortItems(
			list,
			TilesetSortKey::Name,
			TilesetSortDirection::Descending,
			[](const TestItem* i) { return i->getName(); },
			[](const TestItem* i) { return i->getID(); }
		);

		check(list[0]->getName() == "Water", "1st should be 'Water'");
		check(list[1]->getName() == "Grass", "2nd should be 'Grass'");
		check(list[2]->getName() == "Cobblestone", "3rd should be 'Cobblestone'");
		check(list[3]->getName() == "Banana Tree", "4th should be 'Banana Tree'");
		check(list[4]->getName() == "apple tree", "5th should be 'apple tree'");

		// Sort by ID Ascending
		PaletteModel::SortItems(
			list,
			TilesetSortKey::ID,
			TilesetSortDirection::Ascending,
			[](const TestItem* i) { return i->getName(); },
			[](const TestItem* i) { return i->getID(); }
		);

		check(list[0]->getID() == 50, "1st by ID asc should be 50");
		check(list[1]->getID() == 101, "2nd by ID asc should be 101");
		check(list[2]->getID() == 102, "3rd by ID asc should be 102");
		check(list[3]->getID() == 200, "4th by ID asc should be 200");
		check(list[4]->getID() == 201, "5th by ID asc should be 201");

		// Sort by ID Descending
		PaletteModel::SortItems(
			list,
			TilesetSortKey::ID,
			TilesetSortDirection::Descending,
			[](const TestItem* i) { return i->getName(); },
			[](const TestItem* i) { return i->getID(); }
		);

		check(list[0]->getID() == 201, "1st by ID desc should be 201");
		check(list[1]->getID() == 200, "2nd by ID desc should be 200");
		check(list[2]->getID() == 102, "3rd by ID desc should be 102");
		check(list[3]->getID() == 101, "4th by ID desc should be 101");
		check(list[4]->getID() == 50, "5th by ID desc should be 50");
	}

	// -----------------------------------------------------------------------
	// 2. Sorting Ties & Edge Cases
	// -----------------------------------------------------------------------
	{
		TestItem t1("Stone", 10);
		TestItem t2("Stone", 5);
		TestItem t3("stone", 20);
		std::vector<const TestItem*> ties = { &t1, &t2, &t3 };

		// Same name, should break tie deterministically by ID
		PaletteModel::SortItems(
			ties,
			TilesetSortKey::Name,
			TilesetSortDirection::Ascending,
			[](const TestItem* i) { return i->getName(); },
			[](const TestItem* i) { return i->getID(); }
		);

		check(ties[0]->getID() == 5, "tied names should break tie by ID: 5");
		check(ties[1]->getID() == 10, "tied names should break tie by ID: 10");
		check(ties[2]->getID() == 20, "tied names should break tie by ID: 20");

		// Empty list
		std::vector<const TestItem*> emptyList;
		PaletteModel::SortItems(
			emptyList,
			TilesetSortKey::Name,
			TilesetSortDirection::Ascending,
			[](const TestItem* i) { return i->getName(); },
			[](const TestItem* i) { return i->getID(); }
		);
		check(emptyList.empty(), "sorting empty list should not crash");

		// Single element
		std::vector<const TestItem*> single = { &b1 };
		PaletteModel::SortItems(
			single,
			TilesetSortKey::ID,
			TilesetSortDirection::Descending,
			[](const TestItem* i) { return i->getName(); },
			[](const TestItem* i) { return i->getID(); }
		);
		check(single.size() == 1 && single[0] == &b1, "sorting single element preserves it");
	}

	// -----------------------------------------------------------------------
	// 3. Filtering Tests (Using Production PaletteModel::FilterItems)
	// -----------------------------------------------------------------------
	{
		std::vector<const TestItem*> pool = { &b1, &b2, &b3, &b4, &b5, &b6, &bCreature };

		auto filterHelper = [&](const std::string& query) {
			return PaletteModel::FilterItems(
				pool,
				query,
				[](const TestItem* i) { return i->getName(); },
				[](const TestItem* i) { return i->getSearchableIDs(); }
			);
		};

		// Substring match case-insensitive
		auto resTree = filterHelper("tree");
		check(resTree.size() == 2, "filtering 'tree' should return 2 brushes");
		check(resTree[0]->getName() == "apple tree", "resTree[0] matches");
		check(resTree[1]->getName() == "Banana Tree", "resTree[1] matches");

		// Substring match uppercase query
		auto resStone = filterHelper("STONE");
		check(resStone.size() == 2, "filtering 'STONE' should match 'Cobblestone' and 'ancient stone'");

		// Server ID match
		auto resID = filterHelper("50");
		check(resID.size() == 1 && resID[0]->getName() == "Water", "filtering '50' matches Water (id=50)");

		// Client ID match
		auto resClient = filterHelper("2101");
		check(resClient.size() == 1 && resClient[0]->getName() == "Cobblestone", "filtering ClientID '2101' matches Cobblestone");

		// Creature lookType match
		auto resLookType = filterHelper("35");
		check(resLookType.size() == 1 && resLookType[0]->getName() == "Demon", "filtering lookType '35' matches Demon");

		// Empty query returns full pool
		auto resAll = filterHelper("");
		check(resAll.size() == pool.size(), "empty query returns full pool");

		// No match query
		auto resNone = filterHelper("dragon_nonexistent");
		check(resNone.empty(), "query with no matches returns empty vector");
	}

	// -----------------------------------------------------------------------
	// 4. Selection Preservation across Sort and Filter
	// -----------------------------------------------------------------------
	{
		std::vector<const TestItem*> pool = { &b1, &b2, &b3, &b4, &b5 };
		const TestItem* selected = &b4; // Banana Tree

		// Sort pool
		PaletteModel::SortItems(
			pool,
			TilesetSortKey::Name,
			TilesetSortDirection::Ascending,
			[](const TestItem* i) { return i->getName(); },
			[](const TestItem* i) { return i->getID(); }
		);

		int newIdx = PaletteModel::RestoreSelection(pool, selected);
		check(newIdx != -1, "selected item must be found after sort");
		check(pool[newIdx] == selected, "restored index points to the exact same pointer identity");

		// Filter that includes selected
		auto filtered1 = PaletteModel::FilterItems(
			pool,
			"tree",
			[](const TestItem* i) { return i->getName(); },
			[](const TestItem* i) { return i->getSearchableIDs(); }
		);
		int idxInFilter = PaletteModel::RestoreSelection(filtered1, selected);
		check(idxInFilter != -1, "selected item found when included in filtered results");
		check(filtered1[idxInFilter] == selected, "identity preserved in filtered subset");

		// Filter that excludes selected
		auto filtered2 = PaletteModel::FilterItems(
			pool,
			"water",
			[](const TestItem* i) { return i->getName(); },
			[](const TestItem* i) { return i->getSearchableIDs(); }
		);
		int idxExcluded = PaletteModel::RestoreSelection(filtered2, selected);
		check(idxExcluded == -1, "selection safely resolves to -1 when query excludes selected brush");
	}

	// -----------------------------------------------------------------------
	// 5. Global Palette Aggregation & Deduplication
	// -----------------------------------------------------------------------
	{
		std::vector<const TestItem*> tilesetA = { &b1, &b2, &b3 };
		std::vector<const TestItem*> tilesetB = { &b3, &b4, &b5 }; // b3 is shared
		std::vector<const TestItem*> tilesetC = { &b1, &b5, &b6 }; // b1 and b5 are shared

		std::vector<const TestItem*> all;
		all.insert(all.end(), tilesetA.begin(), tilesetA.end());
		all.insert(all.end(), tilesetB.begin(), tilesetB.end());
		all.insert(all.end(), tilesetC.begin(), tilesetC.end());

		auto deduped = PaletteModel::DeduplicateItems(all);
		check(deduped.size() == 6, "aggregated list must deduplicate down to 6 unique brushes");

		std::unordered_set<uint32_t> uniqueIds;
		for (const auto* b : deduped) {
			uniqueIds.insert(b->getID());
		}
		check(uniqueIds.size() == 6, "all 6 distinct brush IDs present");
	}

	// -----------------------------------------------------------------------
	// 6. Global Search Metadata & Provenance
	// -----------------------------------------------------------------------
	{
		struct MockProvenanceResult {
			const TestItem* item;
			int category;
			std::string tilesetName;
		};

		std::vector<MockProvenanceResult> globalCatalog = {
			{ &b1, 1 /* TILESET_TERRAIN */, "Nature" },
			{ &b2, 1 /* TILESET_TERRAIN */, "Nature" },
			{ &b3, 2 /* TILESET_DOODAD */, "Trees" },
			{ &b4, 2 /* TILESET_DOODAD */, "Trees" },
			{ &bCreature, 4 /* TILESET_CREATURE */, "Monsters" },
		};

		// Filter preserving provenance
		std::vector<MockProvenanceResult> results;
		std::string query = "tree";
		std::string queryLower = PaletteModel::NormalizeQuery(query);
		for (const auto& entry : globalCatalog) {
			if (PaletteModel::MatchesSearch(entry.item->getName(), entry.item->getSearchableIDs(), queryLower)) {
				results.push_back(entry);
			}
		}

		check(results.size() == 2, "global search matched 2 entries for 'tree'");
		check(results[0].category == 2 && results[0].tilesetName == "Trees", "provenance category and tileset preserved for result 0");
		check(results[1].category == 2 && results[1].tilesetName == "Trees", "provenance category and tileset preserved for result 1");
	}

	// -----------------------------------------------------------------------
	// 7. Tile Size Validation & 16px Preservation
	// -----------------------------------------------------------------------
	{
		check(PaletteModel::SanitizeTileSize(16) == 16, "16px is valid");
		check(PaletteModel::SanitizeTileSize(32) == 32, "32px is valid");
		check(PaletteModel::SanitizeTileSize(64) == 64, "64px is valid");
		check(PaletteModel::SanitizeTileSize(128) == 128, "128px is valid");
		check(PaletteModel::SanitizeTileSize(0) == 32, "0px falls back to 32");
		check(PaletteModel::SanitizeTileSize(48) == 32, "48px falls back to 32");
		check(PaletteModel::SanitizeTileSize(256) == 32, "256px falls back to 32");
		check(PaletteModel::SanitizeTileSize(-10) == 32, "-10px falls back to 32");

		// Derive initial tile size without override
		check(PaletteModel::DeriveInitialTileSize(0, true) == 16, "small icons without override should derive 16px");
		check(PaletteModel::DeriveInitialTileSize(0, false) == 32, "large icons without override should derive 32px");

		// Derive initial tile size with explicit user override
		check(PaletteModel::DeriveInitialTileSize(64, true) == 64, "explicit 64px override overrides small icons");
		check(PaletteModel::DeriveInitialTileSize(16, false) == 16, "explicit 16px override respected for large icons");
		check(PaletteModel::DeriveInitialTileSize(128, false) == 128, "explicit 128px override respected");
		check(PaletteModel::DeriveInitialTileSize(999, true) == 16, "invalid override falls back to 16px for small icons");
		check(PaletteModel::DeriveInitialTileSize(999, false) == 32, "invalid override falls back to 32px for large icons");
	}

	// -----------------------------------------------------------------------
	// 8. UTF-8 Search Query Normalization
	// -----------------------------------------------------------------------
	{
		std::string ascii = "Stone WALL 123";
		check(PaletteModel::NormalizeQuery(ascii) == "stone wall 123", "ASCII characters lowercased correctly");

		// Multibyte characters should not be corrupted
		std::string utf8Str = "Ação";
		std::string normalized = PaletteModel::NormalizeQuery(utf8Str);
		check(normalized[0] == 'a', "First letter lowercased to 'a'");
		check(normalized.find("ção") != std::string::npos, "UTF-8 bytes 'ção' preserved without corruption");
	}

	// -----------------------------------------------------------------------
	// 9. Session Scoping Validation
	// -----------------------------------------------------------------------
	{
		std::map<std::string, std::vector<const TestItem*>> session1Tilesets;
		std::map<std::string, std::vector<const TestItem*>> session2Tilesets;

		TestItem s1_b("Tab1Brush", 1001);
		TestItem s2_b("Tab2Brush", 2002);

		session1Tilesets["Nature"].push_back(&s1_b);
		session2Tilesets["Nature"].push_back(&s2_b);

		auto resSession1 = PaletteModel::FilterItems(
			session1Tilesets["Nature"],
			"Brush",
			[](const TestItem* i) { return i->getName(); },
			[](const TestItem* i) { return i->getSearchableIDs(); }
		);
		check(resSession1.size() == 1 && resSession1[0]->getID() == 1001, "Session 1 contains only Tab1Brush");

		auto resSession2 = PaletteModel::FilterItems(
			session2Tilesets["Nature"],
			"Brush",
			[](const TestItem* i) { return i->getName(); },
			[](const TestItem* i) { return i->getSearchableIDs(); }
		);
		check(resSession2.size() == 1 && resSession2[0]->getID() == 2002, "Session 2 contains only Tab2Brush");

		auto crossCheck = PaletteModel::FilterItems(
			session1Tilesets["Nature"],
			"Tab2Brush",
			[](const TestItem* i) { return i->getName(); },
			[](const TestItem* i) { return i->getSearchableIDs(); }
		);
		check(crossCheck.empty(), "Session 1 does not leak into Session 2");
	}

	// -----------------------------------------------------------------------
	// 10. List Item Layout Geometry (No Row-Stretching)
	// -----------------------------------------------------------------------
	{
		// Test standard row width 300
		auto l300 = PaletteModel::CalculateListItemLayout(0, 0, 300, 36, 32, 14);
		check(l300.iconWidth == 32, "row width 300: icon width must remain 32");
		check(l300.iconHeight == 32, "row width 300: icon height must remain 32");
		check(l300.iconX == 2, "iconX has left padding");
		check(l300.iconY == 2, "iconY is vertically centered: (36 - 32)/2 = 2");
		check(l300.textX == 40, "text starts after icon without overlapping: 2 + 32 + 6 = 40");
		check(l300.textY == 11, "text is vertically centered: (36 - 14)/2 = 11");

		// Test wide row width 800
		auto l800 = PaletteModel::CalculateListItemLayout(10, 50, 800, 36, 32, 14);
		check(l800.iconWidth == 32, "row width 800: icon width must still remain 32, never 800");
		check(l800.iconX == 12, "iconX accounts for offset");
		check(l800.textX == 50, "textX accounts for offset and stays after icon");
	}

	if (failures == 0) {
		std::cout << "All palette toolbar tests passed successfully!\n";
		return 0;
	} else {
		std::cerr << failures << " palette toolbar test(s) failed!\n";
		return 1;
	}
}
