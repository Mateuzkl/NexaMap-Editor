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

#ifndef RME_REPLACE_TOOL_REPLACE_ITEM_GRID_PANEL_H_
#define RME_REPLACE_TOOL_REPLACE_ITEM_GRID_PANEL_H_

#include "replace_library_catalog.h"

#include <functional>
#include <vector>
#include <wx/scrolwin.h>

class ReplaceItemGridPanel : public wxScrolledWindow {
public:
	using SelectionHandler = std::function<void(const ReplaceLibraryItem&)>;

	ReplaceItemGridPanel(wxWindow* parent, SelectionHandler selectionHandler);

	void SetItems(std::vector<ReplaceLibraryItem> items);
	void SetDraggable(bool draggable) {
		this->draggable = draggable;
	}

	[[nodiscard]] const ReplaceLibraryItem* GetSelectedItem() const;

private:
	static constexpr int CellWidth = 152;
	static constexpr int CellHeight = 56;

	void UpdateVirtualSize();
	[[nodiscard]] size_t HitTest(const wxPoint& clientPosition) const;
	[[nodiscard]] int GetColumnCount() const;

	void OnPaint(wxPaintEvent& event);
	void OnSize(wxSizeEvent& event);
	void OnLeftDown(wxMouseEvent& event);
	void OnMotion(wxMouseEvent& event);

	std::vector<ReplaceLibraryItem> items;
	SelectionHandler selectionHandler;
	size_t selectedIndex = static_cast<size_t>(-1);
	bool draggable = false;
	bool dragInProgress = false;
};

#endif
