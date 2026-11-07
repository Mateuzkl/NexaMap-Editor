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

#include "editor.h"
#include "map.h"
#include "complexitem.h"
#include "settings.h"
#include "gui.h"
#include "map_display.h"
#include "brush.h"
#include "ground_brush.h"
#include "wall_brush.h"
#include "waypoint_brush.h"
#include "zone_brush.h"
#include "house_exit_brush.h"
#include "doodad_brush.h"
#include "spawn_brush.h"
#include "map_format.h"
#include "spawn_format.h"
#include "item_id_codec.h"
#include "map_backup.h"
#include "multiplayer_session.h"
#include "path_encoding.h"
#include "spawn_source_remap.h"

#include <filesystem>
#include <optional>
#include <utility>
#include <wx/choicdlg.h>

namespace {
	std::string BackupTimestamp() {
		const time_t now = time(nullptr);
		const tm* current_time = localtime(&now);
		ASSERT(current_time);

		std::ostringstream date;
		date << (1900 + current_time->tm_year);
		if (current_time->tm_mon < 9) {
			date << "-"
				 << "0" << current_time->tm_mon + 1;
		} else {
			date << "-" << current_time->tm_mon + 1;
		}
		date << "-" << current_time->tm_mday;
		date << "-" << current_time->tm_hour;
		date << "-" << current_time->tm_min;
		date << "-" << current_time->tm_sec;
		return date.str();
	}

	// The save replaces files by rename, so a hard link keeps the old contents
	// without copying them. A symlink is followed first: link(2) would otherwise
	// preserve the link and not the file behind it.
	bool MakeDatedBackup(const std::filesystem::path& live, const std::filesystem::path& backup) {
		std::error_code ec;
		const std::filesystem::path target = std::filesystem::canonical(live, ec);
		if (ec) {
			return false;
		}
		std::filesystem::create_hard_link(target, backup, ec);
		if (ec) {
			ec.clear();
			std::filesystem::copy_file(target, backup, ec);
		}
		return !ec;
	}

	MapStorageFormat ChooseMapStorageFormat(const OTBMFileMetadata& metadata, const MapFormatDetection& detection) {
		wxArrayString choices;
		choices.Add("TFS - Server IDs and one combined spawn XML");
		choices.Add("Canary/Crystal - ClientIDs and separate monster/NPC XML");
		wxString message = wxstr(detection.reason);
		if (detection.conflict) {
			message += "\n\nConflicting format evidence was found. Choose which format should be used to open this map:";
		} else {
			message += "\n\nThe format cannot be determined safely. Choose how this map should be opened:";
		}
		wxSingleChoiceDialog dialog(g_gui.root, message, "Choose map format", choices);
		dialog.SetSelection(metadata.version.otbm >= MAP_OTBM_5 ? 1 : 0);
		if (dialog.ShowModal() != wxID_OK) {
			return MapStorageFormat::Unknown;
		}
		return dialog.GetSelection() == 1 ? MapStorageFormat::CanaryCrystal : MapStorageFormat::Tfs;
	}

	ClientVersionID ChooseTfsClientVersion(ClientVersionID headerVersion) {
		if (ClientVersion::get(headerVersion)) {
			return headerVersion;
		}

		const ClientVersionList versions = ClientVersion::getAllVisible();
		wxArrayString choices;
		std::vector<ClientVersionID> identifiers;
		int suggestedIndex = 0;
		for (ClientVersion* version : versions) {
			if (!version || !version->isVisible()) {
				continue;
			}
			choices.Add(wxstr(version->getName()));
			identifiers.push_back(version->getID());
			if (version->getID() == g_settings.getInteger(Config::DEFAULT_CLIENT_VERSION)) {
				suggestedIndex = static_cast<int>(identifiers.size() - 1);
			}
		}
		if (choices.empty()) {
			return CLIENT_VERSION_NONE;
		}

		wxSingleChoiceDialog dialog(
			g_gui.root,
			wxString::Format(
				"The OTBM item version %d does not match a configured TFS client.\nChoose the client profile used by this map:",
				headerVersion
			),
			"Choose TFS client profile",
			choices
		);
		dialog.SetSelection(suggestedIndex);
		if (dialog.ShowModal() != wxID_OK) {
			return CLIENT_VERSION_NONE;
		}
		return identifiers.at(static_cast<size_t>(dialog.GetSelection()));
	}

	bool ShouldSkipZoneChange(Brush* brush, const Tile* tile, bool drawing) {
		if (!brush->isZone()) {
			return false;
		}

		const ZoneBrush* zoneBrush = brush->asZone();
		const unsigned int zoneId = zoneBrush->getZone();
		if (zoneId == 0) {
			return true;
		}

		const bool removing = !drawing || zoneBrush->isEraseMode();
		const bool hasZone = tile->hasZone(zoneId);
		return (removing && !hasZone) || (!removing && (!tile->hasGround() || hasZone));
	}
}

Editor::Editor(CopyBuffer& copybuffer) :
	actionQueue(std::make_unique<ActionQueue>(*this)),
	selection(*this),
	copybuffer(copybuffer),
	replace_brush(nullptr) {
	wxString error;
	wxArrayString warnings;
	bool ok = true;

	// A new map belongs to the resource session that is active when its tab is
	// created. Loading a different default here would close every existing tab,
	// including maps owned by other independent resource sessions.
	if (!g_gui.IsCanaryCrystalAssetsLoaded() && g_gui.GetCurrentVersionID() == CLIENT_VERSION_NONE) {
		auto defaultVersion = ClientVersionID(g_settings.getInteger(Config::DEFAULT_CLIENT_VERSION));
		if (defaultVersion == CLIENT_VERSION_NONE) {
			defaultVersion = ClientVersion::getLatestVersion()->getID();
		}
		ok = g_gui.LoadVersion(defaultVersion, error, warnings);
		g_gui.PopupDialog("Error", error, wxOK);
		g_gui.ListDialog("Warnings", warnings);
	}

	if (!ok) {
		throw std::runtime_error("Couldn't load client version");
	}

	MapVersion version;
	version.otbm = g_gui.IsCanaryCrystalAssetsLoaded() ? MAP_OTBM_5 : g_gui.GetCurrentVersion().getPrefferedMapVersionID();
	version.client = g_gui.GetCurrentVersionID();
	map.convert(version);
	map.storageFormat = g_gui.IsCanaryCrystalAssetsLoaded() ? MapStorageFormat::CanaryCrystal : MapStorageFormat::Tfs;
	map.itemIdSpace = g_gui.IsCanaryCrystalAssetsLoaded() ? ItemIdSpace::Client : ItemIdSpace::Server;
	map.sourceItemMajorVersion = g_gui.IsCanaryCrystalAssetsLoaded() ? 4 : g_items.MajorVersion;
	map.sourceItemMinorVersion = g_gui.IsCanaryCrystalAssetsLoaded() ? 4 : g_items.MinorVersion;

	map.height = 2048;
	map.width = 2048;

	static int unnamed_counter = 0;

	std::string sname = "Untitled-" + i2s(++unnamed_counter);
	map.name = sname + ".otbm";
	if (g_gui.IsCanaryCrystalAssetsLoaded()) {
		map.spawnfile = sname + "-monster.xml";
		map.spawnNpcFile = sname + "-npc.xml";
		map.spawnFormat = SpawnFormat::CanaryCrystal;
	} else {
		map.spawnfile = sname + "-spawn.xml";
		map.spawnNpcFile.clear();
		map.spawnFormat = SpawnFormat::Tfs;
	}
	map.housefile = sname + "-house.xml";
	map.waypointfile = sname + "-waypoint.xml";
	map.zonefile = sname + "-zones.xml";
	map.description = "No map description available.";
	map.unnamed = true;

	map.doChange();
}

