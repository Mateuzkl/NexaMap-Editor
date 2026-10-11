// SPDX-License-Identifier: GPL-3.0-or-later
// Hidden native controls and synthetic map definitions. Never loads user data.
#include "main.h"
#include "common.h"
#include "action.h"
#include "editor.h"
#include "editor_resource_session.h"
#include "copybuffer.h"
#include "cross_client_clipboard.h"
#include "complexitem.h"
#include "filehandle.h"
#include "graphics.h"
#include "gui.h"
#include "gui_ids.h"
#include "iomap_otbm.h"
#include "otbm_item_attribute_parser.h"
#include "map_window.h"
#include "map_display.h"
#include "map_drawer.h"
#include "map_diagnostics_scanner.h"
#include "properties_window.h"
#include "numbertextctrl.h"
#include "ingame_preview/ingame_preview_window.h"
#include "ingame_preview/playtest_map.h"
#include "ingame_preview/playtest_weather.h"
#include "theme.h"
#include <wx/evtloop.h>
#include <wx/weakref.h>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

namespace {
	class ItemRoundTripFormat final : public IOMap {
	public:
		explicit ItemRoundTripFormat(MapVersionID version) {
			this->version = { version, CLIENT_VERSION_1100 };
		}
		bool loadMap(Map&, const FileName&) override {
			return false;
		}
		bool saveMap(Map&, const FileName&) override {
			return false;
		}
	};

	class TrackingItem final : public Item {
	public:
		explicit TrackingItem(std::atomic<int>& destructions) :
			Item(100, 1), destructions(destructions) { }
		~TrackingItem() override {
			++destructions;
		}

	private:
		std::atomic<int>& destructions;
	};
}

