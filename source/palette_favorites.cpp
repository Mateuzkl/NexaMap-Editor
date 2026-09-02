#include "main.h"
#include "palette_favorites.h"
#include "favorites_resources.h"
#include "editor_resource_session.h"
#include "gui.h"
#include "map_tab.h"
#include "map_display.h"
#include "palette_saved_terrain.h"
#include "theme.h"

#include <algorithm>
#include <wx/dcbuffer.h>

class FavoritesPalettePanel::Grid : public wxScrolledWindow {
public:
	explicit Grid(FavoritesPalettePanel* owner) :
		wxScrolledWindow(owner, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE), owner_(*owner) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetScrollRate(0, FromDIP(20));
		SetName("Favorites grid");
		Bind(wxEVT_PAINT, &Grid::OnPaint, this);
		Bind(wxEVT_SIZE, [this](wxSizeEvent& event) { LayoutEntries(); event.Skip(); });
		Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
			const wxPoint point = CalcUnscrolledPosition(event.GetPosition());
			for (const auto& header : headers_) {
				if (header.rect.Contains(point)) {
					if (owner_.collapsed_.contains(header.category)) {
						owner_.collapsed_.erase(header.category);
					} else {
						owner_.collapsed_.insert(header.category);
					}
					LayoutEntries();
					return;
				}
			}
			if (const Cell* cell = Hit(point)) {
				const auto entry = cell->entry;
				owner_.Use(entry);
			}
		});
		Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent& event) {
			if (!owner_.HasCurrentResources()) {
				return;
			}
			if (const Cell* cell = Hit(CalcUnscrolledPosition(event.GetPosition()))) {
				wxMenu menu;
				FavoriteResources::AppendMenu(menu, this, cell->entry);
				PopupMenu(&menu);
			}
		});
		Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
			if (const Cell* cell = Hit(CalcUnscrolledPosition(event.GetPosition()))) {
				if (hover_ != cell->tooltip) {
					hover_ = cell->tooltip;
					SetToolTip(hover_);
				}
			} else if (!hover_.empty()) {
				hover_.clear();
				UnsetToolTip();
			}
			event.Skip();
		});
	}

	void Reload() {
		cells_.clear();
		if (owner_.HasCurrentResources()) {
			const wxString query = owner_.search_->GetValue().Lower().Trim().Trim(false);
			for (const auto& entry : g_gui.GetFavorites().entries()) {
				if (entry.context != owner_.context_) {
					continue;
				}
				const wxString searchable = wxString::FromUTF8(entry.displayName + " " + entry.stableId + " " + entry.tileset) + " " + FavoritesManager::categoryName(entry.category) + wxString::Format(" %u %u", entry.itemId, entry.clientId);
				if (!query.empty() && !searchable.Lower().Contains(query)) {
					continue;
				}
				const bool available = FavoriteResources::IsAvailable(entry);
				const int pixels = FromDIP(40);
				const std::string key = std::to_string(static_cast<int>(entry.kind)) + ":" + entry.stableId + ":" + entry.definition + ":" + std::to_string(pixels);
				wxBitmap bitmap;
				if (available) {
					auto found = owner_.previews_.find(key);
					if (found == owner_.previews_.end()) {
						found = owner_.previews_.emplace(key, FavoriteResources::Preview(entry, pixels)).first;
					}
					bitmap = found->second;
				}
				cells_.push_back({ entry, {}, bitmap, FavoriteResources::Tooltip(entry), available });
			}
		}
		LayoutEntries();
	}

