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

#ifndef RME_REPLACE_TOOL_REPLACE_LIBRARY_PANEL_H_
#define RME_REPLACE_TOOL_REPLACE_LIBRARY_PANEL_H_

#include "replace_item_grid_panel.h"
#include "replace_library_catalog.h"
#include "replace_visual_similarity_service.h"

#include <wx/panel.h>
#include <wx/timer.h>

class wxNotebook;
class wxSearchCtrl;

class ReplaceLibraryPanel : public wxPanel {
public:
	class Listener {
	public:
		virtual ~Listener() = default;
		virtual void OnReplaceLibraryItemSelected(ServerItemId serverId) = 0;
	};

	ReplaceLibraryPanel(wxWindow* parent, Listener* listener);
	~ReplaceLibraryPanel() override;

	void Reload();
	void SetSimilaritySource(ServerItemId serverId);

private:
	void BuildLayout();
	void ApplyItemFilter();
	void ApplyBrushFilter();
	void OnGridSelection(ReplaceItemGridPanel* source, const ReplaceLibraryItem& item);
	void UpdateSimilarityResults();

	Listener* listener = nullptr;
	ReplaceLibraryCatalog catalog;
	ReplaceVisualSimilarityService similarityService;
	wxTimer similarityRefreshTimer;
	ServerItemId similaritySource;
	wxNotebook* notebook = nullptr;
	wxSearchCtrl* itemSearch = nullptr;
	wxSearchCtrl* brushSearch = nullptr;
	ReplaceItemGridPanel* itemGrid = nullptr;
	ReplaceItemGridPanel* brushGrid = nullptr;
	ReplaceItemGridPanel* relatedGrid = nullptr;
	ReplaceItemGridPanel* similarityGrid = nullptr;
	wxStaticText* similarityStatus = nullptr;
};

#endif
