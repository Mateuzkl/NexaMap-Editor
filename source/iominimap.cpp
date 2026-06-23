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

#include "iominimap.h"

#include "editor.h"
#include "filehandle.h"
#include "gui.h"

#include <zlib.h>

#include <wx/image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

void MinimapBlock::updateTile(int x, int y, const MinimapTile& tile) {
	m_tiles[getTileIndex(x, y)] = tile;
}

IOMinimap::IOMinimap(Editor* editor, MinimapExportFormat format, MinimapExportMode mode, bool updateLoadbar) :
	m_editor(editor),
	m_format(format),
	m_mode(mode),
	m_updateLoadbar(updateLoadbar) {
	////
}

bool IOMinimap::saveMinimap(const std::string& directory, const std::string& name, int floor) {
	if (m_mode == MinimapExportMode::AllFloors || m_mode == MinimapExportMode::SelectedArea) {
		floor = -1;
	} else if (m_mode == MinimapExportMode::GroundFloor) {
		floor = GROUND_LAYER;
	} else if (m_mode == MinimapExportMode::SpecificFloor) {
		if (floor < 0 || floor > MAP_MAX_LAYER) {
			floor = GROUND_LAYER;
		}
	}

	m_floor = floor;

	if (m_format == MinimapExportFormat::Otmm) {
		wxFileName file(wxstr(directory), wxstr(name) + ".otmm");
		return saveOtmm(nstr(file.GetFullPath()));
	}
	return saveImage(directory, name);
}

bool IOMinimap::saveOtmm(const std::string& path) {
	try {
		FileWriteHandle writer(path);
		if (!writer.isOk()) {
			m_error = "Unable to open file '" + path + "' to save the minimap.";
			return false;
		}

		// TODO: optional zlib whole-file compression flag.
		uint32_t flags = 0;

		// header
		writer.addU32(OTMM_SIGNATURE);
		writer.addU16(0); // data start, overwritten below
		writer.addU16(OTMM_VERSION);
		writer.addU32(flags);

		// version 1 header
		writer.addString("OTMM 1.0"); // description

		// go back and rewrite where the map data starts
		uint32_t start = static_cast<uint32_t>(writer.tell());
		writer.seek(4);
		writer.addU16(static_cast<uint16_t>(start));
		writer.seek(start);

		const unsigned long blockSize = MMBLOCK_SIZE * MMBLOCK_SIZE * sizeof(MinimapTile);
		std::vector<uint8_t> buffer(compressBound(blockSize));
		constexpr int COMPRESS_LEVEL = 3;

		readBlocks();

		for (uint8_t z = 0; z <= MAP_MAX_LAYER; ++z) {
			for (auto& it : m_blocks[z]) {
				const uint32_t index = it.first;
				auto& block = it.second;

				// write index pos
				const uint16_t x = static_cast<uint16_t>((index % (65536 / MMBLOCK_SIZE)) * MMBLOCK_SIZE);
				const uint16_t y = static_cast<uint16_t>((index / (65536 / MMBLOCK_SIZE)) * MMBLOCK_SIZE);
				writer.addU16(x);
				writer.addU16(y);
				writer.addU8(z);

				unsigned long len = blockSize;
				const int ret = compress2(buffer.data(), &len, reinterpret_cast<const Bytef*>(block.getTiles().data()), blockSize, COMPRESS_LEVEL);
				if (ret != Z_OK) {
					m_error = "Failed to compress minimap block.";
					return false;
				}
				writer.addU16(static_cast<uint16_t>(len));
				writer.addRAW(buffer.data(), len);
			}
			m_blocks[z].clear();
		}

		// end of file is an invalid pos
		writer.addU16(65535);
		writer.addU16(65535);
		writer.addU8(255);

		writer.close();
	} catch (std::exception& e) {
		m_error = std::string("Failed to save OTMM minimap: ") + e.what();
		return false;
	}

	return true;
}

