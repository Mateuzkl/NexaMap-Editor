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
#include "favorites_resources.h"

#include "palette_brushlist.h"
#include "gui.h"
#include "brush.h"
#include "add_tileset_window.h"
#include "add_item_window.h"
#include "materials.h"
#include "theme.h"
#include "settings.h"

#include <wx/artprov.h>
#include <wx/dcbuffer.h>
#include <algorithm>
#include <cctype>
#include <unordered_set>

// ============================================================================
// Brush Palette Panel
// A common class for terrain/doodad/item/raw palette

BEGIN_EVENT_TABLE(BrushPalettePanel, PalettePanel)
EVT_BUTTON(wxID_ADD, BrushPalettePanel::OnClickAddItemToTileset)
EVT_BUTTON(wxID_NEW, BrushPalettePanel::OnClickAddTileset)
EVT_CHOICEBOOK_PAGE_CHANGING(wxID_ANY, BrushPalettePanel::OnSwitchingPage)
EVT_CHOICEBOOK_PAGE_CHANGED(wxID_ANY, BrushPalettePanel::OnPageChanged)
END_EVENT_TABLE()

BrushPalettePanel::BrushPalettePanel(wxWindow* parent, const TilesetContainer& tilesets, TilesetCategoryType category, wxWindowID id) :
	PalettePanel(parent, id),
	palette_type(category),
	choicebook(nullptr),
	size_panel(nullptr),
	toolbar(nullptr),
	m_searchCtrl(nullptr),
	m_searchToolbar(nullptr),
	m_filterQuery(""),
	m_filterAll(false),
	m_sortKey(TilesetSortKey::Name),
	m_sortDir(TilesetSortDirection::Ascending),
	m_hasSort(false),
	m_showLabels(false),
	m_tileSize(32),
	m_tilesets(&tilesets) {
	LoadPaletteFilters();

	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);

	// Create search bar + filter-all toolbar
	wxSizer* searchRowSizer = newd wxBoxSizer(wxHORIZONTAL);
	m_searchCtrl = newd PaletteSearchCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	m_searchCtrl->ShowSearchButton(true);
	m_searchCtrl->ShowCancelButton(true);
	m_searchCtrl->SetDescriptiveText("Search brushes...");
	if (!m_filterQuery.empty()) {
		m_searchCtrl->ChangeValue(wxstr(m_filterQuery));
	}
	m_debounceTimer.SetOwner(this);
	Bind(wxEVT_TIMER, &BrushPalettePanel::OnDebounceTimer, this);

	m_searchCtrl->Bind(wxEVT_TEXT, &BrushPalettePanel::OnSearchText, this);
	m_searchCtrl->Bind(wxEVT_TEXT_ENTER, &BrushPalettePanel::OnSearchText, this);
	m_searchCtrl->Bind(wxEVT_SEARCHCTRL_SEARCH_BTN, &BrushPalettePanel::OnSearchText, this);
	m_searchCtrl->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, &BrushPalettePanel::OnSearchCancel, this);
	m_searchCtrl->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
		if (event.GetKeyCode() == WXK_ESCAPE) {
			if (!m_searchCtrl->GetValue().empty()) {
				ResetFilter();
				return;
			}
		}
		event.Skip();
	});
	m_searchCtrl->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
		SavePaletteFilters();
		event.Skip();
	});
	searchRowSizer->Add(m_searchCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);

	m_searchToolbar = newd wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_PLAIN_BACKGROUND);
	m_searchToolbar->AddTool(TOOL_FILTER_ALL, "All", wxArtProvider::GetBitmap(wxART_FIND, wxART_OTHER, wxSize(16, 16)), "Search across all palettes", wxITEM_CHECK);
	m_searchToolbar->ToggleTool(TOOL_FILTER_ALL, m_filterAll);
	m_searchToolbar->Realize();
	m_searchToolbar->Bind(wxEVT_TOOL, &BrushPalettePanel::OnToolClick, this);
	searchRowSizer->Add(m_searchToolbar, 0, wxALIGN_CENTER_VERTICAL);
	topsizer->Add(searchRowSizer, 0, wxEXPAND | wxALL, 2);

	// Create tileset choicebook with toolbar header
	auto* ts_sizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Tileset");

	wxSizer* headerRowSizer = newd wxBoxSizer(wxHORIZONTAL);
	toolbar = newd wxAuiToolBar(ts_sizer->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_PLAIN_BACKGROUND);
	toolbar->AddTool(TOOL_SORT_AZ, "A-Z", wxArtProvider::GetBitmap(wxART_LIST_VIEW, wxART_OTHER, wxSize(16, 16)), "Sort Ascending");
	toolbar->AddTool(TOOL_SORT_ZA, "Z-A", wxArtProvider::GetBitmap(wxART_LIST_VIEW, wxART_OTHER, wxSize(16, 16)), "Sort Descending");
	toolbar->AddTool(TOOL_TOGGLE_LABELS, "Labels", wxArtProvider::GetBitmap(wxART_REPORT_VIEW, wxART_OTHER, wxSize(16, 16)), "Toggle Labels", wxITEM_CHECK);
	toolbar->AddTool(TOOL_CHANGE_SIZE, "Size", wxArtProvider::GetBitmap(wxART_FULL_SCREEN, wxART_OTHER, wxSize(16, 16)), "Change Tile Size");
	toolbar->ToggleTool(TOOL_TOGGLE_LABELS, m_showLabels);
	toolbar->Realize();
	toolbar->Bind(wxEVT_TOOL, &BrushPalettePanel::OnToolClick, this);
	headerRowSizer->Add(toolbar, 0, wxALIGN_CENTER_VERTICAL | wxBOTTOM, 2);
	ts_sizer->Add(headerRowSizer, 0, wxEXPAND | wxLEFT | wxRIGHT, 2);

	auto* tmp_choicebook = newd wxChoicebook(ts_sizer->GetStaticBox(), wxID_ANY, wxDefaultPosition, FromDIP(wxSize(180, 250)));
	ts_sizer->Add(tmp_choicebook, 1, wxEXPAND);
	topsizer->Add(ts_sizer, 1, wxEXPAND);

	if (g_settings.getBoolean(Config::SHOW_TILESET_EDITOR)) {
		wxSizer* tmpsizer = newd wxBoxSizer(wxHORIZONTAL);
		auto* buttonAddTileset = newd wxButton(this, wxID_NEW, "Add new Tileset");
		tmpsizer->Add(buttonAddTileset, wxSizerFlags(0).Center());

		auto* buttonAddItemToTileset = newd wxButton(this, wxID_ADD, "Add new Item");
		tmpsizer->Add(buttonAddItemToTileset, wxSizerFlags(0).Center());

		topsizer->Add(tmpsizer, 0, wxCENTER, 10);
	}

	for (auto iter = tilesets.begin(); iter != tilesets.end(); ++iter) {
		const Tileset* tileset = iter->second;
		const TilesetCategory* tcg = tileset->getCategory(category);
		if (tcg && tcg->size() > 0) {
			auto* panel = newd BrushPanel(tmp_choicebook);
			panel->AssignTileset(tcg);
			if (m_hasSort) {
				panel->SetSort(m_sortKey, m_sortDir);
			}
			panel->SetShowLabels(m_showLabels);
			panel->SetTileSize(m_tileSize);
			tmp_choicebook->AddPage(panel, wxstr(iter->second->name));
		}
	}

	SetSizerAndFit(topsizer);

	choicebook = tmp_choicebook;

	if (!m_filterQuery.empty()) {
		ApplyFilter();
	}
}