class PlaytestIntegrationTests {
	static void check(bool value, const char* message) {
		if (!value) {
			throw std::runtime_error(message);
		}
	}
	struct Definitions {
		ItemDatabase saved;
		Definitions() {
			g_items.swap(saved);
		}
		~Definitions() {
			g_items.clear();
			g_items.swap(saved);
		}
		ItemType& add(uint16_t id) {
			auto* type = new ItemType();
			type->id = id;
			g_items.items.set(id, type);
			return *type;
		}
	};
	static Tile* ground(Map& map, Position at) {
		auto* tile = map.allocator(map.createTileL(at));
		tile->addItem(Item::Create(100));
		map.setTile(at, tile);
		return tile;
	}
	static std::unique_ptr<Item> roundTripItem(const Item& source, MapVersionID version) {
		ItemRoundTripFormat format(version);
		MemoryNodeFileWriteHandle writer;
		check(source.serializeItemNode_OTBM(format, writer) && writer.error_code == FILE_NO_ERROR, "Serialize OTBM item node");
		std::vector<uint8_t> bytes(writer.getData(), writer.getData() + writer.getSize());
		MemoryNodeFileReadHandle reader(bytes.data(), bytes.size());
		BinaryNode* root = reader.getRootNode();
		uint8_t nodeType = 0;
		check(root && root->getU8(nodeType) && nodeType == OTBM_ITEM, "Read serialized OTBM item node");
		std::unique_ptr<Item> loaded(Item::Create_OTBM(format, root, nullptr, true));
		check(loaded && loaded->unserializeItemNode_OTBM(format, root), "Round-trip OTBM item attributes");
		return loaded;
	}
	static Teleport* findTeleport(Tile* tile) {
		if (!tile) {
			return nullptr;
		}
		if (auto* teleport = dynamic_cast<Teleport*>(tile->ground)) {
			return teleport;
		}
		for (Item* item : tile->items) {
			if (auto* teleport = dynamic_cast<Teleport*>(item)) {
				return teleport;
			}
		}
		return nullptr;
	}
	static void teleportPersistence() {
		g_settings.setDefaults();
		g_settings.setInteger(Config::USE_AUTOMAGIC, 0);
		g_settings.setInteger(Config::BORDERIZE_PASTE, 0);
		g_settings.setInteger(Config::BORDERIZE_DRAG, 0);
		g_settings.setInteger(Config::MERGE_PASTE, 1);

		Definitions definitions;
		definitions.add(100).group = ITEM_GROUP_GROUND;
		constexpr uint16_t CanaryTeleportId = 36973;
		constexpr uint16_t CrystalTeleportId = 25050;
		constexpr uint16_t ExistingFallbackId = 36975;
		constexpr uint16_t TfsTeleportId = 1387;
		constexpr uint16_t DoorId = 36976;
		constexpr uint16_t DepotId = 36977;
		constexpr uint16_t BedId = 36978;
		constexpr uint16_t GenericId = 36979;
		for (uint16_t id : { CanaryTeleportId, CrystalTeleportId, ExistingFallbackId, TfsTeleportId, DoorId, DepotId, BedId, GenericId }) {
			definitions.add(id);
		}
		g_items.MajorVersion = 4;
		g_items.MinorVersion = 4;

		pugi::xml_document metadata;
		static constexpr char metadataXml[] = "<items>"
											  "<item id='36973'><attribute key='type' value='teleport'/></item>"
											  "<item id='25050'><attribute key='primarytype' value='teleporters'/><attribute key='type' value='teleport'/></item>"
											  "<item id='1387'><attribute key='type' value='teleport'/></item>"
											  "<item id='36976'><attribute key='type' value='door'/></item>"
											  "<item id='36977'><attribute key='type' value='depot'/></item>"
											  "<item id='36978'><attribute key='type' value='bed'/></item>"
											  "</items>";
		const auto parsed = metadata.load_buffer(metadataXml, sizeof(metadataXml) - 1);
		check(static_cast<bool>(parsed), "Parse ClientID-native item metadata fixture");
		for (pugi::xml_node node = metadata.child("items").first_child(); node; node = node.next_sibling()) {
			const uint16_t clientId = node.attribute("id").as_ushort();
			check(g_items.loadItemFromGameXml(node, clientId, false), "Apply logical metadata to the same ClientID ItemType");
		}

		check(g_items[CanaryTeleportId].isTeleport() && g_items[CrystalTeleportId].isTeleport(), "Canary and Crystal metadata classify ClientIDs as teleports");
		std::unique_ptr<Item> canaryNew(Item::Create(CanaryTeleportId));
		std::unique_ptr<Item> crystalNew(Item::Create(CrystalTeleportId));
		auto* canaryTeleport = dynamic_cast<Teleport*>(canaryNew.get());
		auto* crystalTeleport = dynamic_cast<Teleport*>(crystalNew.get());
		check(canaryTeleport && crystalTeleport, "New Canary and Crystal items are Teleport objects before TELE_DEST exists");
		check(canaryTeleport->getDestination() == Position() && crystalTeleport->getDestination() == Position(), "New teleports expose an initially empty destination");

		canaryTeleport->setDestination({ 1500, 1600, 7 });
		canaryNew = roundTripItem(*canaryTeleport, MAP_OTBM_5);
		canaryTeleport = dynamic_cast<Teleport*>(canaryNew.get());
		check(canaryTeleport && canaryTeleport->getDestination() == Position(1500, 1600, 7), "New Canary teleport persists X/Y/Z");
		canaryTeleport->setDestination({ 2000, 1600, 7 });
		canaryNew = roundTripItem(*canaryTeleport, MAP_OTBM_5);
		canaryTeleport = dynamic_cast<Teleport*>(canaryNew.get());
		check(canaryTeleport && canaryTeleport->getDestination() == Position(2000, 1600, 7), "Changing only teleport X persists");
		canaryTeleport->setDestination({ 2000, 2001, 7 });
		canaryNew = roundTripItem(*canaryTeleport, MAP_OTBM_5);
		canaryTeleport = dynamic_cast<Teleport*>(canaryNew.get());
		check(canaryTeleport && canaryTeleport->getDestination() == Position(2000, 2001, 7), "Changing only teleport Y persists");
		canaryTeleport->setDestination({ 2000, 2001, 6 });
		canaryNew = roundTripItem(*canaryTeleport, MAP_OTBM_5);
		canaryTeleport = dynamic_cast<Teleport*>(canaryNew.get());
		check(canaryTeleport && canaryTeleport->getDestination() == Position(2000, 2001, 6), "Changing only teleport Z persists");

		Teleport fallbackSource(ExistingFallbackId);
		fallbackSource.setDestination({ 1000, 1001, 7 });
		auto fallbackLoaded = roundTripItem(fallbackSource, MAP_OTBM_5);
		auto* fallbackTeleport = dynamic_cast<Teleport*>(fallbackLoaded.get());
		check(!g_items[ExistingFallbackId].isTeleport() && fallbackTeleport && fallbackTeleport->getDestination() == Position(1000, 1001, 7), "Existing Canary/Crystal TELE_DEST upgrades incomplete metadata to Teleport");

		crystalTeleport->setDestination({ 1700, 1701, 5 });
		crystalNew = roundTripItem(*crystalTeleport, MAP_OTBM_5);
		crystalTeleport = dynamic_cast<Teleport*>(crystalNew.get());
		check(crystalTeleport && crystalTeleport->getDestination() == Position(1700, 1701, 5), "Crystal teleport round-trip preserves destination");

		{
			Map propertiesMap;
			propertiesMap.convert({ MAP_OTBM_5, CLIENT_VERSION_1100 });
			propertiesMap.setWidth(65000);
			propertiesMap.setHeight(65000);
			wxFrame propertiesParent(nullptr, wxID_ANY, "Hidden teleport properties validation");
			PropertiesWindow properties(&propertiesParent, &propertiesMap, nullptr, crystalTeleport);
			auto* destinationX = wxDynamicCast(properties.FindWindow(ITEM_PROPERTIES_TELEPORT_X), NumberTextCtrl);
			auto* destinationY = wxDynamicCast(properties.FindWindow(ITEM_PROPERTIES_TELEPORT_Y), NumberTextCtrl);
			auto* destinationZ = wxDynamicCast(properties.FindWindow(ITEM_PROPERTIES_TELEPORT_Z), NumberTextCtrl);
			check(destinationX && destinationY && destinationZ, "Tabbed Properties window exposes teleport Destination X/Y/Z");
			check(destinationX->GetIntValue() == 1700 && destinationY->GetIntValue() == 1701 && destinationZ->GetIntValue() == 5, "Properties window loads the existing teleport destination");
			destinationX->SetIntValue(1800);
			destinationY->SetIntValue(1801);
			destinationZ->SetIntValue(6);
			check(properties.TransferDataFromWindow(), "Properties window accepts the edited teleport destination");
			check(crystalTeleport->getDestination() == Position(1800, 1801, 6), "Properties window saves Destination X/Y/Z to the Teleport object");
		}
		crystalNew = roundTripItem(*crystalTeleport, MAP_OTBM_5);
		crystalTeleport = dynamic_cast<Teleport*>(crystalNew.get());
		check(crystalTeleport && crystalTeleport->getDestination() == Position(1800, 1801, 6), "Properties-edited destination survives OTBM save/reopen");

		std::unique_ptr<Item> tfs(Item::Create(TfsTeleportId));
		auto* tfsTeleport = dynamic_cast<Teleport*>(tfs.get());
		check(tfsTeleport != nullptr, "Legacy TFS metadata still creates Teleport");
		tfsTeleport->setDestination({ 320, 321, 7 });
		tfs = roundTripItem(*tfsTeleport, MAP_OTBM_4);
		tfsTeleport = dynamic_cast<Teleport*>(tfs.get());
		check(tfsTeleport && tfsTeleport->getDestination() == Position(320, 321, 7), "Legacy TFS TELE_DEST round-trip is unchanged");

		std::unique_ptr<Item> door(Item::Create(DoorId));
		std::unique_ptr<Item> depot(Item::Create(DepotId));
		std::unique_ptr<Item> bed(Item::Create(BedId));
		std::unique_ptr<Item> generic(Item::Create(GenericId));
		check(dynamic_cast<Door*>(door.get()) != nullptr, "Door metadata remains Door");
		check(dynamic_cast<Depot*>(depot.get()) != nullptr, "Depot metadata remains Depot");
		check(g_items[BedId].isBed() && dynamic_cast<Teleport*>(bed.get()) == nullptr, "Bed metadata remains Bed without becoming Teleport");
		check(dynamic_cast<Teleport*>(generic.get()) == nullptr && dynamic_cast<Door*>(generic.get()) == nullptr && dynamic_cast<Depot*>(generic.get()) == nullptr, "Generic item without special evidence stays generic");

		std::unique_ptr<Item> copied(canaryTeleport->deepCopy());
		auto* copiedTeleport = dynamic_cast<Teleport*>(copied.get());
		check(copiedTeleport && copiedTeleport->getDestination() == canaryTeleport->getDestination(), "Teleport deep copy preserves dynamic type and destination");

		{
			CopyBuffer copyBuffer;
			Editor pasteEditor(copyBuffer, nullptr);
			auto* sourceTile = ground(pasteEditor.map, { 100, 100, 7 });
			Item* sourceItem = Item::Create(CanaryTeleportId);
			auto* sourceTeleport = dynamic_cast<Teleport*>(sourceItem);
			check(sourceTeleport != nullptr, "Create teleport for copy/paste fixture");
			sourceTeleport->setDestination({ 1500, 1600, 7 });
			sourceTeleport->select();
			sourceTile->addItem(sourceTeleport);
			sourceTile->update();
			pasteEditor.selection.addInternal(sourceTile);
			copyBuffer.copy(pasteEditor, 7);
			Teleport* copiedBufferTeleport = findTeleport(copyBuffer.getBufferMap().getTile({ 100, 100, 7 }));
			check(copiedBufferTeleport && copiedBufferTeleport->getDestination() == Position(1500, 1600, 7), "Copy buffer preserves teleport destination");
		}

		Map sourceMap;
		sourceMap.convert({ MAP_OTBM_5, CLIENT_VERSION_1100 });
		sourceMap.setStorageFormat(MapStorageFormat::CanaryCrystal);
		sourceMap.setItemIdSpace(ItemIdSpace::Client);
		auto* persistedTile = ground(sourceMap, { 400, 400, 7 });
		auto* persistedTeleport = dynamic_cast<Teleport*>(Item::Create(CanaryTeleportId));
		persistedTeleport->setDestination({ 2000, 2001, 6 });
		persistedTile->addItem(persistedTeleport);
		const std::filesystem::path mapPath = std::filesystem::temp_directory_path() / ("nexamap-teleport-" + std::to_string(sourceMap.getSessionId()) + ".otbm");
		std::error_code removeError;
		std::filesystem::remove(mapPath, removeError);
		IOMapOTBM saver(sourceMap.getVersion());
		check(saver.saveMapData(sourceMap, FileName(wxstr(mapPath.string()))), "Save Canary/Crystal OTBM teleport map");
		Map reopenedMap;
		IOMapOTBM loader({ MAP_OTBM_5, CLIENT_VERSION_1100 });
		check(loader.loadMapData(reopenedMap, FileName(wxstr(mapPath.string()))), "Reopen Canary/Crystal OTBM teleport map");
		Teleport* reopenedTeleport = findTeleport(reopenedMap.getTile({ 400, 400, 7 }));
		check(reopenedTeleport && reopenedTeleport->getDestination() == Position(2000, 2001, 6), "Full save/close/reopen preserves teleport class and destination");
		std::filesystem::remove(mapPath, removeError);

		std::cout << "PASS Canary/Crystal teleport metadata, fallback, UI type, copy buffer and OTBM round-trip\n";
	}
	static void positionParsing() {
		for (const std::string& text : {
				 "1530, 1540, 7",
				 "(1530, 1540, 7)",
				 "Position(1530, 1540, 7)",
				 "{x = 1530, y = 1540, z = 7}",
				 "{\"x\":1530,\"y\":1540,\"z\":7}" }) {
			Position position;
			check(parsePositionText(text, position) && position == Position(1530, 1540, 7), "Parse a supported single-position clipboard format");
		}
		Position position;
		check(!parsePositionText("{fromx = 100, tox = 110, fromy = 200, toy = 210, z = 7}", position), "Reject an ambiguous multi-selection range");
		check(!parsePositionText("1530, 1540, 7, 99", position), "Reject extra position components");
		check(!parsePositionText("Position(70000, 1540, 7)", position), "Reject an out-of-map position");
		std::cout << "PASS exact teleport clipboard formats and multi-selection rejection\n";
	}
	static void writableItemProperties() {
		Definitions definitions;
		definitions.add(100).group = ITEM_GROUP_GROUND;
		constexpr uint16_t SignId = 2016;
		constexpr uint16_t PlaqueId = 2017;
		constexpr uint16_t BlackboardId = 2018;
		constexpr uint16_t BookId = 401;
		constexpr uint16_t DocumentId = 402;
		constexpr uint16_t ReadOnlyId = 403;
		for (const uint16_t id : { SignId, PlaqueId, BlackboardId, BookId, DocumentId, ReadOnlyId }) {
			auto& type = definitions.add(id);
			type.name = "writable fixture";
			type.canReadText = true;
			type.canWriteText = id != ReadOnlyId;
			type.maxTextLen = id == BookId ? 1023 : 255;
		}
		pugi::xml_document itemMetadata;
		static constexpr char itemMetadataXml[] = "<items>"
												  "<item id='2016' name='sign'><attribute key='allowDistRead' value='1'/></item>"
												  "<item id='401' name='book'><attribute key='writeable' value='1'/><attribute key='maxtextlen' value='1023'/></item>"
												  "</items>";
		check(itemMetadata.load_buffer(itemMetadataXml, sizeof(itemMetadataXml) - 1), "Parse writable ClientID metadata fixture");
		for (pugi::xml_node node = itemMetadata.child("items").first_child(); node; node = node.next_sibling()) {
			check(g_items.loadItemFromGameXml(node, node.attribute("id").as_ushort(), false), "Merge writable metadata into the same ClientID ItemType");
		}
		check(g_items[SignId].canReadText && g_items[SignId].canWriteText && g_items[SignId].allowDistRead, "Sign preserves appearances.dat write flags while items.xml adds distance reading");
		check(g_items[BookId].canWriteText && g_items[BookId].maxTextLen == 1023, "Book items.xml writeable/maxTextLen metadata is active");

		Map map;
		map.convert({ MAP_OTBM_5, CLIENT_VERSION_1100 });
		map.setWidth(65000);
		map.setHeight(65000);
		wxFrame parent(nullptr, wxID_ANY, "Hidden writable properties validation");
		for (const uint16_t id : { SignId, PlaqueId, BlackboardId, BookId, DocumentId }) {
			std::unique_ptr<Item> item(Item::Create(id));
			PropertiesWindow properties(&parent, &map, nullptr, item.get());
			auto* text = wxDynamicCast(properties.FindWindow(ITEM_PROPERTIES_TEXT), wxTextCtrl);
			check(text && text->IsEditable(), "Writable sign/plaque/blackboard/book/document exposes editable Text");
		}

		std::unique_ptr<Item> sign(Item::Create(SignId));
		sign->setText("existing text");
		sign->setDescription("existing description");
		sign->setAttribute("custom-editor-field", 42);
		{
			PropertiesWindow properties(&parent, &map, nullptr, sign.get());
			auto* text = wxDynamicCast(properties.FindWindow(ITEM_PROPERTIES_TEXT), wxTextCtrl);
			auto* description = wxDynamicCast(properties.FindWindow(ITEM_PROPERTIES_DESCRIPTION), wxTextCtrl);
			auto* action = wxDynamicCast(properties.FindWindow(ITEM_PROPERTIES_ACTION_ID), wxSpinCtrl);
			auto* unique = wxDynamicCast(properties.FindWindow(ITEM_PROPERTIES_UNIQUE_ID), wxSpinCtrl);
			check(text && description && action && unique, "Simple tab exposes Text, separate Description, Action ID and Unique ID");
			check(text->GetValue() == "existing text" && description->GetValue() == "existing description", "Simple tab loads existing Text and Description independently");
			text->SetValue(wxString::FromUTF8("ação UTF-8"));
			description->SetValue(wxString::FromUTF8("descrição separada"));
			action->SetValue(4500);
			unique->SetValue(5001);
			check(properties.TransferDataFromWindow(), "Accept valid writable fields and IDs");
		}
		check(sign->getText() == "ação UTF-8" && sign->getDescription() == "descrição separada", "Text and Description save without mixing and preserve UTF-8");
		check(sign->getActionID() == 4500 && sign->getUniqueID() == 5001, "Action ID and Unique ID persist from the modern window");
		check(sign->getIntegerAttribute("custom-editor-field") && *sign->getIntegerAttribute("custom-editor-field") == 42, "Advanced attributes survive saving Simple fields");

		auto reopened = roundTripItem(*sign, MAP_OTBM_5);
		check(reopened->getText() == sign->getText() && reopened->getDescription() == sign->getDescription(), "Text and Description survive Canary OTBM save/reopen");
		check(reopened->getActionID() == 4500 && reopened->getUniqueID() == 5001, "Action and Unique IDs survive Canary OTBM save/reopen");
		auto reopenedCrystal = roundTripItem(*sign, MAP_OTBM_6);
		check(reopenedCrystal->getText() == sign->getText() && reopenedCrystal->getDescription() == sign->getDescription(), "Text and Description survive Crystal OTBM 6 save/reopen");

		std::unique_ptr<Item> copy(sign->deepCopy());
		check(copy->getText() == sign->getText() && copy->getDescription() == sign->getDescription(), "Item deep copy preserves Text and Description");
		std::unique_ptr<Item> readOnly(Item::Create(ReadOnlyId));
		readOnly->setText("server text");
		{
			PropertiesWindow properties(&parent, &map, nullptr, readOnly.get());
			auto* text = wxDynamicCast(properties.FindWindow(ITEM_PROPERTIES_TEXT), wxTextCtrl);
			check(text && !text->IsEditable() && text->GetValue() == "server text", "Readable non-writable item displays protected text");
			check(properties.TransferDataFromWindow(), "Read-only Properties window can close safely");
		}
		check(readOnly->getText() == "server text", "Read-only text is not destroyed on OK");

		const wxString utf8 = wxString::FromUTF8("ação");
		check(PropertiesWindow::IsTextLengthValid(utf8, nstr(utf8).size()), "UTF-8 limit counts serialized bytes exactly");
		check(!PropertiesWindow::IsTextLengthValid(utf8, nstr(utf8).size() - 1), "Text over maxTextLen is rejected without truncation");
		check(PropertiesWindow::IsTextLengthValid(wxString(), 1), "Empty writable text is valid");
		check(PropertiesWindow::IsTextLengthValid(wxString(65534, 'x'), 0) && !PropertiesWindow::IsTextLengthValid(wxString(65535, 'x'), 0), "OTBM string length remains below 65535 bytes");
		std::cout << "PASS writable item UI, metadata behavior, validation, IDs and OTBM round-trip\n";
	}
	static void modernOtbmPreservation() {
		Definitions definitions;
		constexpr uint16_t GenericId = 510;
		constexpr uint16_t PodiumId = 511;
		definitions.add(GenericId);
		definitions.add(PodiumId).type = ITEM_TYPE_PODIUM;

		auto verifyOpaqueRoundTrip = [&](MapVersionID version) {
			ItemRoundTripFormat format(version);
			MemoryNodeFileWriteHandle originalWriter;
			originalWriter.addNode(OTBM_ITEM);
			originalWriter.addU16(GenericId);
			originalWriter.addU8(OTBMItemAttributeParser::WRITTEN_BY);
			originalWriter.addString("author");
			originalWriter.addU8(OTBMItemAttributeParser::NAME);
			originalWriter.addString("custom name");
			originalWriter.addU8(OTBMItemAttributeParser::CUSTOM_ATTRIBUTES);
			originalWriter.addU64(1);
			originalWriter.addString("unknown");
			originalWriter.addU8(1);
			originalWriter.addString("opaque value");
			originalWriter.addU8(OTBMItemAttributeParser::OWNER);
			originalWriter.addU32(0x12345678);
			originalWriter.addU8(OTBMItemAttributeParser::MANTRA);
			originalWriter.addU32(0x89ABCDEF);
			originalWriter.endNode();
			const std::vector<uint8_t> original(originalWriter.getData(), originalWriter.getData() + originalWriter.getSize());
			MemoryNodeFileReadHandle reader(original.data(), original.size());
			BinaryNode* root = reader.getRootNode();
			uint8_t nodeType = 0;
			check(root && root->getU8(nodeType) && nodeType == OTBM_ITEM, "Read modern opaque attribute fixture");
			std::unique_ptr<Item> item(Item::Create_OTBM(format, root, nullptr, true));
			check(item && item->unserializeItemNode_OTBM(format, root), "Load modern opaque attributes");
			MemoryNodeFileWriteHandle saved;
			check(item->serializeItemNode_OTBM(format, saved), "Save modern opaque attributes");
			const std::vector<uint8_t> result(saved.getData(), saved.getData() + saved.getSize());
			check(result == original, "Modern non-editable attributes survive byte-for-byte, including MANTRA 45");
		};
		verifyOpaqueRoundTrip(MAP_OTBM_5);
		verifyOpaqueRoundTrip(MAP_OTBM_6);

		std::unique_ptr<Item> podiumItem(Item::Create(PodiumId));
		auto* podium = dynamic_cast<Podium*>(podiumItem.get());
		check(podium != nullptr, "Create modern Podium fixture");
		Outfit outfit;
		outfit.lookType = 128;
		outfit.lookHead = 10;
		outfit.lookBody = 20;
		outfit.lookLegs = 30;
		outfit.lookFeet = 40;
		outfit.lookAddon = 3;
		outfit.lookMount = 426;
		outfit.lookMountHead = 50;
		outfit.lookMountBody = 60;
		outfit.lookMountLegs = 70;
		outfit.lookMountFeet = 80;
		podium->setOutfit(outfit);
		podium->setDirection(2);
		podium->setShowOutfit(true);
		podium->setShowMount(true);
		podium->setShowPlatform(false);
		for (MapVersionID version : { MAP_OTBM_5, MAP_OTBM_6 }) {
			auto loaded = roundTripItem(*podium, version);
			auto* reopened = dynamic_cast<Podium*>(loaded.get());
			check(reopened && reopened->getOutfit() == outfit, "Modern Podium preserves every outfit and mount field");
			check(reopened->getDirection() == 2 && reopened->hasShowOutfit() && reopened->hasShowMount() && !reopened->hasShowPlatform(), "Modern Podium preserves visibility flags and direction");
		}
		std::cout << "PASS OTBM 5/6 opaque modern attributes, MANTRA 45 and Podium custom state\n";
	}
	static void mapAdapter() {
		Definitions definitions;
		definitions.add(100).group = ITEM_GROUP_GROUND;
		auto& closed = definitions.add(101);
		closed.type = ITEM_TYPE_DOOR;
		closed.unpassable = true;
		closed.rotateTo = 102;
		auto& open = definitions.add(102);
		open.type = ITEM_TYPE_DOOR;
		open.isOpen = true;
		open.rotateTo = 101;
		definitions.add(103).type = ITEM_TYPE_TELEPORT;
		auto& stairs = definitions.add(104);
		stairs.floorChangeNorth = true;
		stairs.unpassable = true;
		definitions.add(105).name = "rope spot";
		definitions.add(106).name = "ladder";
		definitions.add(107).unpassable = true;
		Map map;
		ground(map, { 100, 100, 7 });
		auto* doorTile = ground(map, { 101, 100, 7 });
		auto* door = Item::Create(101);
		door->setActionID(4500);
		door->setUniqueID(5001);
		doorTile->addItem(door);
		auto* teleportTile = ground(map, { 100, 101, 7 });
		auto* teleport = dynamic_cast<Teleport*>(Item::Create(103));
		check(teleport != nullptr, "Teleport factory fixture");
		teleport->setDestination({ 110, 110, 7 });
		teleportTile->addItem(teleport);
		ground(map, { 110, 110, 7 });
		ground(map, { 99, 100, 7 })->addItem(Item::Create(104));
		ground(map, { 99, 99, 6 });
		ground(map, { 100, 99, 7 })->addItem(Item::Create(105));
		ground(map, { 100, 100, 6 });
		ground(map, { 102, 100, 7 })->addItem(Item::Create(106));
		ground(map, { 102, 101, 6 });
		const auto revision = map.getChunkRevisionTracker().getStats().contentChanges;
		Playtest::MapWorld world(map);
		Playtest::Controller player;
		player.reset({ 100, 100, 7 });
		check(!player.move(world, Playtest::Facing::East), "Closed map door blocks walking");
		check(player.use(world, Position(101, 100, 7)), "Use paired map door");
		check(player.move(world, Playtest::Facing::East), "Walk through local door override");
		check(door->getID() == 101 && door->getActionID() == 4500 && door->getUniqueID() == 5001, "Playtest mutated map door/properties");
		player.reset({ 100, 100, 7 });
		check(player.move(world, Playtest::Facing::South) && player.position() == Position(110, 110, 7), "Real teleport destination");
		player.reset({ 100, 100, 7 });
		check(player.move(world, Playtest::Facing::West) && player.position() == Position(99, 99, 6), "Real floor-change flags");
		player.reset({ 100, 100, 7 });
		check(player.use(world, Position(100, 99, 7)) && player.position() == Position(100, 100, 6), "Metadata rope spot");
		player.reset({ 101, 100, 7 });
		check(player.use(world, Position(102, 100, 7)) && player.position() == Position(102, 101, 6), "Metadata ladder");
		check(map.getChunkRevisionTracker().getStats().contentChanges == revision, "Read-only interactions changed render revisions");
		doorTile->addItem(Item::Create(107));
		player.reset({ 100, 100, 7 });
		check(player.use(world, Position(101, 100, 7)) && !player.move(world, Playtest::Facing::East), "Opening door must not bypass another blocking item");
		{
			Definitions secondDefinitions;
			secondDefinitions.add(100).group = ITEM_GROUP_GROUND;
			secondDefinitions.add(101).unpassable = true;
			Map independent;
			ground(independent, { 100, 100, 7 });
			ground(independent, { 101, 100, 7 })->addItem(Item::Create(101));
			Playtest::MapWorld secondWorld(independent);
			player.reset({ 100, 100, 7 });
			check(!player.move(secondWorld, Playtest::Facing::East) && !player.use(secondWorld, Position(101, 100, 7)), "Same numeric ID in another resource set reused an old door definition");
		}
		std::cout << "PASS actual Item/Tile adapter: door, teleport, stairs, rope, ladder, no map mutation\n";
	}
	static void pump() {
		wxTheApp->ProcessPendingEvents();
		wxTheApp->ProcessIdle();
	}
	static void lifecycle() {
		g_settings.setDefaults();
		CopyBuffer buffer;
		Editor first(buffer, nullptr), second(buffer, nullptr);
		const auto previous = GetActiveEditorResourceSession();
		auto sessionA = CreateEditorResourceSession(), sessionB = CreateEditorResourceSession();
		for (auto theme : { Theme::Type::System, Theme::Type::Dark, Theme::Type::Light }) {
			Theme::SetType(theme);
			for (int exitKey : { WXK_ESCAPE, WXK_F6 }) {
				wxFrame frame(nullptr, wxID_ANY, "Hidden playtest lifecycle");
				auto* panel = new IngamePreviewWindow(&frame);
				wxWeakRef<IngamePreviewWindow> weak(panel);
				g_gui.ingame_preview = panel;
				panel->SetSize(panel->FromDIP(wxSize(740, 680)));
				SetActiveEditorResourceSession(sessionA);
				panel->SyncEditor(&first);
				check(panel->previewView && panel->editor == &first, "Initial map binding");
				wxWeakRef<MapWindow> oldView(panel->previewView);
				panel->input.press(Playtest::Facing::East);
				SetActiveEditorResourceSession(sessionB);
				panel->SyncEditor(&second);
				check(!oldView && panel->editor == &second && !panel->input.direction() && panel->controller.doorOverrides().empty(), "Map/session switch retained canvas/input/doors");
				for (const auto size : { wxSize(400, 450), wxSize(740, 680) }) {
					panel->SetSize(panel->FromDIP(size));
					panel->Layout();
					auto* view = panel->previewView;
					check(view->GetCanvas()->GetClientSize().x > 0 && view->GetCanvas()->GetClientSize().y > 0, "Invalid responsive canvas size");
					for (auto* child : panel->GetChildren()) {
						check(child->GetRect().GetBottom() < panel->GetClientSize().y && child->GetRect().GetRight() < panel->GetClientSize().x, "Playtest controls extend beyond the panel");
					}
					view->GetCanvas()->SetIngamePreviewLighting(true);
					check(view->GetCanvas()->ingamePreviewLighting, "Lighting on");
					view->GetCanvas()->SetIngamePreviewLighting(false);
					check(!view->GetCanvas()->ingamePreviewLighting, "Lighting off");
					for (double zoom : { 0.5, 1.0, 2.0 }) {
						view->GetCanvas()->SetZoom(zoom);
						view->SetScreenCenterPositionInterpolated({ 100, 100, 6 }, 0, 0);
						// The camera centers the tile's top-left corner. With an odd
						// viewport, that boundary can fall between two screen pixels;
						// hit-test the inside of the player's tile, not its boundary.
						auto* canvas = view->GetCanvas();
						int x = 0, y = 0;
						const int inset = static_cast<int>(16 / (zoom * canvas->GetContentScaleFactor()));
						canvas->ScreenToMap(canvas->GetClientSize().x / 2 + inset, canvas->GetClientSize().y / 2 + inset, &x, &y);
						check(x == 100 && y == 100 && canvas->GetFloor() == 6, "Preview camera/hit test uses the wrong zoom or floor");
					}
				}
				wxWeakRef<MapWindow> released(panel->previewView);
				panel->ReleaseEditor(&second);
				check(!released && !panel->previewView && !panel->editor, "Closing tab must synchronously release its canvas");
				panel->SyncEditor(&first);
				wxWeakRef<MapWindow> closing(panel->previewView);
				wxKeyEvent key(wxEVT_CHAR_HOOK);
				key.m_keyCode = exitKey;
				panel->GetEventHandler()->ProcessEvent(key);
				pump();
				pump();
				check(!g_gui.ingame_preview && !weak && !closing, "Escape/F6 failed to destroy playtest and its canvas");
			}
		}
		SetActiveEditorResourceSession(previous);
		Theme::SetType(Theme::Type::System);
		std::cout << "PASS hidden native lifecycle: 3 themes, resize, camera/hit test, session switch, tab close, F6/ESC\n";
	}
	static void weatherGL() {
		CopyBuffer buffer;
		Editor editor(buffer, nullptr);
		wxFrame frame(nullptr, wxID_ANY, "Hidden weather renderer");
		auto preview = std::make_unique<MapWindow>(&frame, editor, true);
		const int attributes[] = { WX_GL_RGBA, WX_GL_DOUBLEBUFFER, 0 };
		auto* canvas = new wxGLCanvas(&frame, wxID_ANY, attributes, wxDefaultPosition, wxSize(320, 240));
		wxGLContext context(canvas);
		check(context.IsOK() && canvas->SetCurrent(context), "Weather GL context");
		GLRenderer renderer;
		renderer.init();
		renderer.ensureFBO(320, 240);
		check(renderer.hasFBO(), "Weather GL framebuffer");
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		auto capture = [&](Playtest::Weather weather, double time) {
			renderer.beginFrame();
			renderer.beginFBO();
			glViewport(0, 0, 320, 240);
			glClearColor(0.1f, 0.2f, 0.3f, 1);
			glClear(GL_COLOR_BUFFER_BIT);
			renderer.setOrtho(0, 640, 480, 0);
			renderer.drawColoredQuad(40, 40, 200, 200, { 20, 100, 70, 255 });
			renderer.flush();
			renderer.setOrtho(0, 320, 240, 0);
			Playtest::DrawWeather(renderer, Playtest::BuildWeather(weather, 320, 240, time, 1.5f));
			renderer.flush();
			renderer.setOrtho(0, 640, 480, 0);
			renderer.drawColoredQuad(0, 0, 16, 16, { 255, 0, 0, 255 });
			renderer.endFrame();
			std::vector<uint8_t> pixels(320 * 240 * 4);
			glReadPixels(0, 0, 320, 240, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			renderer.endFBO();
			check(glGetError() == GL_NO_ERROR, "Weather affected GL state");
			check(renderer.getFrameStats().drawCalls <= 3, "Weather breaks batching");
			return pixels;
		};
		const auto baseline = capture(Playtest::Weather::Off, 0);
		for (auto weather : { Playtest::Weather::Rain, Playtest::Weather::Storm, Playtest::Weather::Fog, Playtest::Weather::Snow, Playtest::Weather::DesertHeat }) {
			const auto first = capture(weather, 0);
			check(first != baseline && first != capture(weather, 2), "Weather missing or not animated");
			check(capture(Playtest::Weather::Off, 0) == baseline, "Weather OFF leaves stale framebuffer/state");
		}
		check(capture(Playtest::Weather::Storm, 6.2) != capture(Playtest::Weather::Storm, 6.0), "Lightning pulse missing in OpenGL");
		std::cout << "PASS OpenGL weather: five animated modes, lightning, OFF pixel equivalence, <=1 weather draw call; " << glGetString(GL_RENDERER) << '\n';
		// Reproduce the map's QTree painter order with solid ground and an
		// avatar footprint. Check every avatar pixel throughout all four steps,
		// including crossing 4x4 chunks, using the production draw-tile selector.
		auto& playerCanvas = *preview->GetCanvas();
		for (int origin : { 99, 100, 103, 104 }) {
			for (auto facing : { Playtest::Facing::North, Playtest::Facing::East, Playtest::Facing::South, Playtest::Facing::West }) {
				const Position delta = Playtest::Controller::offset(facing);
				const Position target = Position(origin, origin, 7) + delta;
				for (int remaining : { 32, 24, 16, 8, 0 }) {
					const int offsetX = -delta.x * remaining, offsetY = -delta.y * remaining;
					playerCanvas.SetIngamePreviewPlayer(target, static_cast<Direction>(facing), offsetX, offsetY, 1);
					const Position drawTile = playerCanvas.GetIngamePreviewDrawTile();
					const int playerX = (target.x - 96) * 16 + offsetX / 2;
					const int playerY = (target.y - 96) * 16 + offsetY / 2;
					renderer.beginFrame();
					renderer.beginFBO();
					glViewport(0, 0, 320, 240);
					renderer.setOrtho(0, 320, 240, 0);
					glClear(GL_COLOR_BUFFER_BIT);
					int submissions = 0;
					for (int chunkX = 96; chunkX < 112; chunkX += 4) {
						for (int chunkY = 96; chunkY < 112; chunkY += 4) {
							for (int x = chunkX; x < chunkX + 4; ++x) {
								for (int y = chunkY; y < chunkY + 4; ++y) {
									renderer.drawColoredQuad((x - 96) * 16, (y - 96) * 16, 16, 16, { 25, 70, 30, 255 });
									if (Position(x, y, 7) == drawTile) {
										++submissions;
										renderer.drawColoredQuad(playerX, playerY, 16, 16, { 255, 0, 220, 255 });
									}
								}
							}
						}
					}
					renderer.endFrame();
					std::array<uint8_t, 16 * 16 * 4> pixels;
					glReadPixels(playerX, 240 - playerY - 16, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
					renderer.endFBO();
					check(submissions == 1 && glGetError() == GL_NO_ERROR, "Avatar submitted twice or GL failure");
					for (size_t i = 0; i < pixels.size(); i += 4) {
						check(pixels[i] == 255 && pixels[i + 1] == 0 && pixels[i + 2] == 220, "Ground erased the walking avatar (north/west regression)");
					}
				}
			}
		}
		std::cout << "PASS OpenGL avatar coverage: 80 frames, four directions, QTree chunk boundaries, one submission per frame\n";
	}

public:
	static void rendererLifecycle() {
		g_settings.setDefaults();
		CopyBuffer buffer;
		Editor editor(buffer, nullptr);
		wxFrame frame(nullptr, wxID_ANY, "Hidden frame teardown validation");
		auto view = std::make_unique<MapWindow>(&frame, editor, true);
		auto* canvas = view->GetCanvas();
		check(canvas->SetCurrent(*g_gui.GetGLContext(canvas)), "Map canvas GL context");
		for (int cycle = 0; cycle < 100; ++cycle) {
			GLint projection = 0, modelview = 0;
			glGetIntegerv(GL_PROJECTION_STACK_DEPTH, &projection);
			glGetIntegerv(GL_MODELVIEW_STACK_DEPTH, &modelview);
			auto drawer = std::make_unique<MapDrawer>(canvas);
			if (cycle % 3 != 0) {
				drawer->SetupVars();
				drawer->SetupGL();
				drawer->getRenderer()->ensureFBO(64 + cycle, 64 + cycle);
				if (cycle % 3 == 1) {
					drawer->Release();
					drawer->Release();
				}
			}
			drawer.reset(); // Never begun, already ended, or interrupted frame.
			GLint finalProjection = 0, finalModelview = 0;
			glGetIntegerv(GL_PROJECTION_STACK_DEPTH, &finalProjection);
			glGetIntegerv(GL_MODELVIEW_STACK_DEPTH, &finalModelview);
			check(glGetError() == GL_NO_ERROR, "MapDrawer teardown produced an OpenGL error");
			check(projection == finalProjection && modelview == finalModelview, "MapDrawer unbalanced matrix stacks");
		}
		std::cout << "PASS 100 MapDrawer lifecycles: unpainted, repeated Release, interrupted frame, resized FBO\n";
	}
	static void clipboardSessionLifetime() {
		Definitions definitions;
		definitions.add(100).group = ITEM_GROUP_GROUND;
		CopyBuffer source;
		auto map = std::make_unique<BaseMap>();
		auto* tile = map->allocator(map->createTileL({ 100, 100, 7 }));
		tile->addItem(Item::Create(100));
		map->setTile(tile);
		source.replace(std::move(map), { 100, 100, 7 });
		auto session = CreateEditorResourceSession();
		std::weak_ptr<EditorResourceSession> released = session;
		CrossClientClipboard clipboard;
		wxString error;
		check(clipboard.capture(source, session, error), "Cross-client clipboard capture");
		session.reset();
		check(released.expired() && clipboard.canPaste(), "Clipboard retained the complete source resource session");
		std::cout << "PASS cross-client clipboard releases closed source resource session\n";
	}
	static void sharedTabOwnership() {
		const bool wasClosing = g_gui.IsApplicationClosing();
		struct RestoreClosingState {
			bool value;
			~RestoreClosingState() {
				g_gui.SetApplicationClosing(value);
			}
		} restore { wasClosing };
		g_gui.SetApplicationClosing(true);
		Definitions definitions;
		definitions.add(100).group = ITEM_GROUP_GROUND;
		CopyBuffer buffer;
		std::atomic<int> destructions = 0;
		wxFrame frame(nullptr, wxID_ANY, "Hidden shared tab ownership");
		auto* tabs = new MapTabbook(&frame, wxID_ANY);
		for (int cycle = 0; cycle < 100; ++cycle) {
			auto editor = std::make_unique<Editor>(buffer, nullptr);
			auto* tile = editor->map.allocator(editor->map.createTileL({ 100, 100, 7 }));
			tile->addItem(new TrackingItem(destructions));
			editor->map.setTile(tile);
			auto* first = new MapTab(tabs, std::move(editor));
			auto* second = new MapTab(first);
			check(!first->IsUniqueReference(), "Multiple map views did not share ownership");
			if (cycle % 2 == 0) {
				tabs->DeleteTab(0);
				check(second->IsUniqueReference(), "Closing the first view corrupted shared ownership");
			} else {
				tabs->DeleteTab(1);
				check(first->IsUniqueReference(), "Closing the second view corrupted shared ownership");
			}
			check(destructions.load() <= cycle, "Closing a non-final view destroyed the shared editor");
			tabs->DeleteTab(0);
		}
		g_gui.DrainEditorDisposals();
		check(destructions.load() == 100, "Final views did not drain editor disposal exactly once");
		std::cout << "PASS 100 shared MapTab lifecycles and deterministic disposal drain\n";
	}
	static void visualPreviewMemoryBound() {
		GameSprite sprite;
		sprite.width = std::numeric_limits<uint8_t>::max();
		sprite.height = std::numeric_limits<uint8_t>::max();
		sprite.layers = 1;
		std::vector<uint8_t> pixels(4, 0xFF);
		int width = 0;
		int height = 0;
		bool pending = false;
		check(!sprite.getVisualPreviewRGBA(pixels, width, height, pending, false) && pixels.empty(), "Oversized sprite preview was allowed to allocate hundreds of megabytes");
		std::cout << "PASS oversized visual preview allocation is bounded\n";
	}
	static void diagnosticsReset() {
		Definitions definitions;
		definitions.add(100).group = ITEM_GROUP_GROUND;
		Map map;
		ground(map, { 100, 100, 7 })->ground->setUniqueID(5001);
		ground(map, { 101, 100, 7 })->ground->setUniqueID(5001);
		MapDiagnosticsScanner scanner(map);
		scanner.Start();
		while (scanner.IsRunning()) {
			scanner.Step(64);
		}
		check(scanner.IsComplete() && !scanner.GetIssues().empty(), "Diagnostics fixture did not retain scan results");
		scanner.Reset();
		check(!scanner.IsRunning() && !scanner.IsComplete() && scanner.GetIssues().empty() && scanner.GetProcessedTileCount() == 0 && scanner.GetTotalTileCount() == 0, "Diagnostics reset retained results or iterators");
		std::cout << "PASS diagnostics reset releases results and iterator state\n";
	}
	static void run() {
		teleportPersistence();
		positionParsing();
		writableItemProperties();
		modernOtbmPreservation();
		mapAdapter();
		clipboardSessionLifetime();
		sharedTabOwnership();
		visualPreviewMemoryBound();
		diagnosticsReset();
		lifecycle();
		weatherGL();
	}
};
void RunPlaytestIntegrationTests() {
	PlaytestIntegrationTests::run();
}
void RunRendererLifecycleTests() {
	PlaytestIntegrationTests::rendererLifecycle();
}
