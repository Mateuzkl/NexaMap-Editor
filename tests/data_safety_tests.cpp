// SPDX-License-Identifier: GPL-3.0-or-later
// Regression tests for the data-loss audit, run against the real editor code on
// synthetic maps in a temporary directory.
#include "main.h"

#include "application.h"
#include "artprovider.h"
#include "copybuffer.h"
#include "editor.h"
#include "gui.h"
#include "iomap_otbm.h"
#include "items.h"
#include "map_backup.h"
#include "multiplayer_session.h"
#include "settings.h"
#include "spawn_format.h"
#include "sprite_appearances.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <wx/evtloop.h>

namespace fs = std::filesystem;

Editor::Editor(CopyBuffer& buffer, std::nullptr_t) :
	actionQueue(new ActionQueue(*this)), selection(*this), copybuffer(buffer), replace_brush(nullptr) {
	map.mapVersion = { MAP_OTBM_4, CLIENT_VERSION_860 };
	map.setWidth(512);
	map.setHeight(512);
	map.setName("Data safety test fixture");
	map.unnamed = true;
}

namespace {
	constexpr uint16_t GROUND_ID = 100;
	constexpr uint16_t PLAIN_ID = 101;
	constexpr uint16_t STACK_ID = 102;
	constexpr uint16_t PODIUM_ID = 103;

	int failures = 0;

	void check(bool condition, const std::string& message) {
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	}

	std::string readFile(const fs::path& file) {
		std::ifstream stream(file, std::ios::binary);
		std::ostringstream content;
		content << stream.rdbuf();
		return content.str();
	}

	void writeFile(const fs::path& file, const std::string& content) {
		std::ofstream(file, std::ios::binary) << content;
	}

	bool hasTemporarySiblings(const fs::path& directory) {
		for (const auto& entry : fs::directory_iterator(directory)) {
			const std::string name = entry.path().filename().string();
			if (name.find(".rme-") != std::string::npos || name.back() == '~') {
				return true;
			}
		}
		return false;
	}

	FileName mapName(const fs::path& file) {
		return FileName(wxString::FromUTF8(file.string()));
	}

	void defineItems() {
		for (uint16_t id : { GROUND_ID, PLAIN_ID, STACK_ID, PODIUM_ID }) {
			auto* type = new ItemType();
			type->id = id;
			type->clientID = id;
			type->group = id == GROUND_ID ? ITEM_GROUP_GROUND : ITEM_GROUP_NONE;
			type->stackable = id == STACK_ID;
			type->type = id == PODIUM_ID ? ITEM_TYPE_PODIUM : ITEM_TYPE_NONE;
			g_items.items.set(id, type);
		}
		g_items.MajorVersion = 3;
		g_items.MinorVersion = CLIENT_VERSION_860;
	}

	Tile* addTile(Map& map, const Position& position) {
		Tile* tile = map.allocator(map.createTileL(position));
		tile->addItem(Item::Create(GROUND_ID));
		map.setTile(tile);
		return tile;
	}

	struct Fixture {
		CopyBuffer buffer;
		Editor editor;
		House* house = nullptr;
		Fixture() :
			editor(buffer, nullptr) {
			Town* town = new Town(1);
			town->setName("Town");
			town->setTemplePosition(Position(100, 100, 7));
			editor.map.towns.addTown(town);
			house = new House(editor.map);
			house->setID(5);
			house->name = "House";
			house->townid = 1;
			editor.map.houses.addHouse(house);
			house->addTile(addTile(editor.map, Position(100, 100, 7)));
		}
	};

	IOMapOTBM writerFor(const Map& map) {
		return IOMapOTBM(map.getVersion());
	}

	void testFailedSaveLeavesFilesAlone(const fs::path& root) {
		const fs::path directory = root / "rollback";
		fs::create_directories(directory);
		const fs::path file = directory / "world.otbm";
		Fixture fixture;
		fixture.editor.map.setHouseFilename("world-house.xml");
		fixture.editor.map.setSpawnFilename("world-spawn.xml");
		check(writerFor(fixture.editor.map).saveMap(fixture.editor.map, mapName(file)), "fixture map should save");
		const std::string originalMap = readFile(file);

		// A directory on the house path fails that stage after the map was staged.
		addTile(fixture.editor.map, Position(101, 100, 7));
		fs::remove(directory / "world-house.xml");
		fs::create_directory(directory / "world-house.xml");
		IOMapOTBM writer = writerFor(fixture.editor.map);
		check(!writer.saveMap(fixture.editor.map, mapName(file)), "a failing sidecar must fail the save");
		check(!writer.getError().empty(), "the failed save must report why");
		check(readFile(file) == originalMap, "a failed save must not touch the published map");
		check(!hasTemporarySiblings(directory), "a failed save must not leave temporary files");
	}

	void testSaveKeepsWaypointFileAndSymlinks(const fs::path& root) {
		const fs::path directory = root / "keep";
		const fs::path server = root / "keep-server";
		fs::create_directories(directory);
		fs::create_directories(server);
		const fs::path real = server / "world.otbm";
		writeFile(real, "placeholder");
		const fs::path mapFile = directory / "world.otbm";
		std::error_code linkError;
		fs::create_symlink(real, mapFile, linkError);
#ifdef _WIN32
		if (linkError == std::error_code(ERROR_PRIVILEGE_NOT_HELD, std::system_category())) {
			std::cout << "Symlink creation requires additional privileges; checking a regular save instead.\n";
			fs::rename(real, mapFile);
		} else
#endif
			if (linkError) {
			check(false, "could not create the test symlink: " + linkError.message());
			return;
		}
#ifndef _WIN32
		fs::permissions(real, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read);
#endif
		writeFile(directory / "world-waypoint.xml", "<waypoints/>");

		Fixture fixture;
		check(fixture.editor.saveMap(mapName(mapFile), false), "saving the map should succeed");
		if (!linkError) {
			check(fs::is_symlink(mapFile), "a save must not replace a symlink with a file");
			check(readFile(real) != "placeholder", "the save must reach the symlink's target");
#ifndef _WIN32
			check((fs::status(real).permissions() & fs::perms::all) == (fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read), "a save must keep the file mode");
#endif
		} else {
			check(readFile(mapFile) != "placeholder", "the regular save must replace the previous contents");
		}
		check(readFile(directory / "world-waypoint.xml") == "<waypoints/>", "a save must not delete the waypoint sidecar");
		check(!hasTemporarySiblings(directory) && !hasTemporarySiblings(server), "a successful save must not leave temporary files");
	}

