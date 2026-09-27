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

// ============================================================================
// Waypoint palette

#include "main.h"

#include "gui.h"
#include "palette_waypoints.h"
#include "multiplayer_session.h"
#include "waypoint_brush.h"
#include "map.h"
#include "map_tab.h"

namespace {
constexpr const char* kUncategorizedLabel = "Uncategorized";

wxString categoryTreeLabel(const std::string& name) {
	return wxString::FromUTF8(name);
}
} // namespace

BEGIN_EVENT_TABLE(WaypointPalettePanel, PalettePanel)
EVT_BUTTON(PALETTE_WAYPOINT_ADD_CATEGORY, WaypointPalettePanel::OnClickAddCategory)
EVT_BUTTON(PALETTE_WAYPOINT_ADD_WAYPOINT, WaypointPalettePanel::OnClickAddWaypoint)
EVT_BUTTON(PALETTE_WAYPOINT_REMOVE_WAYPOINT, WaypointPalettePanel::OnClickRemoveWaypoint)

EVT_TREE_SEL_CHANGED(PALETTE_WAYPOINT_TREE, WaypointPalettePanel::OnTreeSelectionChanged)
EVT_TREE_ITEM_ACTIVATED(PALETTE_WAYPOINT_TREE, WaypointPalettePanel::OnTreeItemActivated)
EVT_TREE_BEGIN_LABEL_EDIT(PALETTE_WAYPOINT_TREE, WaypointPalettePanel::OnTreeBeginLabelEdit)
EVT_TREE_END_LABEL_EDIT(PALETTE_WAYPOINT_TREE, WaypointPalettePanel::OnTreeEndLabelEdit)
EVT_TREE_ITEM_RIGHT_CLICK(PALETTE_WAYPOINT_TREE, WaypointPalettePanel::OnTreeRightClick)
EVT_TREE_BEGIN_DRAG(PALETTE_WAYPOINT_TREE, WaypointPalettePanel::OnTreeBeginDrag)
EVT_TREE_END_DRAG(PALETTE_WAYPOINT_TREE, WaypointPalettePanel::OnTreeEndDrag)
EVT_TREE_ITEM_EXPANDED(PALETTE_WAYPOINT_TREE, WaypointPalettePanel::OnTreeItemExpanded)
EVT_TREE_ITEM_COLLAPSED(PALETTE_WAYPOINT_TREE, WaypointPalettePanel::OnTreeItemCollapsed)

EVT_MENU(PALETTE_WAYPOINT_RENAME, WaypointPalettePanel::OnMenuRename)
EVT_MENU(PALETTE_WAYPOINT_DELETE, WaypointPalettePanel::OnMenuDelete)
EVT_TIMER(PALETTE_DELAYED_REFRESH_TIMER, WaypointPalettePanel::OnRefreshTimer)
END_EVENT_TABLE()

WaypointPalettePanel::WaypointPalettePanel(wxWindow* parent, wxWindowID id) :
	NamedEntityPalettePanel(parent, id) {
	auto* sidesizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Waypoints");
	wxWindow* box = sidesizer->GetStaticBox();

	waypoint_tree = newd wxTreeCtrl(
		box,
		PALETTE_WAYPOINT_TREE,
		wxDefaultPosition,
		wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_SINGLE | wxTR_EDIT_LABELS | wxTR_NO_LINES
	);
	sidesizer->Add(waypoint_tree, 1, wxEXPAND);

	wxSizer* tmpsizer = newd wxBoxSizer(wxHORIZONTAL);
	tmpsizer->Add(add_category_button = newd wxButton(box, PALETTE_WAYPOINT_ADD_CATEGORY, "Category", wxDefaultPosition, wxSize(60, -1)), 1, wxEXPAND);
	tmpsizer->Add(add_waypoint_button = newd wxButton(box, PALETTE_WAYPOINT_ADD_WAYPOINT, "Add", wxDefaultPosition, wxSize(50, -1)), 1, wxEXPAND);
	tmpsizer->Add(remove_waypoint_button = newd wxButton(box, PALETTE_WAYPOINT_REMOVE_WAYPOINT, "Remove", wxDefaultPosition, wxSize(70, -1)), 1, wxEXPAND);
	sidesizer->Add(tmpsizer, 0, wxEXPAND);

	SetSizerAndFit(sidesizer);
}

