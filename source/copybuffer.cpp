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

#include "main.h"

#include "copybuffer.h"
#include "editor.h"
#include "gui.h"
#include "creature.h"

#include <algorithm>
#include <memory>

CopyBuffer::CopyBuffer() :
	tiles(newd BaseMap()) {
	;
}

BaseMap& CopyBuffer::getBufferMap() {
	ASSERT(tiles);
	return *tiles;
}

CopyBuffer::~CopyBuffer() {
	clear();
}

void CopyBuffer::swap(CopyBuffer& other) noexcept {
	std::swap(tiles, other.tiles);
	std::swap(copyPos, other.copyPos);
	std::swap(minimumPosition, other.minimumPosition);
	std::swap(maximumPosition, other.maximumPosition);
	std::swap(boundsValid, other.boundsValid);
}

Position CopyBuffer::getPosition() const {
	ASSERT(tiles);
	return copyPos;
}

void CopyBuffer::clear() {
	delete tiles;
	tiles = nullptr;
	resetBounds();
}

void CopyBuffer::replace(BaseMap* map, const Position& position) {
	clear();
	tiles = map;
	copyPos = position;
	rebuildBounds();
}

void CopyBuffer::resetBounds() {
	minimumPosition = Position();
	maximumPosition = Position();
	boundsValid = false;
}

void CopyBuffer::includePosition(const Position& position) {
	if (!boundsValid) {
		minimumPosition = position;
		maximumPosition = position;
		boundsValid = true;
		return;
	}
	minimumPosition.x = std::min(minimumPosition.x, position.x);
	minimumPosition.y = std::min(minimumPosition.y, position.y);
	minimumPosition.z = std::min(minimumPosition.z, position.z);
	maximumPosition.x = std::max(maximumPosition.x, position.x);
	maximumPosition.y = std::max(maximumPosition.y, position.y);
	maximumPosition.z = std::max(maximumPosition.z, position.z);
}

void CopyBuffer::rebuildBounds() {
	resetBounds();
	if (!tiles) {
		return;
	}
	for (MapIterator iterator = tiles->begin(); iterator != tiles->end(); ++iterator) {
		includePosition((*iterator)->getPosition());
	}
}

void CopyBuffer::copy(Editor& editor, int floor) {
	if (editor.selection.size() == 0) {
		g_gui.SetStatusText("No tiles to copy.");
		return;
	}

	clear();
	tiles = newd BaseMap();

	int tile_count = 0;
	int item_count = 0;
	copyPos = Position(0xFFFF, 0xFFFF, floor);

	for (auto it = editor.selection.begin(); it != editor.selection.end(); ++it) {
		++tile_count;

		Tile* tile = *it;
		TileLocation* newlocation = tiles->createTileL(tile->getPosition());
		Tile* copied_tile = tiles->allocator(newlocation);

		if (tile->ground && tile->ground->isSelected()) {
			copied_tile->house_id = tile->house_id;
			copied_tile->setMapFlags(tile->getMapFlags());
			copied_tile->zones = tile->zones;
		}

		ItemVector tile_selection = tile->getSelectedItems();
		for (auto iit = tile_selection.begin(); iit != tile_selection.end(); ++iit) {
			++item_count;
			// Copy items to copybuffer
			copied_tile->addItem((*iit)->deepCopy());
		}

		if (tile->creature && tile->creature->isSelected()) {
			copied_tile->creature = tile->creature->deepCopy();
		}
		if (tile->spawn && tile->spawn->isSelected()) {
			copied_tile->spawn = tile->spawn->deepCopy();
		}

		tiles->setTile(copied_tile);
		includePosition(copied_tile->getPosition());

		if (copied_tile->getX() < copyPos.x) {
			copyPos.x = copied_tile->getX();
		}

		if (copied_tile->getY() < copyPos.y) {
			copyPos.y = copied_tile->getY();
		}
	}

	std::ostringstream ss;
	ss << "Copied " << tile_count << " tile" << (tile_count > 1 ? "s" : "") << " (" << item_count << " item" << (item_count > 1 ? "s" : "") << ")";
	g_gui.SetStatusText(wxstr(ss.str()));
	g_gui.CaptureCrossClientCopy(*this);
}