BrushPalettePanel::~BrushPalettePanel() {
	m_debounceTimer.Stop();
	Unbind(wxEVT_TIMER, &BrushPalettePanel::OnDebounceTimer, this);
	if (toolbar) {
		toolbar->Unbind(wxEVT_TOOL, &BrushPalettePanel::OnToolClick, this);
	}
	if (m_searchToolbar) {
		m_searchToolbar->Unbind(wxEVT_TOOL, &BrushPalettePanel::OnToolClick, this);
	}
}

void BrushPalettePanel::InvalidateContents() {
	if (!choicebook) {
		return;
	}
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		auto* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if (panel) {
			panel->InvalidateContents();
		}
	}
	PalettePanel::InvalidateContents();
}

void BrushPalettePanel::LoadCurrentContents() {
	if (!choicebook) {
		return;
	}
	wxWindow* page = choicebook->GetCurrentPage();
	auto* panel = dynamic_cast<BrushPanel*>(page);
	if (panel) {
		panel->OnSwitchIn();
	}
	PalettePanel::LoadCurrentContents();
}

void BrushPalettePanel::LoadAllContents() {
	if (!choicebook) {
		return;
	}
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		auto* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if (panel) {
			panel->LoadContents();
		}
	}
	PalettePanel::LoadAllContents();
}

PaletteType BrushPalettePanel::GetType() const {
	return palette_type;
}

void BrushPalettePanel::SetListType(BrushListType ltype) {
	if (!choicebook) {
		return;
	}
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		auto* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if (panel) {
			panel->SetListType(ltype);
		}
	}
}

void BrushPalettePanel::SetListType(const wxString& ltype) {
	if (!choicebook) {
		return;
	}
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		auto* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if (panel) {
			panel->SetListType(ltype);
		}
	}
}

Brush* BrushPalettePanel::GetSelectedBrush() const {
	if (!choicebook) {
		return nullptr;
	}
	wxWindow* page = choicebook->GetCurrentPage();
	auto* panel = dynamic_cast<BrushPanel*>(page);
	Brush* res = nullptr;
	if (panel) {
		for (auto iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
			res = (*iter)->GetSelectedBrush();
			if (res) {
				return res;
			}
		}
		res = panel->GetSelectedBrush();
	}
	return res;
}

void BrushPalettePanel::SelectFirstBrush() {
	if (!choicebook) {
		return;
	}
	wxWindow* page = choicebook->GetCurrentPage();
	auto* panel = dynamic_cast<BrushPanel*>(page);
	if (panel) {
		panel->SelectFirstBrush();
	}
}

bool BrushPalettePanel::SelectBrush(const Brush* whatbrush) {
	if (!choicebook) {
		return false;
	}

	auto* panel = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage());
	if (!panel) {
		return false;
	}

	for (PalettePanel* toolBar : tool_bars) {
		if (toolBar->SelectBrush(whatbrush)) {
			panel->SelectBrush(nullptr);
			return true;
		}
	}

	for (PalettePanel* toolBar : tool_bars) {
		toolBar->DeselectAll();
	}

	if (panel->SelectBrush(whatbrush)) {
		for (PalettePanel* toolBar : tool_bars) {
			toolBar->SelectBrush(nullptr);
		}
		return true;
	}

	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		if ((int)iz == choicebook->GetSelection()) {
			continue;
		}

		panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if (panel && panel->SelectBrush(whatbrush)) {
			choicebook->ChangeSelection(iz);
			for (PalettePanel* toolBar : tool_bars) {
				toolBar->SelectBrush(nullptr);
			}
			return true;
		}
	}
	return false;
}

void BrushPalettePanel::OnSwitchingPage(wxChoicebookEvent& event) {
	event.Skip();
	if (!choicebook) {
		return;
	}
	auto* old_panel = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage());
	if (old_panel) {
		old_panel->OnSwitchOut();
		for (auto iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
			Brush* tmp = (*iter)->GetSelectedBrush();
			if (tmp) {
				remembered_brushes[old_panel] = tmp;
			}
		}
	}

	wxWindow* page = choicebook->GetPage(event.GetSelection());
	auto* panel = dynamic_cast<BrushPanel*>(page);
	if (panel) {
		if (!m_filterQuery.empty() && !m_filterAll) {
			panel->SetFilterQuery(m_filterQuery, nullptr);
		}
		panel->OnSwitchIn();
		for (auto iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
			(*iter)->SelectBrush(remembered_brushes[panel]);
		}
	}
}

void BrushPalettePanel::OnPageChanged(wxChoicebookEvent& event) {
	if (!choicebook) {
		return;
	}
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}

void BrushPalettePanel::OnSwitchIn() {
	LoadCurrentContents();
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetBrushSizeInternal(last_brush_size);
	OnUpdateBrushSize(g_gui.GetBrushShape(), last_brush_size);
}

void BrushPalettePanel::OnClickAddTileset(wxCommandEvent& WXUNUSED(event)) {
	if (!choicebook) {
		return;
	}

	wxDialog* w = newd AddTilesetWindow(g_gui.root, palette_type);
	int ret = w->ShowModal();
	w->Destroy();

	if (ret != 0) {
		g_gui.DestroyPalettes();
		g_gui.NewPalette();
	}
}

void BrushPalettePanel::OnClickAddItemToTileset(wxCommandEvent& WXUNUSED(event)) {
	if (!choicebook) {
		return;
	}
	std::string tilesetName = nstr(choicebook->GetPageText(choicebook->GetSelection()));

	auto _it = g_materials.tilesets.find(tilesetName);
	if (_it != g_materials.tilesets.end()) {
		wxDialog* w = newd AddItemWindow(g_gui.root, palette_type, _it->second);
		int ret = w->ShowModal();
		w->Destroy();

		if (ret != 0) {
			g_gui.RebuildPalettes();
		}
	}
}