WaypointPalettePanel::~WaypointPalettePanel() {
	////
}

void WaypointPalettePanel::SetMap(Map* m) {
	map = m;
	this->Enable(m);
}

Brush* WaypointPalettePanel::GetSelectedBrush() const {
	if (WaypointTreeItemData* data = getSelectedItemData()) {
		if (data->kind == TreeItemKind::Waypoint && map) {
			g_gui.waypoint_brush->setWaypoint(map->waypoints.getWaypoint(data->name));
			return g_gui.waypoint_brush;
		}
	}
	g_gui.waypoint_brush->setWaypoint(nullptr);
	return g_gui.waypoint_brush;
}

bool WaypointPalettePanel::SelectBrush(const Brush* whatbrush) {
	ASSERT(whatbrush == g_gui.waypoint_brush);
	if (!map || !whatbrush || !whatbrush->isWaypoint()) {
		return false;
	}
	Waypoint* wp = map->waypoints.getWaypoint(g_gui.waypoint_brush->getWaypoint());
	if (!wp) {
		return false;
	}
	SelectWaypoint(wp);
	return true;
}

PaletteType WaypointPalettePanel::GetType() const {
	return TILESET_WAYPOINT;
}

wxString WaypointPalettePanel::GetName() const {
	return "Waypoint Palette";
}

WaypointPalettePanel::WaypointTreeItemData* WaypointPalettePanel::getSelectedItemData() const {
	if (!waypoint_tree) {
		return nullptr;
	}
	const wxTreeItemId item = waypoint_tree->GetSelection();
	if (!item.IsOk()) {
		return nullptr;
	}
	return dynamic_cast<WaypointTreeItemData*>(waypoint_tree->GetItemData(item));
}

wxTreeItemId WaypointPalettePanel::findWaypointItem(const std::string& name) const {
	const auto found = waypoint_items_.find(as_lower_str(name));
	if (found != waypoint_items_.end()) {
		return found->second;
	}
	return wxTreeItemId();
}

wxTreeItemId WaypointPalettePanel::findCategoryItem(const std::string& name) const {
	const wxTreeItemId root = waypoint_tree->GetRootItem();
	if (!root.IsOk()) {
		return wxTreeItemId();
	}
	wxTreeItemIdValue cookie;
	wxTreeItemId child = waypoint_tree->GetFirstChild(root, cookie);
	while (child.IsOk()) {
		if (auto* data = dynamic_cast<WaypointTreeItemData*>(waypoint_tree->GetItemData(child))) {
			if (data->kind == TreeItemKind::Category && data->name == name) {
				return child;
			}
		}
		child = waypoint_tree->GetNextChild(root, cookie);
	}
	return wxTreeItemId();
}

void WaypointPalettePanel::refreshWaypointTree() {
	if (!map) {
		waypoint_tree->DeleteAllItems();
		waypoint_items_.clear();
		return;
	}
	rebuildTree();
}

void WaypointPalettePanel::OnRefreshTimer(wxTimerEvent&) {
	refreshWaypointTree();
	g_gui.RefreshOtherPalettes(GetParentPalette());
}

std::string WaypointPalettePanel::getTargetCategory() const {
	if (WaypointTreeItemData* data = getSelectedItemData()) {
		if (data->kind == TreeItemKind::Category) {
			return data->name;
		}
		if (data->kind == TreeItemKind::Waypoint && map) {
			if (Waypoint* wp = map->waypoints.getWaypoint(data->name)) {
				return wp->category;
			}
		}
	}
	return std::string();
}

void WaypointPalettePanel::markCategoryExpanded(const std::string& category) {
	collapsed_categories_.erase(category);
}

