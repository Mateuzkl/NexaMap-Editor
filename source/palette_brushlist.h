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

#ifndef RME_PALETTE_BRUSHLIST_
#define RME_PALETTE_BRUSHLIST_

#include "main.h"
#include "palette_common.h"
#include "palette_brush_tool.h"

#include "palette_model.h"

#include <wx/aui/auibar.h>
#include <wx/srchctrl.h>

#include <string_view>

enum BrushListType {
	BRUSHLIST_LARGE_ICONS,
	BRUSHLIST_SMALL_ICONS,
	BRUSHLIST_LISTBOX,
	BRUSHLIST_TEXT_LISTBOX,
};

class PaletteSearchCtrl final : public wxSearchCtrl {
public:
	using wxSearchCtrl::wxSearchCtrl;

	bool IsTopNavigationDomain(NavigationKind kind) const override {
		if (kind == Navigation_Accel) {
			return true;
		}
		return wxSearchCtrl::IsTopNavigationDomain(kind);
	}

#ifdef __WXMSW__
	bool MSWTranslateMessage(WXMSG* msg) override {
		return false;
	}

	bool MSWShouldPreProcessMessage(WXMSG* msg) override {
		return false;
	}
#endif
};

class BrushBoxInterface {
public:
	BrushBoxInterface(const TilesetCategory* _tileset) :
		tileset(_tileset), loaded(false) {
		ASSERT(tileset);
	}
	virtual ~BrushBoxInterface() { }

	virtual wxWindow* GetSelfWindow() = 0;

	// Select the first brush
	virtual void SelectFirstBrush() = 0;
	// Returns the currently selected brush (First brush if panel is not loaded)
	virtual Brush* GetSelectedBrush() const = 0;
	// Select the brush in the parameter, this only changes the look of the panel
	virtual bool SelectBrush(const Brush* brush) = 0;

	virtual void SetSort(TilesetSortKey key, TilesetSortDirection dir) { }
	virtual void ClearSort() { }
	virtual void SetShowLabels(bool show) { }
	virtual void SetTileSize(int sizePx) { }
	virtual void SetFilterQuery(const std::string& query, const std::vector<Brush*>* overrideSource = nullptr) { }

protected:
	const TilesetCategory* const tileset;
	bool loaded;
};

class BrushListBox : public wxVListBox, public BrushBoxInterface {
public:
	BrushListBox(wxWindow* parent, const TilesetCategory* _tileset);
	~BrushListBox() override;

	wxWindow* GetSelfWindow() override {
		return this;
	}

	// Select the first brush
	void SelectFirstBrush() override;
	// Returns the currently selected brush (First brush if panel is not loaded)
	Brush* GetSelectedBrush() const override;
	// Select the brush in the parameter, this only changes the look of the panel
	bool SelectBrush(const Brush* brush) override;

	// Event handlers
	void OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const override;
	wxCoord OnMeasureItem(size_t n) const override;

	void OnKey(wxKeyEvent& event);

	void SetSort(TilesetSortKey key, TilesetSortDirection dir) override;
	void ClearSort() override;
	void SetFilterQuery(const std::string& query, const std::vector<Brush*>* overrideSource = nullptr) override;

protected:
	void UpdateDisplayedBrushes();

	std::vector<Brush*> displayed_brushes;
	bool has_sort = false;
	TilesetSortKey sort_key = TilesetSortKey::Name;
	TilesetSortDirection sort_dir = TilesetSortDirection::Ascending;
	std::string filter_query;
	std::vector<Brush*> override_brushes;
	bool has_override_brushes = false;

	DECLARE_EVENT_TABLE();
};

class BrushIconBox : public wxScrolledWindow, public BrushBoxInterface {
public:
	BrushIconBox(wxWindow* parent, const TilesetCategory* _tileset, RenderSize rsz);
	~BrushIconBox() override;

	wxWindow* GetSelfWindow() override {
		return this;
	}

	void EnsureVisible(size_t n);
	void SelectFirstBrush() override;
	// Returns the currently selected brush (First brush if panel is not loaded)
	Brush* GetSelectedBrush() const override;
	// Select the brush in the parameter, this only changes the look of the panel
	bool SelectBrush(const Brush* brush) override;

	void SetSort(TilesetSortKey key, TilesetSortDirection dir) override;
	void ClearSort() override;
	void SetShowLabels(bool show) override;
	void SetTileSize(int sizePx) override;
	void SetFilterQuery(const std::string& query, const std::vector<Brush*>* overrideSource = nullptr) override;

	void OnPaint(wxPaintEvent& event);
	void OnEraseBackground(wxEraseEvent& event);
	void OnSize(wxSizeEvent& event);
	void OnLeftDown(wxMouseEvent& event);
	void OnRightUp(wxMouseEvent& event);
	void OnMotion(wxMouseEvent& event);
	void OnMouseLeave(wxMouseEvent& event);
	void OnKeyDown(wxKeyEvent& event);

protected:
	void UpdateDisplayedBrushes();
	void UpdateLayout();
	void CalculateCellMetrics(int& cell_w, int& cell_h, int& sprite_dim) const;

protected:
	std::vector<Brush*> displayed_brushes;
	RenderSize icon_size;
	bool has_sort = false;
	TilesetSortKey sort_key = TilesetSortKey::Name;
	TilesetSortDirection sort_dir = TilesetSortDirection::Ascending;
	bool show_labels = false;
	int tile_size_px = 32;
	int selected_index = -1;
	int hover_index = -1;
	int m_cols = 1;
	std::string filter_query;
	std::vector<Brush*> override_brushes;
	bool has_override_brushes = false;