Editor::Editor(CopyBuffer& copybuffer, const FileName& fn, EditorClientVersionPolicy clientVersionPolicy, const ItemIdCodec* readCodec, bool detachedDecodedView) :
	actionQueue(nullptr),
	selection(*this),
	copybuffer(copybuffer),
	replace_brush(nullptr) {
	OTBMFileMetadata metadata;
	if (!IOMapOTBM::getFileMetadata(fn, metadata)) {
		// g_gui.PopupDialog("Error", "Could not open file \"" + fn.GetFullPath() + "\".", wxOK);
		throw std::runtime_error("Could not open file \"" + nstr(fn.GetFullPath()) + "\".\nThis is not a valid OTBM file or it does not exist.");
	}
	MapVersion ver = metadata.version;

	/*
	if(ver < CLIENT_VERSION_760) {
		long b = g_gui.PopupDialog("Error", "Unsupported Client Version (pre 7.6), do you want to try to load the map anyways?", wxYES | wxNO);
		if(b == wxID_NO) {
			valid_state = false;
			return;
		}
	}
	*/

	const bool keepLoadedClientVersion = clientVersionPolicy == EditorClientVersionPolicy::KeepLoaded;
	MapStorageFormat storageFormat = g_gui.IsCanaryCrystalAssetsLoaded() ? MapStorageFormat::CanaryCrystal : MapStorageFormat::Tfs;
	if (!keepLoadedClientVersion) {
		const std::filesystem::path directory = PathEncoding::FromUtf8(nstr(fn.GetPath()));
		const SpawnDetectionResult spawnDetection = SpawnFormatIO::Detect(
			directory,
			IOMapOTBM::resolveSidecarName(fn, metadata.spawnFile),
			IOMapOTBM::resolveSidecarName(fn, metadata.spawnNpcFile),
			nstr(fn.GetName())
		);
		const MapFormatEvidence evidence {
			metadata.version.otbm,
			static_cast<ClientVersionID>(metadata.itemMinorVersion),
			ClientVersion::get(static_cast<ClientVersionID>(metadata.itemMinorVersion)) != nullptr,
			spawnDetection.format,
			spawnDetection.conflict,
			!metadata.zoneFile.empty(),
		};
		const MapFormatDetection detection = MapFormatDetector::Detect(evidence);
		storageFormat = detection.format;
		if (storageFormat == MapStorageFormat::Unknown) {
			storageFormat = ChooseMapStorageFormat(metadata, detection);
			if (storageFormat == MapStorageFormat::Unknown) {
				throw std::runtime_error("Map opening was cancelled while choosing its storage format.");
			}
		}
	}

	bool success = true;
	wxString loadError;
	if (keepLoadedClientVersion && g_gui.GetCurrentVersionID() == CLIENT_VERSION_NONE) {
		throw std::runtime_error("No client assets are loaded for the converted map.");
	}

	if (!keepLoadedClientVersion && storageFormat == MapStorageFormat::CanaryCrystal && !g_gui.IsCanaryCrystalAssetsLoaded()) {
		wxArrayString warnings;
		if (g_gui.CloseAllEditors()) {
			success = g_gui.LoadCanaryCrystalAssets(loadError, warnings);
			if (success) {
				g_gui.ListDialog("Warnings", warnings);
			}
		} else {
			throw std::runtime_error("All maps using another asset format were not closed.");
		}
	} else if (!keepLoadedClientVersion && storageFormat == MapStorageFormat::Tfs) {
		const ClientVersionID selectedVersion = ChooseTfsClientVersion(ver.client);
		if (selectedVersion == CLIENT_VERSION_NONE) {
			throw std::runtime_error("Map opening was cancelled while choosing its TFS client profile.");
		}
		ver.client = selectedVersion;
		if (g_gui.GetCurrentVersionID() == selectedVersion && !g_gui.IsCanaryCrystalAssetsLoaded()) {
			success = true;
		} else {
			wxArrayString warnings;
			if (g_gui.CloseAllEditors()) {
				success = g_gui.LoadVersion(selectedVersion, loadError, warnings);
				if (success) {
					g_gui.ListDialog("Warnings", warnings);
				}
			} else {
				throw std::runtime_error("All maps of different versions were not closed.");
			}
		}
	}

	if (!success) {
		throw std::runtime_error(nstr(loadError));
	}

	map.storageFormat = storageFormat;
	map.itemIdSpace = storageFormat == MapStorageFormat::CanaryCrystal ? ItemIdSpace::Client : ItemIdSpace::Server;
	{
		ScopedLoadingBar LoadingBar("Loading OTBM map...");
		success = map.open(nstr(fn.GetFullPath()), readCodec);
		if (success) {
			map.storageFormat = storageFormat;
			map.itemIdSpace = storageFormat == MapStorageFormat::CanaryCrystal ? ItemIdSpace::Client : ItemIdSpace::Server;
			map.sourceItemMajorVersion = metadata.itemMajorVersion;
			map.sourceItemMinorVersion = metadata.itemMinorVersion;
		}
		if (success && (keepLoadedClientVersion || storageFormat == MapStorageFormat::CanaryCrystal || ver.client != metadata.version.client)) {
			// Converter output was already reopened and round-trip validated with
			// the currently loaded item database. Its OTB minor value does not
			// need to be a registered RME client profile for this trusted route.
			map.mapVersion.client = g_gui.GetCurrentVersionID();
		}
		/* TODO
		if(success && ver.client == CLIENT_VERSION_854_BAD) {
			int ok = g_gui.PopupDialog("Incorrect OTB", "This map has been saved with an incorrect OTB version, do you want to convert it to the new OTB version?\n\nIf you are not sure, click Yes.", wxYES | wxNO);

			if(ok == wxID_YES){
				ver.client = CLIENT_VERSION_854;
				map.convert(ver);
			}
		}
		*/
	}

	if (!success) {
		throw std::runtime_error("Could not open map.\n" + nstr(map.getError()));
	}
	if (detachedDecodedView) {
		// Client-ID files are decoded to Server IDs for display. Detach the
		// editable view so a normal save can never overwrite the Client-ID file.
		map.filename.clear();
		map.name = nstr(fn.GetName()) + "-server-view.otbm";
		map.unnamed = true;
	}

	actionQueue = std::make_unique<ActionQueue>(*this);
}

Editor::~Editor() {
	multiplayer.reset();
	// Views and multiplayer are released on the GUI thread before queued
	// disposal. Clearing selection is data-only and must precede the undo queue;
	// both still need the map's tile locations to be alive.
	selection.clear();
	actionQueue.reset();
}

void Editor::addBatch(BatchAction* action, int stacking_delay) {
	actionQueue->addBatch(action, stacking_delay);
	g_gui.UpdateMenus();
}

void Editor::addAction(Action* action, int stacking_delay) {
	actionQueue->addAction(action, stacking_delay);
	g_gui.UpdateMenus();
}

bool Editor::saveMap(const FileName& filename, bool showdialog) {
	if (multiplayer && !multiplayer->isHost()) {
		g_gui.PopupDialog("Multiplayer", "Only the host saves the official map. Ask the host to save it.", wxOK);
		return false;
	}
	const std::string originalFilename = map.filename;
	const std::string originalName = map.name;
	const std::string originalSpawnfile = map.spawnfile;
	const std::string originalSpawnNpcFile = map.spawnNpcFile;
	const std::string originalHousefile = map.housefile;
	const std::string originalWaypointfile = map.waypointfile;
	const std::string originalZonefile = map.zonefile;
	const bool originallyUnnamed = map.unnamed;
	const bool originalSpawnFilenamesExplicit = map.spawnFilenamesExplicit;
	std::string savefile = filename.GetFullPath().mb_str(wxConvUTF8).data();
	bool save_as = false;

	if (savefile.empty()) {
		savefile = map.filename;
	}
	FileName c1(wxstr(savefile));
	FileName c2(wxstr(map.filename));
	save_as = c1 != c2;

	// A codec cannot fail per item, so unmapped IDs would be written as their raw
	// value. Save As shows this up front; items placed later must be caught here.
	if (const auto conversion = MapBackupService::storageConversion(map)) {
		const ItemIdConversionPreview preview = AnalyzeItemIdConversion(map, *conversion);
		const uint64_t issues = preview.missingItems + preview.ambiguousItems;
		if (issues > map.getConfirmedItemIdIssues()) {
			const long answer = g_gui.PopupDialog(
				"Confirm item ID conversion",
				wxString::Format(
					"This map is stored in a different item ID space.\n%llu item(s) have no mapping and keep their raw ID, and %llu ambiguous item(s) use the preferred candidate.\n\nSave anyway?",
					static_cast<unsigned long long>(preview.missingItems),
					static_cast<unsigned long long>(preview.ambiguousItems)
				),
				wxYES | wxNO
			);
			if (answer != wxID_YES) {
				return false;
			}
			map.setConfirmedItemIdIssues(issues);
		}
	}

	// If not named yet, or saving under a new name, propagate the file name to the
	// auxilliary files. Keeping the old names on Save As would make both maps
	// share one set of sidecars.
	if (map.unnamed || save_as) {
		FileName _name(filename);
		_name.SetExt("xml");

		if (map.spawnFilenamesExplicit) {
			map.spawnFilenamesExplicit = false;
		} else if (map.spawnFormat == SpawnFormat::CanaryCrystal) {
			_name.SetName(filename.GetName() + "-monster");
			map.spawnfile = nstr(_name.GetFullName());
			_name.SetName(filename.GetName() + "-npc");
			map.spawnNpcFile = nstr(_name.GetFullName());
		} else {
			_name.SetName(filename.GetName() + "-spawn");
			map.spawnfile = nstr(_name.GetFullName());
			map.spawnNpcFile.clear();
		}
		_name.SetName(filename.GetName() + "-house");
		map.housefile = nstr(_name.GetFullName());
		_name.SetName(filename.GetName() + "-waypoint");
		map.waypointfile = nstr(_name.GetFullName());
		_name.SetName(filename.GetName() + "-zones");
		map.zonefile = nstr(_name.GetFullName());

		map.unnamed = false;
	}

	// File object to convert between local paths etc.
	FileName converter;
	converter.Assign(wxstr(savefile));
	const std::filesystem::path map_directory = PathEncoding::FromUtf8(nstr(converter.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME)));

	// Take the dated copies while the old files are still in place. Live files are
	// never moved: IOMapOTBM replaces them atomically once the new ones are durable.
	std::vector<std::pair<std::filesystem::path, std::filesystem::path>> dated_backups;
	wxArrayString backup_warnings;
	if (!save_as && g_settings.getInteger(Config::ALWAYS_MAKE_BACKUP)) {
		const std::string date = BackupTimestamp();
		const auto backup_file = [&](const std::string& name) {
			const std::filesystem::path live = map_directory / PathEncoding::FromUtf8(name);
			std::error_code ec;
			if (name.empty() || !std::filesystem::exists(live, ec)) {
				return;
			}
			std::filesystem::path backup = live;
			backup.replace_filename(live.stem());
			backup += PathEncoding::FromUtf8("." + date);
			backup += live.extension();
			if (MakeDatedBackup(live, backup)) {
				dated_backups.emplace_back(live, backup);
			} else {
				const std::u8string backupName = backup.u8string();
				backup_warnings.push_back("Could not create backup " + wxString::FromUTF8(reinterpret_cast<const char*>(backupName.c_str())));
			}
		};
		backup_file(nstr(converter.GetFullName()));
		backup_file(map.housefile);
		backup_file(map.spawnfile);
		backup_file(map.spawnNpcFile);
		backup_file(map.zonefile);
		backup_file(nstr(converter.GetName()) + "-waypoint-groups.xml");
	}

	// Set up the Map paths
	wxFileName fn = wxstr(savefile);
	map.filename = fn.GetFullPath().mb_str(wxConvUTF8);
	map.name = fn.GetFullName().mb_str(wxConvUTF8);

	if (showdialog) {
		g_gui.CreateLoadBar("Saving OTBM map...");
	}

	// Perform the actual save
	IOMapOTBM mapsaver(map.getVersion());
	std::optional<MappingItemIdCodec> itemIdCodec;
	MapBackupService::configureWriter(mapsaver, map, itemIdCodec);
	const bool success = mapsaver.saveMap(map, fn);

	if (showdialog) {
		g_gui.DestroyLoadBar();
	}

	// Check for errors...
	if (!success) {
		map.filename = originalFilename;
		map.name = originalName;
		map.spawnfile = originalSpawnfile;
		map.spawnNpcFile = originalSpawnNpcFile;
		map.housefile = originalHousefile;
		map.waypointfile = originalWaypointfile;
		map.zonefile = originalZonefile;
		map.unnamed = originallyUnnamed;
		map.spawnFilenamesExplicit = originalSpawnFilenamesExplicit;
		for (const auto& [live, backup] : dated_backups) {
			std::error_code ec;
			std::filesystem::remove(backup, ec);
		}

		// Display the stage and underlying reason instead of reporting every
		// pipeline failure as an OTBM open error.
		wxString message = "Could not save map.";
		if (!mapsaver.getError().empty()) {
			message += "\n\n" + mapsaver.getError();
		} else {
			message += "\n\nNo detailed error was reported.";
		}
		g_gui.PopupDialog("Error", message, wxOK);
		return false;
	}

	// A file the save left alone is still the same inode as its backup.
	for (const auto& [live, backup] : dated_backups) {
		std::error_code ec;
		if (std::filesystem::equivalent(live, backup, ec) && !ec) {
			std::filesystem::remove(backup, ec);
		}
	}

	map.clearChanges();
	if (showdialog) {
		wxArrayString save_warnings = mapsaver.getWarnings();
		for (const wxString& warning : backup_warnings) {
			save_warnings.push_back(warning);
		}
		g_gui.ListDialog("Warnings", save_warnings);
	}
	return true;
}