	void testDatedBackups(const fs::path& root) {
		const fs::path directory = root / "backups";
		fs::create_directories(directory);
		const fs::path file = directory / "world.otbm";
		Fixture fixture;
		check(fixture.editor.saveMap(mapName(file), false), "first save should succeed");
		const std::string firstMap = readFile(file);

		const int previous = g_settings.getInteger(Config::ALWAYS_MAKE_BACKUP);
		g_settings.setInteger(Config::ALWAYS_MAKE_BACKUP, 1);
		addTile(fixture.editor.map, Position(101, 100, 7));
		check(fixture.editor.saveMap(mapName(file), false), "second save should succeed");
		g_settings.setInteger(Config::ALWAYS_MAKE_BACKUP, previous);

		std::vector<fs::path> backups;
		const std::regex datedName(R"(world(-house|-spawn)?\.\d+-\d+-\d+-\d+-\d+-\d+\.(otbm|xml))");
		for (const auto& entry : fs::directory_iterator(directory)) {
			if (std::regex_match(entry.path().filename().string(), datedName)) {
				backups.push_back(entry.path());
			}
		}
		check(backups.size() == 1, "only the replaced map should get a dated backup, unchanged sidecars should not");
		check(!backups.empty() && readFile(backups.front()) == firstMap, "the dated backup must hold the previous contents");
		check(readFile(file) != firstMap, "the live map must hold the new contents");
		check(!hasTemporarySiblings(directory), "backups must not leave temporary files");
	}

	void testSaveAsGetsOwnSidecars(const fs::path& root) {
		const fs::path directory = root / "saveas";
		fs::create_directories(directory);
		Fixture fixture;
		check(fixture.editor.saveMap(mapName(directory / "world.otbm"), false), "first save should succeed");
		const std::string originalHouses = readFile(directory / "world-house.xml");

		check(fixture.editor.saveMap(mapName(directory / "copy.otbm"), false), "Save As should succeed");
		check(fixture.editor.map.getHouseFilename() == "copy-house.xml", "Save As should derive the house file name");
		check(fixture.editor.map.getSpawnFilename() == "copy-spawn.xml", "Save As should derive the spawn file name");
		fixture.house->name = "Edited";
		check(fixture.editor.saveMap(FileName(wxString()), false), "saving the copy should succeed");
		check(readFile(directory / "world-house.xml") == originalHouses, "the original project must keep its house file");
		check(readFile(directory / "copy-house.xml").find("Edited") != std::string::npos, "the copy must get its own house data");
	}

	void testUnloadedSidecarsAreProtected(const fs::path& root) {
		const fs::path directory = root / "sidecars";
		fs::create_directories(directory);
		const fs::path file = directory / "world.otbm";
		{
			Fixture fixture;
			check(fixture.editor.saveMap(mapName(file), false), "fixture should save");
		}
		const std::string damaged = "<houses><house";
		writeFile(directory / "world-house.xml", damaged);

		Map loaded;
		IOMapOTBM reader(MapVersion { MAP_OTBM_4, CLIENT_VERSION_860 });
		check(reader.loadMap(loaded, mapName(file)), "a bad sidecar alone should not stop the map from opening");
		IOMapOTBM writer(loaded.getVersion());
		check(!writer.saveMap(loaded, mapName(file)), "saving over a sidecar that failed to load must be refused");
		check(writer.getError().Find("could not be loaded") != wxNOT_FOUND, "the refusal must explain itself");
		check(readFile(directory / "world-house.xml") == damaged, "the unloaded sidecar must survive the refused save");

		loaded.setHouseFilename("renamed-house.xml");
		IOMapOTBM saveAs(loaded.getVersion());
		check(saveAs.saveMap(loaded, mapName(directory / "renamed.otbm")), "a save under another name should work");
		check(readFile(directory / "world-house.xml") == damaged, "saving elsewhere must leave the unloaded sidecar alone");
	}

	void testSidecarCaseMismatch(const fs::path& root) {
		const fs::path directory = root / "case";
		fs::create_directories(directory);
		const fs::path file = directory / "world.otbm";
		{
			Fixture fixture;
			fixture.house->rent = 1234;
			fixture.editor.map.setHouseFilename("Thais-House.xml");
			fixture.editor.map.setSpawnFilename("world-spawn.xml");
			check(writerFor(fixture.editor.map).saveMap(fixture.editor.map, mapName(file)), "fixture should save");
		}
		fs::rename(directory / "Thais-House.xml", directory / "thais-house.xml");

		Map loaded;
		IOMapOTBM reader(MapVersion { MAP_OTBM_4, CLIENT_VERSION_860 });
		check(reader.loadMap(loaded, mapName(file)), "the map should open");
		const auto expectedName = fs::exists(directory / "Thais-House.xml") ? "Thais-House.xml" : "thais-house.xml";
		check(loaded.getHouseFilename() == expectedName, "the sidecar name must resolve on this filesystem");
		const House* house = loaded.houses.getHouse(5);
		check(house && house->rent == 1234, "a case-mismatched house file must be loaded");
	}

	void testTruncatedMapsAreRefused(const fs::path& root) {
		const fs::path directory = root / "truncated";
		fs::create_directories(directory);
		const fs::path file = directory / "world.otbm";
		{
			Fixture fixture;
			for (int x = 0; x < 8; ++x) {
				addTile(fixture.editor.map, Position(200 + x * 100, 100, 7));
			}
			check(fixture.editor.saveMap(mapName(file), false), "fixture should save");
		}
		const std::string complete = readFile(file);
		const fs::path cut = directory / "cut.otbm";
		size_t accepted = 0;
		for (size_t size = 8; size < complete.size(); ++size) {
			writeFile(cut, complete.substr(0, size));
			Map loaded;
			IOMapOTBM reader(MapVersion { MAP_OTBM_4, CLIENT_VERSION_860 });
			accepted += reader.loadMapData(loaded, mapName(cut)) ? 1 : 0;
		}
		check(accepted == 0, "every truncation point must be refused, " + std::to_string(accepted) + " were accepted");

		Map loaded;
		IOMapOTBM reader(MapVersion { MAP_OTBM_4, CLIENT_VERSION_860 });
		check(reader.loadMapData(loaded, mapName(file)), "the complete file must still load");
	}

	void testBackupUsesStorageHeader(const fs::path& root) {
		const fs::path directory = root / "s8";
		fs::create_directories(directory);
		const fs::path file = directory / "world.otbm";
		Fixture fixture;
		fixture.editor.map.setSourceItemVersion(3, 57);
		check(fixture.editor.saveMap(mapName(file), false), "fixture should save");
		const std::string backup = MapBackupService::createBackup(fixture.editor, nullptr);
		check(!backup.empty(), "the backup should be written");
		OTBMFileMetadata metadata;
		check(IOMapOTBM::getFileMetadata(mapName(backup), metadata), "the backup should be readable");
		check(metadata.itemMinorVersion == 57, "the backup must carry the map's item version header");
	}