void WaypointPalettePanel::rememberExpandedCategoriesFromTree() {
	if (!waypoint_tree) {
		return;
	}
	const wxTreeItemId root = waypoint_tree->GetRootItem();
	if (!root.IsOk()) {
		return;
	}
	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = waypoint_tree->GetFirstChild(root, cookie); child.IsOk(); child = waypoint_tree->GetNextChild(root, cookie)) {
		auto* data = dynamic_cast<WaypointTreeItemData*>(waypoint_tree->GetItemData(child));
		if (!data || data->kind != TreeItemKind::Category) {
			continue;
		}
		if (waypoint_tree->IsExpanded(child)) {
			markCategoryExpanded(data->name);
		}
	}
}

wxTreeItemId WaypointPalettePanel::resolveCategoryDropTarget(wxTreeItemId item) const {
	const wxTreeItemId root = waypoint_tree->GetRootItem();
	while (item.IsOk() && item != root) {
		if (auto* data = dynamic_cast<WaypointTreeItemData*>(waypoint_tree->GetItemData(item))) {
			if (data->kind == TreeItemKind::Category) {
				return item;
			}
		}
		item = waypoint_tree->GetItemParent(item);
	}
	return wxTreeItemId();
}

bool WaypointPalettePanel::computeInsertBefore(int hitFlags, const std::string& sourceKey, const std::string& targetKey, bool categoryDrop) const {
	if (hitFlags & wxTREE_HITTEST_ABOVE) {
		return true;
	}
	if (hitFlags & wxTREE_HITTEST_BELOW) {
		return false;
	}
	if (!map) {
		return false;
	}
	if (categoryDrop) {
		auto sourceIndex = std::find(map->waypoints.categories.begin(), map->waypoints.categories.end(), sourceKey);
		auto targetIndex = std::find(map->waypoints.categories.begin(), map->waypoints.categories.end(), targetKey);
		if (sourceIndex != map->waypoints.categories.end() && targetIndex != map->waypoints.categories.end() && sourceIndex != targetIndex) {
			return sourceIndex > targetIndex;
		}
		return false;
	}

	const Waypoint* source = map->waypoints.getWaypoint(sourceKey);
	const Waypoint* target = map->waypoints.getWaypoint(targetKey);
	if (!source || !target) {
		return false;
	}
	const std::vector<std::string> order = map->waypoints.orderedWaypointsInCategory(source->category);
	const auto sourceIndex = std::find(order.begin(), order.end(), source->name);
	const auto targetIndex = std::find(order.begin(), order.end(), target->name);
	if (sourceIndex != order.end() && targetIndex != order.end() && sourceIndex != targetIndex) {
		return sourceIndex > targetIndex;
	}
	return false;
}

void WaypointPalettePanel::rebuildTree() {
	const bool previousSuppress = suppress_tree_expansion_events_;
	suppress_tree_expansion_events_ = true;
	rememberExpandedCategoriesFromTree();
	waypoint_items_.clear();
	waypoint_tree->DeleteAllItems();
	if (!map) {
		suppress_tree_expansion_events_ = previousSuppress;
		return;
	}

	map->waypoints.syncWaypointOrders();

	const wxTreeItemId root = waypoint_tree->AddRoot("Waypoints");
	std::unordered_map<std::string, wxTreeItemId> categoryItems;
	for (const auto& category : map->waypoints.categories) {
		const wxTreeItemId categoryItem = waypoint_tree->AppendItem(
			root,
			categoryTreeLabel(category),
			-1,
			-1,
			newd WaypointTreeItemData(TreeItemKind::Category, category)
		);
		categoryItems.emplace(category, categoryItem);

		for (const std::string& wpName : map->waypoints.orderedWaypointsInCategory(category)) {
			Waypoint* wp = map->waypoints.getWaypoint(wpName);
			if (!wp) {
				continue;
			}
			const wxTreeItemId item = waypoint_tree->AppendItem(
				categoryItem,
				wxstr(wp->name),
				-1,
				-1,
				newd WaypointTreeItemData(TreeItemKind::Waypoint, wp->name)
			);
			waypoint_items_.emplace(as_lower_str(wp->name), item);
		}

		if (collapsed_categories_.contains(category)) {
			waypoint_tree->Collapse(categoryItem);
		} else {
			waypoint_tree->Expand(categoryItem);
		}
	}

	wxTreeItemId uncategorizedItem;
	const std::vector<std::string>& uncategorized = map->waypoints.orderedWaypointsInCategory("");
	if (!uncategorized.empty()) {
		uncategorizedItem = waypoint_tree->AppendItem(
			root,
			kUncategorizedLabel,
			-1,
			-1,
			newd WaypointTreeItemData(TreeItemKind::Category, std::string())
		);
		for (const std::string& wpName : uncategorized) {
			Waypoint* wp = map->waypoints.getWaypoint(wpName);
			if (!wp) {
				continue;
			}
			const wxTreeItemId item = waypoint_tree->AppendItem(
				uncategorizedItem,
				wxstr(wp->name),
				-1,
				-1,
				newd WaypointTreeItemData(TreeItemKind::Waypoint, wp->name)
			);
			waypoint_items_.emplace(as_lower_str(wp->name), item);
		}
		if (collapsed_categories_.contains(std::string())) {
			waypoint_tree->Collapse(uncategorizedItem);
		} else {
			waypoint_tree->Expand(uncategorizedItem);
		}
	}
	waypoint_tree->Expand(root);
	suppress_tree_expansion_events_ = previousSuppress;
}