private:
	struct Cell {
		FavoriteEntry entry;
		wxRect rect;
		wxBitmap preview;
		wxString tooltip;
		bool available;
	};
	struct Header {
		FavoriteCategory category;
		wxRect rect;
		bool expanded;
		size_t count;
	};
	const Cell* Hit(const wxPoint& point) const {
		for (const auto& cell : cells_) {
			if (!cell.rect.IsEmpty() && cell.rect.Contains(point)) {
				return &cell;
			}
		}
		return nullptr;
	}
	void LayoutEntries() {
		headers_.clear();
		for (auto& cell : cells_) {
			cell.rect = {};
		}
		const int gap = FromDIP(4);
		const int width = std::max(1, GetClientSize().x - gap * 2);
		const int columns = std::max(1, width / FromDIP(74));
		const int cellWidth = width / columns;
		const int cellHeight = FromDIP(72);
		int y = gap;
		for (int category = static_cast<int>(FavoriteCategory::Terrain); category <= static_cast<int>(FavoriteCategory::Other); ++category) {
			const auto kind = static_cast<FavoriteCategory>(category);
			const size_t count = std::count_if(cells_.begin(), cells_.end(), [kind](const auto& cell) { return cell.entry.category == kind; });
			if (!count) {
				continue;
			}
			const bool expanded = !owner_.search_->GetValue().empty() || !owner_.collapsed_.contains(kind);
			headers_.push_back({ kind, wxRect(gap, y, width, FromDIP(28)), expanded, count });
			y += FromDIP(32);
			if (!expanded) {
				continue;
			}
			int index = 0;
			for (auto& cell : cells_) {
				if (cell.entry.category == kind) {
					cell.rect = wxRect(gap + (index % columns) * cellWidth, y + (index / columns) * cellHeight, cellWidth - gap, cellHeight - gap);
					++index;
				}
			}
			y += ((index + columns - 1) / columns) * cellHeight + gap;
		}
		SetVirtualSize(GetClientSize().x, y + gap);
		Refresh();
	}
	void OnPaint(wxPaintEvent&) {
		wxAutoBufferedPaintDC dc(this);
		PrepareDC(dc);
		dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
		dc.Clear();
		if (!owner_.HasCurrentResources()) {
			return;
		}
		if (cells_.empty()) {
			dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
			dc.DrawText("No matching favorites.\nRight-click a brush or map item\nto add it here.", FromDIP(8), FromDIP(12));
			return;
		}
		for (const auto& header : headers_) {
			dc.SetBrush(wxBrush(Theme::Get(Theme::Role::RaisedSurface)));
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.DrawRoundedRectangle(header.rect, FromDIP(4));
			dc.SetTextForeground(Theme::Get(Theme::Role::Text));
			const wxString label = (header.expanded ? "v  " : ">  ") + wxString::FromUTF8(FavoritesManager::categoryName(header.category)) + wxString::Format(" (%zu)", header.count);
			dc.DrawText(wxControl::Ellipsize(label, dc, wxELLIPSIZE_END, std::max(1, header.rect.width - FromDIP(8))), header.rect.x + FromDIP(4), header.rect.y + FromDIP(5));
		}
		for (const auto& cell : cells_) {
			if (cell.rect.IsEmpty()) {
				continue;
			}
			const bool selected = owner_.selected_ && owner_.selected_->sameResource(cell.entry);
			dc.SetBrush(wxBrush(Theme::Get(selected ? Theme::Role::SelectionFill : Theme::Role::Surface)));
			dc.SetPen(wxPen(Theme::Get(selected ? Theme::Role::Accent : Theme::Role::Border)));
			dc.DrawRoundedRectangle(cell.rect, FromDIP(4));
			if (cell.available && cell.preview.IsOk()) {
				dc.DrawBitmap(cell.preview, cell.rect.x + (cell.rect.width - cell.preview.GetWidth()) / 2, cell.rect.y + FromDIP(5), true);
			} else {
				dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
				dc.DrawText(cell.available ? "*" : "?", cell.rect.x + cell.rect.width / 2, cell.rect.y + FromDIP(12));
			}
			dc.SetTextForeground(Theme::Get(cell.available ? Theme::Role::Text : Theme::Role::TextSubtle));
			const auto name = wxControl::Ellipsize(wxString::FromUTF8(cell.entry.displayName), dc, wxELLIPSIZE_END, std::max(1, cell.rect.width - FromDIP(6)));
			dc.DrawText(name, cell.rect.x + FromDIP(3), cell.rect.y + FromDIP(48));
		}
	}
	FavoritesPalettePanel& owner_;
	std::vector<Cell> cells_;
	std::vector<Header> headers_;
	wxString hover_;
};

FavoritesPalettePanel::FavoritesPalettePanel(wxWindow* parent) :
	PalettePanel(parent), session_(GetActiveEditorResourceSession()), context_(FavoriteResources::ActiveContext()) {
	auto* sizer = new wxBoxSizer(wxVERTICAL);
	search_ = new wxTextCtrl(this, wxID_ANY);
	search_->SetHint("Search favorites...");
	search_->SetName("Search favorites");
	sizer->Add(search_, 0, wxEXPAND | wxALL, FromDIP(6));
	grid_ = new Grid(this);
	grid_->SetMinSize(wxSize(1, FromDIP(80)));
	sizer->Add(grid_, 1, wxEXPAND);
	clear_ = new wxButton(this, wxID_ANY, "Clear All Favorites...");
	sizer->Add(clear_, 0, wxEXPAND | wxALL, FromDIP(6));
	SetSizer(sizer);
	ApplyColours();
	search_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { grid_->Reload(); });
	search_->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) { searchHasHotkeys_ = g_gui.AreHotkeysEnabled(); g_gui.DisableHotkeys(); event.Skip(); });
	search_->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) { ReleaseSearchFocus(); event.Skip(); });
	clear_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		if (!HasCurrentResources()) {
			return;
		}
		if (wxMessageBox("Remove all favorites for this resource workspace?\nFavorites for other clients and servers will be kept.", "Clear All Favorites", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES || !HasCurrentResources()) {
			return;
		}
		std::string error;
		if (!g_gui.GetFavorites().clearContext(context_, error)) {
			wxMessageBox(wxString::FromUTF8(error), "Favorites", wxOK | wxICON_ERROR, this);
			return;
		}
		g_gui.RefreshFavorites();
	});
	Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& event) { ApplyColours(); event.Skip(); });
	Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) { previews_.clear(); CallAfter([this] { RefreshFavorites(); }); event.Skip(); });
	RefreshFavorites();
}