void BrushPalettePanel::SetSort(TilesetSortKey key, TilesetSortDirection dir) {
	m_hasSort = true;
	m_sortKey = key;
	m_sortDir = dir;
	SavePaletteFilters();

	if (!choicebook) {
		return;
	}
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		auto* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if (panel) {
			panel->SetSort(key, dir);
		}
	}
}

void BrushPalettePanel::ClearSort() {
	m_hasSort = false;
	SavePaletteFilters();

	if (!choicebook) {
		return;
	}
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		auto* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if (panel) {
			panel->ClearSort();
		}
	}
}

void BrushPalettePanel::SetShowLabels(bool show) {
	m_showLabels = show;
	SavePaletteFilters();
	if (toolbar && toolbar->GetToolToggled(TOOL_TOGGLE_LABELS) != show) {
		toolbar->ToggleTool(TOOL_TOGGLE_LABELS, show);
		toolbar->Refresh();
	}

	if (!choicebook) {
		return;
	}
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		auto* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if (panel) {
			panel->SetShowLabels(show);
		}
	}
}

void BrushPalettePanel::SetTileSize(int sizePx) {
	m_tileSize = sizePx;
	SavePaletteFilters();

	if (!choicebook) {
		return;
	}
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		auto* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if (panel) {
			panel->SetTileSize(sizePx);
		}
	}
}

void BrushPalettePanel::ApplyTheme() {
	if (toolbar) {
		toolbar->SetBackgroundColour(Theme::Get(Theme::Role::Surface));
		toolbar->SetForegroundColour(Theme::Get(Theme::Role::Text));
		toolbar->Refresh();
	}
	if (m_searchToolbar) {
		m_searchToolbar->SetBackgroundColour(Theme::Get(Theme::Role::Surface));
		m_searchToolbar->SetForegroundColour(Theme::Get(Theme::Role::Text));
		m_searchToolbar->Refresh();
	}
	if (m_searchCtrl) {
		m_searchCtrl->SetBackgroundColour(Theme::Get(Theme::Role::Surface));
		m_searchCtrl->SetForegroundColour(Theme::Get(Theme::Role::Text));
		m_searchCtrl->Refresh();
	}
}

void BrushPalettePanel::OnToolClick(wxCommandEvent& event) {
	int id = event.GetId();
	if (id == TOOL_SORT_AZ) {
		OnSortButtonClick(TilesetSortDirection::Ascending, id);
	} else if (id == TOOL_SORT_ZA) {
		OnSortButtonClick(TilesetSortDirection::Descending, id);
	} else if (id == TOOL_TOGGLE_LABELS) {
		SetShowLabels(toolbar ? toolbar->GetToolToggled(TOOL_TOGGLE_LABELS) : event.IsChecked());
	} else if (id == TOOL_CHANGE_SIZE) {
		OnSizeButtonClick(id);
	} else if (id == TOOL_FILTER_ALL) {
		m_filterAll = m_searchToolbar ? m_searchToolbar->GetToolToggled(TOOL_FILTER_ALL) : event.IsChecked();
		SavePaletteFilters();
		ApplyFilter();
	}
}

void BrushPalettePanel::OnSortButtonClick(TilesetSortDirection dir, int toolId) {
	wxMenu menu;
	auto* itemID = menu.AppendRadioItem(MENU_SORT_BY_ID, "By ID");
	auto* itemName = menu.AppendRadioItem(MENU_SORT_BY_NAME, "By Name");
	auto* itemDefault = menu.AppendRadioItem(MENU_SORT_DEFAULT, "Default");

	if (!m_hasSort) {
		itemDefault->Check(true);
	} else if (m_sortKey == TilesetSortKey::ID) {
		itemID->Check(true);
	} else {
		itemName->Check(true);
	}

	wxRect rect = toolbar->GetToolRect(toolId);
	wxPoint pos(rect.x, rect.y + rect.height);
	int selected = toolbar->GetPopupMenuSelectionFromUser(menu, pos);
	if (selected == MENU_SORT_BY_ID) {
		SetSort(TilesetSortKey::ID, dir);
	} else if (selected == MENU_SORT_BY_NAME) {
		SetSort(TilesetSortKey::Name, dir);
	} else if (selected == MENU_SORT_DEFAULT) {
		ClearSort();
	}
}

void BrushPalettePanel::OnSizeButtonClick(int toolId) {
	wxMenu menu;
	auto* item16 = menu.AppendRadioItem(MENU_SIZE_16, "16x16");
	auto* item32 = menu.AppendRadioItem(MENU_SIZE_32, "32x32");
	auto* item64 = menu.AppendRadioItem(MENU_SIZE_64, "64x64");
	auto* item128 = menu.AppendRadioItem(MENU_SIZE_128, "128x128");

	if (m_tileSize == 16) {
		item16->Check(true);
	} else if (m_tileSize == 64) {
		item64->Check(true);
	} else if (m_tileSize == 128) {
		item128->Check(true);
	} else {
		item32->Check(true);
	}

	wxRect rect = toolbar->GetToolRect(toolId);
	wxPoint pos(rect.x, rect.y + rect.height);
	int selected = toolbar->GetPopupMenuSelectionFromUser(menu, pos);
	if (selected == MENU_SIZE_16) {
		SetTileSize(16);
	} else if (selected == MENU_SIZE_32) {
		SetTileSize(32);
	} else if (selected == MENU_SIZE_64) {
		SetTileSize(64);
	} else if (selected == MENU_SIZE_128) {
		SetTileSize(128);
	}
}

void BrushPalettePanel::OnSearchText(wxCommandEvent& event) {
	if (!m_searchCtrl) {
		return;
	}
	const auto eventType = event.GetEventType();
	m_filterQuery = m_searchCtrl->GetValue().ToStdString();

	if (eventType == wxEVT_TEXT) {
		m_debounceTimer.Start(180, wxTIMER_ONE_SHOT);
		return;
	}

	m_debounceTimer.Stop();
	SavePaletteFilters();
	ApplyFilter();

	if (choicebook) {
		PaletteWindow* pw = GetParentPalette();
		if (pw) {
			g_gui.ActivatePalette(pw);
		}

		auto* panel = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage());
		if (panel) {
			Brush* brush = panel->GetSelectedBrush();
			if (!brush) {
				panel->SelectFirstBrush();
				brush = panel->GetSelectedBrush();
			}
			if (brush) {
				if (m_filterAll && pw) {
					int targetCat = 0;
					std::string targetTs;
					for (const auto& res : m_globalResults) {
						if (res.brush == brush) {
							targetCat = res.category;
							targetTs = res.tilesetName;
							break;
						}
					}
					pw->JumpToBrush(brush, GetName().ToStdString(), targetCat, targetTs);
				} else {
					g_gui.SelectBrushInternal(brush);
				}
			}
		}
	}
}