void CopyBuffer::cut(Editor& editor, int floor) {
	if (editor.selection.size() == 0) {
		g_gui.SetStatusText("No tiles to cut.");
		return;
	}

	clear();
	tiles = newd BaseMap();

	int tile_count = 0;
	int item_count = 0;
	copyPos = Position(0xFFFF, 0xFFFF, floor);

	BatchAction* batch = editor.actionQueue->createBatch(ACTION_CUT_TILES);
	Action* action = editor.actionQueue->createAction(batch);

	PositionList tilestoborder;

	for (auto it = editor.selection.begin(); it != editor.selection.end(); ++it) {
		tile_count++;

		Tile* tile = *it;
		Tile* newtile = tile->deepCopy(editor.map);
		Tile* copied_tile = tiles->allocator(tile->getLocation());

		if (tile->ground && tile->ground->isSelected()) {
			copied_tile->house_id = newtile->house_id;
			newtile->house_id = 0;
			copied_tile->zones = tile->zones;
			copied_tile->setMapFlags(tile->getMapFlags());
			newtile->setMapFlags(TILESTATE_NONE);
			newtile->removeZones();
		}

		ItemVector tile_selection = newtile->popSelectedItems();
		for (auto iit = tile_selection.begin(); iit != tile_selection.end(); ++iit) {
			item_count++;
			// Add items to copybuffer
			copied_tile->addItem(*iit);
		}

		if (newtile->creature && newtile->creature->isSelected()) {
			copied_tile->creature = newtile->creature;
			newtile->creature = nullptr;
		}

		if (newtile->spawn && newtile->spawn->isSelected()) {
			copied_tile->spawn = newtile->spawn;
			newtile->spawn = nullptr;
		}

		tiles->setTile(copied_tile->getPosition(), copied_tile);
		includePosition(copied_tile->getPosition());

		if (copied_tile->getX() < copyPos.x) {
			copyPos.x = copied_tile->getX();
		}

		if (copied_tile->getY() < copyPos.y) {
			copyPos.y = copied_tile->getY();
		}

		if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
			for (int y = -1; y <= 1; y++) {
				for (int x = -1; x <= 1; x++) {
					tilestoborder.push_back(Position(tile->getX() + x, tile->getY() + y, tile->getZ()));
				}
			}
		}
		action->addChange(newd Change(newtile));
	}

	batch->addAndCommitAction(action);

	// Remove duplicates
	tilestoborder.sort();
	tilestoborder.unique();

	if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
		action = editor.actionQueue->createAction(batch);
		for (auto it = tilestoborder.begin(); it != tilestoborder.end(); ++it) {
			TileLocation* location = editor.map.createTileL(*it);
			if (location->get()) {
				Tile* new_tile = location->get()->deepCopy(editor.map);
				new_tile->borderize(&editor.map);
				new_tile->wallize(&editor.map);
				action->addChange(newd Change(new_tile));
			} else {
				Tile* new_tile = editor.map.allocator(location);
				new_tile->borderize(&editor.map);
				if (new_tile->size()) {
					action->addChange(newd Change(new_tile));
				} else {
					delete new_tile;
				}
			}
		}

		batch->addAndCommitAction(action);
	}

	editor.addBatch(batch);
	std::stringstream ss;
	ss << "Cut out " << tile_count << " tile" << (tile_count > 1 ? "s" : "") << " (" << item_count << " item" << (item_count > 1 ? "s" : "") << ")";
	g_gui.SetStatusText(wxstr(ss.str()));
	g_gui.CaptureCrossClientCopy(*this);
}