bool Editor::importMap(const FileName& filename, int import_x_offset, int import_y_offset, int import_z_offset, ImportType house_import_type, ImportType spawn_import_type) {
	Map imported_map;
	bool loaded = imported_map.open(nstr(filename.GetFullPath()));

	if (!loaded) {
		g_gui.PopupDialog("Error", "Error loading map!\n" + imported_map.getError(), wxOK | wxICON_INFORMATION);
		return false;
	}

	// The merge cannot be undone and invalidates both, but a failed load must not
	// cost the user their undo history.
	selection.clear();
	actionQueue->clear();
	g_gui.ListDialog("Warning", imported_map.getWarnings());

	Position offset(import_x_offset, import_y_offset, import_z_offset);

	bool resizemap = false;
	bool resize_asked = false;
	int newsize_x = map.getWidth(), newsize_y = map.getHeight();
	int discarded_tiles = 0;

	g_gui.CreateLoadBar("Merging maps...");

	std::map<uint32_t, uint32_t> town_id_map;
	std::map<uint32_t, uint32_t> house_id_map;

	if (house_import_type != IMPORT_DONT) {
		for (auto tit = imported_map.towns.begin(); tit != imported_map.towns.end();) {
			Town* imported_town = tit->second;
			const uint32_t old_town_id = imported_town->getID();
			Town* current_town = map.towns.getTown(old_town_id);

			Position oldexit = imported_town->getTemplePosition();
			Position newexit = oldexit + offset;
			if (newexit.isValid()) {
				imported_town->setTemplePosition(newexit);
				map.getOrCreateTile(newexit)->getLocation()->increaseTownCount();
			}

			switch (house_import_type) {
				case IMPORT_MERGE: {
					town_id_map[old_town_id] = old_town_id;
					if (current_town) {
						++tit;
						continue;
					}
					break;
				}
				case IMPORT_SMART_MERGE: {
					if (current_town) {
						// Compare and insert/merge depending on parameters
						if (current_town->getName() == imported_town->getName() && current_town->getID() == imported_town->getID()) {
							// Just add to map
							town_id_map[old_town_id] = current_town->getID();
							++tit;
							continue;
						} else {
							// Conflict! Find a newd id and replace old
							uint32_t new_id = map.towns.getEmptyID();
							imported_town->setID(new_id);
							town_id_map[old_town_id] = new_id;
						}
					} else {
						town_id_map[old_town_id] = old_town_id;
					}
					break;
				}
				case IMPORT_INSERT: {
					// Find a newd id and replace old
					uint32_t new_id = map.towns.getEmptyID();
					imported_town->setID(new_id);
					town_id_map[old_town_id] = new_id;
					break;
				}
				case IMPORT_DONT: {
					++tit;
					continue; // Should never happend..?
					break; // Continue or break ?
				}
			}

			map.towns.addTown(imported_town);

#ifdef __VISUALC__ // C++0x compliance to some degree :)
			tit = imported_map.towns.erase(tit);
#else // Bulky, slow way
			TownMap::iterator tmp_iter = tit;
			++tmp_iter;
			uint32_t next_key = 0;
			if (tmp_iter != imported_map.towns.end()) {
				next_key = tmp_iter->first;
			}
			imported_map.towns.erase(tit);
			if (next_key != 0) {
				tit = imported_map.towns.find(next_key);
			} else {
				tit = imported_map.towns.end();
			}
#endif
		}

		auto move_house_exit = [&](House* house, const Position& old_exit) {
			if (!house || old_exit == Position()) {
				return;
			}
			const Position new_exit = old_exit + offset;
			if (new_exit.isValid()) {
				house->setExit(&map, new_exit);
			}
		};

		for (auto hit = imported_map.houses.begin(); hit != imported_map.houses.end();) {
			House* imported_house = hit->second;
			const uint32_t old_house_id = imported_house->getID();
			House* current_house = map.houses.getHouse(old_house_id);
			auto town_remap = town_id_map.find(imported_house->townid);
			if (town_remap != town_id_map.end()) {
				imported_house->townid = town_remap->second;
			}

			Position oldexit = imported_house->getExit();
			imported_house->setExit(nullptr, Position()); // Reset it

			switch (house_import_type) {
				case IMPORT_MERGE: {
					house_id_map[old_house_id] = old_house_id;
					if (current_house) {
						++hit;
						move_house_exit(current_house, oldexit);
						continue;
					}
					break;
				}
				case IMPORT_SMART_MERGE: {
					if (current_house) {
						// Compare and insert/merge depending on parameters
						if (current_house->name == imported_house->name && current_house->townid == imported_house->townid) {
							// Just add to map
							house_id_map[old_house_id] = current_house->getID();
							++hit;
							move_house_exit(current_house, oldexit);
							continue;
						} else {
							// Conflict! Find a newd id and replace old
							uint32_t new_id = map.houses.getEmptyID();
							house_id_map[old_house_id] = new_id;
							imported_house->setID(new_id);
						}
					} else {
						house_id_map[old_house_id] = old_house_id;
					}
					break;
				}
				case IMPORT_INSERT: {
					// Find a newd id and replace old
					uint32_t new_id = map.houses.getEmptyID();
					house_id_map[old_house_id] = new_id;
					imported_house->setID(new_id);
					break;
				}
				case IMPORT_DONT: {
					++hit;
					move_house_exit(imported_house, oldexit);
					continue; // Should never happend..?
					break;
				}
			}

			move_house_exit(imported_house, oldexit);
			if (map.houses.addHouse(imported_house)) {
				hit = imported_map.houses.erase(hit);
			} else {
				++hit;
			}
		}
	}

	std::map<Position, Spawn*> spawn_map;
	if (spawn_import_type != IMPORT_DONT) {
		for (auto siter = imported_map.spawns.begin(); siter != imported_map.spawns.end();) {
			auto next = siter;
			++next;
			const Position old_spawn_pos = *siter;
			const Position new_spawn_pos = old_spawn_pos + offset;
			Tile* imported_tile = imported_map.getTile(old_spawn_pos);
			if (imported_tile && imported_tile->spawn && new_spawn_pos.isValid()) {
				// spawn_map owns it from here on: the tile may yet be discarded and
				// would take the Spawn with it.
				spawn_map[new_spawn_pos] = imported_tile->spawn;
				imported_tile->spawn = nullptr;
				imported_map.spawns.erase(siter);
			}
			siter = next;
		}
	}

	map.waypoints.importWaypointsFrom(imported_map.waypoints, offset);

	uint64_t tiles_merged = 0;
	uint64_t tiles_to_import = imported_map.tilecount;
	for (MapIterator mit = imported_map.begin(); mit != imported_map.end(); ++mit) {
		if (tiles_merged % 8092 == 0) {
			g_gui.SetLoadDone(int(100.0 * tiles_merged / tiles_to_import));
		}
		++tiles_merged;

		Tile* import_tile = (*mit)->get();
		Position new_pos = import_tile->getPosition() + offset;
		if (!new_pos.isValid()) {
			++discarded_tiles;
			continue;
		}

		if (!resizemap && (new_pos.x > map.getWidth() || new_pos.y > map.getHeight())) {
			if (resize_asked) {
				++discarded_tiles;
				continue;
			} else {
				resize_asked = true;
				int ret = g_gui.PopupDialog("Collision", "The imported tiles are outside the current map scope. Do you want to resize the map? (Else additional tiles will be removed)", wxYES | wxNO);

				if (ret == wxID_YES) {
					// ...
					resizemap = true;
				} else {
					++discarded_tiles;
					continue;
				}
			}
		}

		if (new_pos.x > newsize_x) {
			newsize_x = new_pos.x;
		}
		if (new_pos.y > newsize_y) {
			newsize_y = new_pos.y;
		}

		imported_map.setTile(import_tile->getPosition(), nullptr);
		TileLocation* location = map.createTileL(new_pos);

		// Check if we should update any houses
		uint32_t new_houseid = 0;
		auto house_remap = house_id_map.find(import_tile->getHouseID());
		if (house_remap != house_id_map.end()) {
			new_houseid = house_remap->second;
		}
		House* house = map.houses.getHouse(new_houseid);
		if (import_tile->isHouseTile() && house_import_type != IMPORT_DONT && house) {
			// We need to notify houses of the tile moving
			house->removeTile(import_tile);
			import_tile->setLocation(location);
			house->addTile(import_tile);
		} else {
			// The imported house ID means nothing in this map's registry.
			import_tile->setHouseID(0);
			import_tile->setLocation(location);
		}

		if (offset != Position(0, 0, 0)) {
			if (import_tile->creature && import_tile->creature->hasSpawnSource()) {
				import_tile->creature->setSpawnSource(import_tile->creature->getSpawnSource() + offset);
			}
			for (auto iter = import_tile->items.begin(); iter != import_tile->items.end(); ++iter) {
				Item* item = *iter;
				if (auto* teleport = dynamic_cast<Teleport*>(item)) {
					teleport->setDestination(teleport->getDestination() + offset);
				}
			}
		}

		Tile* old_tile = map.getTile(new_pos);
		if (old_tile) {
			map.removeSpawn(old_tile);
		}
		// Only spawns that were not imported are left on the tile.
		delete import_tile->spawn;
		import_tile->spawn = nullptr;

		map.setTile(new_pos, import_tile, true);
	}

	for (auto spawn_iter = spawn_map.begin(); spawn_iter != spawn_map.end(); ++spawn_iter) {
		Position pos = spawn_iter->first;
		if (!resizemap && (pos.x > map.getWidth() || pos.y > map.getHeight())) {
			// Its tile was discarded with the rest of the out-of-scope area.
			delete spawn_iter->second;
			continue;
		}
		TileLocation* location = map.createTileL(pos);
		Tile* tile = location->get();
		if (!tile) {
			tile = map.allocator(location);
			map.setTile(pos, tile);
		} else if (tile->spawn) {
			map.removeSpawnInternal(tile);
			delete tile->spawn;
		}
		tile->spawn = spawn_iter->second;

		map.addSpawn(tile);
	}

	g_gui.DestroyLoadBar();

	map.setWidth(newsize_x);
	map.setHeight(newsize_y);
	map.doChange();
	g_gui.PopupDialog("Success", "Map imported successfully, " + i2ws(discarded_tiles) + " tiles were discarded as invalid.", wxOK);

	g_gui.RefreshPalettes();
	g_gui.FitViewToMap();

	return true;
}

