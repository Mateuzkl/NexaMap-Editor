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

#include "editor_tabs.h"
#include "gui.h"
#include "map_tab.h"

EditorTab::EditorTab() {
	;
}

EditorTab::~EditorTab() {
	;
}

BEGIN_EVENT_TABLE(MapTabbook, wxPanel)
EVT_AUINOTEBOOK_PAGE_CHANGING(wxID_ANY, MapTabbook::OnNotebookPageChanging)
EVT_AUINOTEBOOK_PAGE_CLOSE(wxID_ANY, MapTabbook::OnNotebookPageClose)
EVT_AUINOTEBOOK_PAGE_CHANGED(wxID_ANY, MapTabbook::OnNotebookPageChanged)
END_EVENT_TABLE()

MapTabbook::MapTabbook(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id, wxDefaultPosition, wxDefaultSize) {
	wxSizer* wxz = newd wxBoxSizer(wxHORIZONTAL);
	notebook = newd wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_NB_DEFAULT_STYLE | wxAUI_NB_CLOSE_ON_ACTIVE_TAB);
	addPage = newd wxPanel(notebook, wxID_ANY);
	notebook->AddPage(addPage, "+", false);
	for (wxAuiTabCtrl* tabControl : notebook->GetAllTabCtrls()) {
		tabControl->Bind(wxEVT_LEFT_DOWN, &MapTabbook::OnNotebookLeftDown, this);
	}
	initializing = false;
	wxz->Add(notebook, 1, wxEXPAND);
	SetSizerAndFit(wxz);
}

MapTabbook::~MapTabbook() {
	;
}

void MapTabbook::CycleTab(bool forward) {
	if (!notebook) {
		return;
	}

	auto pageCount = static_cast<int32_t>(notebook->GetPageCount());
	if (addPage != nullptr) {
		--pageCount;
	}
	if (pageCount <= 0) {
		g_gui.ShowNewMapTabDialog();
		return;
	}
	int32_t currentSelection = GetSelection();

	int32_t selection;
	if (forward) {
		selection = (currentSelection + 1) % pageCount;
	} else {
		selection = (currentSelection - 1 + pageCount) % pageCount;
	}
	SetFocusedTab(selection);
}

void MapTabbook::OnNotebookPageChanging(wxAuiNotebookEvent& evt) {
	if (initializing) {
		evt.Skip();
		return;
	}
	const int newSelection = evt.GetSelection();
	if (addPage && newSelection == static_cast<int>(notebook->GetPageIndex(addPage))) {
		evt.Veto();
		RequestNewMapTab();
		return;
	}

	if (auto* mapTab = dynamic_cast<MapTab*>(GetInternalTabByPageIndex(newSelection))) {
		g_gui.ActivateResourceSession(mapTab->GetResourceSession());
	}
	evt.Skip();
}

void MapTabbook::OnNotebookPageClose(wxAuiNotebookEvent& evt) {
	if (addPage && evt.GetInt() == static_cast<int>(notebook->GetPageIndex(addPage))) {
		evt.Veto();
		return;
	}
	EditorTab* editorTab = GetInternalTabByPageIndex(evt.GetInt());

	auto* mapTab = dynamic_cast<MapTab*>(editorTab);
	if (mapTab && evt.GetInt() == notebook->GetSelection() && mapTab->IsUniqueReference() && mapTab->GetMap()) {
		g_gui.RefreshPalettes(nullptr, false);
		g_gui.UpdateMenus();
		return;
	}
}

void MapTabbook::OnNotebookPageChanged(wxAuiNotebookEvent& evt) {
	if (initializing) {
		evt.Skip();
		return;
	}
	g_gui.FinalizeResourceSessionActivation();
	g_gui.UpdateMinimap();

	int32_t oldSelection = evt.GetOldSelection();
	int32_t newSelection = evt.GetSelection();

	MapTab* oldMapTab;
	if (oldSelection != -1) {
		oldMapTab = dynamic_cast<MapTab*>(GetInternalTabByPageIndex(oldSelection));
	} else {
		oldMapTab = nullptr;
	}

	MapTab* newMapTab;
	if (newSelection != -1) {
		newMapTab = dynamic_cast<MapTab*>(GetInternalTabByPageIndex(newSelection));
	} else {
		newMapTab = nullptr;
	}

	if (!newMapTab) {
		g_gui.RefreshPalettes(nullptr);
	} else if (!oldMapTab || !oldMapTab->HasSameReference(newMapTab)) {
		g_gui.RefreshPalettes(newMapTab->GetMap());
		g_gui.UpdateMenus();
	}

	if (oldMapTab) {
		oldMapTab->VisibilityCheck();
	}
	if (newMapTab) {
		newMapTab->VisibilityCheck();
	}
	g_gui.InvalidateAutoborderPreview();
	g_gui.UpdateIngamePreview();
}

void MapTabbook::OnNotebookLeftDown(wxMouseEvent& evt) {
	auto* tabControl = dynamic_cast<wxAuiTabCtrl*>(evt.GetEventObject());
	const wxWindow* clickedPage = tabControl ? tabControl->TabHitTest(evt.GetPosition()).window : nullptr;
	if (addPage && clickedPage == addPage) {
		RequestNewMapTab();
		return;
	}
	evt.Skip();
}