void BrushPalettePanel::OnDebounceTimer(wxTimerEvent& WXUNUSED(event)) {
	ApplyFilter();
}

void BrushPalettePanel::OnSearchCancel(wxCommandEvent& event) {
	ResetFilter();
}

void BrushPalettePanel::ApplyFilter() {
	if (!choicebook) {
		return;
	}
	auto* panel = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage());
	if (!panel) {
		return;
	}

	if (m_filterAll && !m_filterQuery.empty() && m_tilesets) {
		m_globalResults = PaletteModel::AggregateGlobalBrushes(*m_tilesets, palette_type);
		std::vector<Brush*> matchingBrushes;
		matchingBrushes.reserve(m_globalResults.size());
		std::string queryLower = PaletteModel::NormalizeQuery(m_filterQuery);
		for (const auto& res : m_globalResults) {
			if (res.brush && PaletteModel::BrushMatchesQuery(res.brush, queryLower)) {
				matchingBrushes.push_back(res.brush);
			}
		}
		panel->SetFilterQuery(m_filterQuery, &matchingBrushes);
		if (choicebook->GetChoiceCtrl()) {
			choicebook->GetChoiceCtrl()->Enable(false);
		}
	} else {
		m_globalResults.clear();
		panel->SetFilterQuery(m_filterQuery, nullptr);
		if (choicebook->GetChoiceCtrl()) {
			choicebook->GetChoiceCtrl()->Enable(true);
		}
	}
}

void BrushPalettePanel::ResetFilter() {
	m_debounceTimer.Stop();
	if (m_searchCtrl) {
		m_searchCtrl->ChangeValue(wxEmptyString);
	}
	m_filterQuery.clear();
	SavePaletteFilters();
	ApplyFilter();
}

bool BrushPalettePanel::JumpToTilesetAndBrush(std::string_view tilesetName, const Brush* brush) {
	if (!choicebook || !brush) {
		return false;
	}

	if (m_searchCtrl) {
		m_searchCtrl->ChangeValue(wxEmptyString);
	}
	m_filterQuery.clear();
	SavePaletteFilters();

	int targetIndex = wxNOT_FOUND;
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		auto* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if (panel) {
			if (panel->GetTileset() && panel->GetTileset()->tileset.name == tilesetName) {
				targetIndex = static_cast<int>(iz);
				break;
			} else if (nstr(choicebook->GetPageText(iz)) == tilesetName) {
				targetIndex = static_cast<int>(iz);
				break;
			}
		}
	}

	if (targetIndex != wxNOT_FOUND) {
		if (choicebook->GetSelection() != targetIndex) {
			choicebook->SetSelection(targetIndex);
		}
	}

	ApplyFilter();

	auto* currentPanel = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage());
	bool selected = false;
	if (currentPanel) {
		selected = currentPanel->SelectBrush(brush);
	}

	if (!selected) {
		for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
			auto* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
			if (panel && panel->SelectBrush(brush)) {
				choicebook->SetSelection(iz);
				selected = true;
				break;
			}
		}
	}

	PaletteWindow* pw = GetParentPalette();
	if (pw) {
		g_gui.ActivatePalette(pw);
	}
	g_gui.SelectBrushInternal(const_cast<Brush*>(brush));
	Layout();
	Refresh();

	return selected;
}

std::string BrushPalettePanel::FindTilesetNameForBrush(const Brush* brush) const {
	if (!brush || !m_tilesets) {
		return "";
	}
	for (const auto& [name, tileset] : *m_tilesets) {
		if (tileset) {
			const auto* category = tileset->getCategory(palette_type);
			if (category && category->containsBrush(brush)) {
				return name;
			}
		}
	}
	return "";
}

void BrushPalettePanel::LoadPaletteFilters() {
	m_sortKey = static_cast<TilesetSortKey>(g_settings.getInteger(Config::PALETTE_SORT_KEY));
	m_sortDir = static_cast<TilesetSortDirection>(g_settings.getInteger(Config::PALETTE_SORT_DIR));
	m_hasSort = g_settings.getInteger(Config::PALETTE_HAS_SORT) != 0;
	m_showLabels = g_settings.getInteger(Config::PALETTE_SHOW_LABELS) != 0;
	m_tileSize = g_settings.getInteger(Config::PALETTE_TILE_SIZE);
	if (m_tileSize != 16 && m_tileSize != 32 && m_tileSize != 64 && m_tileSize != 128) {
		m_tileSize = 32;
	}
	m_filterAll = g_settings.getInteger(Config::PALETTE_FILTER_ALL) != 0;
	m_filterQuery = g_settings.getString(Config::PALETTE_FILTER_QUERY);
}

void BrushPalettePanel::SavePaletteFilters() {
	g_settings.setInteger(Config::PALETTE_SORT_KEY, static_cast<int>(m_sortKey));
	g_settings.setInteger(Config::PALETTE_SORT_DIR, static_cast<int>(m_sortDir));
	g_settings.setInteger(Config::PALETTE_HAS_SORT, m_hasSort ? 1 : 0);
	g_settings.setInteger(Config::PALETTE_SHOW_LABELS, m_showLabels ? 1 : 0);
	g_settings.setInteger(Config::PALETTE_TILE_SIZE, m_tileSize);
	g_settings.setInteger(Config::PALETTE_FILTER_ALL, m_filterAll ? 1 : 0);
	g_settings.setString(Config::PALETTE_FILTER_QUERY, m_filterQuery);
}

// ============================================================================
// Brush Panel
// A container of brush buttons

BEGIN_EVENT_TABLE(BrushPanel, wxPanel)
EVT_LISTBOX(wxID_ANY, BrushPanel::OnClickListBoxRow)
END_EVENT_TABLE()

BrushPanel::BrushPanel(wxWindow* parent) :
	wxPanel(parent, wxID_ANY),
	tileset(nullptr),
	brushbox(nullptr),
	loaded(false),
	list_type(BRUSHLIST_LISTBOX),
	has_sort(false),
	sort_key(TilesetSortKey::Name),
	sort_dir(TilesetSortDirection::Ascending),
	show_labels(false),
	tile_size_px(32),
	has_override_brushes(false) {
	sizer = newd wxBoxSizer(wxVERTICAL);
	SetSizerAndFit(sizer);
}