void WaypointPalettePanel::activateWaypoint(Waypoint* wp) {
	if (!wp) {
		return;
	}
	g_gui.waypoint_brush->setWaypoint(wp);
	g_gui.SelectBrushInternal(g_gui.waypoint_brush);
	g_gui.SetScreenCenterPosition(wp->pos);
}

void WaypointPalettePanel::SelectWaypoint(Waypoint* wp) {
	if (!map || !wp) {
		return;
	}
	wxTreeItemId item = findWaypointItem(wp->name);
	if (!item.IsOk()) {
		rebuildTree();
		item = findWaypointItem(wp->name);
	}
	if (item.IsOk()) {
		waypoint_tree->SelectItem(item);
		waypoint_tree->EnsureVisible(item);
	}
	g_gui.waypoint_brush->setWaypoint(wp);
}

void WaypointPalettePanel::OnUpdate() {
	if (editing_new_waypoint_ && map) {
		if (WaypointTreeItemData* data = getSelectedItemData()) {
			if (data->kind == TreeItemKind::Waypoint) {
				if (Waypoint* wp = map->waypoints.getWaypoint(data->name)) {
					if (wp->name.empty() && wp->pos == Position()) {
						map->waypoints.removeWaypoint(wp->name);
					}
				}
			}
		}
	}

	if (!map) {
		waypoint_tree->Enable(false);
		add_category_button->Enable(false);
		add_waypoint_button->Enable(false);
		remove_waypoint_button->Enable(false);
		waypoint_tree->DeleteAllItems();
		waypoint_items_.clear();
	} else {
		waypoint_tree->Enable(true);
		add_category_button->Enable(true);
		add_waypoint_button->Enable(true);
		remove_waypoint_button->Enable(true);
		rebuildTree();
	}
}

void WaypointPalettePanel::OnTreeSelectionChanged(wxTreeEvent& event) {
	event.Skip();
	if (!map) {
		return;
	}
	if (WaypointTreeItemData* data = dynamic_cast<WaypointTreeItemData*>(waypoint_tree->GetItemData(event.GetItem()))) {
		if (data->kind == TreeItemKind::Waypoint) {
			if (Waypoint* wp = map->waypoints.getWaypoint(data->name)) {
				activateWaypoint(wp);
			}
		}
	}
}

void WaypointPalettePanel::OnTreeItemActivated(wxTreeEvent& event) {
	if (!map) {
		return;
	}
	if (WaypointTreeItemData* data = dynamic_cast<WaypointTreeItemData*>(waypoint_tree->GetItemData(event.GetItem()))) {
		if (data->kind == TreeItemKind::Waypoint) {
			if (Waypoint* wp = map->waypoints.getWaypoint(data->name)) {
				activateWaypoint(wp);
			}
		}
	}
}