void CopyBuffer::paste(Editor& editor, const Position& toPosition) {
	if (!tiles) {
		return;
	}

	const uint64_t totalTiles = tiles->size();
	const bool showProgress = totalTiles >= 4096;
	std::unique_ptr<ScopedLoadingBar> progress;
	if (showProgress) {
		progress = std::make_unique<ScopedLoadingBar>(wxString::Format("Preparing large paste (%llu tiles)...", static_cast<unsigned long long>(totalTiles)));
		ScopedLoadingBar::SetLoadScale(0, 65);
	}

	constexpr size_t ActionChunkSize = 1024;
	BatchAction* batchAction = editor.actionQueue->createBatch(ACTION_PASTE_TILES);
	Action* action = editor.actionQueue->createAction(batchAction);
	uint64_t processedTiles = 0;
	uint64_t pastedTiles = 0;
	for (MapIterator it = tiles->begin(); it != tiles->end(); ++it) {
		++processedTiles;
		if (showProgress && processedTiles % 256 == 0) {
			const int percentage = static_cast<int>(std::min<uint64_t>(99, processedTiles * 100 / std::max<uint64_t>(1, totalTiles)));
			ScopedLoadingBar::SetLoadDone(percentage, wxString::Format("Preparing destination tiles (%llu / %llu)...", static_cast<unsigned long long>(processedTiles), static_cast<unsigned long long>(totalTiles)));
		}
		Tile* buffer_tile = (*it)->get();
		Position pos = buffer_tile->getPosition() - copyPos + toPosition;

		if (!pos.isValid()) {
			continue;
		}

		TileLocation* location = editor.map.createTileL(pos);
		Tile* copy_tile = buffer_tile->deepCopy(editor.map);
		Tile* old_dest_tile = location->get();
		Tile* new_dest_tile = nullptr;
		copy_tile->setLocation(location);

		if (g_settings.getInteger(Config::MERGE_PASTE) || !copy_tile->ground) {
			if (old_dest_tile) {
				new_dest_tile = old_dest_tile->deepCopy(editor.map);
			} else {
				new_dest_tile = editor.map.allocator(location);
			}
			new_dest_tile->merge(copy_tile);
			delete copy_tile;
		} else {
			// If the copied tile has ground, replace target tile
			new_dest_tile = copy_tile;
		}

		// Add all surrounding tiles to the map, so they get borders
		editor.map.createTile(pos.x - 1, pos.y - 1, pos.z);
		editor.map.createTile(pos.x, pos.y - 1, pos.z);
		editor.map.createTile(pos.x + 1, pos.y - 1, pos.z);
		editor.map.createTile(pos.x - 1, pos.y, pos.z);
		editor.map.createTile(pos.x + 1, pos.y, pos.z);
		editor.map.createTile(pos.x - 1, pos.y + 1, pos.z);
		editor.map.createTile(pos.x, pos.y + 1, pos.z);
		editor.map.createTile(pos.x + 1, pos.y + 1, pos.z);

		action->addChange(newd Change(new_dest_tile));
		++pastedTiles;
		if (action->size() >= ActionChunkSize) {
			batchAction->addAndCommitAction(action);
			action = editor.actionQueue->createAction(batchAction);
		}
	}
	batchAction->addAndCommitAction(action);
	if (batchAction->size() == 0) {
		delete batchAction;
		g_gui.SetStatusText("Nothing was pasted because the complete area is outside the valid map bounds.");
		return;
	}

	if (g_settings.getInteger(Config::USE_AUTOMAGIC) && g_settings.getInteger(Config::BORDERIZE_PASTE)) {
		if (showProgress) {
			ScopedLoadingBar::SetLoadScale(65, 90);
			ScopedLoadingBar::SetLoadDone(1, "Updating borders around the pasted area...");
		}
		action = editor.actionQueue->createAction(batchAction);
		TileList borderize_tiles;
		Map& map = editor.map;

		// Go through all modified (selected) tiles (might be slow)
		for (MapIterator it = tiles->begin(); it != tiles->end(); ++it) {
			bool add_me = false; // If this tile is touched
			Position pos = (*it)->getPosition() - copyPos + toPosition;
			if (pos.z < 0 || pos.z >= MAP_LAYERS) {
				continue;
			}
			// Go through all neighbours
			Tile* t;
			t = map.getTile(pos.x - 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x + 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x - 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x + 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x - 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x + 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			if (add_me) {
				borderize_tiles.push_back(map.getTile(pos));
			}
		}
		// Remove duplicates
		borderize_tiles.sort();
		borderize_tiles.unique();

		uint64_t processedBorders = 0;
		const uint64_t totalBorders = borderize_tiles.size();
		for (Tile* tile : borderize_tiles) {
			++processedBorders;
			if (showProgress && processedBorders % 256 == 0) {
				const int percentage = static_cast<int>(std::min<uint64_t>(99, processedBorders * 100 / std::max<uint64_t>(1, totalBorders)));
				ScopedLoadingBar::SetLoadDone(percentage, wxString::Format("Updating paste borders (%llu / %llu)...", static_cast<unsigned long long>(processedBorders), static_cast<unsigned long long>(totalBorders)));
			}
			if (tile) {
				Tile* newTile = tile->deepCopy(editor.map);
				newTile->borderize(&map);

				if (tile->ground && tile->ground->isSelected()) {
					newTile->selectGround();
				}

				newTile->wallize(&map);
				action->addChange(newd Change(newTile));
			}
		}

		// Commit changes to map
		batchAction->addAndCommitAction(action);
	}

	if (showProgress) {
		ScopedLoadingBar::SetLoadScale(90, 99);
		ScopedLoadingBar::SetLoadDone(1, "Finalizing large paste...");
	}
	editor.addBatch(batchAction);
	g_gui.SetStatusText(wxString::Format("Pasted %llu tiles successfully.", static_cast<unsigned long long>(pastedTiles)));
}

bool CopyBuffer::canPaste() const {
	return tiles && tiles->size() != 0;
}

uint64_t CopyBuffer::getTileCount() const {
	return tiles ? tiles->size() : 0;
}

bool CopyBuffer::getBounds(Position& minimum, Position& maximum) const {
	if (!boundsValid) {
		return false;
	}
	minimum = minimumPosition;
	maximum = maximumPosition;
	return true;
}
