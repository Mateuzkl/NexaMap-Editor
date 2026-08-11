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

#include "../main.h"
#include "replace_item_grid_panel.h"

#include "../gui.h"
#include "../theme.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <wx/control.h>
#include <wx/dataobj.h>
#include <wx/dcbuffer.h>
#include <wx/dnd.h>
#include <wx/weakref.h>

ReplaceItemGridPanel::ReplaceItemGridPanel(wxWindow* parent, SelectionHandler selectionHandler) :
	wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_SIMPLE),
	selectionHandler(std::move(selectionHandler)) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetScrollRate(0, CellHeight);
	Bind(wxEVT_PAINT, &ReplaceItemGridPanel::OnPaint, this);
	Bind(wxEVT_SIZE, &ReplaceItemGridPanel::OnSize, this);
	Bind(wxEVT_LEFT_DOWN, &ReplaceItemGridPanel::OnLeftDown, this);
	Bind(wxEVT_MOTION, &ReplaceItemGridPanel::OnMotion, this);
}

void ReplaceItemGridPanel::SetItems(std::vector<ReplaceLibraryItem> newItems) {
	items = std::move(newItems);
	selectedIndex = static_cast<size_t>(-1);
	Scroll(0, 0);
	UpdateVirtualSize();
	Refresh();
}

const ReplaceLibraryItem* ReplaceItemGridPanel::GetSelectedItem() const {
	return selectedIndex < items.size() ? &items[selectedIndex] : nullptr;
}

void ReplaceItemGridPanel::UpdateVirtualSize() {
	const int columns = GetColumnCount();
	const int rows = static_cast<int>((items.size() + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns));
	SetVirtualSize(std::max(GetClientSize().x, CellWidth), std::max(rows * CellHeight, GetClientSize().y));
}

size_t ReplaceItemGridPanel::HitTest(const wxPoint& clientPosition) const {
	int unscrolledX = 0;
	int unscrolledY = 0;
	CalcUnscrolledPosition(clientPosition.x, clientPosition.y, &unscrolledX, &unscrolledY);
	if (unscrolledX < 0 || unscrolledY < 0) {
		return static_cast<size_t>(-1);
	}
	const int columns = GetColumnCount();
	const int column = unscrolledX / CellWidth;
	if (column >= columns) {
		return static_cast<size_t>(-1);
	}
	const size_t index = static_cast<size_t>(unscrolledY / CellHeight) * static_cast<size_t>(columns) + static_cast<size_t>(column);
	return index < items.size() ? index : static_cast<size_t>(-1);
}

int ReplaceItemGridPanel::GetColumnCount() const {
	return std::max(1, GetClientSize().x / CellWidth);
}

void ReplaceItemGridPanel::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
	dc.Clear();
	PrepareDC(dc);

	const int columns = GetColumnCount();
	int viewStartY = 0;
	GetViewStart(nullptr, &viewStartY);
	const int firstRow = std::max(0, viewStartY - 1);
	const int visibleRows = GetClientSize().y / CellHeight + 3;
	const size_t firstIndex = static_cast<size_t>(firstRow) * static_cast<size_t>(columns);
	const size_t lastIndex = std::min(items.size(), static_cast<size_t>(firstRow + visibleRows) * static_cast<size_t>(columns));

	for (size_t index = firstIndex; index < lastIndex; ++index) {
		const int column = static_cast<int>(index % static_cast<size_t>(columns));
		const int row = static_cast<int>(index / static_cast<size_t>(columns));
		const wxRect cell(column * CellWidth, row * CellHeight, CellWidth, CellHeight);
		wxRect drawCell = cell;
		drawCell.Deflate(2);
		const bool selected = index == selectedIndex;
		dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
		dc.SetBrush(wxBrush(selected ? Theme::Get(Theme::Role::SelectionFill) : Theme::Get(Theme::Role::Surface)));
		dc.DrawRectangle(drawCell);

		const ReplaceLibraryItem& item = items[index];
		if (item.clientId != 0 && !g_gui.gfx.isUnloaded()) {
			if (Sprite* sprite = g_gui.gfx.getSprite(item.clientId)) {
				sprite->DrawTo(&dc, SPRITE_SIZE_32x32, cell.x + 6, cell.y + 10, cell.width, cell.height);
			}
		}

		dc.SetTextForeground(selected ? Theme::Get(Theme::Role::TextOnAccent) : Theme::Get(Theme::Role::Text));
		const int textX = cell.x + 44;
		const int textWidth = cell.width - 50;
		const wxString label = wxControl::Ellipsize(wxString::FromUTF8(item.name), dc, wxELLIPSIZE_END, textWidth, wxELLIPSIZE_FLAGS_NONE);
		dc.DrawText(label, textX, cell.y + 9);
		dc.SetTextForeground(selected ? Theme::Get(Theme::Role::TextOnAccent) : Theme::Get(Theme::Role::TextSubtle));
		dc.DrawText(wxString::Format("SID %u  CID %u", item.serverId.value, item.clientId), textX, cell.y + 30);
	}
}

void ReplaceItemGridPanel::OnSize(wxSizeEvent& event) {
	UpdateVirtualSize();
	event.Skip();
}

void ReplaceItemGridPanel::OnLeftDown(wxMouseEvent& event) {
	selectedIndex = HitTest(event.GetPosition());
	if (const ReplaceLibraryItem* selected = GetSelectedItem(); selected && selectionHandler) {
		selectionHandler(*selected);
	}
	Refresh();
	SetFocus();
}

void ReplaceItemGridPanel::OnMotion(wxMouseEvent& event) {
	if (!draggable || dragInProgress || !event.Dragging() || !event.LeftIsDown()) {
		event.Skip();
		return;
	}
	const ReplaceLibraryItem* selected = GetSelectedItem();
	if (!selected || !selected->serverId.isValid()) {
		return;
	}

	dragInProgress = true;
	wxTextDataObject data(wxString::Format("NEXAMAP_SERVER_ITEM_ID:%u", selected->serverId.value));
	wxDropSource source(this);
	source.SetData(data);
	wxWeakRef<ReplaceItemGridPanel> panel(this);
	source.DoDragDrop(wxDrag_CopyOnly);
	if (panel) {
		panel->dragInProgress = false;
	}
}
