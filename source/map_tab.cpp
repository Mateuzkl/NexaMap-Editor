//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "gui.h"
#include "editor.h"
#include "map.h"
#include "sprites.h"
#include "map_tab.h"
#include "editor_tabs.h"
#include "editor_resource_session.h"
#include "map_display.h"
#include "multiplayer_session.h"

MapTab::InternalReference::InternalReference(std::unique_ptr<Editor> editor) :
	editor(std::move(editor)), resourceSession(GetActiveEditorResourceSession()) { }

MapTab::InternalReference::~InternalReference() {
	// Socket/timer teardown must finish before handing pure map data to a worker.
	editor->multiplayer.reset();
	g_gui.DisposeEditor(std::move(editor));
}

MapTab::MapTab(MapTabbook* aui, std::unique_ptr<Editor> editor) :
	EditorTab(),
	MapWindow(aui, *editor),
	aui(aui),
	iref(std::make_shared<InternalReference>(std::move(editor))) {

	aui->AddTab(this, true);
	FitToMap();
}

MapTab::MapTab(const MapTab* other) :
	EditorTab(),
	MapWindow(other->aui, *other->iref->editor),
	aui(other->aui),
	iref(other->iref) {
	aui->AddTab(this, true);
	FitToMap();
	int x, y;
	other->GetCanvas()->GetScreenCenter(&x, &y);
	SetScreenCenterPosition(Position(x, y, other->GetCanvas()->GetFloor()));
}

MapTab::~MapTab() {
	if (IsUniqueReference()) {
		g_gui.ReleaseIngamePreviewEditor(iref->editor.get());
	}
	// wxPanel's base destructor runs AFTER iref's destruction. Release all
	// canvases/dialogs now, while their Editor& and resource session still exist.
	DestroyChildren();
}

bool MapTab::IsUniqueReference() const {
	return iref.use_count() == 1;
}

wxWindow* MapTab::GetWindow() const {
	return const_cast<MapTab*>(this);
}

MapWindow* MapTab::GetView() const {
	return const_cast<MapWindow*>((const MapWindow*)this);
}

wxString MapTab::GetTitle() const {
	wxString ss;
	ss << wxstr(iref->editor->map.getName()) << (iref->editor->map.hasChanged() ? "*" : "");
	return ss;
}

Editor* MapTab::GetEditor() const {
	return &editor;
}

Map* MapTab::GetMap() const {
	return &editor.map;
}

std::shared_ptr<EditorResourceSession> MapTab::GetResourceSession() const {
	return iref->resourceSession;
}

void MapTab::VisibilityCheck() {
	EditorTab* editorTab = aui->GetCurrentTab();
	auto* mapTab = dynamic_cast<MapTab*>(editorTab);
	UpdateDialogs(mapTab && HasSameReference(mapTab));
}

void MapTab::OnSwitchEditorMode(EditorMode mode) {
	gem->SetSprite(mode == DRAWING_MODE ? EDITOR_SPRITE_DRAWING_GEM : EDITOR_SPRITE_SELECTION_GEM);
	if (mode == SELECTION_MODE) {
		canvas->EnterSelectionMode();
	} else {
		canvas->EnterDrawingMode();
	}
}
