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
	m_searchCtrl = newd wxSearchCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	m_searchCtrl->ShowSearchButton(true);
	m_searchCtrl->ShowCancelButton(true);
	m_searchCtrl->SetDescriptiveText("Search brushes...");
	if (!m_filterQuery.empty()) {
		m_searchCtrl->ChangeValue(wxstr(m_filterQuery));
	}
	m_searchCtrl->Bind(wxEVT_TEXT, &BrushPalettePanel::OnSearchText, this);
	m_searchCtrl->Bind(wxEVT_TEXT_ENTER, &BrushPalettePanel::OnSearchText, this);
	m_searchCtrl->Bind(wxEVT_SEARCHCTRL_SEARCH_BTN, &BrushPalettePanel::OnSearchText, this);
	m_searchCtrl->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, &BrushPalettePanel::OnSearchCancel, this);
	m_searchCtrl->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
		if (event.GetKeyCode() == WXK_ESCAPE) {
			ResetFilter();
		} else {
			event.Skip();
		}
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
	auto* item32 = menu.AppendRadioItem(MENU_SIZE_32, "32x32");
	auto* item64 = menu.AppendRadioItem(MENU_SIZE_64, "64x64");
	auto* item128 = menu.AppendRadioItem(MENU_SIZE_128, "128x128");

	if (m_tileSize == 64) {
		item64->Check(true);
	} else if (m_tileSize == 128) {
		item128->Check(true);
	} else {
		item32->Check(true);
	}

	wxRect rect = toolbar->GetToolRect(toolId);
	wxPoint pos(rect.x, rect.y + rect.height);
	int selected = toolbar->GetPopupMenuSelectionFromUser(menu, pos);
	if (selected == MENU_SIZE_32) {
		SetTileSize(32);
	} else if (selected == MENU_SIZE_64) {
		SetTileSize(64);
	} else if (selected == MENU_SIZE_128) {
		SetTileSize(128);
	}
}

void BrushPalettePanel::OnSearchText(wxCommandEvent& event) {
	if (m_searchCtrl) {
		m_filterQuery = m_searchCtrl->GetValue().ToStdString();
		SavePaletteFilters();
		ApplyFilter();

		const auto eventType = event.GetEventType();
		if ((eventType == wxEVT_TEXT_ENTER || eventType == wxEVT_SEARCHCTRL_SEARCH_BTN) && choicebook) {
			PaletteWindow* pw = GetParentPalette();
			if (pw) {
				g_gui.ActivatePalette(pw);
			}

			auto* panel = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage());
			if (panel) {
				panel->SelectFirstBrush();
				Brush* brush = panel->GetSelectedBrush();
				if (brush) {
					if (m_filterAll && pw) {
						pw->JumpToBrush(brush, GetName().ToStdString());
					} else {
						g_gui.SelectBrushInternal(brush);
					}
				}
			}
		}
	}
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
		std::vector<Brush*> allBrushes;
		std::unordered_set<const Brush*> seen;
		for (const auto& kv : *m_tilesets) {
			const Tileset* ts = kv.second;
			if (!ts) {
				continue;
			}
			for (int cat = TILESET_UNKNOWN; cat <= TILESET_HOUSE; ++cat) {
				const TilesetCategory* tcg = ts->getCategory(static_cast<TilesetCategoryType>(cat));
				if (!tcg) {
					continue;
				}
				for (Brush* b : tcg->brushlist) {
					if (b && seen.insert(b).second) {
						allBrushes.push_back(b);
					}
				}
			}
		}
		panel->SetFilterQuery(m_filterQuery, &allBrushes);
		if (choicebook->GetChoiceCtrl()) {
			choicebook->GetChoiceCtrl()->Enable(false);
		}
	} else {
		panel->SetFilterQuery(m_filterQuery, nullptr);
		if (choicebook->GetChoiceCtrl()) {
			choicebook->GetChoiceCtrl()->Enable(true);
		}
	}
}

void BrushPalettePanel::ResetFilter() {
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
			if (palette->JumpToBrush(b, bpp->GetName().ToStdString())) {
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
EVT_TOGGLEBUTTON(wxID_ANY, BrushIconBox::OnClickBrushButton)
END_EVENT_TABLE()

BrushIconBox::BrushIconBox(wxWindow* parent, const TilesetCategory* _tileset, RenderSize rsz) :
	wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL),
	BrushBoxInterface(_tileset),
	icon_size(rsz),
	has_sort(false),
	sort_key(TilesetSortKey::Name),
	sort_dir(TilesetSortDirection::Ascending),
	show_labels(false),
	tile_size_px(32),
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

	RebuildButtons();
}

