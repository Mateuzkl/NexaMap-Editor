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
#include "replace_library_panel.h"

#include "../brush.h"
#include "../items.h"
#include "../theme.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <wx/notebook.h>
#include <wx/srchctrl.h>
#include <wx/splitter.h>

ReplaceLibraryPanel::ReplaceLibraryPanel(wxWindow* parent, Listener* listener) :
	wxPanel(parent, wxID_ANY),
	listener(listener),
	similarityRefreshTimer(this) {
	BuildLayout();
	similarityRefreshTimer.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
		UpdateSimilarityResults();
	});
	Reload();
}

ReplaceLibraryPanel::~ReplaceLibraryPanel() {
	similarityRefreshTimer.Stop();
}

void ReplaceLibraryPanel::Reload() {
	std::vector<ReplaceLibraryItem> items;
	std::unordered_map<uint16_t, ReplaceLibraryItem> clientItems;
	std::unordered_map<Brush*, std::vector<ReplaceLibraryItem>> relatedByBrush;
	items.reserve(g_items.getMaxID());
	for (uint32_t id = 1; id <= g_items.getMaxID(); ++id) {
		if (!g_items.typeExists(static_cast<int>(id))) {
			continue;
		}
		const ItemType& itemType = g_items.getItemType(static_cast<int>(id));
		if (itemType.id == 0 || itemType.clientID == 0 || itemType.isMetaItem()) {
			continue;
		}

		ReplaceLibraryItem item;
		item.key = itemType.id;
		item.serverId = ServerItemId(itemType.id);
		item.clientId = itemType.clientID;
		item.name = itemType.name.empty() ? "Unnamed item" : itemType.name;
		items.push_back(item);
		clientItems.try_emplace(item.clientId, item);

		Brush* itemBrushes[] = { itemType.brush, itemType.doodad_brush, itemType.collection_brush };
		for (Brush* brush : itemBrushes) {
			if (brush) {
				relatedByBrush[brush].push_back(item);
			}
		}
	}
	catalog.SetItems(std::move(items));

	std::vector<ReplaceLibraryBrush> brushes;
	std::unordered_set<Brush*> visited;
	for (const auto& [name, brush] : g_brushes.getMap()) {
		if (!brush || !visited.insert(brush).second || brush->isRaw() || brush->isEraser()) {
			continue;
		}
		const int lookClientId = brush->getLookID();
		if (lookClientId <= 0 || lookClientId > std::numeric_limits<uint16_t>::max()) {
			continue;
		}
		const auto lookIterator = clientItems.find(static_cast<uint16_t>(lookClientId));
		if (lookIterator == clientItems.end()) {
			continue;
		}

		ReplaceLibraryBrush entry;
		entry.key = brush->getID();
		entry.name = brush->getName();
		entry.look = lookIterator->second;
		auto relatedIterator = relatedByBrush.find(brush);
		if (relatedIterator != relatedByBrush.end()) {
			entry.relatedItems = std::move(relatedIterator->second);
		}
		if (std::none_of(entry.relatedItems.begin(), entry.relatedItems.end(), [&entry](const ReplaceLibraryItem& item) {
				return item.serverId == entry.look.serverId;
			})) {
			entry.relatedItems.push_back(entry.look);
		}
		std::sort(entry.relatedItems.begin(), entry.relatedItems.end(), [](const ReplaceLibraryItem& left, const ReplaceLibraryItem& right) {
			return left.serverId.value < right.serverId.value;
		});
		entry.relatedItems.erase(std::unique(entry.relatedItems.begin(), entry.relatedItems.end(), [](const ReplaceLibraryItem& left, const ReplaceLibraryItem& right) {
									 return left.serverId == right.serverId;
								 }),
								 entry.relatedItems.end());
		if (!entry.name.empty()) {
			brushes.push_back(std::move(entry));
		}
	}
	catalog.SetBrushes(std::move(brushes));

	ApplyItemFilter();
	ApplyBrushFilter();
	relatedGrid->SetItems({});
	similaritySource = ServerItemId();
	similarityGrid->SetItems({});
	similarityService.StartIndexing(catalog.GetItems());
	similarityStatus->SetLabel(wxString::Format("Indexing sprites: 0 / %zu", similarityService.GetTotalCount()));
}

void ReplaceLibraryPanel::SetSimilaritySource(ServerItemId serverId) {
	similaritySource = serverId;
	UpdateSimilarityResults();
	if (serverId.isValid() && similarityService.HasPendingWork() && !similarityRefreshTimer.IsRunning()) {
		similarityRefreshTimer.Start(250);
	}
}

