// SPDX-License-Identifier: GPL-3.0-or-later
// Hidden native controls and synthetic resources. Never loads client assets or maps.
#include "main.h"

#include "copybuffer.h"
#include "cross_client_clipboard.h"
#include "editor.h"
#include "editor_resource_session.h"
#include "editor_tabs.h"
#include "gui.h"
#include "items.h"
#include "map_tab.h"

#include <iostream>

class ResourceSessionTabTests {
	static void check(bool value, const char* message) {
		if (!value) {
			throw std::runtime_error(message);
		}
	}

	static void defineGround(uint16_t id, const std::string& name) {
		auto* type = new ItemType();
		type->id = id;
		type->clientID = id;
		type->group = ITEM_GROUP_GROUND;
		type->name = name;
		g_items.items.set(id, type);
	}

	static void defineWall(uint16_t id, const std::string& name, bool wall) {
		auto* type = new ItemType();
		type->id = id;
		type->clientID = id;
		type->name = name;
		type->isWall = wall;
		g_items.items.set(id, type);
	}

	static void fillCopyBuffer(uint16_t id) {
		auto map = std::make_unique<BaseMap>();
		auto* tile = map->allocator(map->createTileL({ 100, 100, 7 }));
		tile->addItem(Item::Create(id));
		map->setTile(tile);
		g_gui.copybuffer.replace(std::move(map), { 100, 100, 7 });
	}

	static uint16_t copiedItemId() {
		Tile* tile = g_gui.copybuffer.getBufferMap().getTile({ 100, 100, 7 });
		check(tile && (tile->ground || !tile->items.empty()), "The session copybuffer lost its item");
		return tile->ground ? tile->ground->getID() : tile->items.front()->getID();
	}

	static void activateWithoutUi(const EditorResourceSessionPtr& next) {
		const EditorResourceSessionPtr current = GetActiveEditorResourceSession();
		g_gui.SwapResourceSessionState(*current);
		current->swapWithGlobals();
		next->swapWithGlobals();
		g_gui.SwapResourceSessionState(*next);
		SetActiveEditorResourceSession(next);
	}