BrushIconBox::~BrushIconBox() {
	////
}

void BrushIconBox::DeselectAll() {
	for (auto* btn : brush_buttons) {
		if (btn) {
			btn->SetValue(false);
		}
	}
}

void BrushIconBox::SetSort(TilesetSortKey key, TilesetSortDirection dir) {
	has_sort = true;
	sort_key = key;
	sort_dir = dir;
	RebuildButtons();
}

void BrushIconBox::ClearSort() {
	has_sort = false;
	RebuildButtons();
}

void BrushIconBox::SetShowLabels(bool show) {
	if (show_labels != show) {
		show_labels = show;
		RebuildButtons();
	}
}

void BrushIconBox::SetTileSize(int sizePx) {
	if (tile_size_px != sizePx) {
		tile_size_px = sizePx;
		RebuildButtons();
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
	RebuildButtons();
}

void BrushIconBox::RebuildButtons() {
	DeselectAll();
	brush_buttons.clear();

	wxSizer* existing = GetSizer();
	if (existing) {
		existing->Clear(true);
	}

	displayed_brushes.clear();
	const std::vector<Brush*>& src = has_override_brushes ? override_brushes : (tileset ? tileset->brushlist : std::vector<Brush*>());

	std::string queryLower = filter_query;
	for (char& c : queryLower) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}

	for (Brush* b : src) {
		if (!b) {
			continue;
		}
		if (!queryLower.empty()) {
			std::string nameLower = b->getName();
			for (char& c : nameLower) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			std::string idStr = std::to_string(b->getID());
			if (nameLower.find(queryLower) == std::string::npos && idStr.find(queryLower) == std::string::npos) {
				continue;
			}
		}
		displayed_brushes.push_back(b);
	}

	if (has_sort) {
		std::stable_sort(displayed_brushes.begin(), displayed_brushes.end(), [this](const Brush* a, const Brush* b) {
			if (!a || !b) {
				return a != nullptr;
			}
			if (sort_key == TilesetSortKey::ID) {
				if (a->getID() != b->getID()) {
					return sort_dir == TilesetSortDirection::Ascending ? (a->getID() < b->getID()) : (a->getID() > b->getID());
				}
			} else {
				std::string na = a->getName();
				std::string nb = b->getName();
				for (char& c : na) {
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}
				for (char& c : nb) {
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}
				if (na != nb) {
					return sort_dir == TilesetSortDirection::Ascending ? (na < nb) : (na > nb);
				}
			}
			return a->getID() < b->getID();
		});
	}

	RenderSize rsz = RENDER_SIZE_32x32;
	if (tile_size_px == 128) {
		rsz = RENDER_SIZE_128x128;
	} else if (tile_size_px == 64) {
		rsz = RENDER_SIZE_64x64;
	} else if (tile_size_px == 16) {
		rsz = RENDER_SIZE_16x16;
	} else {
		rsz = RENDER_SIZE_32x32;
	}
	icon_size = rsz;

	int width;
	if (tile_size_px == 128) {
		width = std::max(1, g_settings.getInteger(Config::PALETTE_COL_COUNT) / 4);
	} else if (tile_size_px == 64) {
		width = std::max(1, g_settings.getInteger(Config::PALETTE_COL_COUNT) / 2);
	} else if (show_labels) {
		width = std::max(1, g_settings.getInteger(Config::PALETTE_COL_COUNT) / 2);
	} else {
		width = std::max(1, g_settings.getInteger(Config::PALETTE_COL_COUNT));
	}

	wxSizer* stacksizer = newd wxBoxSizer(wxVERTICAL);
	wxSizer* rowsizer = nullptr;
	int item_counter = 0;

	for (Brush* b : displayed_brushes) {
		++item_counter;
		if (!rowsizer) {
			rowsizer = newd wxBoxSizer(wxHORIZONTAL);
		}

		auto* bb = newd BrushButton(this, b, rsz, wxID_ANY, show_labels);
		rowsizer->Add(bb);
		brush_buttons.push_back(bb);

		if (item_counter % width == 0) {
			stacksizer->Add(rowsizer);
			rowsizer = nullptr;
		}
	}
	if (rowsizer) {
		stacksizer->Add(rowsizer);
	}

	int row_height = (tile_size_px == 128 ? 132 : tile_size_px == 64 ? 68
																	 : 36)
		+ (show_labels ? 16 : 0);
	int total_rows = (item_counter + width - 1) / std::max(1, width);
	SetScrollbars(20, 20, 8, std::max(1, total_rows * row_height / 20), 0, 0);
	SetSizer(stacksizer);
	Layout();
	Refresh();
}