BrushPanel::~BrushPanel() {
	////
}

void BrushPanel::AssignTileset(const TilesetCategory* _tileset) {
	if (_tileset != tileset) {
		InvalidateContents();
		tileset = _tileset;
	}
}

void BrushPanel::SetListType(BrushListType ltype) {
	if (list_type != ltype) {
		InvalidateContents();
		list_type = ltype;
	}
}

void BrushPanel::SetListType(const wxString& ltype) {
	if (ltype == "small icons") {
		SetListType(BRUSHLIST_SMALL_ICONS);
	} else if (ltype == "large icons") {
		SetListType(BRUSHLIST_LARGE_ICONS);
	} else if (ltype == "listbox") {
		SetListType(BRUSHLIST_LISTBOX);
	} else if (ltype == "textlistbox") {
		SetListType(BRUSHLIST_TEXT_LISTBOX);
	}
}

void BrushPanel::InvalidateContents() {
	sizer->Clear(true);
	loaded = false;
	brushbox = nullptr;
}

void BrushPanel::LoadContents() {
	if (loaded) {
		return;
	}
	loaded = true;
	ASSERT(tileset != nullptr);
	switch (list_type) {
		case BRUSHLIST_LARGE_ICONS:
			brushbox = newd BrushIconBox(this, tileset, RENDER_SIZE_32x32);
			break;
		case BRUSHLIST_SMALL_ICONS:
			brushbox = newd BrushIconBox(this, tileset, RENDER_SIZE_16x16);
			break;
		case BRUSHLIST_LISTBOX:
			brushbox = newd BrushListBox(this, tileset);
			break;
		default:
			break;
	}
	ASSERT(brushbox != nullptr);
	if (has_sort) {
		brushbox->SetSort(sort_key, sort_dir);
	}
	brushbox->SetShowLabels(show_labels);
	brushbox->SetTileSize(tile_size_px);
	if (!filter_query.empty() || has_override_brushes) {
		brushbox->SetFilterQuery(filter_query, has_override_brushes ? &override_brushes : nullptr);
	}
	sizer->Add(brushbox->GetSelfWindow(), 1, wxEXPAND);
	Fit();
	brushbox->SelectFirstBrush();
}

void BrushPanel::SelectFirstBrush() {
	if (loaded) {
		ASSERT(brushbox != nullptr);
		brushbox->SelectFirstBrush();
	}
}

Brush* BrushPanel::GetSelectedBrush() const {
	if (loaded) {
		ASSERT(brushbox != nullptr);
		return brushbox->GetSelectedBrush();
	}

	if (tileset && tileset->size() > 0) {
		return tileset->brushlist[0];
	}
	return nullptr;
}

bool BrushPanel::SelectBrush(const Brush* whatbrush) {
	if (loaded) {
		ASSERT(brushbox != nullptr);
		return brushbox->SelectBrush(whatbrush);
	}
	if (tileset && whatbrush) {
		for (const Brush* b : tileset->brushlist) {
			if (b == whatbrush) {
				LoadContents();
				if (brushbox) {
					return brushbox->SelectBrush(whatbrush);
				}
				break;
			}
		}
	}
	return false;
}

void BrushPanel::OnSwitchIn() {
	LoadContents();
}

void BrushPanel::OnSwitchOut() {
	////
}

void BrushPanel::OnClickListBoxRow(wxCommandEvent& event) {
	Brush* b = GetSelectedBrush();
	if (b) {
		PaletteWindow* palette = GetParentPalette(this);
		if (palette) {
			g_gui.ActivatePalette(palette);
		}
		wxWindow* w = this->GetParent();
		BrushPalettePanel* bpp = nullptr;
		while (w) {
			bpp = dynamic_cast<BrushPalettePanel*>(w);
			if (bpp) {
				break;
			}
			w = w->GetParent();
		}
		if (bpp && bpp->IsFilterAllActive() && palette) {
			int targetCat = 0;
			std::string targetTs;
			for (const auto& res : bpp->GetGlobalSearchResults()) {
				if (res.brush == b) {
					targetCat = res.category;
					targetTs = res.tilesetName;
					break;
				}
			}
			if (palette->JumpToBrush(b, bpp->GetName().ToStdString(), targetCat, targetTs)) {
				return;
			}
		}
		if (tileset) {
			g_gui.SelectBrush(b, tileset->getType());
		} else {
			g_gui.SelectBrushInternal(b);
		}
	}
}

void BrushPanel::SetSort(TilesetSortKey key, TilesetSortDirection dir) {
	has_sort = true;
	sort_key = key;
	sort_dir = dir;
	if (brushbox) {
		brushbox->SetSort(key, dir);
	}
}

void BrushPanel::ClearSort() {
	has_sort = false;
	if (brushbox) {
		brushbox->ClearSort();
	}
}

void BrushPanel::SetShowLabels(bool show) {
	show_labels = show;
	if (brushbox) {
		brushbox->SetShowLabels(show);
	}
}

void BrushPanel::SetTileSize(int sizePx) {
	tile_size_px = sizePx;
	if (brushbox) {
		brushbox->SetTileSize(sizePx);
	}
}

void BrushPanel::SetFilterQuery(const std::string& query, const std::vector<Brush*>* overrideSource) {
	filter_query = query;
	if (overrideSource) {
		override_brushes = *overrideSource;
		has_override_brushes = true;
	} else {
		override_brushes.clear();
		has_override_brushes = false;
	}
	if (brushbox) {
		brushbox->SetFilterQuery(query, overrideSource);
	}
}

// ============================================================================
// BrushIconBox

BEGIN_EVENT_TABLE(BrushIconBox, wxScrolledWindow)
EVT_PAINT(BrushIconBox::OnPaint)
EVT_ERASE_BACKGROUND(BrushIconBox::OnEraseBackground)
EVT_SIZE(BrushIconBox::OnSize)
EVT_LEFT_DOWN(BrushIconBox::OnLeftDown)
EVT_RIGHT_UP(BrushIconBox::OnRightUp)
EVT_MOTION(BrushIconBox::OnMotion)
EVT_LEAVE_WINDOW(BrushIconBox::OnMouseLeave)
EVT_KEY_DOWN(BrushIconBox::OnKeyDown)
END_EVENT_TABLE()

