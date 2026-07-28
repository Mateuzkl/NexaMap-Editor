#include "map_format.h"

MapFormatDetection MapFormatDetector::Detect(const MapFormatEvidence& evidence) {
	MapFormatDetection result;
	if (evidence.spawnConflict) {
		result.ambiguous = true;
		result.conflict = true;
		result.reason = "Both TFS and Canary/Crystal spawn layouts are present.";
		return result;
	}

	if (evidence.otbmVersion >= MAP_OTBM_5) {
		result.format = MapStorageFormat::CanaryCrystal;
		result.reason = "OTBM 5/6 is a Canary/Crystal map format.";
		return result;
	}
	if (evidence.spawnFormat == SpawnFormat::CanaryCrystal) {
		result.format = MapStorageFormat::CanaryCrystal;
		result.reason = "The OTBM references separate monster and NPC spawn files.";
		return result;
	}
	if (evidence.hasZoneFileReference) {
		result.format = MapStorageFormat::CanaryCrystal;
		result.reason = "The OTBM contains the Canary/Crystal zone file reference.";
		return result;
	}
	if (evidence.spawnFormat == SpawnFormat::Tfs && evidence.itemMinorMatchesKnownClient) {
		result.format = MapStorageFormat::Tfs;
		result.reason = "The OTBM item version and combined spawn file match the TFS layout.";
		return result;
	}
	if (evidence.itemMinorMatchesKnownClient) {
		result.format = MapStorageFormat::Tfs;
		result.reason = "The OTBM item version matches a configured TFS client profile.";
		return result;
	}

	result.ambiguous = true;
	result.reason = "The OTBM version, item header and spawn references do not identify one format reliably.";
	return result;
}

const char* MapFormatDetector::GetFormatName(MapStorageFormat format) {
	switch (format) {
		case MapStorageFormat::Tfs:
			return "TFS";
		case MapStorageFormat::CanaryCrystal:
			return "Canary/Crystal";
		case MapStorageFormat::Unknown:
		default:
			return "Unknown";
	}
}