	void testUnexpectedChildrenAndPodiumsSurvive(const fs::path& root) {
		const fs::path directory = root / "m1";
		fs::create_directories(directory);
		const fs::path file = directory / "world.otbm";
		const Position position(120, 100, 7);
		{
			Fixture fixture;
			Tile* tile = addTile(fixture.editor.map, position);
			// Type 101 is no container here, like an ID from another items.otb.
			auto* holder = new Container(PLAIN_ID);
			holder->getVector().push_back(Item::Create(STACK_ID, 9));
			tile->items.push_back(holder);
			auto* podium = new Podium(PLAIN_ID);
			Outfit outfit;
			outfit.lookType = 130;
			podium->setOutfit(outfit);
			tile->items.push_back(podium);
			check(fixture.editor.saveMap(mapName(file), false), "fixture should save");
		}
		Map loaded;
		IOMapOTBM reader(MapVersion { MAP_OTBM_4, CLIENT_VERSION_860 });
		check(reader.loadMap(loaded, mapName(file)), "fixture should load");
		const Tile* tile = loaded.getTile(position);
		check(tile && tile->items.size() == 2, "both items should load");
		if (tile && tile->items.size() == 2) {
			const auto* holder = dynamic_cast<const Container*>(tile->items[0]);
			check(holder && holder->getItemCount() == 1, "a non-container ID must keep its child items");
			check(holder && holder->getItemCount() == 1 && holder->getItem(0)->getSubtype() == 9, "child item attributes must survive");
			auto* podium = dynamic_cast<Podium*>(tile->items[1]);
			check(podium && podium->getOutfit().lookType == 130, "a non-podium ID must keep its outfit");
		}
	}

	void testWrittenAttributesSurvive(const fs::path& root) {
		const fs::path directory = root / "m5";
		fs::create_directories(directory);
		const Position position(121, 100, 7);
		for (MapVersionID version : { MAP_OTBM_3, MAP_OTBM_4 }) {
			const fs::path file = directory / ("world" + std::to_string(version) + ".otbm");
			{
				Fixture fixture;
				fixture.editor.map.convert({ version, CLIENT_VERSION_860 }, false);
				Tile* tile = addTile(fixture.editor.map, position);
				Item* book = Item::Create(PLAIN_ID);
				book->setAttribute("writtenby", std::string("Bubble"));
				book->setAttribute("writtendate", int32_t(1234567));
				book->setText("Hello");
				tile->addItem(book);
				check(fixture.editor.saveMap(mapName(file), false), "fixture should save");
			}
			check(readFile(file).find("writtenby") == std::string::npos, "authorship belongs in OTBM attributes, not custom keys");
			Map loaded;
			IOMapOTBM reader(MapVersion { version, CLIENT_VERSION_860 });
			check(reader.loadMap(loaded, mapName(file)), "fixture should load");
			const Tile* tile = loaded.getTile(position);
			const Item* book = tile && !tile->items.empty() ? tile->items.back() : nullptr;
			const std::string* writer = book ? book->getStringAttribute("writtenby") : nullptr;
			const int32_t* date = book ? book->getIntegerAttribute("writtendate") : nullptr;
			check(writer && *writer == "Bubble", "the author must survive a round trip");
			check(date && *date == 1234567, "the writing date must survive a round trip");
			check(book && book->getText() == "Hello", "the text must survive beside them");
		}
	}

	void testHouseExitOnFloorZero(const fs::path& root) {
		const fs::path directory = root / "exit";
		fs::create_directories(directory);
		const fs::path file = directory / "world.otbm";
		{
			Fixture fixture;
			fixture.house->setExit(Position(90, 90, 0));
			check(fixture.editor.saveMap(mapName(file), false), "fixture should save");
		}
		Map loaded;
		IOMapOTBM reader(MapVersion { MAP_OTBM_4, CLIENT_VERSION_860 });
		check(reader.loadMap(loaded, mapName(file)), "fixture should load");
		const House* house = loaded.houses.getHouse(5);
		check(house && house->getExit() == Position(90, 90, 0), "an exit on floor 0 must survive");
	}

	using NodeWriter = std::function<void(NodeFileWriteHandle&)>;

	// The node writers do not validate anything, so they can produce every
	// malformed map below without hand-escaped bytes.
	void writeRawMap(const fs::path& file, const NodeWriter& header, const NodeWriter& body) {
		DiskNodeFileWriteHandle handle(file.string(), "OTBM");
		handle.addNode(0);
		handle.addU32(MAP_OTBM_4);
		handle.addU16(512);
		handle.addU16(512);
		handle.addU32(3);
		handle.addU32(CLIENT_VERSION_860);
		handle.addNode(OTBM_MAP_DATA);
		if (header) {
			header(handle);
		}
		if (body) {
			body(handle);
		}
		handle.endNode();
		handle.endNode();
	}

	void writeTileArea(NodeFileWriteHandle& handle, uint8_t floor, const NodeWriter& tileContents = {}) {
		handle.addNode(OTBM_TILE_AREA);
		handle.addU16(100);
		handle.addU16(100);
		handle.addU8(floor);
		handle.addNode(OTBM_TILE);
		handle.addU8(0);
		handle.addU8(0);
		if (tileContents) {
			tileContents(handle);
		}
		handle.endNode();
		handle.endNode();
	}

	void writeHeaderName(NodeFileWriteHandle& handle, OTBM_ItemAttribute attribute, const std::string& name) {
		handle.addU8(attribute);
		handle.addString(name);
	}

	void testBadTileAreaFloorIsRefused(const fs::path& root) {
		const fs::path directory = root / "bad-floor";
		fs::create_directories(directory);
		const fs::path file = directory / "world.otbm";
		for (int floor : { 16, 200, 255 }) {
			writeRawMap(file, {}, [&](NodeFileWriteHandle& handle) { writeTileArea(handle, static_cast<uint8_t>(floor)); });
			Map loaded;
			IOMapOTBM reader(MapVersion { MAP_OTBM_4, CLIENT_VERSION_860 });
			check(!reader.loadMapData(loaded, mapName(file)), "a tile area past the last floor must be refused");
			check(reader.getError().Contains("floor"), "the refusal must say why");
		}
		writeRawMap(file, {}, [&](NodeFileWriteHandle& handle) { writeTileArea(handle, 15); });
		Map loaded;
		IOMapOTBM reader(MapVersion { MAP_OTBM_4, CLIENT_VERSION_860 });
		check(reader.loadMapData(loaded, mapName(file)), "the last floor is valid");
		check(loaded.getTile(100, 100, 15) != nullptr, "the tile on the last floor should load");
	}

	void testFloorAccessIsBounded() {
		Map map;
		map.createTileL(Position(100, 100, 7));
		for (int floor : { -1, 16, 200, 255, 1000000 }) {
			const Position position(100, 100, floor);
			check(map.getTileL(position) == nullptr && map.getTile(position) == nullptr, "reading a bad floor finds nothing");
			check(map.getLeaf(100, 100)->getTile(100, 100, floor) == nullptr, "a leaf finds nothing on a bad floor");
			bool refused = false;
			try {
				map.createTileL(position);
			} catch (const std::out_of_range&) {
				refused = true;
			}
			check(refused, "creating a floor that does not exist must fail");
		}
	}