void WaypointPalettePanel::OnTreeItemExpanded(wxTreeEvent& event) {
	if (suppress_tree_expansion_events_) {
		return;
	}
	if (auto* data = dynamic_cast<WaypointTreeItemData*>(waypoint_tree->GetItemData(event.GetItem()))) {
		if (data->kind == TreeItemKind::Category) {
			collapsed_categories_.erase(data->name);
		}
	}
}

void WaypointPalettePanel::OnTreeItemCollapsed(wxTreeEvent& event) {
	if (suppress_tree_expansion_events_) {
		return;
	}
	if (auto* data = dynamic_cast<WaypointTreeItemData*>(waypoint_tree->GetItemData(event.GetItem()))) {
		if (data->kind == TreeItemKind::Category) {
			collapsed_categories_.insert(data->name);
		}
	}
}

void WaypointPalettePanel::OnTreeBeginLabelEdit(wxTreeEvent& event) {
	MultiplayerSession::MetadataEdit multiplayerEdit(map);
	if (!multiplayerEdit.allowed()) {
		event.Veto();
		return;
	}
	if (WaypointTreeItemData* data = dynamic_cast<WaypointTreeItemData*>(waypoint_tree->GetItemData(event.GetItem()))) {
		if (data->kind == TreeItemKind::Category && data->name.empty()) {
			event.Veto();
			return;
		}
	}
	g_gui.DisableHotkeys();
}

void WaypointPalettePanel::OnTreeEndLabelEdit(wxTreeEvent& event) {
	g_gui.EnableHotkeys();
	if (!map || !event.GetItem().IsOk()) {
		editing_new_waypoint_ = false;
		return;
	}
	MultiplayerSession::MetadataEdit multiplayerEdit(map);
	if (!multiplayerEdit.allowed()) {
		event.Veto();
		editing_new_waypoint_ = false;
		return;
	}

	WaypointTreeItemData* data = dynamic_cast<WaypointTreeItemData*>(waypoint_tree->GetItemData(event.GetItem()));
	if (!data) {
		editing_new_waypoint_ = false;
		return;
	}

	const std::string newLabel = nstr(event.GetLabel());
	if (event.IsEditCancelled()) {
		if (editing_new_waypoint_ && data->kind == TreeItemKind::Waypoint) {
			DeleteWaypoint(data->name);
			refreshWaypointTree();
		}
		editing_new_waypoint_ = false;
		return;
	}

	if (data->kind == TreeItemKind::Category) {
		if (newLabel.empty()) {
			event.Veto();
			return;
		}
		if (newLabel == data->name) {
			return;
		}
		if (!map->waypoints.renameCategory(data->name, newLabel)) {
			g_gui.SetStatusText("There already is a category with this name.");
			event.Veto();
			return;
		}
		refreshWaypointTree();
		return;
	}

	if (data->kind == TreeItemKind::Waypoint) {
		if (!RenameWaypoint(data->name, newLabel)) {
			if (newLabel.empty()) {
				DeleteWaypoint(data->name);
				refreshWaypointTree();
			} else {
				event.Veto();
			}
		} else {
			editing_new_waypoint_ = false;
			refreshWaypointTree();
		}
	}
}

void WaypointPalettePanel::OnTreeRightClick(wxTreeEvent& event) {
	if (!map || !event.GetItem().IsOk()) {
		return;
	}
	waypoint_tree->SelectItem(event.GetItem());
	WaypointTreeItemData* data = dynamic_cast<WaypointTreeItemData*>(waypoint_tree->GetItemData(event.GetItem()));
	if (!data) {
		return;
	}
	if (data->kind == TreeItemKind::Category && data->name.empty()) {
		return;
	}

	wxMenu menu;
	menu.Append(PALETTE_WAYPOINT_RENAME, "Rename");
	menu.Append(PALETTE_WAYPOINT_DELETE, "Delete");
	PopupMenu(&menu);
}

