#include "main.h"
#include "complexitem.h"
#include "find_item_search.h"
#include "graphics.h"
#include "gui.h"
#include "iomap_otbm.h"
#include "item.h"
#include "items.h"
#include "map.h"
#include "raw_brush.h"
#include "sprite_appearances.h"
#include "tile.h"

#include <appearances.pb.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
	int failures = 0;

	void check(bool condition, const char* message) {
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	}

	ItemType* addSyntheticItem(
		uint32_t serverId,
		uint32_t clientId,
		const std::string& name,
		ItemGroup_t group = ITEM_GROUP_NONE,
		ItemTypes_t type = ITEM_TYPE_NONE
	) {
		auto* itemType = newd ItemType();
		itemType->id = serverId;
		itemType->clientID = clientId;
		itemType->name = name;
		itemType->group = group;
		itemType->type = type;
		if (g_items.items[serverId]) {
			delete g_items.items[serverId];
		}
		g_items.items.set(serverId, itemType);
		return itemType;
	}

	void testParseUint32ItemId() {
		const uint32_t validCases[] = { 0u, 100u, 65535u, 65536u, 70000u, 100000u, 1000000u, 4294967295u };
		for (uint32_t expected : validCases) {
			const ParsedItemId parsed = ParseUint32ItemId(std::to_string(expected));
			check(parsed.valid && !parsed.overflow && parsed.value == expected, "valid uint32_t ID should parse without truncation");
		}

		const ParsedItemId padded = ParseUint32ItemId("  70000  ");
		check(padded.valid && padded.value == 70000u, "whitespace-padded ID should parse");

		const ParsedItemId overflow1 = ParseUint32ItemId("4294967296");
		check(!overflow1.valid && overflow1.overflow, "4294967296 must be rejected as overflow");

		const ParsedItemId overflow2 = ParseUint32ItemId("999999999999");
		check(!overflow2.valid && overflow2.overflow, "huge value must be rejected as overflow");

		const ParsedItemId negative = ParseUint32ItemId("-1");
		check(!negative.valid && !negative.overflow, "negative value must be rejected");

		const ParsedItemId alpha = ParseUint32ItemId("70000x");
		check(!alpha.valid, "non-numeric value must be rejected");

		const ParsedItemId empty = ParseUint32ItemId("   ");
		check(!empty.valid && empty.empty, "empty value must be rejected");
	}

	void testAppearancesAndXml32BitLoading() {
		using namespace rme::protobuf::appearances;

		g_items.clear();
		g_gui.gfx.clear();
		g_spriteAppearances.unload();

		ClientAssetsManifest manifest;
		const auto addSheet = [&](uint32_t firstId, uint32_t lastId) {
			ClientSpriteSheetManifest sheet;
			sheet.file = "synthetic_sheet.bmp.lzma";
			sheet.spriteType = 0;
			sheet.firstSpriteId = firstId;
			sheet.lastSpriteId = lastId;
			manifest.spriteSheets.push_back(std::move(sheet));
		};
		addSheet(1000u, 70000u);
		addSheet(700000u, 900100u);
		addSheet(4000000000u, 4000000100u);

		wxString catalogError;
		wxArrayString catalogWarnings;
		check(g_spriteAppearances.loadCatalog(manifest, catalogError, catalogWarnings), "g_spriteAppearances.loadCatalog should accept 32-bit sprite ID ranges");

		Appearances appearances;
		const std::vector<std::tuple<uint32_t, std::string, uint32_t, bool, bool>> specs = {
			{ 100u, "Classic Ground", 1001u, true, false },
			{ 65535u, "Max 16-Bit Item", 65535u, false, false },
			{ 65536u, "Halloween Ground 65536", 65536u, true, false },
			{ 70000u, "Halloween Pumpkin", 700001u, false, false },
			{ 100000u, "Halloween Bag", 800001u, false, true },
			{ 1000000u, "Halloween Statue", 900001u, false, false },
			{ 4294967295u, "Max 32-Bit Relic", 4000000000u, false, false },
		};

		for (const auto& [id, name, spriteId, isGround, isContainer] : specs) {
			Appearance* obj = appearances.add_object();
			obj->set_id(id);
			obj->set_name(name);
			AppearanceFlags* flags = obj->mutable_flags();
			if (isGround) {
				flags->mutable_bank()->set_waypoints(100);
			}
			if (isContainer) {
				flags->set_container(true);
			}
			FrameGroup* fg = obj->add_frame_group();
			SpriteInfo* si = fg->mutable_sprite_info();
			si->set_pattern_width(1);
			si->set_pattern_height(1);
			si->set_pattern_depth(1);
			si->set_layers(1);
			si->add_sprite_id(spriteId);
		}

		wxString error;
		wxArrayString warnings;
		const bool loaded = g_items.loadFromAppearances(appearances, error, warnings);
		check(loaded, "loadFromAppearances should load 32-bit item IDs without rejecting > 65535");
		check(g_items.getMaxID() == 4294967295u, "max_item_id should reach UINT32_MAX");
		check(g_items.items.size() <= SparseItemMap::DENSE_LIMIT, "dense storage must never expand beyond 65536 entries");
		check(g_gui.gfx.getItemSpriteMaxID() == 4294967295u, "GraphicManager item_count should track 32-bit appearance IDs");

		size_t iteratedCount = 0;
		const auto t0 = std::chrono::steady_clock::now();
		g_items.forEachItemType([&](const ItemType&) {
			++iteratedCount;
		});
		const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
		check(iteratedCount == specs.size(), "forEachItemType should iterate only loaded items");
		check(elapsedMs < 100, "forEachItemType must be instant even when max_item_id == 4294967295");

		for (const auto& [id, name, spriteId, isGround, isContainer] : specs) {
			(void)isGround;
			(void)isContainer;
			check(g_items.typeExists(id), "loaded 32-bit appearance ID must exist in g_items");
			const ItemType& it = g_items.getItemType(id);
			check(it.id == id && it.clientID == id && it.name == name, "loaded ItemType metadata must match");
			check(it.sprite != nullptr, "loaded 32-bit item must have its GameSprite bound");
			check(ItemHasSpriteId(it, spriteId), "loaded 32-bit item must expose its 32-bit Sprite ID");
		}

		// Verify RAWBrush formats 32-bit IDs without signed overflow (4294967295 != -1)
		RAWBrush maxBrush(4294967295u);
		check(maxBrush.getItemID() == 4294967295u, "RAWBrush::getItemID must preserve uint32_t");
		check(maxBrush.getName().find("4294967295") == 0, "RAWBrush::getName must format UINT32_MAX unsigned");

		// Verify items.xml loader also handles 32-bit IDs without truncating to uint16_t
		const std::filesystem::path xmlPath = std::filesystem::temp_directory_path() / "nexamap_find_item_32bit_items.xml";
		{
			std::ofstream out(xmlPath);
			out << "<items>\n"
				<< "  <item id=\"70000\" name=\"Halloween Pumpkin XML\" />\n"
				<< "  <item id=\"4294967295\" name=\"Max 32-Bit Relic XML\" />\n"
				<< "</items>\n";
		}
		wxString xmlError;
		wxArrayString xmlWarnings;
		check(g_items.loadFromGameXml(FileName(wxString::FromUTF8(xmlPath.string())), xmlError, xmlWarnings, false), "loadFromGameXml should accept 32-bit IDs");
		check(g_items.getItemType(70000u).name == "Halloween Pumpkin XML", "items.xml should update 70000");
		check(g_items.getItemType(4294967295u).name == "Max 32-Bit Relic XML", "items.xml should update 4294967295");
		std::filesystem::remove(xmlPath);
	}

	void testMapSearchServerClientSpriteAndContainers() {
		// Create a distinct Server ID / Client ID / Sprite ID item to verify separation
		ItemType* pumpkin = g_items.items[70000u];
		check(pumpkin != nullptr, "item 70000 must exist from previous test");
		if (pumpkin) {
			pumpkin->clientID = 80000u; // Distinct from Server ID 70000 and Sprite ID 700001
		}

		// Also add item 4464 (== 70000 & 0xFFFF) to ensure zero 16-bit wrap-around false positives!
		addSyntheticItem(4464u, 4464u, "Wraparound Decoy 4464");

		Map map;
		map.setWidth(256);
		map.setHeight(256);

		// Tile 1: (100, 100, 7) - Ground 65536, Tile item 70000 (x2), Decoy 4464
		Tile* t1 = map.getOrCreateTile(Position(100, 100, 7));
		t1->ground = Item::Create(65536u);
		t1->addItem(Item::Create(70000u));
		t1->addItem(Item::Create(70000u));
		t1->addItem(Item::Create(4464u));
		t1->select();

		// Tile 2: (105, 110, 7) - Ground 100, Container 100000 holding 70000 and nested Container 100000 holding 1000000 & 4294967295
		Tile* t2 = map.getOrCreateTile(Position(105, 110, 7));
		t2->ground = Item::Create(100u);
		auto* outerBag = dynamic_cast<Container*>(Item::Create(100000u));
		check(outerBag != nullptr, "item 100000 should instantiate as Container");
		if (outerBag) {
			outerBag->getVector().push_back(Item::Create(70000u));
			auto* innerBag = dynamic_cast<Container*>(Item::Create(100000u));
			check(innerBag != nullptr, "nested item 100000 should instantiate as Container");
			if (innerBag) {
				innerBag->getVector().push_back(Item::Create(1000000u));
				innerBag->getVector().push_back(Item::Create(4294967295u));
				outerBag->getVector().push_back(innerBag);
			}
			t2->addItem(outerBag);
		}

		// Tile 3: (200, 200, 6) - Ground 100, Tile item 65535
		Tile* t3 = map.getOrCreateTile(Position(200, 200, 6));
		t3->ground = Item::Create(100u);
		t3->addItem(Item::Create(65535u));

		// 1. Search by Server ID 70000 -> 3 occurrences across 2 tiles (2 on t1, 1 in container on t2)
		const MapItemSearchSummary s70000 = SearchItemsOnMap(map, g_items, ItemIdSearchKind::ServerId, 70000u, false, 1000u);
		check(s70000.totalOccurrences == 3, "Server ID 70000 must find 3 occurrences (2 tile + 1 container)");
		check(s70000.matchedTiles == 2, "Server ID 70000 must match 2 tiles");
		check(s70000.matches.size() == 3, "Server ID 70000 must list all 3 occurrences");

		bool sawT1Tile = false;
		bool sawT2Container = false;
		for (const auto& m : s70000.matches) {
			check(m.serverId == 70000u && m.clientId == 80000u, "match must preserve distinct Server ID and Client ID");
			if (m.position == Position(100, 100, 7) && m.location == ItemLocationKind::TileItem) {
				sawT1Tile = true;
			}
			if (m.position == Position(105, 110, 7) && m.location == ItemLocationKind::ContainerItem && m.containerDepth == 1) {
				sawT2Container = true;
			}
		}
		check(sawT1Tile && sawT2Container, "Server ID 70000 must report exact coordinates and container depth");

		// 2. Search by Server ID 4464 -> ONLY 1 occurrence (must NOT match 70000 via 16-bit truncation!)
		const MapItemSearchSummary s4464 = SearchItemsOnMap(map, g_items, ItemIdSearchKind::ServerId, 4464u, false, 1000u);
		check(s4464.totalOccurrences == 1 && s4464.matches.front().serverId == 4464u, "4464 must not match 70000");

		// 3. Search by Client ID 80000 -> finds all 3 occurrences of 70000; Client ID 70000 -> 0 occurrences!
		const MapItemSearchSummary c80000 = SearchItemsOnMap(map, g_items, ItemIdSearchKind::ClientId, 80000u, false, 1000u);
		check(c80000.totalOccurrences == 3, "Client ID 80000 must find 3 occurrences");
		const MapItemSearchSummary c70000 = SearchItemsOnMap(map, g_items, ItemIdSearchKind::ClientId, 70000u, false, 1000u);
		check(c70000.totalOccurrences == 0, "Client ID 70000 must not falsely match Server ID 70000 when Client ID is 80000");

		// 4. Search by Sprite ID 700001 -> finds all 3 occurrences of 70000; Sprite ID 80000 -> 0 occurrences!
		const MapItemSearchSummary sp700001 = SearchItemsOnMap(map, g_items, ItemIdSearchKind::SpriteId, 700001u, false, 1000u);
		check(sp700001.totalOccurrences == 3, "Sprite ID 700001 must find 3 occurrences");
		const MapItemSearchSummary sp80000 = SearchItemsOnMap(map, g_items, ItemIdSearchKind::SpriteId, 80000u, false, 1000u);
		check(sp80000.totalOccurrences == 0, "Sprite ID 80000 must not falsely match Client ID 80000");

		// 5. Ground ID 65536 -> 1 occurrence at (100, 100, 7) with Ground location
		const MapItemSearchSummary g65536 = SearchItemsOnMap(map, g_items, ItemIdSearchKind::ServerId, 65536u, false, 1000u);
		check(g65536.totalOccurrences == 1 && g65536.matches.front().location == ItemLocationKind::Ground && g65536.matches.front().position == Position(100, 100, 7), "Ground 65536 must be found");

		// 6. Nested container items 1000000 and 4294967295 at depth 2
		const MapItemSearchSummary s1m = SearchItemsOnMap(map, g_items, ItemIdSearchKind::ServerId, 1000000u, false, 1000u);
		check(s1m.totalOccurrences == 1 && s1m.matches.front().containerDepth == 2 && s1m.matches.front().position == Position(105, 110, 7), "Nested container item 1000000 must be found");

		const MapItemSearchSummary sMax = SearchItemsOnMap(map, g_items, ItemIdSearchKind::ServerId, 4294967295u, false, 1000u);
		check(sMax.totalOccurrences == 1 && sMax.matches.front().containerDepth == 2 && sMax.matches.front().position == Position(105, 110, 7), "Nested container item 4294967295 must be found");

		// 7. Selection-only filter on Server ID 70000 -> only 2 occurrences on t1 (not t2)
		const MapItemSearchSummary s70000Sel = SearchItemsOnMap(map, g_items, ItemIdSearchKind::ServerId, 70000u, true, 1000u);
		check(s70000Sel.totalOccurrences == 2 && s70000Sel.matchedTiles == 1, "selectedOnly must restrict search to selected tiles");

		// 8. Classic IDs 100 and 65535
		const MapItemSearchSummary s100 = SearchItemsOnMap(map, g_items, ItemIdSearchKind::ServerId, 100u, false, 1000u);
		check(s100.totalOccurrences == 2, "Classic Ground ID 100 must find 2 occurrences");
		const MapItemSearchSummary s65535 = SearchItemsOnMap(map, g_items, ItemIdSearchKind::ServerId, 65535u, false, 1000u);
		check(s65535.totalOccurrences == 1 && s65535.matches.front().position == Position(200, 200, 6), "Max 16-bit ID 65535 must find 1 occurrence at (200, 200, 6)");

		// 9. Absent valid 32-bit ID -> 0 occurrences
		const MapItemSearchSummary sAbsent = SearchItemsOnMap(map, g_items, ItemIdSearchKind::ServerId, 999999u, false, 1000u);
		check(sAbsent.totalOccurrences == 0 && sAbsent.matches.empty(), "Absent valid 32-bit ID must return 0 occurrences");
	}

	void testOtbmCompatibilityAndNoTruncation() {
		// Ensure classic 16-bit container item exists for round-trip test
		addSyntheticItem(1988u, 1988u, "Classic Backpack", ITEM_GROUP_CONTAINER, ITEM_TYPE_CONTAINER);

		const std::filesystem::path otbmPath = std::filesystem::temp_directory_path() / "nexamap_find_item_roundtrip.otbm";
		std::filesystem::remove(otbmPath);

		// 1. Save & reopen classic 16-bit map -> preserve IDs, ActionID, UniqueID, text, containers
		{
			Map map;
			map.setWidth(128);
			map.setHeight(128);

			Tile* tile = map.getOrCreateTile(Position(32, 48, 7));
			tile->ground = Item::Create(100u);

			Item* special = Item::Create(65535u);
			special->setActionID(1234);
			special->setUniqueID(5678);
			special->setText("Halloween Event Sign");
			tile->addItem(special);

			auto* bag = dynamic_cast<Container*>(Item::Create(1988u));
			check(bag != nullptr, "classic backpack 1988 should be a Container");
			if (bag) {
				Item* inside = Item::Create(65535u);
				inside->setActionID(4321);
				bag->getVector().push_back(inside);
				tile->addItem(bag);
			}

			IOMapOTBM saver(MapVersion(MAP_OTBM_3, CLIENT_VERSION_860));
			check(saver.saveMapData(map, FileName(wxString::FromUTF8(otbmPath.string()))), "16-bit OTBM save must succeed");
		}

		{
			Map loaded;
			IOMapOTBM loader(MapVersion(MAP_OTBM_3, CLIENT_VERSION_860));
			check(loader.loadMapData(loaded, FileName(wxString::FromUTF8(otbmPath.string()))), "16-bit OTBM reload must succeed");

			Tile* tile = loaded.getTile(Position(32, 48, 7));
			check(tile != nullptr && tile->ground != nullptr && tile->ground->getID() == 100u, "reloaded ground ID 100 must be preserved");
			const MapItemSearchSummary found65535 = SearchItemsOnMap(loaded, g_items, ItemIdSearchKind::ServerId, 65535u, false, 100u);
			check(found65535.totalOccurrences == 2, "reloaded map must preserve both 65535 items (tile + container)");

			bool verifiedAttrs = false;
			if (tile) {
				for (const Item* item : tile->items) {
					if (item && item->getID() == 65535u && item->getActionID() == 1234 && item->getUniqueID() == 5678 && item->getText() == "Halloween Event Sign") {
						verifiedAttrs = true;
					}
				}
			}
			check(verifiedAttrs, "reloaded item must preserve ActionID, UniqueID, and text");
		}

		// Record exact bytes of the valid 16-bit file to verify it is not corrupted on failed 32-bit save
		std::vector<char> originalBytes;
		{
			std::ifstream in(otbmPath, std::ios::binary);
			originalBytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}
		check(!originalBytes.empty(), "saved 16-bit OTBM file must be non-empty");

		// 2. Attempt to save a map containing item ID 70000 to 16-bit OTBM -> MUST FAIL cleanly without truncating or corrupting!
		{
			Map map32;
			map32.setWidth(128);
			map32.setHeight(128);

			Tile* tile = map32.getOrCreateTile(Position(40, 50, 7));
			tile->ground = Item::Create(100u);
			tile->addItem(Item::Create(70000u));

			IOMapOTBM saver(MapVersion(MAP_OTBM_3, CLIENT_VERSION_860));
			const bool saved = saver.saveMapData(map32, FileName(wxString::FromUTF8(otbmPath.string())));
			check(!saved, "saving item ID > 65535 into 16-bit OTBM must fail instead of truncating");
			const std::string err = nstr(saver.getError());
			check(err.find("70000") != std::string::npos && err.find("16-bit") != std::string::npos, "error message must clearly report item ID and 16-bit OTBM limitation");
		}

		// Verify original file on disk was NOT corrupted
		std::vector<char> afterBytes;
		{
			std::ifstream in(otbmPath, std::ios::binary);
			afterBytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}
		check(afterBytes == originalBytes, "aborted 32-bit OTBM save must leave existing file completely intact");

		std::filesystem::remove(otbmPath);
	}
}

int RunMultiplayerSessionTests(int, char**) {
	testParseUint32ItemId();
	testAppearancesAndXml32BitLoading();
	testMapSearchServerClientSpriteAndContainers();
	testOtbmCompatibilityAndNoTruncation();

	g_items.clear();
	g_gui.gfx.clear();
	g_spriteAppearances.unload();

	if (failures != 0) {
		std::cerr << failures << " test(s) failed.\n";
		return 1;
	}
	std::cout << "All Find Item 32-bit tests passed.\n";
	return 0;
}