void Editor::borderizeSelection() {
	if (selection.size() == 0) {
		g_gui.SetStatusText("No items selected. Can't borderize.");
	}

	Action* action = actionQueue->createAction(ACTION_BORDERIZE);
	for (Tile* tile : selection) {
		Tile* newTile = tile->deepCopy(map);
		newTile->borderize(&map);
		newTile->select();
		action->addChange(newd Change(newTile));
	}
	addAction(action);
}

void Editor::borderizeMap(bool showdialog) {
	if (showdialog) {
		g_gui.CreateLoadBar("Borderizing map...");
	}

	uint64_t tiles_done = 0;
	for (TileLocation* tileLocation : map) {
		if (showdialog && tiles_done % 4096 == 0) {
			g_gui.SetLoadDone(static_cast<int32_t>(tiles_done / double(map.tilecount) * 100.0));
		}

		Tile* tile = tileLocation->get();
		ASSERT(tile);

		tile->borderize(&map);
		++tiles_done;
	}

	if (showdialog) {
		g_gui.DestroyLoadBar();
	}
	map.doChange();
}

void Editor::randomizeSelection() {
	if (selection.size() == 0) {
		g_gui.SetStatusText("No items selected. Can't randomize.");
	}

	Action* action = actionQueue->createAction(ACTION_RANDOMIZE);
	for (Tile* tile : selection) {
		Tile* newTile = tile->deepCopy(map);
		GroundBrush* groundBrush = newTile->getGroundBrush();
		if (groundBrush && groundBrush->isReRandomizable()) {
			groundBrush->draw(&map, newTile, nullptr);

			Item* oldGround = tile->ground;
			Item* newGround = newTile->ground;
			if (oldGround && newGround) {
				newGround->setActionID(oldGround->getActionID());
				newGround->setUniqueID(oldGround->getUniqueID());
			}

			newTile->select();
			action->addChange(newd Change(newTile));
		}
	}
	addAction(action);
}

void Editor::randomizeMap(bool showdialog) {
	MapChunkRevisionTracker::Batch chunkBatch(map.getChunkRevisionTracker());
	// Replacing a selected ground leaves its tile flagged unselected, and undo
	// then skips removing that tile from the selection when it is swapped out.
	selection.clear();
	if (showdialog) {
		g_gui.CreateLoadBar("Randomizing map...");
	}

	uint64_t tiles_done = 0;
	for (TileLocation* tileLocation : map) {
		if (showdialog && tiles_done % 4096 == 0) {
			g_gui.SetLoadDone(static_cast<int32_t>(tiles_done / double(map.tilecount) * 100.0));
		}

		Tile* tile = tileLocation->get();
		ASSERT(tile);

		GroundBrush* groundBrush = tile->getGroundBrush();
		if (groundBrush) {
			Item* oldGround = tile->ground;

			uint16_t actionId, uniqueId;
			if (oldGround) {
				actionId = oldGround->getActionID();
				uniqueId = oldGround->getUniqueID();
			} else {
				actionId = 0;
				uniqueId = 0;
			}
			groundBrush->draw(&map, tile, nullptr);

			Item* newGround = tile->ground;
			if (newGround) {
				newGround->setActionID(actionId);
				newGround->setUniqueID(uniqueId);
			}
			tile->update();
		}
		++tiles_done;
	}

	if (showdialog) {
		g_gui.DestroyLoadBar();
	}
	map.doChange();
}

void Editor::clearInvalidHouseTiles(bool showdialog) {
	if (showdialog) {
		g_gui.CreateLoadBar("Clearing invalid house tiles...");
	}

	Houses& houses = map.houses;

	auto iter = houses.begin();
	while (iter != houses.end()) {
		House* h = iter->second;
		if (!h) {
			iter = houses.erase(iter);
		} else if (map.towns.getTown(h->townid) == nullptr) {
			++iter;
			houses.removeHouse(h);
		} else {
			++iter;
		}
	}

	uint64_t tiles_done = 0;
	for (MapIterator map_iter = map.begin(); map_iter != map.end(); ++map_iter) {
		if (showdialog && tiles_done % 4096 == 0) {
			g_gui.SetLoadDone(int(tiles_done / double(map.tilecount) * 100.0));
		}

		Tile* tile = (*map_iter)->get();
		ASSERT(tile);
		if (tile->isHouseTile()) {
			if (houses.getHouse(tile->getHouseID()) == nullptr) {
				tile->setHouse(nullptr);
			}
		}
		++tiles_done;
	}

	if (showdialog) {
		g_gui.DestroyLoadBar();
	}

	// Undo snapshots can still name the removed houses, and palettes and house
	// brushes can still point at them.
	actionQueue->clear();
	map.doChange();
	g_gui.RefreshPalettes();
}