BrushIconBox::BrushIconBox(wxWindow* parent, const TilesetCategory* _tileset, RenderSize rsz) :
	wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxFULL_REPAINT_ON_RESIZE),
	BrushBoxInterface(_tileset),
	icon_size(rsz),
	has_sort(false),
	sort_key(TilesetSortKey::Name),
	sort_dir(TilesetSortDirection::Ascending),
	show_labels(false),
	tile_size_px(32),
	selected_index(-1),
	hover_index(-1),
	m_cols(1),
	has_override_brushes(false) {
	if (icon_size == RENDER_SIZE_16x16) {
		tile_size_px = 16;
	} else if (icon_size == RENDER_SIZE_64x64) {
		tile_size_px = 64;
	} else if (icon_size == RENDER_SIZE_128x128) {
		tile_size_px = 128;
	} else {
		tile_size_px = 32;
	}

	SetBackgroundStyle(wxBG_STYLE_PAINT);
	UpdateDisplayedBrushes();
}

BrushIconBox::~BrushIconBox() {
	////
}

void BrushIconBox::CalculateCellMetrics(int& cell_w, int& cell_h, int& sprite_dim) const {
	sprite_dim = tile_size_px;
	if (sprite_dim != 16 && sprite_dim != 64 && sprite_dim != 128) {
		sprite_dim = 32;
	}

	if (show_labels) {
		if (sprite_dim == 16) {
			cell_w = 76;
			cell_h = 36;
		} else if (sprite_dim == 64) {
			cell_w = 96;
			cell_h = 86;
		} else if (sprite_dim == 128) {
			cell_w = 144;
			cell_h = 150;
		} else {
			cell_w = 84;
			cell_h = 54;
		}
	} else {
		if (sprite_dim == 16) {
			cell_w = 24;
			cell_h = 24;
		} else if (sprite_dim == 64) {
			cell_w = 70;
			cell_h = 70;
		} else if (sprite_dim == 128) {
			cell_w = 134;
			cell_h = 134;
		} else {
			cell_w = 38;
			cell_h = 38;
		}
	}
}

void BrushIconBox::UpdateLayout() {
	int client_w = 0, client_h = 0;
	GetClientSize(&client_w, &client_h);
	if (client_w <= 0) {
		client_w = 200;
	}
	int cell_w, cell_h, sprite_dim;
	CalculateCellMetrics(cell_w, cell_h, sprite_dim);

	m_cols = std::max(1, client_w / std::max(1, cell_w));
	int total_items = static_cast<int>(displayed_brushes.size());
	int total_rows = (total_items + m_cols - 1) / std::max(1, m_cols);
	int total_height = total_rows * cell_h;

	SetVirtualSize(client_w, std::max(total_height, client_h));
	SetScrollRate(0, std::max(10, cell_h / 2));
	Refresh();
}

void BrushIconBox::UpdateDisplayedBrushes() {
	Brush* selectedBefore = GetSelectedBrush();
	const std::vector<Brush*>& src = has_override_brushes ? override_brushes : (tileset ? tileset->brushlist : std::vector<Brush*>());

	displayed_brushes = PaletteModel::FilterBrushes(src, filter_query);

	if (has_sort) {
		PaletteModel::SortBrushes(displayed_brushes, sort_key, sort_dir);
	}

	selected_index = PaletteModel::RestoreSelectionIndex(displayed_brushes, selectedBefore);
	UpdateLayout();
}

void BrushIconBox::OnPaint(wxPaintEvent& event) {
	wxAutoBufferedPaintDC dc(this);
	DoPrepareDC(dc);

	int client_w = 0, client_h = 0;
	GetClientSize(&client_w, &client_h);

	dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Surface)));
	dc.Clear();

	if (displayed_brushes.empty()) {
		return;
	}

	int cell_w, cell_h, sprite_dim;
	CalculateCellMetrics(cell_w, cell_h, sprite_dim);
	if (cell_w <= 0 || cell_h <= 0 || m_cols <= 0) {
		return;
	}

	int start_x, start_y;
	GetViewStart(&start_x, &start_y);
	int ppx, ppy;
	GetScrollPixelsPerUnit(&ppx, &ppy);
	int scroll_y = start_y * ppy;

	int total_items = static_cast<int>(displayed_brushes.size());
	int total_rows = (total_items + m_cols - 1) / std::max(1, m_cols);

	int first_row = std::max(0, scroll_y / cell_h);
	int last_row = std::min(total_rows - 1, (scroll_y + client_h + cell_h) / cell_h);

	wxFont labelFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
	dc.SetFont(labelFont);

	for (int r = first_row; r <= last_row; ++r) {
		for (int c = 0; c < m_cols; ++c) {
			int idx = r * m_cols + c;
			if (idx >= total_items) {
				break;
			}
			Brush* b = displayed_brushes[idx];
			if (!b) {
				continue;
			}

			int x = c * cell_w;
			int y = r * cell_h;
			wxRect cellRect(x + 1, y + 1, cell_w - 2, cell_h - 2);

			const bool isSelected = (idx == selected_index);
			const bool isHover = (idx == hover_index);

			if (isSelected) {
				dc.SetBrush(wxBrush(Theme::Get(Theme::Role::SelectionFill)));
				dc.SetPen(wxPen(Theme::Get(Theme::Role::AccentHover)));
				dc.DrawRoundedRectangle(cellRect, 2);
			} else if (isHover) {
				dc.SetBrush(wxBrush(Theme::Get(Theme::Role::RaisedSurface)));
				dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
				dc.DrawRoundedRectangle(cellRect, 2);
			} else {
				dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Surface)));
				dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
				dc.DrawRoundedRectangle(cellRect, 2);
			}

			Sprite* spr = g_gui.gfx.getSprite(b->getLookID());
			int spr_x = x + (cell_w - sprite_dim) / 2;
			int spr_y = show_labels ? (y + 2) : (y + (cell_h - sprite_dim) / 2);
			if (spr) {
				spr->DrawTo(&dc, SPRITE_SIZE_32x32, spr_x, spr_y, sprite_dim, sprite_dim);
			}

			if (show_labels) {
				dc.SetTextForeground(isSelected ? Theme::Get(Theme::Role::TextOnAccent) : Theme::Get(Theme::Role::Text));
				wxString name = wxstr(b->getName());
				wxCoord tw, th;
				dc.GetTextExtent(name, &tw, &th);
				if (tw > cell_w - 4 && name.length() > 3) {
					while (name.length() > 3 && tw > cell_w - 4) {
						name.RemoveLast();
						dc.GetTextExtent(name + "...", &tw, &th);
					}
					name += "...";
				}
				dc.DrawLabel(name, wxRect(x + 2, y + sprite_dim + 2, cell_w - 4, cell_h - sprite_dim - 4), wxALIGN_CENTER_HORIZONTAL | wxALIGN_TOP);
			}
		}
	}
}

