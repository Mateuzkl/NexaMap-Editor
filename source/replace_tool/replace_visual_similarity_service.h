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

#ifndef RME_REPLACE_TOOL_REPLACE_VISUAL_SIMILARITY_SERVICE_H_
#define RME_REPLACE_TOOL_REPLACE_VISUAL_SIMILARITY_SERVICE_H_

#include "replace_library_catalog.h"
#include "visual_similarity_index.h"

#include <deque>
#include <unordered_map>
#include <vector>
#include <wx/event.h>
#include <wx/timer.h>

class ReplaceVisualSimilarityService : public wxEvtHandler {
public:
	ReplaceVisualSimilarityService();
	~ReplaceVisualSimilarityService() override;

	uint64_t StartIndexing(const std::vector<ReplaceLibraryItem>& items);
	void Stop();
	[[nodiscard]] std::vector<ReplaceLibraryItem> FindSimilar(ServerItemId sourceServerId, size_t count) const;
	[[nodiscard]] size_t GetIndexedCount() const;
	[[nodiscard]] size_t GetTotalCount() const {
		return itemLookup.size();
	}
	[[nodiscard]] bool HasPendingWork() const;

private:
	struct PendingItem {
		ReplaceLibraryItem item;
		uint16_t attempts = 0;
	};

	void OnTimer(wxTimerEvent& event);

	VisualSimilarityIndex index;
	wxTimer timer;
	std::deque<PendingItem> pendingItems;
	std::unordered_map<uint16_t, ReplaceLibraryItem> itemLookup;
	uint64_t generation = 0;
};

#endif
