#include "main.h"

#include "editor_resource_session.h"

#include "brush.h"
#include "copybuffer.h"
#include "creatures.h"
#include "graphics.h"
#include "gui.h"
#include "items.h"
#include "materials.h"
#include "sprite_appearances.h"
#include "sprite_preloader.h"
#include "workspace_session.h"

struct EditorResourceSession::Storage {
	~Storage() {
		materials.clear();
		brushes.clear();
		creatures.clear();
		items.clear();
		// This is inactive storage. Its destruction must not stop the sprite
		// preloader that belongs to the currently displayed resource session.
		graphics.clear(false);
		spriteAppearances.unload();
	}

	ItemDatabase items;
	Materials materials;
	Brushes brushes;
	CreatureDatabase creatures;
	GraphicManager graphics;
	SpriteAppearances spriteAppearances;
	WorkspaceSession workspace;
	CopyBuffer copyBuffer;
};

namespace {
	EditorResourceSessionPtr& ActiveSession() {
		static EditorResourceSessionPtr active = std::make_shared<EditorResourceSession>();
		return active;
	}
}

EditorResourceSession::EditorResourceSession() :
	storage(std::make_unique<Storage>()) { }

EditorResourceSession::~EditorResourceSession() {
	delete doodadBufferMap;
}

void EditorResourceSession::swapWithGlobals() {
	g_spritePreloader.clear();
	g_materials.swap(storage->materials);
	g_brushes.swap(storage->brushes);
	g_creatures.swap(storage->creatures);
	g_items.swap(storage->items);
	g_gui.gfx.swap(storage->graphics);
	g_spriteAppearances.swap(storage->spriteAppearances);
	g_workspace.swap(storage->workspace);
	g_gui.copybuffer.swap(storage->copyBuffer);
	ClientAssets::swapState(clientAssetsState);
}

EditorResourceSessionPtr GetActiveEditorResourceSession() {
	return ActiveSession();
}

EditorResourceSessionPtr CreateEditorResourceSession() {
	return std::make_shared<EditorResourceSession>();
}

void SetActiveEditorResourceSession(const EditorResourceSessionPtr& session) {
	if (session) {
		ActiveSession() = session;
	}
}