	void testBadPositionsAreDropped(const fs::path& root) {
		const fs::path directory = root / "bad-positions";
		fs::create_directories(directory);
		const fs::path file = directory / "world.otbm";
		const auto writeTown = [](NodeFileWriteHandle& handle, uint32_t id, uint8_t floor) {
			handle.addNode(OTBM_TOWN);
			handle.addU32(id);
			handle.addString("Town");
			handle.addU16(100);
			handle.addU16(100);
			handle.addU8(floor);
			handle.endNode();
		};
		writeRawMap(
			file, [](NodeFileWriteHandle& handle) {
				writeHeaderName(handle, OTBM_ATTR_EXT_HOUSE_FILE, "world-house.xml");
				writeHeaderName(handle, OTBM_ATTR_EXT_SPAWN_FILE, "world-spawn.xml"); },
			[&](NodeFileWriteHandle& handle) {
				handle.addNode(OTBM_TILE_AREA);
				handle.addU16(100);
				handle.addU16(100);
				handle.addU8(7);
				for (uint8_t offset = 0; offset < 3; ++offset) {
					handle.addNode(OTBM_HOUSETILE);
					handle.addU8(offset);
					handle.addU8(0);
					handle.addU32(5 + offset);
					handle.endNode();
				}
				handle.endNode();
				handle.addNode(OTBM_TOWNS);
				writeTown(handle, 1, 7);
				writeTown(handle, 2, 200);
				handle.endNode();
			}
		);
		writeFile(directory / "world-house.xml", "<houses>"
												 "<house name='a' houseid='5' entryx='90' entryy='90' entryz='99' townid='1'/>"
												 "<house name='b' houseid='6' entryx='90' entryy='90' entryz='-1' townid='1'/>"
												 "<house name='c' houseid='7' entryx='91' entryy='91' entryz='3' townid='1'/>"
												 "</houses>");
		writeFile(directory / "world-zones.xml", "<zones><zone name='Arena' zoneid='3'>"
												 "<position x='100' y='100' z='99'/><position x='100' y='100' z='-5'/><position x='100' y='100' z='7'/>"
												 "</zone></zones>");
		writeFile(directory / "world-spawn.xml", "<spawns>"
												 "<spawn centerx='100' centery='100' centerz='200' radius='1'><monster name='Rat' x='0' y='0' spawntime='30'/></spawn>"
												 "<spawn centerx='100' centery='100' centerz='7' radius='1'><monster name='Rat' x='0' y='0' spawntime='30'/></spawn>"
												 "</spawns>");

		Map loaded;
		IOMapOTBM reader(MapVersion { MAP_OTBM_4, CLIENT_VERSION_860 });
		check(reader.loadMap(loaded, mapName(file)), "bad metadata positions must not fail the map");
		check(loaded.towns.getTown(1) && loaded.towns.getTown(1)->getTemplePosition() == Position(100, 100, 7), "a valid temple should load");
		check(loaded.towns.getTown(2) && loaded.towns.getTown(2)->getTemplePosition() == Position(), "a temple on a bad floor must not be set");
		check(loaded.houses.getHouse(5) && loaded.houses.getHouse(5)->getExit() == Position(), "an exit above the last floor must not be set");
		check(loaded.houses.getHouse(6) && loaded.houses.getHouse(6)->getExit() == Position(), "an exit on a negative floor must not be set");
		check(loaded.houses.getHouse(7) && loaded.houses.getHouse(7)->getExit() == Position(91, 91, 3), "a valid exit should load");
		const Tile* tile = loaded.getTile(100, 100, 7);
		check(tile && tile->hasZone(3), "a valid zone position should load");
		check(tile && tile->spawn != nullptr && tile->creature != nullptr, "a valid spawn should load");

		SpawnDocument document;
		SpawnAreaData area;
		area.centerX = 100;
		area.centerY = 100;
		area.centerZ = 16;
		document.areas.push_back(area);
		std::vector<std::string> warnings;
		check(!SpawnMapAdapter::Apply(loaded, document, warnings), "a spawn document on a bad floor must not be applied");
	}

	void testTeleportFloorIsValidated(const fs::path& root) {
		const fs::path directory = root / "teleport-floor";
		fs::create_directories(directory);
		const fs::path file = directory / "world.otbm";
		writeRawMap(file, {}, [](NodeFileWriteHandle& handle) {
			writeTileArea(handle, 7, [](NodeFileWriteHandle& tile) {
				for (uint8_t floor : { uint8_t(200), uint8_t(8) }) {
					tile.addNode(OTBM_ITEM);
					tile.addU16(PLAIN_ID);
					tile.addU8(OTBM_ATTR_TELE_DEST);
					tile.addU16(300);
					tile.addU16(301);
					tile.addU8(floor);
					tile.addU8(OTBM_ATTR_ACTION_ID);
					tile.addU16(1234);
					tile.endNode();
				}
			});
		});
		Map loaded;
		IOMapOTBM reader(MapVersion { MAP_OTBM_4, CLIENT_VERSION_860 });
		check(reader.loadMapData(loaded, mapName(file)), "a bad destination must not fail the map");
		const Tile* tile = loaded.getTile(100, 100, 7);
		check(tile && tile->items.size() == 2, "both teleports should load");
		if (tile && tile->items.size() == 2) {
			const auto* invalid = dynamic_cast<const Teleport*>(tile->items[0]);
			const auto* valid = dynamic_cast<const Teleport*>(tile->items[1]);
			check(invalid && !invalid->hasDestination() && invalid->getDestination().z <= MAP_MAX_LAYER, "a destination on a bad floor must be dropped");
			check(tile->items[0]->getActionID() == 1234, "attributes after a bad destination must survive");
			check(valid && valid->getDestination() == Position(300, 301, 8), "a valid destination should load");
		}
		check(reader.invalidTeleportDestinations == 1, "only the bad destination should be counted");
	}

	void testPodiumAttributesCannotChangeItsClass(const fs::path& root) {
		const fs::path directory = root / "podium-type";
		fs::create_directories(directory);
		const fs::path file = directory / "world.otbm";
		writeRawMap(file, {}, [](NodeFileWriteHandle& handle) {
			writeTileArea(handle, 7, [](NodeFileWriteHandle& tile) {
				const auto writePodium = [&](const NodeWriter& attribute) {
					tile.addNode(OTBM_ITEM);
					tile.addU16(PODIUM_ID);
					attribute(tile);
					tile.addU8(OTBM_ATTR_ACTION_ID);
					tile.addU16(77);
					tile.endNode();
				};
				writePodium([](NodeFileWriteHandle& item) {
					item.addU8(OTBM_ATTR_TELE_DEST);
					item.addU16(1);
					item.addU16(2);
					item.addU8(3);
				});
				writePodium([](NodeFileWriteHandle& item) {
					item.addU8(OTBM_ATTR_HOUSEDOORID);
					item.addU8(4);
				});
				writePodium([](NodeFileWriteHandle& item) {
					item.addU8(OTBM_ATTR_DEPOT_ID);
					item.addU16(5);
				});
			});
		});
		Map loaded;
		IOMapOTBM reader(MapVersion { MAP_OTBM_4, CLIENT_VERSION_860 });
		check(reader.loadMapData(loaded, mapName(file)), "the podium fixture should load");
		const Tile* tile = loaded.getTile(100, 100, 7);
		check(tile && tile->items.size() == 3, "all podiums should load");
		if (tile) {
			for (const Item* item : tile->items) {
				check(dynamic_cast<const Podium*>(item) != nullptr, "a podium ID must stay a Podium");
				check(item->getActionID() == 77, "attributes after a foreign attribute must survive");
			}
		}
	}