size_t Editor::removeEmptySpawns(bool showdialog) {
	if (showdialog) {
		g_gui.CreateLoadBar("Searching map for empty spawns to remove...");
	}

	selection.clear();

	CreatureVector creatures;
	TileVector toDeleteSpawns;
	for (const auto& spawnPosition : map.spawns) {
		Tile* tile = map.getTile(spawnPosition);
		if (!tile || !tile->spawn) {
			continue;
		}

		const int32_t radius = tile->spawn->getSize();

		bool empty = true;
		for (int32_t y = -radius; y <= radius; ++y) {
			for (int32_t x = -radius; x <= radius; ++x) {
				Tile* creature_tile = map.getTile(spawnPosition + Position(x, y, 0));
				if (creature_tile && creature_tile->creature && !creature_tile->creature->isSaved()) {
					if (creature_tile->creature->hasSpawnSource() && creature_tile->creature->getSpawnSource() != spawnPosition) {
						continue;
					}
					creature_tile->creature->save();
					creatures.push_back(creature_tile->creature);
					empty = false;
				}
			}
		}

		if (empty) {
			toDeleteSpawns.push_back(tile);
		}
	}

	for (Creature* creature : creatures) {
		creature->reset();
	}

	const size_t count = toDeleteSpawns.size();
	if (count == 0) {
		if (showdialog) {
			g_gui.DestroyLoadBar();
		}
		return 0;
	}

	BatchAction* batch = actionQueue->createBatch(ACTION_DELETE_TILES);
	Action* action = actionQueue->createAction(batch);

	size_t removed = 0;
	for (const auto& tile : toDeleteSpawns) {
		Tile* newtile = tile->deepCopy(map);
		delete newtile->spawn;
		newtile->spawn = nullptr;
		++removed;
		if (showdialog && count > 0 && removed % 5 == 0) {
			g_gui.SetLoadDone(static_cast<int32_t>(100 * removed / count));
		}
		action->addChange(newd Change(newtile));
	}

	batch->addAndCommitAction(action);
	addBatch(batch);

	if (showdialog) {
		g_gui.DestroyLoadBar();
	}

	map.doChange();
	return removed;
}

void Editor::clearModifiedTileState(bool showdialog) {
	if (showdialog) {
		g_gui.CreateLoadBar("Clearing modified state from all tiles...");
	}

	uint64_t tiles_done = 0;
	for (MapIterator map_iter = map.begin(); map_iter != map.end(); ++map_iter) {
		if (showdialog && tiles_done % 4096 == 0) {
			g_gui.SetLoadDone(int(tiles_done / double(map.tilecount) * 100.0));
		}

		Tile* tile = (*map_iter)->get();
		ASSERT(tile);
		tile->unmodify();
		++tiles_done;
	}

	if (showdialog) {
		g_gui.DestroyLoadBar();
	}
}

void Editor::moveSelection(Position offset) {
	BatchAction* batchAction = actionQueue->createBatch(ACTION_MOVE); // Our saved action batch, for undo!
	Action* action;

	// Remove tiles from the map
	action = actionQueue->createAction(batchAction); // Our action!
	bool doborders = false;
	TileSet tmp_storage;
	std::set<Position> movedSpawnCenters;

	// Update the tiles with the newd positions
	for (auto it = selection.begin(); it != selection.end(); ++it) {
		// First we get the old tile and it's position
		Tile* tile = (*it);
		if (tile->spawn && tile->spawn->isSelected()) {
			movedSpawnCenters.insert(tile->getPosition());
		}
		// const Position pos = tile->getPosition();

		// Create the duplicate source tile, which will replace the old one later
		Tile* old_src_tile = tile;
		Tile* new_src_tile;

		new_src_tile = old_src_tile->deepCopy(map);

		Tile* tmp_storage_tile = map.allocator(tile->getLocation());

		// Get all the selected items from the NEW source tile and iterate through them
		// This transfers ownership to the temporary tile
		ItemVector tile_selection = new_src_tile->popSelectedItems();
		for (auto iit = tile_selection.begin(); iit != tile_selection.end(); iit++) {
			// Add the copied item to the newd destination tile,
			Item* item = (*iit);
			tmp_storage_tile->addItem(item);
		}
		// Move spawns
		if (new_src_tile->spawn && new_src_tile->spawn->isSelected()) {
			tmp_storage_tile->spawn = new_src_tile->spawn;
			new_src_tile->spawn = nullptr;
		}
		// Move creatures
		if (new_src_tile->creature && new_src_tile->creature->isSelected()) {
			tmp_storage_tile->creature = new_src_tile->creature;
			new_src_tile->creature = nullptr;
		}

		// Move house data & tile status if ground is transferred
		if (tmp_storage_tile->ground) {
			tmp_storage_tile->house_id = new_src_tile->house_id;
			new_src_tile->house_id = 0;
			tmp_storage_tile->setMapFlags(new_src_tile->getMapFlags());
			tmp_storage_tile->zones = new_src_tile->zones;
			new_src_tile->setMapFlags(TILESTATE_NONE);
			new_src_tile->removeZones();
			doborders = true;
		}

		tmp_storage.insert(tmp_storage_tile);
		// Add the tile copy to the action
		action->addChange(newd Change(new_src_tile));
	}
	// Commit changes to map
	if (!batchAction->addAndCommitAction(action)) {
		for (Tile* tile : tmp_storage) {
			delete tile;
		}
		batchAction->rollback();
		delete batchAction;
		g_gui.SetStatusText("Move cancelled: unable to remove the source tiles.");
		return;
	}

	// Remove old borders (and create some newd?)
	if (g_settings.getInteger(Config::USE_AUTOMAGIC) && g_settings.getInteger(Config::BORDERIZE_DRAG) && selection.size() < size_t(g_settings.getInteger(Config::BORDERIZE_DRAG_THRESHOLD))) {
		action = actionQueue->createAction(batchAction);
		TileList borderize_tiles;
		// Go through all modified (selected) tiles (might be slow)
		for (auto it = tmp_storage.begin(); it != tmp_storage.end(); ++it) {
			Position pos = (*it)->getPosition();
			// Go through all neighbours
			Tile* t;
			t = map.getTile(pos.x, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x - 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x + 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x - 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x + 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x - 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x + 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
		}
		// Remove duplicates
		borderize_tiles.sort();
		borderize_tiles.unique();
		// Do le borders!
		for (auto it = borderize_tiles.begin(); it != borderize_tiles.end(); ++it) {
			Tile* tile = *it;
			Tile* new_tile = (*it)->deepCopy(map);
			if (doborders) {
				new_tile->borderize(&map);
			}
			new_tile->wallize(&map);
			new_tile->tableize(&map);
			new_tile->carpetize(&map);
			if (tile->ground && tile->ground->isSelected()) {
				new_tile->selectGround();
			}
			action->addChange(newd Change(new_tile));
		}
		// Commit changes to map
		if (action->size() == 0) {
			delete action;
		} else if (!batchAction->addAndCommitAction(action)) {
			for (Tile* tile : tmp_storage) {
				delete tile;
			}
			batchAction->rollback();
			delete batchAction;
			g_gui.SetStatusText("Move cancelled: unable to update source borders.");
			return;
		}
	}

	// New action for adding the destination tiles
	action = actionQueue->createAction(batchAction);
	for (auto it = tmp_storage.begin(); it != tmp_storage.end(); ++it) {
		Tile* tile = (*it);
		const Position old_pos = tile->getPosition();
		Position new_pos;

		new_pos = old_pos - offset;

		if (!new_pos.isValid()) {
			delete tile;
			continue;
		}
		// Create the duplicate dest tile, which will replace the old one later
		TileLocation* location = map.createTileL(new_pos);
		Tile* old_dest_tile = location->get();
		Tile* new_dest_tile = nullptr;

		Creature* transferredCreature = tile->creature;
		if (transferredCreature && transferredCreature->hasSpawnSource()) {
			const Position origSource = transferredCreature->getSpawnSource();
			if (movedSpawnCenters.contains(origSource)) {
				transferredCreature->setSpawnSource(origSource - offset);
			}
		}

		if (g_settings.getInteger(Config::MERGE_MOVE) || !tile->ground) {
			// Move items
			if (old_dest_tile) {
				new_dest_tile = old_dest_tile->deepCopy(map);
				if (new_dest_tile->spawn && tile->spawn) {
					auto* mergedSpawn = new_dest_tile->spawn->deepCopy();
					MergeSpawnMetadata(*mergedSpawn, *tile->spawn);
					if (tile->spawn->isSelected()) {
						mergedSpawn->select();
					}
					delete tile->spawn;
					tile->spawn = mergedSpawn;
				}
			} else {
				new_dest_tile = map.allocator(location);
			}
			new_dest_tile->merge(tile);
			delete tile;
		} else {
			// Replace tile instead of just merge
			tile->setLocation(location);
			new_dest_tile = tile;
		}

		action->addChange(newd Change(new_dest_tile));
	}

	// Commit changes to the map
	if (action->size() == 0) {
		delete action;
	} else if (!batchAction->addAndCommitAction(action)) {
		batchAction->rollback();
		delete batchAction;
		g_gui.SetStatusText("Move cancelled: unable to commit destination tiles.");
		return;
	}

	// Create borders
	if (g_settings.getInteger(Config::USE_AUTOMAGIC) && g_settings.getInteger(Config::BORDERIZE_DRAG) && selection.size() < size_t(g_settings.getInteger(Config::BORDERIZE_DRAG_THRESHOLD))) {
		action = actionQueue->createAction(batchAction);
		TileList borderize_tiles;
		// Go through all modified (selected) tiles (might be slow)
		for (auto it = selection.begin(); it != selection.end(); it++) {
			bool add_me = false; // If this tile is touched
			Position pos = (*it)->getPosition();
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
				borderize_tiles.push_back(*it);
			}
		}
		// Remove duplicates
		borderize_tiles.sort();
		borderize_tiles.unique();
		// Do le borders!
		for (auto it = borderize_tiles.begin(); it != borderize_tiles.end(); it++) {
			Tile* tile = *it;
			if (tile->ground) {
				if (tile->ground->getGroundBrush()) {
					Tile* new_tile = tile->deepCopy(map);

					if (doborders) {
						new_tile->borderize(&map);
					}

					new_tile->wallize(&map);
					new_tile->tableize(&map);
					new_tile->carpetize(&map);
					if (tile->ground->isSelected()) {
						new_tile->selectGround();
					}

					action->addChange(newd Change(new_tile));
				}
			}
		}
		// Commit changes to map
		if (action->size() == 0) {
			delete action;
		} else if (!batchAction->addAndCommitAction(action)) {
			batchAction->rollback();
			delete batchAction;
			g_gui.SetStatusText("Move cancelled: unable to update destination borders.");
			return;
		}
	}

	// Store the action for undo
	addBatch(batchAction);
	selection.updateSelectionCount();
}