void IOMinimap::readBlocks() {
	if (m_mode == MinimapExportMode::SelectedArea && !m_editor->hasSelection()) {
		return;
	}

	Map& map = m_editor->getMap();

	int tiles_iterated = 0;
	for (auto it = map.begin(); it != map.end(); ++it) {
		Tile* tile = (*it)->get();

		if (m_updateLoadbar) {
			++tiles_iterated;
			if (tiles_iterated % 8192 == 0) {
				g_gui.SetLoadDone(int(tiles_iterated / double(map.size()) * 90.0));
			}
		}

		if (!tile || (!tile->ground && tile->items.empty())) {
			continue;
		}

		const Position& position = tile->getPosition();

		if (m_mode == MinimapExportMode::SelectedArea) {
			if (!tile->isSelected()) {
				continue;
			}
		} else if (m_floor != -1 && position.z != m_floor) {
			continue;
		}

		MinimapTile minimapTile;
		minimapTile.color = tile->getMiniMapColor();
		minimapTile.flags |= MinimapTileWasSeen;
		if (tile->isBlocking()) {
			minimapTile.flags |= MinimapTileNotWalkable;
		}
		minimapTile.speed = static_cast<uint8_t>(std::min<int>(static_cast<int>(std::ceil(tile->getGroundSpeed() / 10.f)), 0xFF));

		auto& blocks = m_blocks[position.z];
		const uint32_t index = getBlockIndex(position);
		if (blocks.find(index) == blocks.end()) {
			blocks.emplace(index, MinimapBlock());
		}

		auto& block = blocks.at(index);
		const int offset_x = position.x - (position.x % MMBLOCK_SIZE);
		const int offset_y = position.y - (position.y % MMBLOCK_SIZE);
		block.updateTile(position.x - offset_x, position.y - offset_y, minimapTile);
	}
}

bool IOMinimap::saveImage(const std::string& directory, const std::string& name) {
	try {
		switch (m_mode) {
			case MinimapExportMode::AllFloors:
			case MinimapExportMode::GroundFloor:
			case MinimapExportMode::SpecificFloor:
				return exportMinimap(directory, name);
			case MinimapExportMode::SelectedArea:
				return exportSelection(directory, name);
		}
	} catch (std::bad_alloc&) {
		m_error = "There is not enough memory available to complete the operation.";
		return false;
	}
	return true;
}

bool IOMinimap::exportMinimap(const std::string& directory, const std::string& name) {
	Map& map = m_editor->getMap();
	if (map.size() == 0) {
		return true;
	}

	const int min_z = m_floor == -1 ? 0 : m_floor;
	const int max_z = m_floor == -1 ? MAP_MAX_LAYER : m_floor;

	// Per-floor bounding box of all drawable tiles.
	int min_x[MAP_LAYERS], min_y[MAP_LAYERS], max_x[MAP_LAYERS], max_y[MAP_LAYERS];
	for (int z = min_z; z <= max_z; ++z) {
		min_x[z] = MAP_MAX_WIDTH + 1;
		min_y[z] = MAP_MAX_HEIGHT + 1;
		max_x[z] = 0;
		max_y[z] = 0;
	}

	int tiles_iterated = 0;
	for (auto it = map.begin(); it != map.end(); ++it) {
		Tile* tile = (*it)->get();

		if (m_updateLoadbar) {
			++tiles_iterated;
			if (tiles_iterated % 8192 == 0) {
				g_gui.SetLoadDone(int(tiles_iterated / double(map.size()) * 50.0));
			}
		}

		if (!tile || (!tile->ground && tile->items.empty())) {
			continue;
		}

		const Position& position = tile->getPosition();
		if (position.z < min_z || position.z > max_z) {
			continue;
		}

		const int z = position.z;
		if (position.x < min_x[z]) {
			min_x[z] = position.x;
		}
		if (position.y < min_y[z]) {
			min_y[z] = position.y;
		}
		if (position.x > max_x[z]) {
			max_x[z] = position.x;
		}
		if (position.y > max_y[z]) {
			max_y[z] = position.y;
		}
	}

	const bool png = m_format == MinimapExportFormat::Png;
	const wxString extension = png ? "png" : "bmp";
	const wxBitmapType type = png ? wxBITMAP_TYPE_PNG : wxBITMAP_TYPE_BMP;

	// One full-resolution image per floor, covering the whole map bounding box.
	for (int z = min_z; z <= max_z; ++z) {
		if (max_x[z] < min_x[z] || max_y[z] < min_y[z]) {
			continue; // no drawable tiles on this floor
		}

		const int width = max_x[z] - min_x[z] + 1;
		const int height = max_y[z] - min_y[z] + 1;

		std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 3, 0);

		for (auto it = map.begin(); it != map.end(); ++it) {
			Tile* tile = (*it)->get();
			if (!tile || tile->getZ() != z || (!tile->ground && tile->items.empty())) {
				continue;
			}
			const uint8_t color = tile->getMiniMapColor();
			const size_t index = (static_cast<size_t>(tile->getY() - min_y[z]) * width + (tile->getX() - min_x[z])) * 3;
			pixels[index]     = (uint8_t)(static_cast<int>(color / 36) % 6 * 51); // red
			pixels[index + 1] = (uint8_t)(static_cast<int>(color / 6) % 6 * 51);  // green
			pixels[index + 2] = (uint8_t)(color % 6 * 51);                        // blue
		}

		wxImage image(width, height, pixels.data(), true);
		wxFileName file = wxString::Format("%s_%d.%s", wxstr(name), z, extension);
		file.Normalize(wxPATH_NORM_ALL, wxstr(directory));
		image.SaveFile(file.GetFullPath(), type);
	}

	return true;
}