void WaypointPalettePanel::OnTreeBeginDrag(wxTreeEvent& event) {
	drag_item_ = wxTreeItemId();
	if (!map) {
		return;
	}
	MultiplayerSession::MetadataEdit multiplayerEdit(map);
	if (!multiplayerEdit.allowed()) {
		return;
	}

	const wxTreeItemId item = event.GetItem();
	if (!item.IsOk() || item == waypoint_tree->GetRootItem()) {
		return;
	}
	WaypointTreeItemData* data = dynamic_cast<WaypointTreeItemData*>(waypoint_tree->GetItemData(item));
	if (!data) {
		return;
	}
	if (data->kind == TreeItemKind::Category && data->name.empty()) {
		return;
	}

	drag_item_ = item;
	suppress_tree_expansion_events_ = true;
	event.Allow();
}

void WaypointPalettePanel::OnTreeEndDrag(wxTreeEvent& event) {
	wxUnusedVar(event);
	if (!map || !drag_item_.IsOk()) {
		drag_item_ = wxTreeItemId();
		suppress_tree_expansion_events_ = false;
		return;
	}

	const wxTreeItemId source = drag_item_;
	drag_item_ = wxTreeItemId();

	wxPoint client = waypoint_tree->ScreenToClient(wxGetMousePosition());
	int hitFlags = 0;
	const wxTreeItemId target = waypoint_tree->HitTest(client, hitFlags);
	if (!target.IsOk() || target == source || target == waypoint_tree->GetRootItem()) {
		suppress_tree_expansion_events_ = false;
		return;
	}

	WaypointTreeItemData* sourceData = dynamic_cast<WaypointTreeItemData*>(waypoint_tree->GetItemData(source));
	if (!sourceData) {
		suppress_tree_expansion_events_ = false;
		return;
	}

	if (!applyTreeDrop(source, target, hitFlags)) {
		suppress_tree_expansion_events_ = false;
		return;
	}

	const std::string selectedName = sourceData->name;
	const TreeItemKind sourceKind = sourceData->kind;
	if (sourceKind == TreeItemKind::Category) {
		markCategoryExpanded(selectedName);
	}
	refreshWaypointTree();
	if (sourceKind == TreeItemKind::Waypoint) {
		if (Waypoint* wp = map->waypoints.getWaypoint(selectedName)) {
			markCategoryExpanded(wp->category);
			SelectWaypoint(wp);
		}
	} else if (sourceKind == TreeItemKind::Category) {
		const wxTreeItemId item = findCategoryItem(selectedName);
		if (item.IsOk()) {
			markCategoryExpanded(selectedName);
			waypoint_tree->SelectItem(item);
			waypoint_tree->EnsureVisible(item);
		}
	}
	CallAfter([this]() { suppress_tree_expansion_events_ = false; });
}

bool WaypointPalettePanel::applyTreeDrop(const wxTreeItemId& source, const wxTreeItemId& target, int hitFlags) {
	auto* sourceData = dynamic_cast<WaypointTreeItemData*>(waypoint_tree->GetItemData(source));
	auto* targetData = dynamic_cast<WaypointTreeItemData*>(waypoint_tree->GetItemData(target));
	if (!map || !sourceData || !targetData) {
		return false;
	}

	if (sourceData->kind == TreeItemKind::Category) {
		if (sourceData->name.empty()) {
			return false;
		}
		const wxTreeItemId categoryTarget = resolveCategoryDropTarget(target);
		if (!categoryTarget.IsOk()) {
			return false;
		}
		auto* categoryData = dynamic_cast<WaypointTreeItemData*>(waypoint_tree->GetItemData(categoryTarget));
		if (!categoryData || categoryData->kind != TreeItemKind::Category || categoryData->name.empty()) {
			return false;
		}
		const bool insertBefore = computeInsertBefore(hitFlags, sourceData->name, categoryData->name, true);
		if (categoryData->name == sourceData->name) {
			return map->waypoints.nudgeCategory(sourceData->name, insertBefore);
		}
		markCategoryExpanded(sourceData->name);
		markCategoryExpanded(categoryData->name);
		return map->waypoints.moveCategoryRelative(sourceData->name, categoryData->name, insertBefore);
	}

	if (sourceData->kind != TreeItemKind::Waypoint) {
		return false;
	}

	const bool insertBefore = computeInsertBefore(hitFlags, sourceData->name, targetData->name, false);
	const bool onItem = (hitFlags & wxTREE_HITTEST_ONITEM) != 0;

	if (targetData->kind == TreeItemKind::Category) {
		if (Waypoint* src = map->waypoints.getWaypoint(sourceData->name)) {
			markCategoryExpanded(src->category);
		}
		markCategoryExpanded(targetData->name);
		if (onItem || !insertBefore) {
			return map->waypoints.moveWaypointIntoCategory(sourceData->name, targetData->name, true);
		}
		return map->waypoints.moveWaypointIntoCategory(sourceData->name, targetData->name, false);
	}

	if (targetData->kind == TreeItemKind::Waypoint) {
		if (Waypoint* wp = map->waypoints.getWaypoint(targetData->name)) {
			markCategoryExpanded(wp->category);
		}
		if (Waypoint* src = map->waypoints.getWaypoint(sourceData->name)) {
			markCategoryExpanded(src->category);
		}
		return map->waypoints.moveWaypointRelative(sourceData->name, targetData->name, insertBefore);
	}

	return false;
}