void Editor::destroySelection() {
	if (selection.size() == 0) {
		g_gui.SetStatusText("No selected items to delete.");
	} else {
		int tile_count = 0;
		int item_count = 0;
		PositionList tilestoborder;

		BatchAction* batch = actionQueue->createBatch(ACTION_DELETE_TILES);
		Action* action = actionQueue->createAction(batch);

		for (auto it = selection.begin(); it != selection.end(); ++it) {
			tile_count++;

			Tile* tile = *it;
			Tile* newtile = tile->deepCopy(map);

			ItemVector tile_selection = newtile->popSelectedItems();
			for (auto iit = tile_selection.begin(); iit != tile_selection.end(); ++iit) {
				++item_count;
				// Delete the items from the tile
				delete *iit;
			}

			if (newtile->creature && newtile->creature->isSelected()) {
				delete newtile->creature;
				newtile->creature = nullptr;
			}

			if (newtile->spawn && newtile->spawn->isSelected()) {
				delete newtile->spawn;
				newtile->spawn = nullptr;
			}

			if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
				for (int y = -1; y <= 1; y++) {
					for (int x = -1; x <= 1; x++) {
						tilestoborder.push_back(
							Position(tile->getPosition().x + x, tile->getPosition().y + y, tile->getPosition().z)
						);
					}
				}
			}
			action->addChange(newd Change(newtile));
		}

		batch->addAndCommitAction(action);

		if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
			// Remove duplicates
			tilestoborder.sort();
			tilestoborder.unique();

			action = actionQueue->createAction(batch);
			for (auto it = tilestoborder.begin(); it != tilestoborder.end(); ++it) {
				TileLocation* location = map.createTileL(*it);
				Tile* tile = location->get();

				if (tile) {
					Tile* new_tile = tile->deepCopy(map);
					new_tile->borderize(&map);
					new_tile->wallize(&map);
					new_tile->tableize(&map);
					new_tile->carpetize(&map);
					action->addChange(newd Change(new_tile));
				} else {
					Tile* new_tile = map.allocator(location);
					new_tile->borderize(&map);
					if (new_tile->size()) {
						action->addChange(newd Change(new_tile));
					} else {
						delete new_tile;
					}
				}
			}

			batch->addAndCommitAction(action);
		}

		addBatch(batch);
		wxString ss;
		ss << "Deleted " << tile_count << " tile" << (tile_count > 1 ? "s" : "") << " (" << item_count << " item" << (item_count > 1 ? "s" : "") << ")";
		g_gui.SetStatusText(ss);
	}
}

// Macro to avoid useless code repetition
void doSurroundingBorders(DoodadBrush* doodad_brush, PositionList& tilestoborder, Tile* buffer_tile, Tile* new_tile) {
	if (doodad_brush->doNewBorders() && g_settings.getInteger(Config::USE_AUTOMAGIC)) {
		tilestoborder.push_back(Position(new_tile->getPosition().x, new_tile->getPosition().y, new_tile->getPosition().z));
		if (buffer_tile->hasGround()) {
			for (int y = -1; y <= 1; y++) {
				for (int x = -1; x <= 1; x++) {
					tilestoborder.push_back(Position(new_tile->getPosition().x + x, new_tile->getPosition().y + y, new_tile->getPosition().z));
				}
			}
		} else if (buffer_tile->hasWall()) {
			tilestoborder.push_back(Position(new_tile->getPosition().x, new_tile->getPosition().y - 1, new_tile->getPosition().z));
			tilestoborder.push_back(Position(new_tile->getPosition().x - 1, new_tile->getPosition().y, new_tile->getPosition().z));
			tilestoborder.push_back(Position(new_tile->getPosition().x + 1, new_tile->getPosition().y, new_tile->getPosition().z));
			tilestoborder.push_back(Position(new_tile->getPosition().x, new_tile->getPosition().y + 1, new_tile->getPosition().z));
		}
	}
}

void removeDuplicateWalls(Tile* buffer, Tile* tile) {
	for (ItemVector::const_iterator iter = buffer->items.begin(); iter != buffer->items.end(); ++iter) {
		if ((*iter)->getWallBrush()) {
			tile->cleanWalls((*iter)->getWallBrush());
		}
	}
}

bool Editor::applyDoodadPositions(const PositionVector& positions, bool alt, bool dodraw, ActionIdentifier identifier) {
	Brush* brush = g_gui.GetCurrentBrush();
	if (!brush || !brush->isDoodad() || positions.empty()) {
		return false;
	}

	std::unique_ptr<BatchAction> batch(actionQueue->createBatch(identifier));
	PositionList tilestoborder;
	try {
		if (dodraw) {
			BaseMap* bufferMap = g_gui.doodad_buffer_map.get();
			DoodadBrush* doodadBrush = brush->asDoodad();
			for (const Position& offset : positions) {
				std::unique_ptr<Action> action(actionQueue->createAction(batch.get()));
				const Position deltaPosition = offset - Position(0x8000, 0x8000, 0x8);
				for (MapIterator iterator = bufferMap->begin(); iterator != bufferMap->end(); ++iterator) {
					Tile* bufferTile = (*iterator)->get();
					const Position position = bufferTile->getPosition() + deltaPosition;
					if (!position.isValid()) {
						continue;
					}

					TileLocation* location = map.createTileL(position);
					Tile* tile = location->get();
					const bool canPlace = doodadBrush->placeOnBlocking() || alt || (tile && !tile->isBlocking());
					if (!canPlace) {
						continue;
					}
					if (tile && !doodadBrush->placeOnDuplicate() && !alt) {
						const bool duplicate = std::any_of(tile->items.begin(), tile->items.end(), [doodadBrush](const Item* item) { return item && doodadBrush->ownsItem(item); });
						if (duplicate) {
							continue;
						}
					}

					std::unique_ptr<Tile> changed(tile ? tile->deepCopy(map) : map.allocator(location));
					removeDuplicateWalls(bufferTile, changed.get());
					doSurroundingBorders(doodadBrush, tilestoborder, bufferTile, changed.get());
					changed->merge(bufferTile);
					action->addChange(newd Change(changed.release()));
				}
				if (action->size() != 0 && !batch->addAndCommitAction(action.release())) {
					batch->rollback();
					throw std::runtime_error("could not commit doodad placement action");
				}
			}
		} else {
			std::unique_ptr<Action> action(actionQueue->createAction(batch.get()));
			for (const Position& position : positions) {
				Tile* tile = map.getTile(position);
				if (!tile) {
					continue;
				}
				std::unique_ptr<Tile> changed(tile->deepCopy(map));
				brush->undraw(&map, changed.get());
				action->addChange(newd Change(changed.release()));
			}
			if (action->size() != 0 && !batch->addAndCommitAction(action.release())) {
				batch->rollback();
				throw std::runtime_error("could not commit doodad erase action");
			}
		}

		if (!tilestoborder.empty()) {
			tilestoborder.sort();
			tilestoborder.unique();
			std::unique_ptr<Action> borderAction(actionQueue->createAction(batch.get()));
			for (const Position& position : tilestoborder) {
				Tile* tile = map.getTile(position);
				if (!tile) {
					continue;
				}
				std::unique_ptr<Tile> changed(tile->deepCopy(map));
				changed->borderize(&map);
				changed->wallize(&map);
				borderAction->addChange(newd Change(changed.release()));
			}
			if (borderAction->size() != 0 && !batch->addAndCommitAction(borderAction.release())) {
				batch->rollback();
				throw std::runtime_error("could not commit doodad border action");
			}
		}
	} catch (...) {
		batch->rollback();
		throw;
	}

	if (batch->size() == 0) {
		return false;
	}
	actionQueue->resetTimer();
	addBatch(batch.release(), 0);
	actionQueue->resetTimer();
	return true;
}

