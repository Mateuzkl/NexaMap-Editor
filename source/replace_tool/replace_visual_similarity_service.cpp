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
#include "replace_visual_similarity_service.h"

#include "../graphics.h"
#include "../gui.h"

#include <limits>
#include <utility>

ReplaceVisualSimilarityService::ReplaceVisualSimilarityService() :
	index(256),
	timer(this) {
	Bind(wxEVT_TIMER, &ReplaceVisualSimilarityService::OnTimer, this, timer.GetId());
}

ReplaceVisualSimilarityService::~ReplaceVisualSimilarityService() {
	Stop();
}

uint64_t ReplaceVisualSimilarityService::StartIndexing(const std::vector<ReplaceLibraryItem>& items) {
	timer.Stop();
	pendingItems.clear();
	itemLookup.clear();
	if (++generation == 0) {
		++generation;
	}
	index.Reset(generation);
	for (const ReplaceLibraryItem& item : items) {
		if (!item.serverId.isValid() || item.clientId == 0) {
			continue;
		}
		pendingItems.push_back({ item, 0 });
		itemLookup[item.serverId.value] = item;
	}
	if (!pendingItems.empty()) {
		timer.Start(5);
	}
	return generation;
}

void ReplaceVisualSimilarityService::Stop() {
	timer.Stop();
	pendingItems.clear();
	itemLookup.clear();
	if (++generation == 0) {
		++generation;
	}
	index.Reset(generation);
}

std::vector<ReplaceLibraryItem> ReplaceVisualSimilarityService::FindSimilar(ServerItemId sourceServerId, size_t count) const {
	std::vector<ReplaceLibraryItem> result;
	for (const VisualSimilarityMatch& match : index.FindSimilar(generation, sourceServerId, count)) {
		const auto iterator = itemLookup.find(match.serverId.value);
		if (iterator != itemLookup.end()) {
			result.push_back(iterator->second);
		}
	}
	return result;
}

size_t ReplaceVisualSimilarityService::GetIndexedCount() const {
	return index.GetIndexedCount(generation);
}

bool ReplaceVisualSimilarityService::HasPendingWork() const {
	return !pendingItems.empty() || index.HasPendingWork(generation);
}

void ReplaceVisualSimilarityService::OnTimer(wxTimerEvent&) {
	if (pendingItems.empty()) {
		timer.Stop();
		return;
	}

	PendingItem pendingItem = std::move(pendingItems.front());
	pendingItems.pop_front();
	GameSprite* sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(pendingItem.item.clientId));
	if (!sprite) {
		return;
	}

	VisualSpriteSample sample;
	sample.generation = generation;
	sample.serverId = pendingItem.item.serverId;
	bool pending = false;
	if (!sprite->getVisualPreviewRGBA(sample.rgba, sample.width, sample.height, pending)) {
		if (pending && pendingItem.attempts < std::numeric_limits<uint16_t>::max()) {
			++pendingItem.attempts;
			pendingItems.push_back(std::move(pendingItem));
		}
		return;
	}

	if (!index.Submit(std::move(sample))) {
		pendingItems.push_front(std::move(pendingItem));
	}
}
