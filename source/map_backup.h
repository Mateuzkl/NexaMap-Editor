//////////////////////////////////////////////////////////////////////
// This file is part of NexaMap Editor
//////////////////////////////////////////////////////////////////////
// NexaMap Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// NexaMap Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#ifndef RME_MAP_BACKUP_H_
#define RME_MAP_BACKUP_H_

#include "item_id_codec.h"

#include <optional>
#include <string>

class Editor;
class IOMapOTBM;
class Map;
class wxWindow;

// ---------------------------------------------------------------------------
// Simple map backup service for destructive operations.
// Creates a timestamped copy of the current map file without changing the
// active editor's filename or dirty state.
// ---------------------------------------------------------------------------
class MapBackupService {
public:
	// Create a backup of the current map.
	// Returns the backup file path on success, or an empty string on failure.
	// If the map has never been saved, prompts the user to save first.
	static std::string createBackup(Editor& editor, wxWindow* parent);

	// Generate a collision-resistant timestamped backup filename from the original path.
	static std::string generateBackupPath(const std::string& originalPath);

	// The conversion a save must apply when the IDs held in memory are not the
	// ones the map's storage format uses.
	static std::optional<ItemIdMapping::Direction> storageConversion(const Map& map);

	// Sets up a writer the way a normal save does, so every copy of a map carries
	// the right item version header and item IDs. The codec, when one is needed,
	// is placed in `codec`, which must outlive the save.
	static void configureWriter(IOMapOTBM& writer, const Map& map, std::optional<MappingItemIdCodec>& codec);

	class StateGuard;
};

#endif