void Editor::drawInternal(Position offset, bool alt, bool dodraw) {
	Brush* brush = g_gui.GetCurrentBrush();
	if (!brush) {
		return;
	}

	if (brush->isDoodad()) {
		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);
		BaseMap* buffer_map = g_gui.doodad_buffer_map.get();

		Position delta_pos = offset - Position(0x8000, 0x8000, 0x8);
		PositionList tilestoborder;

		for (MapIterator it = buffer_map->begin(); it != buffer_map->end(); ++it) {
			Tile* buffer_tile = (*it)->get();
			Position pos = buffer_tile->getPosition() + delta_pos;
			if (!pos.isValid()) {
				continue;
			}

			TileLocation* location = map.createTileL(pos);
			Tile* tile = location->get();
			DoodadBrush* doodad_brush = brush->asDoodad();

			if (doodad_brush->placeOnBlocking() || alt) {
				if (tile) {
					bool place = true;
					if (!doodad_brush->placeOnDuplicate() && !alt) {
						for (ItemVector::const_iterator iter = tile->items.begin(); iter != tile->items.end(); ++iter) {
							if (doodad_brush->ownsItem(*iter)) {
								place = false;
								break;
							}
						}
					}
					if (place) {
						Tile* new_tile = tile->deepCopy(map);
						removeDuplicateWalls(buffer_tile, new_tile);
						doSurroundingBorders(doodad_brush, tilestoborder, buffer_tile, new_tile);
						new_tile->merge(buffer_tile);
						action->addChange(newd Change(new_tile));
					}
				} else {
					Tile* new_tile = map.allocator(location);
					removeDuplicateWalls(buffer_tile, new_tile);
					doSurroundingBorders(doodad_brush, tilestoborder, buffer_tile, new_tile);
					new_tile->merge(buffer_tile);
					action->addChange(newd Change(new_tile));
				}
			} else {
				if (tile && !tile->isBlocking()) {
					bool place = true;
					if (!doodad_brush->placeOnDuplicate() && !alt) {
						for (ItemVector::const_iterator iter = tile->items.begin(); iter != tile->items.end(); ++iter) {
							if (doodad_brush->ownsItem(*iter)) {
								place = false;
								break;
							}
						}
					}
					if (place) {
						Tile* new_tile = tile->deepCopy(map);
						removeDuplicateWalls(buffer_tile, new_tile);
						doSurroundingBorders(doodad_brush, tilestoborder, buffer_tile, new_tile);
						new_tile->merge(buffer_tile);
						action->addChange(newd Change(new_tile));
					}
				}
			}
		}
		batch->addAndCommitAction(action);

		if (tilestoborder.size() > 0) {
			Action* action = actionQueue->createAction(batch);

			// Remove duplicates
			tilestoborder.sort();
			tilestoborder.unique();

			for (PositionList::const_iterator it = tilestoborder.begin(); it != tilestoborder.end(); ++it) {
				Tile* tile = map.getTile(*it);
				if (tile) {
					Tile* new_tile = tile->deepCopy(map);
					new_tile->borderize(&map);
					new_tile->wallize(&map);
					action->addChange(newd Change(new_tile));
				}
			}
			batch->addAndCommitAction(action);
		}
		addBatch(batch, 2);
	} else if (brush->isHouseExit()) {
		HouseExitBrush* house_exit_brush = brush->asHouseExit();
		if (!house_exit_brush->canDraw(&map, offset)) {
			return;
		}

		House* house = map.houses.getHouse(house_exit_brush->getHouseID());
		if (!house) {
			return;
		}

		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);
		action->addChange(Change::Create(house, offset));
		batch->addAndCommitAction(action);
		addBatch(batch, 2);
	} else if (brush->isWaypoint()) {
		WaypointBrush* waypoint_brush = brush->asWaypoint();
		if (waypoint_brush->getWaypoint().empty()) {
			return;
		}

		Waypoint* waypoint = map.waypoints.getWaypoint(waypoint_brush->getWaypoint());
		if (!waypoint || waypoint->pos == offset) {
			return;
		}

		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);
		if (!waypoint_brush->canDraw(&map, offset) && !map.getTile(offset)) {
			Tile* new_tile = map.allocator(map.createTileL(offset));
			action->addChange(newd Change(new_tile));
		}
		action->addChange(Change::Create(waypoint, offset));
		batch->addAndCommitAction(action);
		addBatch(batch, 2);
	} else if (brush->isWall()) {
		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);
		// This will only occur with a size 0, when clicking on a tile (not drawing)
		Tile* tile = map.getTile(offset);
		Tile* new_tile = nullptr;
		if (tile) {
			new_tile = tile->deepCopy(map);
		} else {
			new_tile = map.allocator(map.createTileL(offset));
		}

		if (dodraw) {
			bool b = true;
			brush->asWall()->draw(&map, new_tile, &b);
		} else {
			brush->asWall()->undraw(&map, new_tile);
		}
		action->addChange(newd Change(new_tile));
		batch->addAndCommitAction(action);
		addBatch(batch, 2);
	} else if (brush->isSpawn() || brush->isCreature()) {
		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);

		Tile* tile = map.getTile(offset);
		Tile* new_tile = nullptr;
		if (tile) {
			new_tile = tile->deepCopy(map);
		} else {
			new_tile = map.allocator(map.createTileL(offset));
		}
		int param;
		if (!brush->isCreature()) {
			param = g_gui.GetBrushSize();
		}
		if (dodraw) {
			if (brush->isSpawn() && brush->asSpawn()->hasCreatures()) {
				SpawnBrush* spawnBrush = brush->asSpawn();
				spawnBrush->draw(&map, new_tile, &param);

				const auto placements = spawnBrush->getCreaturePlacements(&map, offset, param);
				for (const SpawnBrush::CreaturePlacement& placement : placements) {
					if (placement.position == offset) {
						spawnBrush->drawCreature(new_tile, placement.brush, placement.weight);
						continue;
					}

					Tile* creature_tile = map.getTile(placement.position);
					if (!creature_tile) {
						continue;
					}

					Tile* new_creature_tile = creature_tile->deepCopy(map);
					spawnBrush->drawCreature(new_creature_tile, placement.brush, placement.weight);
					action->addChange(newd Change(new_creature_tile));
				}
			} else {
				brush->draw(&map, new_tile, &param);
			}
		} else {
			brush->undraw(&map, new_tile);
		}
		action->addChange(newd Change(new_tile));
		batch->addAndCommitAction(action);
		addBatch(batch, 2);
	}
}

void Editor::drawInternal(const PositionVector& tilestodraw, bool alt, bool dodraw) {
	Brush* brush = g_gui.GetCurrentBrush();
	if (!brush) {
		return;
	}

#ifdef __DEBUG__
	if (brush->isGround() || brush->isWall()) {
		// Wrong function, end call
		return;
	}
#endif

	Action* action = actionQueue->createAction(ACTION_DRAW);

	if (brush->isOptionalBorder()) {
		// We actually need to do borders, but on the same tiles we draw to
		for (auto it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
			TileLocation* location = map.createTileL(*it);
			Tile* tile = location->get();
			if (tile) {
				if (dodraw) {
					Tile* new_tile = tile->deepCopy(map);
					brush->draw(&map, new_tile);
					new_tile->borderize(&map);
					action->addChange(newd Change(new_tile));
				} else if (!dodraw && tile->hasOptionalBorder()) {
					Tile* new_tile = tile->deepCopy(map);
					brush->undraw(&map, new_tile);
					new_tile->borderize(&map);
					action->addChange(newd Change(new_tile));
				}
			} else if (dodraw) {
				Tile* new_tile = map.allocator(location);
				brush->draw(&map, new_tile);
				new_tile->borderize(&map);
				if (new_tile->size() == 0) {
					delete new_tile;
					continue;
				}
				action->addChange(newd Change(new_tile));
			}
		}
	} else {

		for (auto it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
			TileLocation* location = map.createTileL(*it);
			Tile* tile = location->get();
			if (tile) {
				if (ShouldSkipZoneChange(brush, tile, dodraw)) {
					continue;
				}

				Tile* new_tile = tile->deepCopy(map);
				if (dodraw) {
					brush->draw(&map, new_tile, &alt);
				} else {
					brush->undraw(&map, new_tile);
				}
				action->addChange(newd Change(new_tile));
			} else if (dodraw && !brush->isZone()) {
				Tile* new_tile = map.allocator(location);
				brush->draw(&map, new_tile, &alt);
				action->addChange(newd Change(new_tile));
			}
		}
	}
	addAction(action, 2);
}

