// SPDX-License-Identifier: GPL-3.0-or-later
#include "main.h"
#include "action.h"
#include "complexitem.h"
#include "copybuffer.h"
#include "cross_client_clipboard.h"
#include "cross_client_test_sprite.h"
#include "editor.h"
#include "editor_resource_session.h"
#include "gui.h"
#include "items.h"
#include "iomap_otbm.h"
#include "workspace_session.h"
#include <iostream>
#include <cstdlib>

namespace {
	void check(bool value, const char* message) {
		if (!value) {
			throw std::runtime_error(message);
		}
	}
	void activate(const EditorResourceSessionPtr& next) {
		auto current = GetActiveEditorResourceSession();
		if (current == next) {
			return;
		}
		current->swapWithGlobals();
		next->swapWithGlobals();
		SetActiveEditorResourceSession(next);
	}
	struct Fixture {
		EditorResourceSessionPtr previous = GetActiveEditorResourceSession();
		EditorResourceSessionPtr source = CreateEditorResourceSession();
		EditorResourceSessionPtr target = CreateEditorResourceSession();
		std::vector<std::unique_ptr<CrossClientTestSprite>> sprites;
		Fixture() {
			activate(source);
		}
		~Fixture() {
			activate(previous);
		}
		ItemType& define(uint16_t id, uint8_t colour, uint8_t patterns = 1, uint8_t frames = 1) {
			sprites.push_back(std::make_unique<CrossClientTestSprite>(colour, patterns, frames));
			auto* type = new ItemType();
			type->id = type->clientID = id;
			type->name = "same name";
			type->sprite = sprites.back().get();
			g_items.items.set(id, type);
			return *type;
		}
		void buffer(CopyBuffer& buffer, uint16_t id) {
			auto map = std::make_unique<BaseMap>();
			auto* tile = map->allocator(map->createTileL({ 100, 100, 7 }));
			tile->addItem(Item::Create(id));
			map->setTile(tile);
			buffer.replace(std::move(map), { 100, 100, 7 });
		}
	};
	void fullVariantsAndAmbiguity() {
		Fixture f;
		auto& original = f.define(100, 45, 2, 2);
		CopyBuffer buffer, converted;
		f.buffer(buffer, 100);
		CrossClientClipboard clipboard;
		wxString error;
		check(clipboard.capture(buffer, f.source, error), "Capture actual source pixels");
		activate(f.target);
		f.define(100, 200, 2, 2); // Same SID, wrong graphics must never win.
		auto& correct = f.define(210, 45, 2, 2);
		correct.sprite->minimap_color = 25;
		correct.sprite->ground_speed = 80;
		auto analysis = clipboard.analyze(f.target, {}, error);
		check(analysis.canApply() && analysis.remapped == 1 && analysis.rows[0].destinationId == 210, "Unique full-variant match must remap automatically despite irrelevant DAT metadata");
		check(clipboard.apply(analysis, converted, error), "Apply exact mapping");
		check(converted.getBufferMap().getTile({ 100, 100, 7 })->items.front()->getID() == 210, "Output ID is from target");
		check(buffer.getBufferMap().getTile({ 100, 100, 7 })->items.front()->getID() == 100, "Source buffer was modified");
		// Pixel 33 is not on the old 16x16 sampling grid; frame/pattern zero stays identical.
		auto* sprite = static_cast<CrossClientTestSprite*>(correct.sprite);
		sprite->change(3, 33, 99);
		analysis = clipboard.analyze(f.target, {}, error);
		check(!analysis.canApply() && !analysis.rows[0].recommendations.empty(), "Later frame/pattern mismatch must require review");
		check(!CrossClientClipboard::hasSafeAutomaticMapping(analysis.rows[0]), "Identical first frame cannot enable automatic mapping");
		sprite->change(3, 33, 45);
		sprite->change(0, 33, 99);
		analysis = clipboard.analyze(f.target, {}, error);
		check(!analysis.canApply(), "A pixel skipped by preview sampling cannot prove equality");
		sprite->change(0, 33, 45);
		f.define(211, 45, 2, 2);
		analysis = clipboard.analyze(f.target, {}, error);
		check(!analysis.canApply() && analysis.rows[0].recommendations.size() == 2, "Duplicate identical targets cannot be resolved by numeric ID/name");
		check(!CrossClientClipboard::hasSafeAutomaticMapping(analysis.rows[0]), "Ambiguous exact aliases are not safe automatic suggestions");
		check(CrossClientClipboard::resolveMapping(analysis, 0, 211, error) && clipboard.apply(analysis, converted, error), "Explicit ambiguity resolution remains available");
		// Closing the source does not invalidate the frozen pixels/map.
		f.source.reset();
		analysis = clipboard.analyze(f.target, {}, error);
		check(analysis.valid && !analysis.rows.empty(), "Closed source lost its clipboard snapshot");
		(void)original;
		std::cout << "PASS complete frames/patterns, ID collision, odd pixels, metadata differences, ambiguity, closed source\n";
	}
	void renderedFingerprintCoverage() {
		CrossClientTestSprite source(80, 4, 2), target(80, 4, 2);
		source.pattern_x = target.pattern_x = 1;
		source.pattern_y = target.pattern_y = 2;
		source.pattern_z = target.pattern_z = 2;
		const auto equal = [&] {
			SpriteVisualFingerprint a, b;
			bool pending = false;
			return source.getRenderedFingerprint(a, pending, false)
				&& target.getRenderedFingerprint(b, pending, false) && a == b;
		};
		check(equal(), "Identical multidimensional patterns differ");
		target.change(7, 33, 91);
		check(!equal(), "Later Y/Z pattern or frame not compared");
		target.change(7, 33, 80);
		target.drawoffset_x = 8;
		check(!equal(), "Sprite displacement not compared");
		target.drawoffset_x = 0;
		source.animator = new Animator(2, -1, 0, true);
		target.animator = new Animator(2, -1, 0, true);
		for (int i = 0; i < 2; ++i) {
			source.animator->getFrameDuration(i)->setValues(100, 100);
			target.animator->getFrameDuration(i)->setValues(100, 100);
		}
		check(equal(), "Identical animation timing differs");
		target.animator->getFrameDuration(1)->setValues(200, 200);
		check(!equal(), "Animation timing difference not compared");
		std::cout << "PASS multidimensional patterns, draw offsets and animation timing\n";
	}
	void cancellationAndStaleResources() {
		Fixture f;
		f.define(100, 40);
		CopyBuffer source, destination;
		f.buffer(source, 100);
		CrossClientClipboard clipboard;
		wxString error;
		check(clipboard.capture(source, f.source, error), "Capture cancellation fixture");
		activate(f.target);
		f.define(200, 40);
		f.buffer(destination, 200);
		auto cancelled = clipboard.analyze(
			f.target, [](size_t, size_t) { return false; }, error
		);
		check(!cancelled.valid && !error.empty() && !clipboard.apply(cancelled, destination, error), "Cancelled analysis became applicable");
		auto analysis = clipboard.analyze(f.target, {}, error);
		check(analysis.canApply(), "Stale test initial match");
		g_workspace.setPersistenceEnabled(false);
		g_workspace.setItemIdModePreference(g_workspace.getItemIdModePreference());
		check(!clipboard.apply(analysis, destination, error), "Reloaded destination accepted an old mapping");
		analysis = clipboard.analyze(f.target, {}, error);
		g_gui.gfx.clear(false);
		check(!clipboard.apply(analysis, destination, error), "Reloaded graphics accepted an old mapping");
		analysis = clipboard.analyze(
			f.target, [&](size_t, size_t) { activate(f.previous); return true; }, error
		);
		check(!analysis.valid && !error.empty(), "Progress callback tab switch was ignored");
		activate(f.target);
		analysis = clipboard.analyze(f.target, {}, error);
		clipboard.clear();
		check(!clipboard.apply(analysis, destination, error), "Changed clipboard accepted an old mapping");
		check(destination.getBufferMap().getTile({ 100, 100, 7 })->items.front()->getID() == 200, "Failed apply changed the destination buffer");
		std::cout << "PASS cancellation, destination reload, tab switch, generation checks and atomic failure\n";
	}
	void incompatibleRoles() {
		Fixture f;
		auto& original = f.define(100, 60);
		original.type = ITEM_TYPE_DOOR;
		original.isOpen = true;
		CopyBuffer source, target;
		f.buffer(source, 100);
		CrossClientClipboard clipboard;
		wxString error;
		check(clipboard.capture(source, f.source, error), "Capture door");
		activate(f.target);
		auto& closed = f.define(200, 60);
		closed.type = ITEM_TYPE_DOOR;
		auto analysis = clipboard.analyze(f.target, {}, error);
		check(!analysis.canApply() && !CrossClientClipboard::resolveMapping(analysis, 0, 200, error), "Same pixels changed open/closed door semantics");
		closed.isOpen = true;
		closed.isLocked = true;
		analysis = clipboard.analyze(f.target, {}, error);
		check(!analysis.canApply(), "Same pixels changed locked door semantics");
		closed.isLocked = false;
		analysis = clipboard.analyze(f.target, {}, error);
		check(analysis.canApply() && clipboard.apply(analysis, target, error), "Compatible door failed to resolve");
		CrossClientPasteRow row;
		row.recommendations = {
			{ 200, 200, "named", 100, true, true, 1.0, true },
			{ 201, 201, "other", 85, false, true, 0.7, false },
			{ 202, 202, "", 90, false, true, 1.0, true }
		};
		check(!CrossClientClipboard::hasSafeAutomaticMapping(row), "Competitors after second recommendation were ignored");
		std::cout << "PASS door state/lock compatibility and all competing suggestions\n";
	}
	void areaAttributesUndoRedo() {
		Fixture f;
		f.define(100, 10).group = ITEM_GROUP_GROUND;
		f.define(101, 20).type = ITEM_TYPE_CONTAINER;
		f.define(102, 30).stackable = true;
		f.define(103, 40).type = ITEM_TYPE_TELEPORT;
		f.define(104, 50).type = ITEM_TYPE_DOOR;
		f.define(105, 60).type = ITEM_TYPE_DEPOT;
		CopyBuffer sourceBuffer, targetBuffer;
		Editor source(sourceBuffer, nullptr);
		auto* tile = source.map.allocator(source.map.createTileL({ 100, 100, 7 }));
		tile->addItem(Item::Create(100));
		auto* container = dynamic_cast<Container*>(Item::Create(101));
		check(container, "Container factory");
		auto* stack = Item::Create(102, 17);
		stack->setActionID(4500);
		stack->setUniqueID(6001);
		stack->setText("clipboard text");
		container->getVector().push_back(stack);
		tile->addItem(container);
		auto* teleport = dynamic_cast<Teleport*>(Item::Create(103));
		teleport->setDestination({ 900, 901, 7 });
		tile->addItem(teleport);
		auto* door = dynamic_cast<Door*>(Item::Create(104));
		door->setDoorID(7);
		tile->addItem(door);
		auto* depot = dynamic_cast<Depot*>(Item::Create(105));
		depot->setDepotID(12);
		tile->addItem(depot);
		auto* house = new House(source.map);
		house->setID(8);
		house->name = "Original house";
		house->rent = 123;
		house->townid = 3;
		source.map.houses.addHouse(house);
		source.map.setTile(tile);
		tile->setHouse(house);
		house->setExit({ 101, 100, 7 });
		sourceBuffer.copyRegion(source, { 100, 100, 7 }, { 100, 100, 7 });
		CrossClientClipboard clipboard;
		wxString error;
		check(clipboard.capture(sourceBuffer, f.source, error), "Capture nested area");
		activate(f.target);
		// 100->200 and 101->100 verifies simultaneous remap, not cascading replacements.
		f.define(200, 10).group = ITEM_GROUP_GROUND;
		f.define(100, 20).type = ITEM_TYPE_CONTAINER;
		f.define(202, 30).stackable = true;
		f.define(203, 40).type = ITEM_TYPE_TELEPORT;
		f.define(204, 50).type = ITEM_TYPE_DOOR;
		f.define(205, 60).type = ITEM_TYPE_DEPOT;
		auto analysis = clipboard.analyze(f.target, {}, error);
		check(analysis.canApply() && analysis.remapped == 6, "All nested area IDs must resolve automatically");
		check(clipboard.apply(analysis, targetBuffer, error), "Convert nested area");
		Editor target(targetBuffer, nullptr);
		auto* existingHouse = new House(target.map);
		existingHouse->setID(8);
		existingHouse->name = "Destination house";
		target.map.houses.addHouse(existingHouse);
		targetBuffer.paste(target, { 400, 400, 7 });
		const auto inspect = [&] {
			auto* pasted = target.map.getTile({ 400, 400, 7 });
			check(pasted && pasted->ground && pasted->ground->getID() == 200, "Ground or paste anchor lost");
			auto* c = dynamic_cast<Container*>(pasted->items.at(0));
			check(c && c->getID() == 100 && c->getItemCount() == 1, "Container/nested count lost");
			auto* s = c->getItem(0);
			check(s->getID() == 202 && s->getSubtype() == 17 && s->getActionID() == 4500 && s->getUniqueID() == 6001 && s->getText() == "clipboard text", "Nested item attributes lost");
			check(dynamic_cast<Teleport*>(pasted->items.at(1))->getDestination() == Position(900, 901, 7), "Teleport destination changed");
			check(dynamic_cast<Door*>(pasted->items.at(2))->getDoorID() == 7, "House door ID changed");
			check(dynamic_cast<Depot*>(pasted->items.at(3))->getDepotID() == 12, "Depot ID changed");
			auto* h = target.map.houses.getHouse(pasted->getHouseID());
			check(h && h->name == "Original house" && h->rent == 123 && h->townid == 3 && h->getExit() == Position(401, 400, 7), "House metadata was lost during cross-client paste");
			check(h->getID() != 8 && target.map.houses.getHouse(8)->name == "Destination house", "House ID collision changed an existing house");
		};
		inspect();
		check(target.actionQueue->undo(), "Paste undo failed");
		check(!target.map.getTile({ 400, 400, 7 }) || target.map.getTile({ 400, 400, 7 })->empty(), "Undo retained pasted objects");
		check(target.actionQueue->redo(), "Paste redo failed");
		inspect();
		check(clipboard.apply(analysis, targetBuffer, error), "Repeated paste corrupted frozen source IDs");
		const wxString temporary = wxFileName::CreateTempFileName("nexamap-cross-client-");
		struct RemoveTemporary {
			wxString path;
			~RemoveTemporary() {
				wxRemoveFile(path);
			}
		} cleanup { temporary };
		IOMapOTBM writer(target.map.getVersion());
		check(writer.saveMapData(target.map, FileName(temporary)), "Converted area could not be saved as OTBM");
		Map reloaded;
		IOMapOTBM reader(target.map.getVersion());
		check(reader.loadMapData(reloaded, FileName(temporary)), "Converted OTBM could not be reopened");
		auto* savedTile = reloaded.getTile({ 400, 400, 7 });
		check(savedTile && savedTile->ground->getID() == 200, "OTBM reload lost target ground ID");
		auto* savedContainer = dynamic_cast<Container*>(savedTile->items.front());
		check(savedContainer && savedContainer->getID() == 100 && savedContainer->getItem(0)->getID() == 202 && savedContainer->getItem(0)->getSubtype() == 17, "OTBM round trip lost converted nested IDs/count");
		std::cout << "PASS actual area paste, simultaneous IDs, container contents/attributes, teleport, door, depot, houses, Undo/Redo\n";
	}
	void realClassicAssets() {
		// Optional local integration fixture; all assets are read-only and paths stay outside the repository.
		const char* sourceClient = std::getenv("NEXAMAP_TEST_SOURCE_CLIENT");
		const char* sourceOtb = std::getenv("NEXAMAP_TEST_SOURCE_OTB");
		const char* targetClient = std::getenv("NEXAMAP_TEST_TARGET_CLIENT");
		const char* targetOtb = std::getenv("NEXAMAP_TEST_TARGET_OTB");
		if (!sourceClient || !sourceOtb || !targetClient || !targetOtb) {
			return;
		}
		Fixture f;
		g_settings.setInteger(Config::CHECK_SIGNATURES, 0);
		g_settings.setInteger(Config::USE_MEMCACHED_SPRITES, 1);
		const auto load = [&](const char* client, const char* otb) {
			wxString error;
			wxArrayString warnings;
			g_gui.gfx.client_version = ClientVersion::get(CLIENT_VERSION_860);
			const wxString root = wxString::FromUTF8(client);
			check(g_gui.gfx.loadOTFI(FileName(root), error, warnings), "Real client OTFI load");
			check(g_gui.gfx.loadSpriteMetadata(FileName(root + "/Tibia.dat"), error, warnings), "Real client DAT load");
			check(g_gui.gfx.loadSpriteData(FileName(root + "/Tibia.spr"), error, warnings), "Real client SPR load");
			check(g_items.loadFromOtb(FileName(wxString::FromUTF8(otb)), error, warnings), "Real server OTB load");
		};
		const auto makeBuffer = [&](CopyBuffer& buffer, const std::vector<uint16_t>& ids) {
			auto map = std::make_unique<BaseMap>();
			for (size_t i = 0; i < ids.size(); ++i) {
				auto* tile = map->allocator(map->createTileL({ 100 + int(i % 16), 100 + int(i / 16), 7 }));
				tile->addItem(Item::Create(ids[i]));
				map->setTile(tile);
			}
			buffer.replace(std::move(map), { 100, 100, 7 });
		};
		load(sourceClient, sourceOtb);
		std::vector<uint16_t> ids;
		for (uint32_t id = 100; id <= g_items.getMaxID() && ids.size() < 128; ++id) {
			if (g_items.typeExists(id) && g_items[id].sprite) {
				ids.push_back(static_cast<uint16_t>(id));
			}
		}
		CopyBuffer source, target;
		makeBuffer(source, ids);
		CrossClientClipboard clipboard;
		wxString error;
		check(clipboard.capture(source, f.source, error), "Real client capture");
		activate(f.target);
		load(targetClient, targetOtb);
		auto analysis = clipboard.analyze(f.target, {}, error);
		check(analysis.valid && error.empty(), "Real assets comparison");
		std::vector<uint16_t> selected;
		std::vector<uint16_t> expected;
		for (const auto& row : analysis.rows) {
			if (row.state != CrossClientMatchState::Missing) {
				selected.push_back(row.source.sourceId);
				expected.push_back(row.destinationId);
			}
		}
		check(!selected.empty(), "No complete equivalent found in real client fixture");
		activate(f.source);
		makeBuffer(source, selected);
		check(clipboard.capture(source, f.source, error), "Real equivalent area capture");
		activate(f.target);
		analysis = clipboard.analyze(f.target, {}, error);
		check(analysis.canApply() && clipboard.apply(analysis, target, error), "Real area did not convert automatically");
		Editor editor(target, nullptr);
		target.paste(editor, { 400, 400, 7 });
		for (size_t i = 0; i < selected.size(); ++i) {
			auto* tile = editor.map.getTile({ 400 + int(i % 16), 400 + int(i / 16), 7 });
			check(tile && (tile->ground || !tile->items.empty()), "Real area lost a tile");
			check((tile->ground ? tile->ground : tile->items.front())->getID() == expected[i], "Real area retained the wrong server ID");
		}
		std::cout << "PASS real classic DAT/SPR/OTFI/OTB: " << selected.size() << " automatic IDs in an actual area paste\n";
	}
}

void RunCrossClientClipboardTests() {
	std::cout << std::unitbuf;
	g_settings.setDefaults();
	g_settings.setInteger(Config::INDIRECTORY_INSTALLATION, 1);
	g_gui.discoverDataDirectory("clients.xml");
	ClientVersion::loadVersions();
	g_settings.setInteger(Config::DEFAULT_CLIENT_VERSION, CLIENT_VERSION_860);
	fullVariantsAndAmbiguity();
	renderedFingerprintCoverage();
	cancellationAndStaleResources();
	incompatibleRoles();
	areaAttributesUndoRedo();
	realClassicAssets();
	ClientVersion::unloadVersions();
}