	static void remapClipboard(
		CrossClientClipboard& clipboard,
		const EditorResourceSessionPtr& source,
		const EditorResourceSessionPtr& destination,
		uint16_t destinationId
	) {
		wxString error;
		check(clipboard.capture(g_gui.copybuffer, source, error), "Could not capture the source session copybuffer");
		activateWithoutUi(destination);
		CrossClientPasteAnalysis analysis = clipboard.analyze(destination, {}, error);
		check(analysis.valid && analysis.rows.size() == 1, "Cross-client analysis did not retain the copied area");
		check(!analysis.rows[0].recommendations.empty() && analysis.rows[0].recommendations.front().automatic, "Matching item properties were not offered for automatic remapping");
		check(analysis.rows[0].recommendations.front().destinationId == destinationId, "Automatic remapping selected resources from the wrong tab");
		check(CrossClientClipboard::resolveMapping(analysis, 0, analysis.rows[0].recommendations.front().destinationId, error), "Could not remap the copied ground item");
		check(clipboard.apply(analysis, g_gui.copybuffer, error), "Could not apply the cross-client clipboard mapping");
		check(copiedItemId() == destinationId, "Cross-client paste used resources from the wrong tab");
	}

public:
	static void run() {
		std::cout << std::unitbuf;
		g_settings.setDefaults();
		g_settings.setInteger(Config::INDIRECTORY_INSTALLATION, 1);
		g_gui.discoverDataDirectory("clients.xml");
		ClientVersion::loadVersions();
		check(ClientVersion::get(CLIENT_VERSION_860) != nullptr, "The 8.60 test profile is unavailable");
		ClientVersion* otherVersion = ClientVersion::getLatestVersion();
		check(otherVersion && otherVersion->getID() != CLIENT_VERSION_860, "A second client profile is unavailable");

		const EditorResourceSessionPtr originalSession = GetActiveEditorResourceSession();
		const EditorResourceSessionPtr sessionA = CreateEditorResourceSession();
		const EditorResourceSessionPtr sessionB = CreateEditorResourceSession();
		activateWithoutUi(sessionA);

		wxFrame frame(nullptr, wxID_ANY, "Hidden independent resource tabs");
		MapTabbook tabHost(&frame, wxID_ANY);
		auto* previousTabs = g_gui.tabbook;
		const bool wasClosing = g_gui.IsApplicationClosing();
		g_gui.tabbook = &tabHost;
		g_gui.SetApplicationClosing(true);
		MapTabbook* tabs = &tabHost;
		g_settings.setInteger(Config::DEFAULT_CLIENT_VERSION, CLIENT_VERSION_860);

		defineGround(100, "Shared cross-client ground");
		defineWall(101, "Shared cross-client wall", true);
		fillCopyBuffer(100);
		auto editorA = std::make_unique<Editor>(g_gui.copybuffer, nullptr);
		editorA->map.setName("World A [8.60]");
		editorA->map.convert({ MAP_OTBM_2, CLIENT_VERSION_860 }, false);
		Editor* editorAPointer = editorA.get();
		auto* worldA = new MapTab(tabs, std::move(editorA));
		check(tabs->GetTabCount() == 1 && worldA->GetResourceSession() == sessionA, "World A was not bound to its resource session");

		activateWithoutUi(sessionB);
		defineGround(200, "Shared cross-client ground");
		defineWall(201, "Shared cross-client wall", true);
		defineWall(202, "Shared cross-client wall", false);
		fillCopyBuffer(200);
		g_gui.loaded_version = otherVersion->getID();
		auto editorB = std::make_unique<Editor>(g_gui.copybuffer);
		g_gui.loaded_version = CLIENT_VERSION_NONE;
		editorB->map.setName("World B [other version]");
		Editor* editorBPointer = editorB.get();
		auto* worldB = new MapTab(tabs, std::move(editorB));
		check(tabs->GetTabCount() == 2, "Creating World B closed World A");
		check(tabs->GetTab(0) == worldA && tabs->GetTab(1) == worldB, "The existing tab was replaced while adding another resource session");
		check(worldA->GetEditor() == editorAPointer && worldB->GetEditor() == editorBPointer, "The tabs did not retain independent editors");
		check(worldB->GetResourceSession() == sessionB && sessionA != sessionB, "World B reused World A's resource session");

		for (int cycle = 0; cycle < 4; ++cycle) {
			activateWithoutUi(sessionA);
			tabs->SetFocusedTab(0);
			check(GetActiveEditorResourceSession() == sessionA && g_items.typeExists(100) && !g_items.typeExists(200), "Switching to World A restored the wrong resources");
			check(copiedItemId() == 100, "World A copybuffer was not restored");
			activateWithoutUi(sessionB);
			tabs->SetFocusedTab(1);
			check(GetActiveEditorResourceSession() == sessionB && g_items.typeExists(200) && !g_items.typeExists(100), "Switching to World B restored the wrong resources");
			check(copiedItemId() == 200, "World B copybuffer was not restored");
		}

		CrossClientClipboard clipboard;
		activateWithoutUi(sessionA);
		tabs->SetFocusedTab(0);
		fillCopyBuffer(101);
		remapClipboard(clipboard, sessionA, sessionB, 201);
		remapClipboard(clipboard, sessionB, sessionA, 101);

		tabs->SetFocusedTab(1);
		tabs->DeleteTab(1);
		check(tabs->GetTabCount() == 1 && tabs->GetTab(0) == worldA && worldA->GetEditor() == editorAPointer, "Closing World B affected World A");

		activateWithoutUi(sessionB);
		auto replacementBEditor = std::make_unique<Editor>(g_gui.copybuffer, nullptr);
		Editor* replacementBPointer = replacementBEditor.get();
		auto* replacementB = new MapTab(tabs, std::move(replacementBEditor));
		tabs->SetFocusedTab(1);
		tabs->DeleteTab(0);
		check(tabs->GetTabCount() == 1 && tabs->GetTab(0) == replacementB && replacementB->GetEditor() == replacementBPointer, "Closing World A affected World B");

		g_gui.CloseAllEditors(false);
		g_gui.DrainEditorDisposals();
		g_gui.tabbook = previousTabs;
		g_gui.SetApplicationClosing(wasClosing);
		activateWithoutUi(originalSession);
		ClientVersion::unloadVersions();
		std::cout << "PASS independent tabs: 8.60 + another client, repeated switching, bidirectional cross-client clipboard and isolated close\n";
	}
};

void RunResourceSessionTabTests() {
	ResourceSessionTabTests::run();
}