	DECLARE_EVENT_TABLE();
};

// A panel capable of displaying a collection of brushes
class BrushPanel : public wxPanel {
public:
	BrushPanel(wxWindow* parent);
	~BrushPanel() override;

	// Interface
	void InvalidateContents();
	void LoadContents();

	// Sets the display type (list or icons)
	void SetListType(BrushListType ltype);
	void SetListType(const wxString& ltype);
	// Assigns a tileset to this list
	void AssignTileset(const TilesetCategory* tileset);

	const TilesetCategory* GetTileset() const {
		return tileset;
	}

	void SetSort(TilesetSortKey key, TilesetSortDirection dir);
	void ClearSort();
	void SetShowLabels(bool show);
	void SetTileSize(int sizePx);
	void SetFilterQuery(const std::string& query, const std::vector<Brush*>* overrideSource = nullptr);

	// Select the first brush
	void SelectFirstBrush();
	// Returns the currently selected brush (First brush if panel is not loaded)
	Brush* GetSelectedBrush() const;
	// Select the brush in the parameter, this only changes the look of the panel
	bool SelectBrush(const Brush* whatbrush);

	// Called when the window is about to be displayed
	void OnSwitchIn();
	// Called when this page is hidden
	void OnSwitchOut();

	// wxWidgets event handlers
	void OnClickListBoxRow(wxCommandEvent& event);

protected:
	const TilesetCategory* tileset;
	wxSizer* sizer;
	BrushBoxInterface* brushbox;
	bool loaded;
	BrushListType list_type;

	bool has_sort = false;
	TilesetSortKey sort_key = TilesetSortKey::Name;
	TilesetSortDirection sort_dir = TilesetSortDirection::Ascending;
	bool show_labels = false;
	int tile_size_px = 32;
	std::string filter_query;
	std::vector<Brush*> override_brushes;
	bool has_override_brushes = false;

	DECLARE_EVENT_TABLE();
};

class BrushPalettePanel : public PalettePanel {
public:
	enum ToolID {
		TOOL_SORT_AZ = wxID_HIGHEST + 6001,
		TOOL_SORT_ZA,
		TOOL_TOGGLE_LABELS,
		TOOL_CHANGE_SIZE,
		TOOL_FILTER_ALL,
	};

	enum MenuID {
		MENU_SORT_BY_ID = wxID_HIGHEST + 6011,
		MENU_SORT_BY_NAME,
		MENU_SORT_DEFAULT,
		MENU_SIZE_16,
		MENU_SIZE_32,
		MENU_SIZE_64,
		MENU_SIZE_128,
	};

	BrushPalettePanel(wxWindow* parent, const TilesetContainer& tilesets, TilesetCategoryType category, wxWindowID id = wxID_ANY);
	~BrushPalettePanel() override;

	// Interface
	void InvalidateContents() override;
	void LoadCurrentContents() override;
	void LoadAllContents() override;

	PaletteType GetType() const override;

	// Sets the display type (list or icons)
	void SetListType(BrushListType ltype);
	void SetListType(const wxString& ltype);

	// Select the first brush
	void SelectFirstBrush() override;
	// Returns the currently selected brush (first brush if panel is not loaded)
	Brush* GetSelectedBrush() const override;
	// Select the brush in the parameter, this only changes the look of the panel
	bool SelectBrush(const Brush* whatbrush) override;

	// Called when this page is displayed
	void OnSwitchIn() override;

	// Event handler for child window
	void OnSwitchingPage(wxChoicebookEvent& event);
	void OnPageChanged(wxChoicebookEvent& event);
	void OnClickAddTileset(wxCommandEvent& WXUNUSED(event));
	void OnClickAddItemToTileset(wxCommandEvent& WXUNUSED(event));

	// Toolbar operations
	void SetSort(TilesetSortKey key, TilesetSortDirection dir);
	void ClearSort();
	void SetShowLabels(bool show);
	void SetTileSize(int sizePx);
	void ApplyTheme();

	// Search and filter operations
	void ResetFilter();
	bool JumpToTilesetAndBrush(std::string_view tilesetName, const Brush* brush);
	std::string FindTilesetNameForBrush(const Brush* brush) const;
	bool IsFilterAllActive() const {
		return m_filterAll && !m_filterQuery.empty();
	}
	const std::vector<PaletteSearchResult>& GetGlobalSearchResults() const {
		return m_globalResults;
	}

protected:
	void OnToolClick(wxCommandEvent& event);
	void OnSortButtonClick(TilesetSortDirection dir, int toolId);
	void OnSizeButtonClick(int toolId);
	void OnSearchText(wxCommandEvent& event);
	void OnSearchCancel(wxCommandEvent& event);
	void OnDebounceTimer(wxTimerEvent& event);
	void ApplyFilter();

	void LoadPaletteFilters();
	void SavePaletteFilters();

	PaletteType palette_type;
	wxChoicebook* choicebook;
	BrushSizePanel* size_panel;
	std::map<wxWindow*, Brush*> remembered_brushes;

	wxAuiToolBar* toolbar;
	PaletteSearchCtrl* m_searchCtrl;
	wxAuiToolBar* m_searchToolbar;
	wxTimer m_debounceTimer;
	std::string m_filterQuery;
	bool m_filterAll;
	std::vector<PaletteSearchResult> m_globalResults;

	TilesetSortKey m_sortKey;
	TilesetSortDirection m_sortDir;
	bool m_hasSort;
	bool m_showLabels;
	int m_tileSize;

	const TilesetContainer* m_tilesets;

	DECLARE_EVENT_TABLE();
};

#endif