void BrushIconBox::OnEraseBackground(wxEraseEvent& WXUNUSED(event)) {
	// Handled in OnPaint with wxAutoBufferedPaintDC
}

void BrushIconBox::OnSize(wxSizeEvent& event) {
	UpdateLayout();
	event.Skip();
}

void BrushIconBox::OnLeftDown(wxMouseEvent& event) {
	int cell_w, cell_h, sprite_dim;
	CalculateCellMetrics(cell_w, cell_h, sprite_dim);
	if (cell_w <= 0 || cell_h <= 0 || m_cols <= 0) {
		return;
	}

	int vx, vy;
	CalcUnscrolledPosition(event.GetX(), event.GetY(), &vx, &vy);
	int col = vx / cell_w;
	int row = vy / cell_h;
	if (col >= 0 && col < m_cols && row >= 0) {
		int idx = row * m_cols + col;
		if (idx >= 0 && idx < static_cast<int>(displayed_brushes.size())) {
			selected_index = idx;
			Refresh();
			Brush* b = displayed_brushes[idx];
			if (b) {
				PaletteWindow* palette = ::GetParentPalette(this);
				if (palette) {
					g_gui.ActivatePalette(palette);
				}
				wxWindow* w = this->GetParent();
				BrushPalettePanel* bpp = nullptr;
				while (w) {
					bpp = dynamic_cast<BrushPalettePanel*>(w);
					if (bpp) {
						break;
					}
					w = w->GetParent();
				}
				if (bpp && bpp->IsFilterAllActive() && palette) {
					int targetCat = 0;
					std::string targetTs;
					for (const auto& res : bpp->GetGlobalSearchResults()) {
						if (res.brush == b) {
							targetCat = res.category;
							targetTs = res.tilesetName;
							break;
						}
					}
					if (palette->JumpToBrush(b, bpp->GetName().ToStdString(), targetCat, targetTs)) {
						return;
					}
				}
				if (tileset) {
					g_gui.SelectBrush(b, tileset->getType());
				} else {
					g_gui.SelectBrushInternal(b);
				}
			}
		}
	}
}

void BrushIconBox::OnRightUp(wxMouseEvent& event) {
	if (!FavoriteResources::IsCurrentPalette(this)) {
		return;
	}
	int cell_w, cell_h, sprite_dim;
	CalculateCellMetrics(cell_w, cell_h, sprite_dim);
	if (cell_w <= 0 || cell_h <= 0 || m_cols <= 0) {
		return;
	}
	int vx, vy;
	CalcUnscrolledPosition(event.GetX(), event.GetY(), &vx, &vy);
	int col = vx / cell_w;
	int row = vy / cell_h;
	if (col >= 0 && col < m_cols && row >= 0) {
		int idx = row * m_cols + col;
		if (idx >= 0 && idx < static_cast<int>(displayed_brushes.size())) {
			FavoriteResources::Popup(this, displayed_brushes[idx]);
		}
	}
}

void BrushIconBox::OnMotion(wxMouseEvent& event) {
	int cell_w, cell_h, sprite_dim;
	CalculateCellMetrics(cell_w, cell_h, sprite_dim);
	if (cell_w <= 0 || cell_h <= 0 || m_cols <= 0) {
		return;
	}

	int vx, vy;
	CalcUnscrolledPosition(event.GetX(), event.GetY(), &vx, &vy);
	int col = vx / cell_w;
	int row = vy / cell_h;
	int idx = -1;
	if (col >= 0 && col < m_cols && row >= 0) {
		int test_idx = row * m_cols + col;
		if (test_idx >= 0 && test_idx < static_cast<int>(displayed_brushes.size())) {
			idx = test_idx;
		}
	}
	if (idx != hover_index) {
		hover_index = idx;
		if (hover_index != -1 && hover_index < static_cast<int>(displayed_brushes.size())) {
			SetToolTip(wxstr(displayed_brushes[hover_index]->getName()));
		} else {
			UnsetToolTip();
		}
		Refresh();
	}
}

void BrushIconBox::OnMouseLeave(wxMouseEvent& event) {
	if (hover_index != -1) {
		hover_index = -1;
		UnsetToolTip();
		Refresh();
	}
	event.Skip();
}

void BrushIconBox::OnKeyDown(wxKeyEvent& event) {
	int key = event.GetKeyCode();
	int total = static_cast<int>(displayed_brushes.size());
	if (total == 0) {
		event.Skip();
		return;
	}
	int new_sel = selected_index;
	if (key == WXK_LEFT) {
		new_sel = std::max(0, selected_index - 1);
	} else if (key == WXK_RIGHT) {
		new_sel = std::min(total - 1, selected_index + 1);
	} else if (key == WXK_UP) {
		new_sel = std::max(0, selected_index - m_cols);
	} else if (key == WXK_DOWN) {
		new_sel = std::min(total - 1, selected_index + m_cols);
	} else {
		event.Skip();
		return;
	}

	if (new_sel != selected_index) {
		selected_index = new_sel;
		EnsureVisible(selected_index);
		Refresh();
		Brush* b = displayed_brushes[selected_index];
		if (b) {
			if (tileset) {
				g_gui.SelectBrush(b, tileset->getType());
			} else {
				g_gui.SelectBrushInternal(b);
			}
		}
	}
}

void BrushIconBox::EnsureVisible(size_t n) {
	if (n >= displayed_brushes.size() || m_cols <= 0) {
		return;
	}
	int cell_w, cell_h, sprite_dim;
	CalculateCellMetrics(cell_w, cell_h, sprite_dim);
	if (cell_h <= 0) {
		return;
	}

	int row = static_cast<int>(n) / m_cols;
	int target_y = row * cell_h;
	int ppx, ppy;
	GetScrollPixelsPerUnit(&ppx, &ppy);
	if (ppy <= 0) {
		ppy = 20;
	}

	int start_x, start_y;
	GetViewStart(&start_x, &start_y);
	int client_w, client_h;
	GetClientSize(&client_w, &client_h);

	int cur_top = start_y * ppy;
	int cur_bot = cur_top + client_h;

	if (target_y < cur_top || target_y + cell_h > cur_bot) {
		Scroll(-1, target_y / ppy);
	}
}

void BrushIconBox::SelectFirstBrush() {
	if (!displayed_brushes.empty()) {
		selected_index = 0;
		EnsureVisible(0);
		Refresh();
	}
}

Brush* BrushIconBox::GetSelectedBrush() const {
	if (selected_index >= 0 && selected_index < static_cast<int>(displayed_brushes.size())) {
		return displayed_brushes[selected_index];
	}
	if (!displayed_brushes.empty()) {
		return displayed_brushes[0];
	}
	return nullptr;
}