void ReplaceLibraryPanel::BuildLayout() {
	auto* mainSizer = new wxBoxSizer(wxVERTICAL);
	notebook = new wxNotebook(this, wxID_ANY);

	auto* itemPage = new wxPanel(notebook);
	auto* itemSizer = new wxBoxSizer(wxVERTICAL);
	itemSearch = new wxSearchCtrl(itemPage, wxID_ANY);
	itemSearch->ShowCancelButton(true);
	itemGrid = new ReplaceItemGridPanel(itemPage, [this](const ReplaceLibraryItem& item) {
		OnGridSelection(itemGrid, item);
	});
	itemGrid->SetDraggable(true);
	itemSizer->Add(itemSearch, 0, wxEXPAND | wxALL, 4);
	itemSizer->Add(itemGrid, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
	itemPage->SetSizer(itemSizer);
	notebook->AddPage(itemPage, "Items");

	auto* brushPage = new wxPanel(notebook);
	auto* brushSizer = new wxBoxSizer(wxVERTICAL);
	brushSearch = new wxSearchCtrl(brushPage, wxID_ANY);
	brushSearch->ShowCancelButton(true);
	brushSizer->Add(brushSearch, 0, wxEXPAND | wxALL, 4);

	auto* splitter = new wxSplitterWindow(brushPage, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);
	auto* brushesPanel = new wxPanel(splitter);
	auto* brushesSizer = new wxBoxSizer(wxVERTICAL);
	brushesSizer->Add(new wxStaticText(brushesPanel, wxID_ANY, "Available brushes"), 0, wxALL, 4);
	brushGrid = new ReplaceItemGridPanel(brushesPanel, [this](const ReplaceLibraryItem& item) {
		OnGridSelection(brushGrid, item);
	});
	brushesSizer->Add(brushGrid, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
	brushesPanel->SetSizer(brushesSizer);

	auto* relatedPanel = new wxPanel(splitter);
	auto* relatedSizer = new wxBoxSizer(wxVERTICAL);
	relatedSizer->Add(new wxStaticText(relatedPanel, wxID_ANY, "Related items"), 0, wxALL, 4);
	relatedGrid = new ReplaceItemGridPanel(relatedPanel, [this](const ReplaceLibraryItem& item) {
		OnGridSelection(relatedGrid, item);
	});
	relatedGrid->SetDraggable(true);
	relatedSizer->Add(relatedGrid, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
	relatedPanel->SetSizer(relatedSizer);

	splitter->SplitHorizontally(brushesPanel, relatedPanel);
	splitter->SetSashGravity(0.5);
	brushSizer->Add(splitter, 1, wxEXPAND);
	brushPage->SetSizer(brushSizer);
	notebook->AddPage(brushPage, "Brushes");

	auto* similarityPage = new wxPanel(notebook);
	auto* similaritySizer = new wxBoxSizer(wxVERTICAL);
	similarityStatus = new wxStaticText(similarityPage, wxID_ANY, "Select an item to find visually similar sprites");
	similarityGrid = new ReplaceItemGridPanel(similarityPage, [this](const ReplaceLibraryItem& item) {
		OnGridSelection(similarityGrid, item);
	});
	similarityGrid->SetDraggable(true);
	similaritySizer->Add(similarityStatus, 0, wxEXPAND | wxALL, 4);
	similaritySizer->Add(similarityGrid, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
	similarityPage->SetSizer(similaritySizer);
	notebook->AddPage(similarityPage, "Smart Suggestions");

	mainSizer->Add(notebook, 1, wxEXPAND);
	SetSizer(mainSizer);

	itemSearch->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
		ApplyItemFilter();
	});
	brushSearch->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
		ApplyBrushFilter();
	});
}

void ReplaceLibraryPanel::ApplyItemFilter() {
	itemGrid->SetItems(catalog.FilterItems(itemSearch->GetValue().ToStdString()));
}

void ReplaceLibraryPanel::ApplyBrushFilter() {
	brushGrid->SetItems(catalog.FilterBrushes(brushSearch->GetValue().ToStdString()));
}

void ReplaceLibraryPanel::OnGridSelection(ReplaceItemGridPanel* source, const ReplaceLibraryItem& item) {
	if (source == brushGrid) {
		const ReplaceLibraryBrush* brush = catalog.FindBrush(item.key);
		relatedGrid->SetItems(brush ? brush->relatedItems : std::vector<ReplaceLibraryItem>());
		return;
	}
	if (item.serverId.isValid()) {
		SetSimilaritySource(item.serverId);
		if (listener) {
			listener->OnReplaceLibraryItemSelected(item.serverId);
		}
	}
}

void ReplaceLibraryPanel::UpdateSimilarityResults() {
	if (!similaritySource.isValid()) {
		similarityGrid->SetItems({});
		similarityStatus->SetLabel("Select an item to find visually similar sprites");
		similarityRefreshTimer.Stop();
		return;
	}

	std::vector<ReplaceLibraryItem> matches = similarityService.FindSimilar(similaritySource, 50);
	similarityGrid->SetItems(std::move(matches));
	const bool pending = similarityService.HasPendingWork();
	similarityStatus->SetLabel(wxString::Format("Similar to SID %u - indexed %zu / %zu%s", similaritySource.value, similarityService.GetIndexedCount(), similarityService.GetTotalCount(), pending ? "" : " - complete"));
	if (!pending) {
		similarityRefreshTimer.Stop();
	}
}