void WaypointPalettePanel::OnMenuRename(wxCommandEvent& WXUNUSED(event)) {
	if (!map) {
		return;
	}
	if (WaypointTreeItemData* data = getSelectedItemData()) {
		if (data->kind == TreeItemKind::Waypoint) {
			const wxTreeItemId item = findWaypointItem(data->name);
			if (item.IsOk()) {
				waypoint_tree->EditLabel(item);
			}
		} else if (data->kind == TreeItemKind::Category && !data->name.empty()) {
			const wxTreeItemId item = findCategoryItem(data->name);
			if (item.IsOk()) {
				waypoint_tree->EditLabel(item);
			}
		}
	}
}

void WaypointPalettePanel::OnMenuDelete(wxCommandEvent& WXUNUSED(event)) {
	if (!map) {
		return;
	}
	MultiplayerSession::MetadataEdit multiplayerEdit(map);
	if (!multiplayerEdit.allowed()) {
		return;
	}
	if (WaypointTreeItemData* data = getSelectedItemData()) {
		if (data->kind == TreeItemKind::Waypoint) {
			DeleteWaypoint(data->name);
			refreshWaypointTree();
		} else if (data->kind == TreeItemKind::Category && !data->name.empty()) {
			map->waypoints.removeCategory(data->name);
			collapsed_categories_.erase(data->name);
			refreshWaypointTree();
		}
	}
}

bool WaypointPalettePanel::RenameWaypoint(const std::string& oldName, const std::string& newName) {
	if (!map) {
		return false;
	}
	MultiplayerSession::MetadataEdit multiplayerEdit(map);
	if (!multiplayerEdit.allowed()) {
		return false;
	}

	if (newName.empty()) {
		return false;
	}

	Waypoint* wp = map->waypoints.getWaypoint(oldName);
	if (!wp) {
		return false;
	}

	if (newName == oldName) {
		return true;
	}

	if (map->waypoints.getWaypoint(newName)) {
		g_gui.SetStatusText("There already is a waypoint with this name.");
		return false;
	}

	auto* nwp = newd Waypoint(*wp);
	nwp->name = newName;

	if (map->getTile(wp->pos)) {
		map->getTileL(wp->pos)->decreaseWaypointCount();
	}
	map->waypoints.renameInOrders(oldName, newName);
	map->waypoints.removeWaypoint(wp->name);
	map->waypoints.addWaypoint(nwp);
	g_gui.waypoint_brush->setWaypoint(nwp);
	return true;
}