bool IOMinimap::exportSelection(const std::string& directory, const std::string& name) {
	int min_x = MAP_MAX_WIDTH + 1;
	int min_y = MAP_MAX_HEIGHT + 1;
	int min_z = MAP_MAX_LAYER + 1;
	int max_x = 0, max_y = 0, max_z = 0;

	const TileSet& tiles = m_editor->selection.getTiles();
	for (Tile* tile : tiles) {
		if (!tile || (!tile->ground && tile->items.empty())) {
			continue;
		}

		const Position& position = tile->getPosition();
		if (position.x < min_x) {
			min_x = position.x;
		}
		if (position.x > max_x) {
			max_x = position.x;
		}
		if (position.y < min_y) {
			min_y = position.y;
		}
		if (position.y > max_y) {
			max_y = position.y;
		}
		if (position.z < min_z) {
			min_z = position.z;
		}
		if (position.z > max_z) {
			max_z = position.z;
		}
	}

	const int numtiles = (max_x - min_x) * (max_y - min_y);
	if (numtiles == 0) {
		return false;
	}

	const int image_width = max_x - min_x + 1;
	const int image_height = max_y - min_y + 1;
	if (image_width > 2048 || image_height > 2048) {
		m_error = "Minimap size greater than 2048px.";
		return false;
	}

	const int pixels_size = image_width * image_height * 3;
	std::vector<uint8_t> pixels(pixels_size);
	wxImage image(image_width, image_height, pixels.data(), true);

	const bool png = m_format == MinimapExportFormat::Png;
	const wxString extension = png ? "png" : "bmp";
	const wxBitmapType type = png ? wxBITMAP_TYPE_PNG : wxBITMAP_TYPE_BMP;

	int tiles_iterated = 0;
	for (int z = min_z; z <= max_z; ++z) {
		bool empty = true;
		std::memset(pixels.data(), 0, pixels_size);

		for (Tile* tile : tiles) {
			if (tile->getZ() != z) {
				continue;
			}

			if (m_updateLoadbar) {
				++tiles_iterated;
				if (tiles_iterated % 8192 == 0) {
					g_gui.SetLoadDone(int(tiles_iterated / double(tiles.size()) * 90.0));
				}
			}

			if (!tile->ground && tile->items.empty()) {
				continue;
			}

			const uint8_t color = tile->getMiniMapColor();
			const uint32_t index = ((tile->getY() - min_y) * image_width + (tile->getX() - min_x)) * 3;
			pixels[index]     = (uint8_t)(static_cast<int>(color / 36) % 6 * 51); // red
			pixels[index + 1] = (uint8_t)(static_cast<int>(color / 6) % 6 * 51);  // green
			pixels[index + 2] = (uint8_t)(color % 6 * 51);                        // blue
			empty = false;
		}

		if (!empty) {
			image.SetData(pixels.data(), true);
			wxFileName file = wxString::Format("%s-%d.%s", wxstr(name), z, extension);
			file.Normalize(wxPATH_NORM_ALL, wxstr(directory));
			image.SaveFile(file.GetFullPath(), type);
		}
	}

	return true;
}
