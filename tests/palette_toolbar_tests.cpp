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

#include <algorithm>
#include <cctype>
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

	enum class SortKey {
		Name = 0,
		ID = 1,
	};

	enum class SortDirection {
		Ascending = 0,
		Descending = 1,
	};

	struct MockBrush {
		std::string name;
		int id;

		MockBrush(std::string n, int i) : name(std::move(n)), id(i) { }
		const std::string& getName() const {
			return name;
		}
		int getID() const {
			return id;
		}
	};

	void sortBrushes(std::vector<const MockBrush*>& brushes, SortKey key, SortDirection dir) {
		std::stable_sort(brushes.begin(), brushes.end(), [key, dir](const MockBrush* a, const MockBrush* b) {
			if (!a || !b) {
				return a != nullptr;
			}
			if (key == SortKey::ID) {
				if (a->getID() != b->getID()) {
					return dir == SortDirection::Ascending ? (a->getID() < b->getID()) : (a->getID() > b->getID());
				}
			} else {
				std::string na = a->getName();
				std::string nb = b->getName();
				for (char& c : na) {
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}
				for (char& c : nb) {
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}
				if (na != nb) {
					return dir == SortDirection::Ascending ? (na < nb) : (na > nb);
				}
			}
			return a->getID() < b->getID();
		});
	}

	std::vector<const MockBrush*> filterBrushes(const std::vector<const MockBrush*>& src, const std::string& query) {
		std::vector<const MockBrush*> result;
		std::string queryLower = query;
		for (char& c : queryLower) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}

		for (const MockBrush* b : src) {
			if (!b) {
				continue;
			}
			if (!queryLower.empty()) {
				std::string nameLower = b->getName();
				for (char& c : nameLower) {
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}
				std::string idStr = std::to_string(b->getID());
				if (nameLower.find(queryLower) == std::string::npos && idStr.find(queryLower) == std::string::npos) {
					continue;
				}
			}
			result.push_back(b);
		}
		return result;
	}

	int sanitizeTileSize(int size) {
		if (size != 16 && size != 32 && size != 64 && size != 128) {
			return 32;
		}
		return size;
	}
}