void MapTabbook::RequestNewMapTab() {
	if (newTabDialogPending) {
		return;
	}
	newTabDialogPending = true;
	CallAfter([this] {
		newTabDialogPending = false;
		if (notebook) {
			g_gui.ShowNewMapTabDialog();
		}
	});
}

// Wrappers

void MapTabbook::AddTab(EditorTab* tab, bool select) {
	if (!notebook || !tab || !tab->GetWindow()) {
		return;
	}

	wxWindow* window = tab->GetWindow();
	window->Reparent(notebook);
	// AddPage may synchronously emit PAGE_CHANGED. Register the tab first so
	// event handlers can safely resolve the new current page during that call.
	conv[window] = tab;
	const int addPageIndex = addPage ? static_cast<int>(notebook->GetPageIndex(addPage)) : wxNOT_FOUND;
	const size_t insertionIndex = addPageIndex == wxNOT_FOUND ? notebook->GetPageCount() : static_cast<size_t>(addPageIndex);
	if (!notebook->InsertPage(insertionIndex, window, tab->GetTitle(), select)) {
		conv.erase(window);
	}
}

void MapTabbook::SetFocusedTab(int idx) {
	const int pageIndex = GetPageIndexForTab(idx);
	if (!notebook || pageIndex == wxNOT_FOUND) {
		return;
	}
	notebook->SetSelection(pageIndex);
}

EditorTab* MapTabbook::GetInternalTab(int idx) {
	return GetInternalTabByPageIndex(GetPageIndexForTab(idx));
}

EditorTab* MapTabbook::GetInternalTabByPageIndex(int pageIndex) {
	if (!notebook || pageIndex < 0 || pageIndex >= static_cast<int>(notebook->GetPageCount())) {
		return nullptr;
	}
	const auto it = conv.find(notebook->GetPage(static_cast<size_t>(pageIndex)));
	return it != conv.end() ? it->second : nullptr;
}

int MapTabbook::GetPageIndexForTab(int tabIndex) const {
	if (!notebook || tabIndex < 0) {
		return wxNOT_FOUND;
	}
	int currentTab = 0;
	for (size_t pageIndex = 0; pageIndex < notebook->GetPageCount(); ++pageIndex) {
		if (notebook->GetPage(pageIndex) == addPage) {
			continue;
		}
		if (currentTab++ == tabIndex) {
			return static_cast<int>(pageIndex);
		}
	}
	return wxNOT_FOUND;
}

int MapTabbook::GetTabIndexForPage(int pageIndex) const {
	if (!notebook || pageIndex < 0 || pageIndex >= static_cast<int>(notebook->GetPageCount())
		|| notebook->GetPage(static_cast<size_t>(pageIndex)) == addPage) {
		return wxNOT_FOUND;
	}
	int tabIndex = 0;
	for (int currentPage = 0; currentPage < pageIndex; ++currentPage) {
		if (notebook->GetPage(static_cast<size_t>(currentPage)) != addPage) {
			++tabIndex;
		}
	}
	return tabIndex;
}

EditorTab* MapTabbook::GetCurrentTab() {
	if (GetTabCount() == 0 || GetSelection() == -1) {
		return nullptr;
	}
	return dynamic_cast<EditorTab*>(GetInternalTab(GetSelection()));
}

EditorTab* MapTabbook::GetTab(int idx) {
	return GetInternalTab(idx);
}

wxWindow* MapTabbook::GetCurrentPage() {
	if (GetTabCount() == 0) {
		return nullptr;
	}
	EditorTab* current = GetCurrentTab();
	return current ? current->GetWindow() : nullptr;
}

void MapTabbook::OnSwitchEditorMode(EditorMode mode) {
	for (int32_t i = 0; i < GetTabCount(); ++i) {
		EditorTab* editorTab = GetTab(i);
		if (editorTab) {
			editorTab->OnSwitchEditorMode(mode);
		}
	}
}

void MapTabbook::SetTabLabel(int idx, const wxString& label) {
	const int pageIndex = GetPageIndexForTab(idx);
	if (notebook && pageIndex != wxNOT_FOUND) {
		notebook->SetPageText(pageIndex, label);
	}
}

void MapTabbook::DeleteTab(int idx) {
	const int pageIndex = GetPageIndexForTab(idx);
	if (!notebook || pageIndex == wxNOT_FOUND) {
		return;
	}
	wxWindow* window = notebook->GetPage(static_cast<size_t>(pageIndex));
	conv.erase(window);
	notebook->DeletePage(static_cast<size_t>(pageIndex));
}

int MapTabbook::GetTabCount() {
	if (notebook) {
		const int pageCount = static_cast<int>(notebook->GetPageCount());
		return addPage ? std::max(0, pageCount - 1) : pageCount;
	}
	return 0;
}

int MapTabbook::GetSelection() {
	if (notebook) {
		return GetTabIndexForPage(notebook->GetSelection());
	}
	return -1;
}