	void testContainerNestingIsBounded(const fs::path& root) {
		const fs::path directory = root / "nesting";
		fs::create_directories(directory);
		const fs::path file = directory / "world.otbm";
		const auto writeNested = [&](size_t levels) {
			writeRawMap(file, {}, [&](NodeFileWriteHandle& handle) {
				writeTileArea(handle, 7, [&](NodeFileWriteHandle& tile) {
					for (size_t level = 0; level < levels; ++level) {
						tile.addNode(OTBM_ITEM);
						tile.addU16(PLAIN_ID);
					}
					for (size_t level = 0; level < levels; ++level) {
						tile.endNode();
					}
				});
			});
		};
		for (size_t levels : { size_t(300), size_t(500000) }) {
			writeNested(levels);
			Map loaded;
			IOMapOTBM reader(MapVersion { MAP_OTBM_4, CLIENT_VERSION_860 });
			check(!reader.loadMapData(loaded, mapName(file)), "nesting past the limit must be refused");
		}
		writeNested(200);
		Map loaded;
		IOMapOTBM reader(MapVersion { MAP_OTBM_4, CLIENT_VERSION_860 });
		check(reader.loadMapData(loaded, mapName(file)), "nesting far beyond real maps should still load");
	}

	void testSidecarNamesStayInMapFolder(const fs::path& root) {
		const fs::path outside = root / "traversal";
		const fs::path directory = outside / "a" / "b";
		fs::create_directories(directory);
		const fs::path file = directory / "world.otbm";
		const fs::path victim = outside / "victim.xml";
		writeFile(victim, "keep me");
		writeRawMap(
			file, [](NodeFileWriteHandle& handle) {
				writeHeaderName(handle, OTBM_ATTR_EXT_ZONE_FILE, "../../victim.xml");
				writeHeaderName(handle, OTBM_ATTR_EXT_HOUSE_FILE, "..\\..\\house.xml");
				writeHeaderName(handle, OTBM_ATTR_EXT_SPAWN_FILE, (fs::temp_directory_path() / "spawn.xml").string());
				writeHeaderName(handle, OTBM_ATTR_EXT_SPAWN_NPC_FILE, ".."); },
			[](NodeFileWriteHandle& handle) { writeTileArea(handle, 7); }
		);

		OTBMFileMetadata metadata;
		check(IOMapOTBM::getFileMetadata(mapName(file), metadata), "the header should be readable");
		check(metadata.zoneFile == "victim.xml" && metadata.spawnFile == "spawn.xml" && metadata.spawnNpcFile.empty(), "format detection must see reduced names");

		{
			Fixture fixture;
			Map& map = fixture.editor.map;
			IOMapOTBM reader(MapVersion { MAP_OTBM_4, CLIENT_VERSION_860 });
			check(reader.loadMap(map, mapName(file)), "a header with traversal names should still load");
			check(map.getZoneFilename() == "victim.xml", "the zone file must stay in the map folder");
			map.zones.addZone("Arena", 3);
			map.getTile(100, 100, 7)->addZone(3);
			IOMapOTBM writer = writerFor(map);
			check(writer.saveMap(map, mapName(file)), "the map should save: " + writer.getError().ToStdString());
			check(readFile(victim) == "keep me", "saving must not write through a header path");
			check(fs::exists(directory / "victim.xml"), "the sidecar should land next to the map");

			// Save As derives fresh names, whatever the header said.
			check(fixture.editor.saveMap(mapName(directory / "copy.otbm"), false), "Save As should succeed");
			check(map.getZoneFilename() == "copy-zones.xml", "Save As should derive the zone file name");
			check(readFile(victim) == "keep me", "Save As must not write through a header path");
		}

		// Unchanged for the only shape real maps have.
		const fs::path regular = directory / "regular.otbm";
		writeRawMap(
			regular, [](NodeFileWriteHandle& handle) {
				writeHeaderName(handle, OTBM_ATTR_EXT_ZONE_FILE, "regular-custom-zones.xml");
				writeHeaderName(handle, OTBM_ATTR_EXT_HOUSE_FILE, "regular-custom-house.xml");
				writeHeaderName(handle, OTBM_ATTR_EXT_SPAWN_FILE, "regular-custom-spawn.xml"); },
			[](NodeFileWriteHandle& handle) {
				handle.addNode(OTBM_TILE_AREA);
				handle.addU16(100);
				handle.addU16(100);
				handle.addU8(7);
				handle.addNode(OTBM_HOUSETILE);
				handle.addU8(0);
				handle.addU8(0);
				handle.addU32(5);
				handle.endNode();
				handle.endNode();
			}
		);
		writeFile(directory / "regular-custom-house.xml", "<houses><house name='Named' houseid='5' entryx='0' entryy='0' entryz='0' townid='1'/></houses>");
		writeFile(directory / "regular-custom-zones.xml", "<zones><zone name='Arena' zoneid='3'><position x='100' y='100' z='7'/></zone></zones>");
		writeFile(directory / "regular-custom-spawn.xml", "<spawns><spawn centerx='100' centery='100' centerz='7' radius='1'><monster name='Rat' x='0' y='0' spawntime='30'/></spawn></spawns>");
		Map loaded;
		IOMapOTBM reader(MapVersion { MAP_OTBM_4, CLIENT_VERSION_860 });
		check(reader.loadMap(loaded, mapName(regular)), "a regular map should load");
		check(loaded.getHouseFilename() == "regular-custom-house.xml" && loaded.getZoneFilename() == "regular-custom-zones.xml" && loaded.getSpawnFilename() == "regular-custom-spawn.xml", "same-folder names must not change");
		check(loaded.houses.getHouse(5) && loaded.houses.getHouse(5)->name == "Named", "same-folder houses should load");
		const Tile* tile = loaded.getTile(100, 100, 7);
		check(tile && tile->hasZone(3) && tile->spawn, "same-folder zones and spawns should load");
	}

	std::string datBytes(uint16_t items, uint16_t creatures, std::initializer_list<int> layout, size_t spriteIds) {
		std::string bytes;
		const auto put = [&](uint64_t value, size_t size) {
			bytes.append(reinterpret_cast<const char*>(&value), size);
		};
		put(0x4C2C7993, 4); // The 8.60 signature, which has a pattern_z field.
		put(items, 2);
		put(creatures, 2);
		put(0, 4);
		bytes.push_back(static_cast<char>(0xFF)); // No flags.
		auto values = layout.begin();
		const int width = *values++;
		const int height = *values++;
		bytes.push_back(static_cast<char>(width));
		bytes.push_back(static_cast<char>(height));
		if (width > 1 || height > 1) {
			bytes.push_back(32);
		}
		for (; values != layout.end(); ++values) {
			bytes.push_back(static_cast<char>(*values));
		}
		bytes.append(spriteIds * 2, '\x01');
		return bytes;
	}