int main() {
	std::cout << "Running palette toolbar, search, and sorting tests...\n";

	MockBrush b1("Cobblestone", 101);
	MockBrush b2("Grass", 102);
	MockBrush b3("apple tree", 200);
	MockBrush b4("Banana Tree", 201);
	MockBrush b5("Water", 50);
	MockBrush b6("ancient stone", 300);

	// -----------------------------------------------------------------------
	// 1. Sorting Tests
	// -----------------------------------------------------------------------
	{
		std::vector<const MockBrush*> list = { &b2, &b1, &b4, &b3, &b5 };

		// Sort by Name Ascending (case-insensitive)
		sortBrushes(list, SortKey::Name, SortDirection::Ascending);
		check(list.size() == 5, "list size should remain 5");
		check(list[0]->getName() == "apple tree", "1st should be 'apple tree'");
		check(list[1]->getName() == "Banana Tree", "2nd should be 'Banana Tree'");
		check(list[2]->getName() == "Cobblestone", "3rd should be 'Cobblestone'");
		check(list[3]->getName() == "Grass", "4th should be 'Grass'");
		check(list[4]->getName() == "Water", "5th should be 'Water'");

		// Sort by Name Descending (case-insensitive)
		sortBrushes(list, SortKey::Name, SortDirection::Descending);
		check(list[0]->getName() == "Water", "1st should be 'Water'");
		check(list[1]->getName() == "Grass", "2nd should be 'Grass'");
		check(list[2]->getName() == "Cobblestone", "3rd should be 'Cobblestone'");
		check(list[3]->getName() == "Banana Tree", "4th should be 'Banana Tree'");
		check(list[4]->getName() == "apple tree", "5th should be 'apple tree'");

		// Sort by ID Ascending
		sortBrushes(list, SortKey::ID, SortDirection::Ascending);
		check(list[0]->getID() == 50, "1st by ID asc should be 50");
		check(list[1]->getID() == 101, "2nd by ID asc should be 101");
		check(list[2]->getID() == 102, "3rd by ID asc should be 102");
		check(list[3]->getID() == 200, "4th by ID asc should be 200");
		check(list[4]->getID() == 201, "5th by ID asc should be 201");

		// Sort by ID Descending
		sortBrushes(list, SortKey::ID, SortDirection::Descending);
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
		MockBrush t1("Stone", 10);
		MockBrush t2("Stone", 5);
		MockBrush t3("stone", 20);
		std::vector<const MockBrush*> ties = { &t1, &t2, &t3 };

		// Same name, should break tie deterministically by ID
		sortBrushes(ties, SortKey::Name, SortDirection::Ascending);
		check(ties[0]->getID() == 5, "tied names should break tie by ID: 5");
		check(ties[1]->getID() == 10, "tied names should break tie by ID: 10");
		check(ties[2]->getID() == 20, "tied names should break tie by ID: 20");

		// Empty list
		std::vector<const MockBrush*> emptyList;
		sortBrushes(emptyList, SortKey::Name, SortDirection::Ascending);
		check(emptyList.empty(), "sorting empty list should not crash");

		// Single element
		std::vector<const MockBrush*> single = { &b1 };
		sortBrushes(single, SortKey::ID, SortDirection::Descending);
		check(single.size() == 1 && single[0] == &b1, "sorting single element preserves it");
	}

	// -----------------------------------------------------------------------
	// 3. Filtering Tests
	// -----------------------------------------------------------------------
	{
		std::vector<const MockBrush*> pool = { &b1, &b2, &b3, &b4, &b5, &b6 };

		// Substring match case-insensitive
		auto resTree = filterBrushes(pool, "tree");
		check(resTree.size() == 2, "filtering 'tree' should return 2 brushes");
		check(resTree[0]->getName() == "apple tree", "resTree[0] matches");
		check(resTree[1]->getName() == "Banana Tree", "resTree[1] matches");

		// Substring match uppercase query
		auto resStone = filterBrushes(pool, "STONE");
		check(resStone.size() == 2, "filtering 'STONE' should match 'Cobblestone' and 'ancient stone'");

		// Numeric ID match
		auto resID = filterBrushes(pool, "50");
		check(resID.size() == 1 && resID[0]->getName() == "Water", "filtering '50' matches Water (id=50)");

		auto resPartialID = filterBrushes(pool, "10");
		check(resPartialID.size() == 2, "filtering '10' matches 101 and 102");

		// Empty query returns everything
		auto resAll = filterBrushes(pool, "");
		check(resAll.size() == pool.size(), "empty query returns full pool");

		// No match query
		auto resNone = filterBrushes(pool, "dragon");
		check(resNone.empty(), "query with no matches returns empty vector");
	}

	// -----------------------------------------------------------------------
	// 4. Global Palette Aggregation & Deduplication
	// -----------------------------------------------------------------------
	{
		// Mock tilesets
		std::vector<const MockBrush*> tilesetA = { &b1, &b2, &b3 };
		std::vector<const MockBrush*> tilesetB = { &b3, &b4, &b5 }; // b3 is shared
		std::vector<const MockBrush*> tilesetC = { &b1, &b5, &b6 }; // b1 and b5 are shared

		std::vector<const MockBrush*> aggregated;
		std::unordered_set<const MockBrush*> seen;

		for (const auto* b : tilesetA) {
			if (b && seen.insert(b).second) {
				aggregated.push_back(b);
			}
		}
		for (const auto* b : tilesetB) {
			if (b && seen.insert(b).second) {
				aggregated.push_back(b);
			}
		}
		for (const auto* b : tilesetC) {
			if (b && seen.insert(b).second) {
				aggregated.push_back(b);
			}
		}

		check(aggregated.size() == 6, "aggregated list must deduplicate down to 6 unique brushes");
		// Verify all 6 exist
		std::unordered_set<int> uniqueIds;
		for (const auto* b : aggregated) {
			uniqueIds.insert(b->getID());
		}
		check(uniqueIds.size() == 6, "all 6 distinct brush IDs present");
	}

	// -----------------------------------------------------------------------
	// 5. Tile Size Validation
	// -----------------------------------------------------------------------
	{
		check(sanitizeTileSize(16) == 16, "16px is valid");
		check(sanitizeTileSize(32) == 32, "32px is valid");
		check(sanitizeTileSize(64) == 64, "64px is valid");
		check(sanitizeTileSize(128) == 128, "128px is valid");
		check(sanitizeTileSize(0) == 32, "0px falls back to 32");
		check(sanitizeTileSize(48) == 32, "48px falls back to 32");
		check(sanitizeTileSize(256) == 32, "256px falls back to 32");
		check(sanitizeTileSize(-10) == 32, "-10px falls back to 32");
	}

	// -----------------------------------------------------------------------
	// 6. Session Scoping Validation
	// -----------------------------------------------------------------------
	{
		// Simulate two separate sessions
		std::map<std::string, std::vector<const MockBrush*>> session1Tilesets;
		std::map<std::string, std::vector<const MockBrush*>> session2Tilesets;

		MockBrush s1_b("Tab1Brush", 1001);
		MockBrush s2_b("Tab2Brush", 2002);

		session1Tilesets["Nature"].push_back(&s1_b);
		session2Tilesets["Nature"].push_back(&s2_b);

		// Query session 1
		auto resSession1 = filterBrushes(session1Tilesets["Nature"], "Brush");
		check(resSession1.size() == 1 && resSession1[0]->getID() == 1001, "Session 1 contains only Tab1Brush");

		// Query session 2
		auto resSession2 = filterBrushes(session2Tilesets["Nature"], "Brush");
		check(resSession2.size() == 1 && resSession2[0]->getID() == 2002, "Session 2 contains only Tab2Brush");

		// Verify no bleed
		auto crossCheck = filterBrushes(session1Tilesets["Nature"], "Tab2Brush");
		check(crossCheck.empty(), "Session 1 does not leak into Session 2");
	}

	if (failures == 0) {
		std::cout << "All palette toolbar tests passed successfully!\n";
		return 0;
	} else {
		std::cerr << failures << " palette toolbar test(s) failed!\n";
		return 1;
	}
}
