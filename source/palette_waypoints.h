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

#ifndef RME_PALETTE_WAYPOINTS_H_
#define RME_PALETTE_WAYPOINTS_H_

#include <set>
#include <unordered_map>

#include <wx/treectrl.h>

#include "waypoints.h"
#include "palette_common.h"

class WaypointPalettePanel : public NamedEntityPalettePanel {
public:
	WaypointPalettePanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	~WaypointPalettePanel() override;

	wxString GetName() const override;
	PaletteType GetType() const override;

	Brush* GetSelectedBrush() const override;
	bool SelectBrush(const Brush* whatbrush) override;

	void OnUpdate() override;

	void SelectWaypoint(Waypoint* wp);
	bool RenameWaypoint(const std::string& oldName, const std::string& newName);
	bool DeleteWaypoint(const std::string& name);
	void refreshWaypointTree();

public:
	void OnTreeSelectionChanged(wxTreeEvent& event);
	void OnTreeItemActivated(wxTreeEvent& event);
	void OnTreeBeginLabelEdit(wxTreeEvent& event);
	void OnTreeEndLabelEdit(wxTreeEvent& event);
	void OnTreeRightClick(wxTreeEvent& event);
	void OnTreeBeginDrag(wxTreeEvent& event);
	void OnTreeEndDrag(wxTreeEvent& event);
	void OnTreeItemExpanded(wxTreeEvent& event);
	void OnTreeItemCollapsed(wxTreeEvent& event);
	void OnMenuRename(wxCommandEvent& event);
	void OnMenuDelete(wxCommandEvent& event);
	void OnClickAddCategory(wxCommandEvent& event);
	void OnClickAddWaypoint(wxCommandEvent& event);
	void OnClickRemoveWaypoint(wxCommandEvent& event);

	void SetMap(Map* map);

protected:
	enum class TreeItemKind {
		Category,
		Waypoint,
	};

	class WaypointTreeItemData : public wxTreeItemData {
	public:
		TreeItemKind kind;
		std::string name;

		WaypointTreeItemData(TreeItemKind itemKind, std::string itemName) :
			kind(itemKind),
			name(std::move(itemName)) { }
	};

	void rebuildTree();
	wxTreeItemId resolveCategoryDropTarget(wxTreeItemId item) const;
	bool computeInsertBefore(int hitFlags, const std::string& sourceKey, const std::string& targetKey, bool categoryDrop) const;
	void markCategoryExpanded(const std::string& category);
	void rememberExpandedCategoriesFromTree();
	void activateWaypoint(Waypoint* wp);
	void activateTreeItem(const wxTreeItemId& item);
	std::string getTargetCategory() const;
	WaypointTreeItemData* getSelectedItemData() const;
	wxTreeItemId findWaypointItem(const std::string& name) const;
	wxTreeItemId findCategoryItem(const std::string& name) const;
	bool applyTreeDropInternal(const wxTreeItemId& source, const wxTreeItemId& target, int hitFlags);
	bool renameWaypointInternal(const std::string& oldName, const std::string& newName);
	bool deleteWaypointInternal(const std::string& name);
	bool deleteCategoryInternal(const std::string& name);
	bool deleteSelectedTreeItem();
	static void markMapMetadataChanged(Map* map);

	wxTreeCtrl* waypoint_tree;
	wxTreeItemId drag_item_;
	wxButton* add_category_button;
	wxButton* add_waypoint_button;
	wxButton* remove_waypoint_button;
	std::set<std::string> collapsed_categories_;
	std::unordered_map<std::string, wxTreeItemId> waypoint_items_;
	bool suppress_tree_expansion_events_ = false;

	DECLARE_EVENT_TABLE()
};

#endif