	void testMalformedClientFiles(const fs::path& root) {
		const fs::path directory = root / "client";
		fs::create_directories(directory);
		const fs::path dat = directory / "Tibia.dat";
		ClientVersion::loadVersions();
		g_gui.gfx.client_version = ClientVersion::get(CLIENT_VERSION_860);
		check(g_gui.gfx.client_version != nullptr, "the 8.60 test profile is unavailable");
		if (!g_gui.gfx.client_version) {
			return;
		}

		const auto load = [&](const std::string& bytes, wxString& error, wxArrayString& warnings) {
			g_gui.gfx.clear(false);
			writeFile(dat, bytes);
			return g_gui.gfx.loadSpriteMetadata(mapName(dat), error, warnings);
		};
		const auto refused = [&](const std::string& bytes, const std::string& message) {
			wxString error;
			wxArrayString warnings;
			check(!load(bytes, error, warnings) && !error.empty(), message);
		};

		// The dimensions dowody/dat/s.cpp found: 32 bit arithmetic gave 768 sprites.
		refused(datBytes(100, 0, { 20, 8, 137, 102, 137, 230, 62 }, 768), "a wrapped sprite count must be refused");
		refused(datBytes(100, 0, { 128, 128, 128, 128, 128, 1, 1 }, 0), "a sprite count wrapping to zero must be refused");
		refused(datBytes(100, 0, { 1, 1, 65, 64, 64, 1, 1 }, 266240), "a sprite count over the cap must be refused");
		refused(datBytes(100, 0, { 1, 1, 1, 2, 1, 1, 1 }, 1), "a sprite list cut short must be refused");
		refused(datBytes(100, 0, { 1, 1, 1, 1 }, 0), "a layout cut short must be refused");
		refused(datBytes(65535, 1, { 1, 1, 1, 1, 1, 1, 1 }, 1), "a truncated catalog past id 65535 must end");
		refused(datBytes(65500, 5000, { 1, 1, 1, 1, 1, 1, 1 }, 1), "a truncated catalog past id 65535 must end");
		refused("DAT", "a truncated header must be refused");

		wxString error;
		wxArrayString warnings;
		for (const std::initializer_list<int>& layout : { std::initializer_list<int> { 1, 1, 1, 1, 1, 1, 0 }, std::initializer_list<int> { 1, 1, 1, 0, 1, 1, 1 }, std::initializer_list<int> { 0, 1, 1, 1, 1, 1, 1 } }) {
			warnings.clear();
			check(load(datBytes(100, 0, layout, 0), error, warnings), "an empty layout should not fail the client");
			check(g_gui.gfx.getSprite(100) == nullptr && !warnings.empty(), "a sprite with a zero dimension must be skipped");
		}

		check(load(datBytes(100, 0, { 2, 1, 1, 1, 1, 1, 1 }, 2), error, warnings), "a valid sprite should load");
		auto* sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(100));
		check(sprite && sprite->numsprites == 2 && sprite->spriteList.size() == 2, "a valid sprite should keep its images");
		check(load(datBytes(100, 0, { 1, 1, 64, 64, 64, 1, 1 }, 262144), error, warnings), "a sprite at the cap should load");
		sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(100));
		check(sprite && sprite->spriteList.size() == 262144, "a sprite at the cap should keep its images");

		// The second image of a two layer sprite has no template behind it.
		check(load(datBytes(100, 0, { 1, 1, 2, 1, 1, 1, 1 }, 2), error, warnings), "a two layer sprite should load");
		sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(100));
		check(sprite != nullptr, "the two layer sprite is available");
		if (sprite) {
			const Outfit outfit;
			check(sprite->getHardwareID(1, 0, 0, 0, 0, outfit, 0) == 0, "a template past the sprite list has no texture");
		}

		GameSprite empty;
		const Outfit outfit;
		check(empty.getHardwareID(0, 0, 0, 0, 0, 0, 0, 0) == 0 && empty.getHardwareID(0, 0, 0, 0, 0, outfit, 0) == 0, "an empty sprite has no texture");
		check(empty.getSpriteTex(0, 0, 0, 0, 0, 0, 0, 0).texture == 0 && empty.getSpriteTex(0, 0, 0, 0, 0, outfit, 0).texture == 0, "an empty sprite has no texture coordinates");

		// One extended header sized the offset table at 16 GiB.
		g_gui.gfx.clear(false);
		const fs::path spr = directory / "Tibia.spr";
		writeFile(directory / "Tibia.otfi", "DatSpr\n  extended: true\n  transparency: false\n  frame-durations: false\n  frame-groups: false\n");
		check(g_gui.gfx.loadOTFI(FileName(wxString::FromUTF8(directory.string())), error, warnings), "the extended OTFI should load");
		writeFile(spr, std::string(4, '\0') + std::string(4, '\xFF'));
		error.clear();
		check(!g_gui.gfx.loadSpriteData(mapName(spr), error, warnings) && !error.empty(), "a sprite count the file cannot hold must be refused");
		std::string validSpr(4, '\0');
		const uint32_t validCount = 2;
		validSpr.append(reinterpret_cast<const char*>(&validCount), sizeof(validCount));
		validSpr.append(validCount * sizeof(uint32_t), '\0');
		writeFile(spr, validSpr);
		check(g_gui.gfx.loadSpriteData(mapName(spr), error, warnings), "a sprite file holding its table should load");

		// The decoder would allocate a declared 4 GiB dictionary before reading anything.
		std::string sheetFile(16, '\0');
		sheetFile += "\x70\x0A\xFA\x80\x24\x01\x5D\xFF\xFF\xFF\xFF";
		sheetFile.append(32, '\0');
		writeFile(directory / "sheet.bmp.lzma", sheetFile);
		ClientAssetsManifest manifest;
		manifest.spriteSheets.push_back({ directory / "sheet.bmp.lzma", 1, 144, 0 });
		SpriteAppearances appearances;
		check(appearances.loadCatalog(manifest, error, warnings), "the sheet catalog should load");
		std::vector<uint8_t> pixels;
		ClientSpriteSize size;
		error.clear();
		check(!appearances.getSpritePixels(1, pixels, size, error) && error.Contains("too much memory"), "a sheet declaring a huge dictionary must be refused before decoding");

		fs::remove(directory / "Tibia.otfi");
		g_gui.gfx.clear(false);
		g_gui.gfx.client_version = nullptr;
		ClientVersion::unloadVersions();
	}

	void testWriterMatchesRaisedHeader(const fs::path& root) {
		const fs::path directory = root / "m2";
		fs::create_directories(directory);
		const fs::path file = directory / "world.otbm";
		const Position position(122, 100, 7);
		{
			Fixture fixture;
			fixture.editor.map.convert({ MAP_OTBM_1, CLIENT_VERSION_860 }, false);
			fixture.editor.map.setStorageFormat(MapStorageFormat::CanaryCrystal);
			fixture.editor.map.setSpawnFormat(SpawnFormat::CanaryCrystal);
			fixture.editor.map.setItemIdSpace(ItemIdSpace::Client);
			Tile* tile = addTile(fixture.editor.map, position);
			tile->addItem(Item::Create(STACK_ID, 7));
			check(fixture.editor.saveMap(mapName(file), false), "fixture should save");
		}
		OTBMFileMetadata metadata;
		check(IOMapOTBM::getFileMetadata(mapName(file), metadata), "metadata should be readable");
		check(metadata.version.otbm == MAP_OTBM_5, "a Canary map must have an OTBM 5 header");
		Map loaded;
		IOMapOTBM reader(MapVersion { MAP_OTBM_5, CLIENT_VERSION_860 });
		// A Canary header names items.otb 4, newer than the fixture's database.
		g_gui.testDialogAnswer = wxID_YES;
		check(reader.loadMapData(loaded, mapName(file)), "fixture should load");
		g_gui.testDialogAnswer = wxID_NO;
		const Tile* tile = loaded.getTile(position);
		check(tile && !tile->items.empty() && tile->items.back()->getSubtype() == 7, "item counts must be encoded for the header version");
	}

	void testCaptureTakesStaleCreatures() {
		Fixture fixture;
		Map& map = fixture.editor.map;
		CreatureType* type = g_creatures["Rat"];
		if (!type) {
			type = g_creatures.addMissingCreatureType("Rat", false);
		}
		const auto addCreature = [&](const Position& position, const Position& source) {
			Tile* tile = addTile(map, position);
			tile->creature = new Creature(type);
			tile->creature->setSpawnSource(source);
		};
		for (const Position& center : { Position(200, 200, 7), Position(220, 200, 7) }) {
			Tile* tile = addTile(map, center);
			tile->spawn = new Spawn(3);
			map.addSpawn(tile);
		}
		addCreature(Position(221, 200, 7), Position(200, 200, 7)); // Dragged into the second spawn.
		addCreature(Position(222, 200, 7), Position(50, 50, 7)); // Its spawn was deleted.
		addCreature(Position(201, 200, 7), Position(200, 200, 7)); // Still where it belongs.
		addCreature(Position(300, 300, 7), Position(50, 50, 7)); // Not inside any spawn.

		const SpawnDocument document = SpawnMapAdapter::Capture(map);
		check(document.entryCount() == 3, "creatures with a stale source must be captured by the spawn they stand in");
		check(document.warnings.size() == 1 && document.warnings.front().rfind("1 creature", 0) == 0, "creatures left out must be counted in a warning");
	}

	void commitTile(Editor& editor, Tile* tile) {
		Action* action = editor.actionQueue->createAction(ACTION_DRAW);
		action->addChange(new Change(tile));
		editor.addAction(action);
	}

	void testUndoKeepsHouseMembership() {
		Fixture fixture;
		Editor& editor = fixture.editor;
		const Position position(100, 100, 7);

		Tile* cleared = editor.map.getTile(position)->deepCopy(editor.map);
		cleared->setHouseID(0);
		commitTile(editor, cleared);
		check(fixture.house->tileCount() == 0, "the cleared tile should leave the house");

		editor.map.clearChanges();
		check(editor.actionQueue->undo(), "undo should work");
		check(editor.map.hasChanged(), "undo must mark the map as changed");
		check(editor.map.getTile(position)->getHouseID() == 5, "undo must restore the tile's house");
		check(fixture.house->tileCount() == 1, "undo must restore the house's position list");

		editor.map.clearChanges();
		check(editor.actionQueue->redo(), "redo should work");
		check(editor.map.hasChanged(), "redo must mark the map as changed");
		check(editor.map.getTile(position)->getHouseID() == 0, "redo must clear the tile's house");
		check(editor.actionQueue->undo(), "second undo should work");
		check(editor.map.getTile(position)->getHouseID() == 5, "repeated undo must keep the house");

		const Position painted(101, 100, 7);
		Tile* base = addTile(editor.map, painted);
		Tile* house_tile = base->deepCopy(editor.map);
		house_tile->setHouseID(5);
		commitTile(editor, house_tile);
		check(editor.actionQueue->undo(), "undoing the paint should work");
		check(fixture.house->tileCount() == 1, "undoing the paint should remove the position");
		check(editor.actionQueue->redo(), "redoing the paint should work");
		check(editor.map.getTile(painted)->getHouseID() == 5, "redo must not lose the painted house");
		check(fixture.house->tileCount() == 2, "redo must restore the painted position");
	}

	void select(Editor& editor, Tile* tile) {
		editor.selection.start();
		editor.selection.add(tile);
		editor.selection.finish();
	}

	void testCutSurvivesItsMap() {
		CopyBuffer buffer;
		const Position position(150, 150, 7);
		{
			Editor source(buffer, nullptr);
			select(source, addTile(source.map, position));
			buffer.cut(source, 7);
		}
		check(buffer.canPaste(), "the cut tile should stay in the buffer");
		Editor target(buffer, nullptr);
		buffer.paste(target, Position(160, 160, 7));
		check(target.map.getTile(160, 160, 7) != nullptr, "the cut tile must paste after its map is gone");
	}

	void testMergeKeepsZonesAndFlags() {
		Fixture fixture;
		Map& map = fixture.editor.map;
		Tile* target = addTile(map, Position(170, 170, 7));
		Tile* source = addTile(map, Position(171, 170, 7));
		source->setMapFlags(TILESTATE_NOPVP);
		source->addZone(7);
		target->merge(source);
		check((target->getMapFlags() & TILESTATE_NOPVP) != 0, "merge must keep map flags");
		check(target->hasZone(7), "merge must keep zones");
	}

	void testPasteRemapsZonesByName() {
		CopyBuffer buffer;
		Editor source(buffer, nullptr);
		Editor target(buffer, nullptr);
		check(source.map.zones.addZone("Arena", 3), "source zone");
		check(source.map.zones.addZone("Fresh", 4), "second source zone");
		check(target.map.zones.addZone("Arena", 9), "target zone");
		check(target.map.zones.addZone("Other", 4), "target zone using the second source id");
		Tile* tile = addTile(source.map, Position(180, 180, 7));
		tile->addZone(3);
		tile->addZone(4);
		buffer.copyRegion(source, Position(180, 180, 7), Position(180, 180, 7));
		buffer.paste(target, Position(190, 190, 7));
		const Tile* pasted = target.map.getTile(190, 190, 7);
		check(pasted && pasted->hasZone(9) && !pasted->hasZone(3), "a known zone must be matched by name");
		check(pasted && target.map.zones.hasZone("Fresh") && pasted->hasZone(target.map.zones.getZoneID("Fresh")), "an unknown zone must be created in the target");
		check(pasted && !pasted->hasZone(4), "a pasted tile must not land in an unrelated zone");
		check(target.actionQueue->undo(), "paste undo should work");
		check(!target.map.zones.hasZone("Fresh"), "undo must remove the zone the paste created");
	}

	void testPasteKeepsTileAtDependentSpawnCenter() {
		CopyBuffer buffer;
		Editor editor(buffer, nullptr);
		const Position center(250, 250, 7);
		Tile* centerTile = addTile(editor.map, center);
		centerTile->spawn = new Spawn(3);
		editor.map.addSpawn(centerTile);
		Tile* creatureTile = addTile(editor.map, Position(251, 250, 7));
		// The global creature database outlives each test, so an earlier test may have added it.
		CreatureType* rat = g_creatures["Rat"];
		creatureTile->creature = new Creature(rat ? rat : g_creatures.addMissingCreatureType("Rat", false));
		creatureTile->creature->setSpawnSource(center);

		editor.selection.start();
		editor.selection.add(centerTile, centerTile->ground);
		editor.selection.add(creatureTile, creatureTile->ground);
		editor.selection.add(creatureTile, creatureTile->creature);
		editor.selection.finish();
		buffer.copy(editor, 7);
		const int mergePaste = g_settings.getInteger(Config::MERGE_PASTE);
		g_settings.setInteger(Config::MERGE_PASTE, 0);
		buffer.paste(editor, Position(300, 300, 7));
		g_settings.setInteger(Config::MERGE_PASTE, mergePaste);

		const Tile* pastedCenter = editor.map.getTile(300, 300, 7);
		check(pastedCenter && pastedCenter->ground, "the dependent spawn must not erase the pasted tile");
		check(pastedCenter && pastedCenter->spawn, "the dependent spawn must be created");
	}

	void testImport(const fs::path& root) {
		const fs::path directory = root / "import";
		fs::create_directories(directory);
		const fs::path file = directory / "import.otbm";
		const Position inside(100, 100, 7);
		const Position outside(450, 100, 7);
		{
			Fixture fixture;
			fixture.editor.map.setWidth(1024);
			Tile* tile = addTile(fixture.editor.map, outside);
			tile->spawn = new Spawn(3);
			fixture.editor.map.addSpawn(tile);
			check(fixture.editor.saveMap(mapName(file), false), "import fixture should save");
		}

		CopyBuffer buffer;
		Editor editor(buffer, nullptr);
		editor.map.setWidth(256);
		editor.map.setHeight(256);
		addTile(editor.map, Position(10, 10, 7));
		commitTile(editor, editor.map.getTile(10, 10, 7)->deepCopy(editor.map));

		const fs::path bad = directory / "bad.otbm";
		writeFile(bad, "not a map");
		check(!editor.importMap(mapName(bad), 0, 0, 0, IMPORT_DONT, IMPORT_DONT), "a broken import should fail");
		check(editor.actionQueue->canUndo(), "a failed import must keep the undo history");

		editor.map.clearChanges();
		// wxID_NO answers the resize question, so the far tile and its spawn go.
		check(editor.importMap(mapName(file), 0, 0, 0, IMPORT_DONT, IMPORT_MERGE), "the import should work");
		g_gui.testDialogAnswer = wxID_NO;
		check(editor.map.hasChanged(), "an import must mark the map as changed");
		const Tile* imported = editor.map.getTile(inside);
		check(imported && imported->getHouseID() == 0, "tiles of houses that were not imported must not keep foreign IDs");
		check(editor.map.spawns.find(outside) == editor.map.spawns.end(), "a spawn on a discarded tile must not be registered");
		check(editor.map.getTile(outside) == nullptr || editor.map.getTile(outside)->spawn == nullptr, "no tile may hold the discarded spawn");
		const SpawnDocument document = SpawnMapAdapter::Capture(editor.map);
		check(document.areas.empty(), "saving after the import must find no freed spawn");
	}

	void testConvertedSaveAsksAboutUnmappedItems(const fs::path& root) {
		const fs::path directory = root / "m3";
		fs::create_directories(directory);
		const fs::path file = directory / "world.otbm";
		Fixture fixture;
		fixture.editor.map.setStorageFormat(MapStorageFormat::CanaryCrystal);
		fixture.editor.map.setSpawnFormat(SpawnFormat::CanaryCrystal);
		fixture.editor.map.setItemIdSpace(ItemIdSpace::Server);
		addTile(fixture.editor.map, Position(101, 100, 7))->addItem(Item::Create(40000));

		g_gui.testDialogAnswer = wxID_NO;
		check(!fixture.editor.saveMap(mapName(file), false), "refusing the conversion warning must stop the save");
		check(!fs::exists(file), "a refused save must not write the map");
		g_gui.testDialogAnswer = wxID_YES;
		check(fixture.editor.saveMap(mapName(file), false), "accepting the conversion warning should save");
		g_gui.testDialogAnswer = wxID_NO;
		check(fixture.editor.saveMap(mapName(file), false), "already accepted items must not ask again");
		addTile(fixture.editor.map, Position(102, 100, 7))->addItem(Item::Create(40001));
		check(!fixture.editor.saveMap(mapName(file), false), "new unmapped items must ask again");
		g_gui.testDialogAnswer = wxID_NO;
	}
} // namespace