bool BrushIconBox::SelectBrush(const Brush* whatbrush) {
	for (size_t i = 0; i < displayed_brushes.size(); ++i) {
		if (displayed_brushes[i] == whatbrush) {
			selected_index = static_cast<int>(i);
			EnsureVisible(i);
			Refresh();
			return true;
		}
	}
	selected_index = -1;
	Refresh();
	return false;
}

void BrushIconBox::SetSort(TilesetSortKey key, TilesetSortDirection dir) {
	has_sort = true;
	sort_key = key;
	sort_dir = dir;
	UpdateDisplayedBrushes();
}

void BrushIconBox::ClearSort() {
	has_sort = false;
	UpdateDisplayedBrushes();
}

void BrushIconBox::SetShowLabels(bool show) {
	if (show_labels != show) {
		show_labels = show;
		UpdateLayout();
		Refresh();
	}
}

void BrushIconBox::SetTileSize(int sizePx) {
	if (tile_size_px != sizePx) {
		tile_size_px = sizePx;
		UpdateLayout();
		Refresh();
	}
}

void BrushIconBox::SetFilterQuery(const std::string& query, const std::vector<Brush*>* overrideSource) {
	filter_query = query;
	if (overrideSource) {
		override_brushes = *overrideSource;
		has_override_brushes = true;
	} else {
		override_brushes.clear();
		has_override_brushes = false;
	}
	UpdateDisplayedBrushes();
}

// ============================================================================
// BrushListBox

BEGIN_EVENT_TABLE(BrushListBox, wxVListBox)
EVT_KEY_DOWN(BrushListBox::OnKey)
END_EVENT_TABLE()

BrushListBox::BrushListBox(wxWindow* parent, const TilesetCategory* tileset) :
	wxVListBox(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE),
	BrushBoxInterface(tileset),
	has_sort(false),
	sort_key(TilesetSortKey::Name),
	sort_dir(TilesetSortDirection::Ascending),
	has_override_brushes(false) {
	SetBackgroundColour(Theme::Get(Theme::Role::Background));
	SetForegroundColour(Theme::Get(Theme::Role::Text));
	SetSelectionBackground(Theme::Get(Theme::Role::SelectionFill));

	Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent& event) {
		if (!FavoriteResources::IsCurrentPalette(this)) {
			return;
		}
		const int index = VirtualHitTest(event.GetY());
		if (index != wxNOT_FOUND && static_cast<size_t>(index) < this->displayed_brushes.size()) {
			FavoriteResources::Popup(this, this->displayed_brushes[index]);
		}
	});

	UpdateDisplayedBrushes();
}

BrushListBox::~BrushListBox() {
	////
}

void BrushListBox::UpdateDisplayedBrushes() {
	Brush* selectedBefore = GetSelectedBrush();
	const std::vector<Brush*>& src = has_override_brushes ? override_brushes : (tileset ? tileset->brushlist : std::vector<Brush*>());

	displayed_brushes = PaletteModel::FilterBrushes(src, filter_query);

	if (has_sort) {
		PaletteModel::SortBrushes(displayed_brushes, sort_key, sort_dir);
	}

	SetItemCount(displayed_brushes.size());
	int newSel = PaletteModel::RestoreSelectionIndex(displayed_brushes, selectedBefore);
	SetSelection(newSel != -1 ? newSel : wxNOT_FOUND);
	Refresh();
}

void BrushListBox::SetSort(TilesetSortKey key, TilesetSortDirection dir) {
	has_sort = true;
	sort_key = key;
	sort_dir = dir;
	UpdateDisplayedBrushes();
}

void BrushListBox::ClearSort() {
	has_sort = false;
	UpdateDisplayedBrushes();
}

void BrushListBox::SetFilterQuery(const std::string& query, const std::vector<Brush*>* overrideSource) {
	filter_query = query;
	if (overrideSource) {
		override_brushes = *overrideSource;
		has_override_brushes = true;
	} else {
		override_brushes.clear();
		has_override_brushes = false;
	}
	UpdateDisplayedBrushes();
}

void BrushListBox::SelectFirstBrush() {
	if (!displayed_brushes.empty()) {
		SetSelection(0);
		wxWindow::ScrollLines(-1);
	}
}

Brush* BrushListBox::GetSelectedBrush() const {
	if (displayed_brushes.empty()) {
		return nullptr;
	}

	int n = GetSelection();
	if (n != wxNOT_FOUND && static_cast<size_t>(n) < displayed_brushes.size()) {
		return displayed_brushes[n];
	} else if (!displayed_brushes.empty()) {
		return displayed_brushes[0];
	}
	return nullptr;
}

bool BrushListBox::SelectBrush(const Brush* whatbrush) {
	for (size_t n = 0; n < displayed_brushes.size(); ++n) {
		if (displayed_brushes[n] == whatbrush) {
			SetSelection(static_cast<int>(n));
			return true;
		}
	}
	return false;
}

void BrushListBox::OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const {
	if (n >= displayed_brushes.size() || !displayed_brushes[n]) {
		return;
	}
	Sprite* spr = g_gui.gfx.getSprite(displayed_brushes[n]->getLookID());
	if (spr) {
		spr->DrawTo(&dc, SPRITE_SIZE_32x32, rect.GetX(), rect.GetY(), rect.GetWidth(), rect.GetHeight());
	}
	if (IsSelected(n)) {
		dc.SetTextForeground(Theme::Get(Theme::Role::TextOnAccent));
	} else {
		dc.SetTextForeground(Theme::Get(Theme::Role::Text));
	}
	dc.DrawText(wxstr(displayed_brushes[n]->getName()), rect.GetX() + 40, rect.GetY() + 6);
}

wxCoord BrushListBox::OnMeasureItem(size_t n) const {
	return 32;
}

void BrushListBox::OnKey(wxKeyEvent& event) {
	switch (event.GetKeyCode()) {
		case WXK_UP:
		case WXK_DOWN:
		case WXK_LEFT:
		case WXK_RIGHT:
			if (g_settings.getInteger(Config::LISTBOX_EATS_ALL_EVENTS)) {
				case WXK_PAGEUP:
				case WXK_PAGEDOWN:
				case WXK_HOME:
				case WXK_END:
					event.Skip(true);
			} else {
				[[fallthrough]];
				default:
					if (g_gui.GetCurrentTab() != nullptr) {
						g_gui.GetCurrentMapTab()->GetEventHandler()->AddPendingEvent(event);
					}
			}
	}
}
