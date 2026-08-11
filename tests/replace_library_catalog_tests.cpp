#include "replace_tool/replace_library_catalog.h"

#include <iostream>
#include <string_view>

namespace {
	int failures = 0;

	void check(bool condition, std::string_view name) {
		if (!condition) {
			std::cerr << "FAILED: " << name << '\n';
			++failures;
		}
	}
}

int main() {
	const ReplaceLibraryItem stoneFloor = { 100, ServerItemId(100), 500, "Stone Floor" };
	const ReplaceLibraryItem grass = { 101, ServerItemId(101), 501, "Grass" };
	const ReplaceLibraryItem stoneBorder = { 102, ServerItemId(102), 502, "Stone Border" };

	ReplaceLibraryCatalog catalog;
	catalog.SetItems({ grass, stoneBorder, stoneFloor, stoneFloor });
	check(catalog.GetItems().size() == 3 && catalog.GetItems().front().serverId == ServerItemId(100), "items must be sorted and deduplicated by ServerID");
	check(catalog.FilterItems("stone").size() == 2, "item search must be case-insensitive by name");
	check(catalog.FilterItems("101") == std::vector<ReplaceLibraryItem>({ grass }), "item search must match ServerID");
	check(catalog.FilterItems("502") == std::vector<ReplaceLibraryItem>({ stoneBorder }), "item search must match ClientID");

	ReplaceLibraryBrush stoneBrush;
	stoneBrush.key = 7;
	stoneBrush.name = "Stone Ground";
	stoneBrush.look = stoneFloor;
	stoneBrush.relatedItems = { stoneFloor, stoneBorder };
	catalog.SetBrushes({ stoneBrush });
	const std::vector<ReplaceLibraryItem> brushes = catalog.FilterBrushes("ground");
	check(brushes.size() == 1 && brushes.front().key == stoneBrush.key && brushes.front().serverId == stoneFloor.serverId, "brush search must preserve brush identity and look ServerID");
	const ReplaceLibraryBrush* found = catalog.FindBrush(7);
	check(found && found->relatedItems.size() == 2, "brush lookup must expose related items");
	check(catalog.FindBrush(999) == nullptr, "unknown brush key must not resolve");

	if (failures != 0) {
		std::cerr << failures << " replacement library catalog test(s) failed.\n";
		return 1;
	}

	std::cout << "All replacement library catalog tests passed.\n";
	return 0;
}