void BrushIconBox::SelectFirstBrush() {
	if (!brush_buttons.empty()) {
		DeselectAll();
		brush_buttons[0]->SetValue(true);
		EnsureVisible((size_t)0);
	}
}

Brush* BrushIconBox::GetSelectedBrush() const {
	for (auto* btn : brush_buttons) {
		if (btn && btn->GetValue()) {
			return btn->brush;
		}
	}
	return nullptr;
}

bool BrushIconBox::SelectBrush(const Brush* whatbrush) {
	DeselectAll();
	for (auto* btn : brush_buttons) {
		if (btn && btn->brush == whatbrush) {
			btn->SetValue(true);
			EnsureVisible(btn);
			return true;
		}
	}
	return false;
}

void BrushIconBox::EnsureVisible(BrushButton* btn) {
	if (!btn) {
		return;
	}
	int x, y;
	btn->GetPosition(&x, &y);
	int scrollUnitX, scrollUnitY;
	GetScrollPixelsPerUnit(&scrollUnitX, &scrollUnitY);
	if (scrollUnitY <= 0) {
		scrollUnitY = 20;
	}
	int scrollPosY = y / scrollUnitY;
	int startScrollPosX, startScrollPosY;
	GetViewStart(&startScrollPosX, &startScrollPosY);
	int clientSizeX, clientSizeY;
	GetClientSize(&clientSizeX, &clientSizeY);
	int endScrollPosY = startScrollPosY + clientSizeY / scrollUnitY;
	if (scrollPosY < startScrollPosY || scrollPosY > endScrollPosY) {
		Scroll(-1, scrollPosY);
	}
}

void BrushIconBox::EnsureVisible(size_t n) {
	if (n < brush_buttons.size()) {
		EnsureVisible(brush_buttons[n]);
	}
}

void BrushIconBox::OnClickBrushButton(wxCommandEvent& event) {
	wxObject* obj = event.GetEventObject();
	auto* btn = dynamic_cast<BrushButton*>(obj);
	if (btn) {
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
			if (palette->JumpToBrush(btn->brush, bpp->GetName().ToStdString())) {
				return;
			}
		}
		if (tileset) {
			g_gui.SelectBrush(btn->brush, tileset->getType());
		} else {
			g_gui.SelectBrushInternal(btn->brush);
		}
	}
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
	displayed_brushes.clear();
	const std::vector<Brush*>& src = has_override_brushes ? override_brushes : (tileset ? tileset->brushlist : std::vector<Brush*>());

	std::string queryLower = filter_query;
	for (char& c : queryLower) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}

	for (Brush* b : src) {
		if (!b) {
			continue;
		}
		if (!queryLower.empty()) {
			std::string nameLower = b->getName();
			for (char& c : nameLower) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			std::string idStr = std::to_string(b->getID());
			if (nameLower.find(queryLower) == std::string::npos && idStr.find(queryLower) == std::string::npos) {
				continue;
			}
		}
		displayed_brushes.push_back(b);
	}

	if (has_sort) {
		std::stable_sort(displayed_brushes.begin(), displayed_brushes.end(), [this](const Brush* a, const Brush* b) {
			if (!a || !b) {
				return a != nullptr;
			}
			if (sort_key == TilesetSortKey::ID) {
				if (a->getID() != b->getID()) {
					return sort_dir == TilesetSortDirection::Ascending ? (a->getID() < b->getID()) : (a->getID() > b->getID());
				}
			} else {
				std::string na = a->getName();
				std::string nb = b->getName();
				for (char& c : na) {
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}
				for (char& c : nb) {
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}
				if (na != nb) {
					return sort_dir == TilesetSortDirection::Ascending ? (na < nb) : (na > nb);
				}
			}
			return a->getID() < b->getID();
		});
	}

	SetItemCount(displayed_brushes.size());
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
