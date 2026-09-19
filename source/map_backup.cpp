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

#include "main.h"

#include "map_backup.h"

#include "editor.h"
#include "gui.h"
#include "iomap_otbm.h"

#include <wx/filename.h>
#include <wx/msgdlg.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <sstream>

class MapBackupService::StateGuard {
public:
	Map& map;
	std::string originalFilename;
	std::string originalName;
	std::string originalSpawnfile;
	std::string originalSpawnNpcFile;
	std::string originalHousefile;
	std::string originalWaypointfile;
	std::string originalZonefile;
	SpawnFormat originalSpawnFormat;
	bool originalSpawnFilenamesExplicit;
	bool originalUnnamed;
	bool originalHasChanged;

	explicit StateGuard(Map& m) :
		map(m),
		originalFilename(m.filename),
		originalName(m.name),
		originalSpawnfile(m.spawnfile),
		originalSpawnNpcFile(m.spawnNpcFile),
		originalHousefile(m.housefile),
		originalWaypointfile(m.waypointfile),
		originalZonefile(m.zonefile),
		originalSpawnFormat(m.spawnFormat),
		originalSpawnFilenamesExplicit(m.spawnFilenamesExplicit),
		originalUnnamed(m.unnamed),
		originalHasChanged(m.has_changed) {
	}

	~StateGuard() {
		map.filename = originalFilename;
		map.name = originalName;
		map.spawnfile = originalSpawnfile;
		map.spawnNpcFile = originalSpawnNpcFile;
		map.housefile = originalHousefile;
		map.waypointfile = originalWaypointfile;
		map.zonefile = originalZonefile;
		map.spawnFormat = originalSpawnFormat;
		map.spawnFilenamesExplicit = originalSpawnFilenamesExplicit;
		map.unnamed = originalUnnamed;
		map.has_changed = originalHasChanged;
	}
};

// ---------------------------------------------------------------------------
// MapBackupService
// ---------------------------------------------------------------------------

std::string MapBackupService::generateBackupPath(const std::string& originalPath) {
	wxFileName fn(wxString::FromUTF8(originalPath));

	// Generate sub-second timestamp
	auto now = std::chrono::system_clock::now();
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
	auto time_t_now = std::chrono::system_clock::to_time_t(now);
	struct tm tm_buf;
#ifdef _WIN32
	localtime_s(&tm_buf, &time_t_now);
#else
	localtime_r(&time_t_now, &tm_buf);
#endif

	char timestamp[64];
	std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm_buf);

	wxString baseName;
	baseName << fn.GetName() << "_backup_" << timestamp << "_" << wxString::Format("%03d", static_cast<int>(ms.count()));

	wxString ext = fn.GetExt();
	if (ext.Lower() == "otgz" || ext.IsEmpty()) {
		ext = "otbm";
	}

	wxFileName backupPath(fn.GetPath(), baseName + "." + ext);
	int counter = 1;
	while (backupPath.FileExists()) {
		backupPath.SetName(baseName + "_" + std::to_string(counter++));
	}
	return backupPath.GetFullPath().utf8_string();
}

std::string MapBackupService::createBackup(Editor& editor, wxWindow* parent) {
	Map& map = editor.getMap();

	// Check if map has a file
	if (!map.hasFile()) {
		int result = wxMessageBox(
			"The map has never been saved. Please save it first to create a backup.\n\n"
			"Would you like to save now?",
			"Backup — Map Not Saved",
			wxYES_NO | wxICON_WARNING,
			parent
		);

		if (result != wxYES) {
			return "";
		}

		// Prompt save
		wxFileDialog saveDialog(
			parent,
			"Save Map Before Backup",
			wxEmptyString,
			wxEmptyString,
			"OTBM files (*.otbm)|*.otbm",
			wxFD_SAVE | wxFD_OVERWRITE_PROMPT
		);

		if (saveDialog.ShowModal() != wxID_OK) {
			return "";
		}

		FileName savePath(saveDialog.GetPath());
		if (!editor.saveMap(savePath, true)) {
			wxMessageBox(
				"Failed to save the map.",
				"Backup Error",
				wxOK | wxICON_ERROR,
				parent
			);
			return "";
		}
	}

	const std::string& originalPath = map.getFilename();
	std::string backupPath = generateBackupPath(originalPath);

	wxFileName fn(wxString::FromUTF8(backupPath));
	std::string base = fn.GetName().utf8_string();

	std::printf("[unreachable_cleaner] creating backup: %s\n", backupPath.c_str());

	StateGuard guard(map);

	// Assign backup-specific sidecar filenames based on backup path
	if (map.spawnFormat == SpawnFormat::CanaryCrystal) {
		map.spawnfile = base + "-monster.xml";
		map.spawnNpcFile = base + "-npc.xml";
	} else {
		map.spawnfile = base + "-spawn.xml";
		map.spawnNpcFile.clear();
	}
	map.housefile = base + "-house.xml";
	map.waypointfile = base + "-waypoint.xml";
	map.zonefile = base + "-zones.xml";

	FileName backupFile(wxString::FromUTF8(backupPath));
	IOMapOTBM writer(map.getVersion());
	bool success = false;
	try {
		success = writer.saveMap(map, backupFile);
	} catch (const std::exception& e) {
		std::printf("[unreachable_cleaner] backup exception: %s\n", e.what());
		return "";
	} catch (...) {
		std::printf("[unreachable_cleaner] backup unknown exception\n");
		return "";
	}

	if (!success) {
		std::printf("[unreachable_cleaner] backup failed\n");
		return "";
	}

	std::printf("[unreachable_cleaner] backup created: %s\n", backupPath.c_str());
	return backupPath;
}