int RunMultiplayerSessionTests(int argc, char** argv) {
	if (!wxEntryStart(argc, argv)) {
		return 1;
	}
	wxTheApp->SetExitOnFrameDelete(false);
	wxLog::SetActiveTarget(new wxLogStderr());
	wxInitAllImageHandlers();
	wxArtProvider::Push(new ArtProvider());

	const fs::path root = fs::temp_directory_path() / ("nexamap-data-safety-" + std::to_string(wxGetProcessId()));
	fs::remove_all(root);
	fs::create_directories(root);
	{
		wxEventLoop loop;
		wxEventLoopActivator activate(&loop);
		// Stays hidden. Import needs it to parent its progress dialog.
		g_gui.root = new MainFrame("Data safety tests", wxDefaultPosition, wxSize(800, 600));
		defineItems();
		g_gui.testDialogAnswer = wxID_NO;
		testFailedSaveLeavesFilesAlone(root);
		testSaveKeepsWaypointFileAndSymlinks(root);
		testDatedBackups(root);
		testSaveAsGetsOwnSidecars(root);
		testUnloadedSidecarsAreProtected(root);
		testSidecarCaseMismatch(root);
		testTruncatedMapsAreRefused(root);
		testBackupUsesStorageHeader(root);
		testUnexpectedChildrenAndPodiumsSurvive(root);
		testWrittenAttributesSurvive(root);
		testHouseExitOnFloorZero(root);
		testBadTileAreaFloorIsRefused(root);
		testFloorAccessIsBounded();
		testBadPositionsAreDropped(root);
		testTeleportFloorIsValidated(root);
		testPodiumAttributesCannotChangeItsClass(root);
		testContainerNestingIsBounded(root);
		testSidecarNamesStayInMapFolder(root);
		testMalformedClientFiles(root);
		testWriterMatchesRaisedHeader(root);
		testCaptureTakesStaleCreatures();
		testUndoKeepsHouseMembership();
		testCutSurvivesItsMap();
		testMergeKeepsZonesAndFlags();
		testPasteRemapsZonesByName();
		testPasteKeepsTileAtDependentSpawnCenter();
		testImport(root);
		testConvertedSaveAsksAboutUnmappedItems(root);
		g_gui.aui_manager->UnInit();
		delete g_gui.aui_manager;
		g_gui.aui_manager = nullptr;
		delete g_gui.root;
		g_gui.root = nullptr;
		g_gui.tabbook = nullptr;
	}
	wxEntryCleanup();
	fs::remove_all(root);

	if (failures != 0) {
		std::cerr << failures << " data safety check(s) failed.\n";
		return 1;
	}
	std::cout << "All data safety tests passed.\n";
	return 0;
}