bool WaypointPalettePanel::DeleteWaypoint(const std::string& name) {
	if (!map) {
		return false;
	}
	MultiplayerSession::MetadataEdit multiplayerEdit(map);
	if (!multiplayerEdit.allowed()) {
		return false;
	}

	Waypoint* wp = map->waypoints.getWaypoint(name);
	if (!wp) {
		return false;
	}

	if (map->getTile(wp->pos)) {
		map->getTileL(wp->pos)->decreaseWaypointCount();
	}
	map->waypoints.removeWaypoint(wp->name);
	if (g_gui.waypoint_brush && as_lower_str(g_gui.waypoint_brush->getWaypoint()) == as_lower_str(name)) {
		g_gui.waypoint_brush->setWaypoint(nullptr);
	}
	return true;
}

void WaypointPalettePanel::OnClickAddCategory(wxCommandEvent& WXUNUSED(event)) {
	if (!map) {
		return;
	}
	MultiplayerSession::MetadataEdit multiplayerEdit(map);
	if (!multiplayerEdit.allowed()) {
		return;
	}

	const wxString name = wxGetTextFromUser("Category name", "New waypoint category", "", this);
	if (name.empty()) {
		return;
	}

	const std::string categoryName = nstr(name);
	for (const auto& existing : map->waypoints.categories) {
		if (existing == categoryName) {
			g_gui.SetStatusText("There already is a category with this name.");
			return;
		}
	}

	map->waypoints.addCategory(categoryName);
	refreshWaypointTree();
	if (const wxTreeItemId item = findCategoryItem(categoryName); item.IsOk()) {
		collapsed_categories_.erase(categoryName);
		waypoint_tree->Expand(item);
		waypoint_tree->SelectItem(item);
	}
}

void WaypointPalettePanel::OnClickAddWaypoint(wxCommandEvent& WXUNUSED(event)) {
	if (!map) {
		return;
	}

	const std::string targetCategory = getTargetCategory();

	if (auto* live = MultiplayerSession::current(); live && &live->getEditor().map == map) {
		MultiplayerSession::MetadataEdit multiplayerEdit(map);
		if (!multiplayerEdit.allowed()) {
			return;
		}
		const wxString name = wxGetTextFromUser("Waypoint name", "New multiplayer waypoint", "", this);
		if (name.empty() || map->waypoints.getWaypoint(nstr(name))) {
			return;
		}
		auto* waypoint = new Waypoint;
		waypoint->name = nstr(name);
		waypoint->category = targetCategory;
		if (auto* tab = g_gui.GetCurrentMapTab()) {
			waypoint->pos = tab->GetScreenCenterPosition();
		}
		map->waypoints.addWaypoint(waypoint);
		refreshWaypointTree();
		return;
	}

	auto* wp = newd Waypoint();
	wp->category = targetCategory;
	if (!targetCategory.empty()) {
		markCategoryExpanded(targetCategory);
	} else {
		markCategoryExpanded(std::string());
	}
	if (MapTab* mapTab = g_gui.GetCurrentMapTab()) {
		wp->pos = mapTab->GetScreenCenterPosition();
	}
	map->waypoints.addWaypoint(wp);
	editing_new_waypoint_ = true;
	rebuildTree();
	SelectWaypoint(wp);
	const wxTreeItemId editItem = findWaypointItem(wp->name);
	if (editItem.IsOk()) {
		waypoint_tree->SelectItem(editItem);
		waypoint_tree->EnsureVisible(editItem);
		CallAfter([this, editItem]() {
			if (editItem.IsOk()) {
				waypoint_tree->EditLabel(editItem);
			}
		});
	}
}

void WaypointPalettePanel::OnClickRemoveWaypoint(wxCommandEvent& WXUNUSED(event)) {
	if (!map) {
		return;
	}
	MultiplayerSession::MetadataEdit multiplayerEdit(map);
	if (!multiplayerEdit.allowed()) {
		return;
	}

	if (WaypointTreeItemData* data = getSelectedItemData()) {
		if (data->kind == TreeItemKind::Category) {
			if (!data->name.empty()) {
				map->waypoints.removeCategory(data->name);
				collapsed_categories_.erase(data->name);
				refreshWaypointTree();
			}
			return;
		}
		if (data->kind == TreeItemKind::Waypoint) {
			DeleteWaypoint(data->name);
			refreshWaypointTree();
		}
	}
}