void Editor::drawInternal(const PositionVector& tilestodraw, PositionVector& tilestoborder, bool alt, bool dodraw) {
	Brush* brush = g_gui.GetCurrentBrush();
	if (!brush) {
		return;
	}

	if (brush->isGround() || brush->isEraser()) {
		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);

		for (auto it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
			TileLocation* location = map.createTileL(*it);
			Tile* tile = location->get();
			if (tile) {
				Tile* new_tile = tile->deepCopy(map);
				if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
					new_tile->cleanBorders();
				}
				if (dodraw) {
					if (brush->isGround() && alt) {
						std::pair<bool, GroundBrush*> param;
						if (replace_brush) {
							param.first = false;
							param.second = replace_brush;
						} else {
							param.first = true;
							param.second = nullptr;
						}
						g_gui.GetCurrentBrush()->draw(&map, new_tile, &param);
					} else {
						g_gui.GetCurrentBrush()->draw(&map, new_tile, nullptr);
					}
				} else {
					g_gui.GetCurrentBrush()->undraw(&map, new_tile);
					tilestoborder.push_back(*it);
				}
				action->addChange(newd Change(new_tile));
			} else if (dodraw) {
				Tile* new_tile = map.allocator(location);
				if (brush->isGround() && alt) {
					std::pair<bool, GroundBrush*> param;
					if (replace_brush) {
						param.first = false;
						param.second = replace_brush;
					} else {
						param.first = true;
						param.second = nullptr;
					}
					g_gui.GetCurrentBrush()->draw(&map, new_tile, &param);
				} else {
					g_gui.GetCurrentBrush()->draw(&map, new_tile, nullptr);
				}
				action->addChange(newd Change(new_tile));
			}
		}

		// Commit changes to map
		batch->addAndCommitAction(action);

		if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
			// Do borders!
			action = actionQueue->createAction(batch);
			for (PositionVector::const_iterator it = tilestoborder.begin(); it != tilestoborder.end(); ++it) {
				TileLocation* location = map.createTileL(*it);
				Tile* tile = location->get();
				if (tile) {
					Tile* new_tile = tile->deepCopy(map);
					if (brush->isEraser()) {
						new_tile->wallize(&map);
						new_tile->tableize(&map);
						new_tile->carpetize(&map);
					}
					new_tile->borderize(&map);
					action->addChange(newd Change(new_tile));
				} else {
					Tile* new_tile = map.allocator(location);
					if (brush->isEraser()) {
						// There are no carpets/tables/walls on empty tiles...
						// new_tile->wallize(map);
						// new_tile->tableize(map);
						// new_tile->carpetize(map);
					}
					new_tile->borderize(&map);
					if (new_tile->size() > 0) {
						action->addChange(newd Change(new_tile));
					} else {
						delete new_tile;
					}
				}
			}
			batch->addAndCommitAction(action);
		}

		addBatch(batch, 2);
	} else if (brush->isTable() || brush->isCarpet()) {
		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);

		for (auto it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
			TileLocation* location = map.createTileL(*it);
			Tile* tile = location->get();
			if (tile) {
				Tile* new_tile = tile->deepCopy(map);
				if (dodraw) {
					g_gui.GetCurrentBrush()->draw(&map, new_tile, nullptr);
				} else {
					g_gui.GetCurrentBrush()->undraw(&map, new_tile);
				}
				action->addChange(newd Change(new_tile));
			} else if (dodraw) {
				Tile* new_tile = map.allocator(location);
				g_gui.GetCurrentBrush()->draw(&map, new_tile, nullptr);
				action->addChange(newd Change(new_tile));
			}
		}

		// Commit changes to map
		batch->addAndCommitAction(action);

		// Do borders!
		action = actionQueue->createAction(batch);
		for (PositionVector::const_iterator it = tilestoborder.begin(); it != tilestoborder.end(); ++it) {
			Tile* tile = map.getTile(*it);
			if (brush->isTable()) {
				if (tile && tile->hasTable()) {
					Tile* new_tile = tile->deepCopy(map);
					new_tile->tableize(&map);
					action->addChange(newd Change(new_tile));
				}
			} else if (brush->isCarpet()) {
				if (tile && tile->hasCarpet()) {
					Tile* new_tile = tile->deepCopy(map);
					new_tile->carpetize(&map);
					action->addChange(newd Change(new_tile));
				}
			}
		}
		batch->addAndCommitAction(action);

		addBatch(batch, 2);
	} else if (brush->isWall()) {
		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);

		if (alt && dodraw) {
			// This is exempt from USE_AUTOMAGIC
			g_gui.doodad_buffer_map->clear();
			BaseMap* draw_map = g_gui.doodad_buffer_map.get();

			for (auto it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
				TileLocation* location = map.createTileL(*it);
				Tile* tile = location->get();
				if (tile) {
					Tile* new_tile = tile->deepCopy(map);
					new_tile->cleanWalls(brush->isWall());
					g_gui.GetCurrentBrush()->draw(draw_map, new_tile);
					draw_map->setTile(*it, new_tile, true);
				} else if (dodraw) {
					Tile* new_tile = map.allocator(location);
					g_gui.GetCurrentBrush()->draw(draw_map, new_tile);
					draw_map->setTile(*it, new_tile, true);
				}
			}
			for (auto it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
				// Get the correct tiles from the draw map instead of the editor map
				Tile* tile = draw_map->getTile(*it);
				if (tile) {
					tile->wallize(draw_map);
					action->addChange(newd Change(tile));
				}
			}
			draw_map->clear(false);
			// Commit
			batch->addAndCommitAction(action);
		} else {
			for (auto it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
				TileLocation* location = map.createTileL(*it);
				Tile* tile = location->get();
				if (tile) {
					Tile* new_tile = tile->deepCopy(map);
					// Wall cleaning is exempt from automagic
					new_tile->cleanWalls(brush->isWall());
					if (dodraw) {
						g_gui.GetCurrentBrush()->draw(&map, new_tile);
					} else {
						g_gui.GetCurrentBrush()->undraw(&map, new_tile);
					}
					action->addChange(newd Change(new_tile));
				} else if (dodraw) {
					Tile* new_tile = map.allocator(location);
					g_gui.GetCurrentBrush()->draw(&map, new_tile);
					action->addChange(newd Change(new_tile));
				}
			}

			// Commit changes to map
			batch->addAndCommitAction(action);

			if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
				// Do borders!
				action = actionQueue->createAction(batch);
				for (PositionVector::const_iterator it = tilestoborder.begin(); it != tilestoborder.end(); ++it) {
					Tile* tile = map.getTile(*it);
					if (tile) {
						Tile* new_tile = tile->deepCopy(map);
						new_tile->wallize(&map);
						action->addChange(newd Change(new_tile));
					}
				}
				batch->addAndCommitAction(action);
			}
		}

		actionQueue->addBatch(batch, 2);
	} else if (brush->isDoor()) {
		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);
		DoorBrush* door_brush = brush->asDoor();

		// Loop is kind of redundant since there will only ever be one index.
		for (auto it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
			TileLocation* location = map.createTileL(*it);
			Tile* tile = location->get();
			if (tile) {
				Tile* new_tile = tile->deepCopy(map);
				// Wall cleaning is exempt from automagic
				if (brush->isWall()) {
					new_tile->cleanWalls(brush->asWall());
				}
				if (dodraw) {
					door_brush->draw(&map, new_tile, &alt);
				} else {
					door_brush->undraw(&map, new_tile);
				}
				action->addChange(newd Change(new_tile));
			} else if (dodraw) {
				Tile* new_tile = map.allocator(location);
				door_brush->draw(&map, new_tile, &alt);
				action->addChange(newd Change(new_tile));
			}
		}

		// Commit changes to map
		batch->addAndCommitAction(action);

		if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
			// Do borders!
			action = actionQueue->createAction(batch);
			for (PositionVector::const_iterator it = tilestoborder.begin(); it != tilestoborder.end(); ++it) {
				Tile* tile = map.getTile(*it);
				if (tile) {
					Tile* new_tile = tile->deepCopy(map);
					new_tile->wallize(&map);
					action->addChange(newd Change(new_tile));
				}
			}
			batch->addAndCommitAction(action);
		}

		addBatch(batch, 2);
	} else {
		Action* action = actionQueue->createAction(ACTION_DRAW);
		for (auto it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
			TileLocation* location = map.createTileL(*it);
			Tile* tile = location->get();
			if (tile) {
				if (ShouldSkipZoneChange(brush, tile, dodraw)) {
					continue;
				}

				Tile* new_tile = tile->deepCopy(map);
				if (dodraw) {
					g_gui.GetCurrentBrush()->draw(&map, new_tile);
				} else {
					g_gui.GetCurrentBrush()->undraw(&map, new_tile);
				}
				action->addChange(newd Change(new_tile));
			} else if (dodraw && !brush->isZone()) {
				Tile* new_tile = map.allocator(location);
				g_gui.GetCurrentBrush()->draw(&map, new_tile);
				action->addChange(newd Change(new_tile));
			}
		}
		addAction(action, 2);
	}
}