FavoritesPalettePanel::~FavoritesPalettePanel() {
	ReleaseSearchFocus();
	DestroyChildren();
}
void FavoritesPalettePanel::ReleaseSearchFocus() {
	if (searchHasHotkeys_) {
		g_gui.EnableHotkeys();
		searchHasHotkeys_ = false;
	}
}
bool FavoritesPalettePanel::HasCurrentResources() const {
	return session_.lock() == GetActiveEditorResourceSession() && !context_.empty() && context_ == FavoriteResources::ActiveContext();
}
Brush* FavoritesPalettePanel::GetSelectedBrush() const {
	return HasCurrentResources() && selected_ ? FavoriteResources::ResolveBrush(*selected_) : nullptr;
}
int FavoritesPalettePanel::GetSelectedBrushSize() const {
	return g_gui.GetBrushSize();
}
bool FavoritesPalettePanel::SelectBrush(const Brush* brush) {
	if (!HasCurrentResources()) {
		return false;
	}
	for (const auto& entry : g_gui.GetFavorites().entries()) {
		if (entry.context == context_ && FavoriteResources::ResolveBrush(entry) == brush && brush) {
			selected_ = entry;
			grid_->Refresh();
			return true;
		}
	}
	return false;
}
void FavoritesPalettePanel::LoadCurrentContents() {
	RefreshFavorites();
}
void FavoritesPalettePanel::InvalidateContents() {
	previews_.clear();
	RefreshFavorites();
}
void FavoritesPalettePanel::OnSwitchIn() {
	ApplyColours();
	RefreshFavorites();
}
void FavoritesPalettePanel::OnSwitchOut() {
	ReleaseSearchFocus();
}
void FavoritesPalettePanel::OnUpdate() {
	if (revision_ != g_gui.GetFavorites().revision()) {
		RefreshFavorites();
	}
}
void FavoritesPalettePanel::RefreshFavorites() {
	if (selected_ && !g_gui.GetFavorites().contains(*selected_)) {
		selected_.reset();
	}
	revision_ = g_gui.GetFavorites().revision();
	grid_->Reload();
	const auto& entries = g_gui.GetFavorites().entries();
	clear_->Enable(HasCurrentResources() && std::any_of(entries.begin(), entries.end(), [this](const auto& entry) { return entry.context == context_; }));
}
void FavoritesPalettePanel::ApplyColours() {
	SetBackgroundColour(Theme::Get(Theme::Role::Background));
	SetForegroundColour(Theme::Get(Theme::Role::Text));
	search_->SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	search_->SetForegroundColour(Theme::Get(Theme::Role::Text));
	clear_->SetBackgroundColour(Theme::Get(Theme::Role::RaisedSurface));
	clear_->SetForegroundColour(Theme::Get(Theme::Role::Text));
	grid_->Refresh();
}
void FavoritesPalettePanel::Use(const FavoriteEntry& entry) {
	if (!HasCurrentResources() || !FavoriteResources::IsAvailable(entry)) {
		RefreshFavorites();
		g_gui.SetStatusText("Favorite is unavailable in this resource workspace.");
		return;
	}
	selected_ = entry;
	g_gui.ActivatePalette(GetParentPalette());
	if (entry.kind == FavoriteKind::TerrainStamp) {
		PlaceSavedTerrain(this, wxString::FromUTF8(entry.stableId));
	} else if (Brush* brush = FavoriteResources::ResolveBrush(entry)) {
		g_gui.SelectBrush(brush, TILESET_FAVORITES);
	}
	if (auto* tab = g_gui.GetCurrentMapTab()) {
		tab->GetCanvas()->SetFocus();
	}
	grid_->Refresh();
}
