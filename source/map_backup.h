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

#include <string>

class Editor;
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

	class StateGuard;
};

#endif
