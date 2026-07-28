#ifndef NEXAMAP_MAP_FORMAT_H_
#define NEXAMAP_MAP_FORMAT_H_

#include "client_version.h"
#include "spawn_format.h"

#include <string>

enum class MapStorageFormat {
	Unknown = 0,
	Tfs = 1,
	CanaryCrystal = 2,
};

enum class ItemIdSpace {
	Server = 0,
	Client = 1,
};

struct MapFormatEvidence {
	MapVersionID otbmVersion = MAP_OTBM_UNKNOWN;
	ClientVersionID itemMinorVersion = CLIENT_VERSION_NONE;
	bool itemMinorMatchesKnownClient = false;
	SpawnFormat spawnFormat = SpawnFormat::Unknown;
	bool spawnConflict = false;
	bool hasZoneFileReference = false;
};

struct MapFormatDetection {
	MapStorageFormat format = MapStorageFormat::Unknown;
	bool ambiguous = false;
	bool conflict = false;
	std::string reason;
};

class MapFormatDetector {
public:
	static MapFormatDetection Detect(const MapFormatEvidence& evidence);
	static const char* GetFormatName(MapStorageFormat format);
};

#endif
