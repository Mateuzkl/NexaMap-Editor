#ifndef RME_SPAWN_FORMAT_H_
#define RME_SPAWN_FORMAT_H_

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

class Map;

enum class SpawnFormat {
	Unknown = 0,
	Tfs = 1,
	CanaryCrystal = 2,
};

enum class SpawnAreaKind {
	Mixed,
	Monsters,
	Npcs,
};

enum class SpawnAlternativeKind {
	None,
	CanaryWeight,
	TfsChance,
};

using SpawnAttributeMap = std::map<std::string, std::string>;

struct SpawnVariantData {
	std::string name;
	bool isNpc = false;
	uint32_t weight = 1;
	bool hasWeight = false;
	SpawnAttributeMap attributes;
};

struct SpawnEntryData {
	std::string name;
	bool isNpc = false;
	int x = 0;
	int y = 0;
	int z = 0;
	int spawnTime = 60;
	int direction = 0;
	bool hasDirection = false;
	uint32_t weight = 1;
	bool hasWeight = false;
	SpawnAttributeMap attributes;
	SpawnAlternativeKind alternativeKind = SpawnAlternativeKind::None;
	std::vector<SpawnVariantData> alternatives;
};

struct SpawnAreaData {
	int centerX = 0;
	int centerY = 0;
	int centerZ = 0;
	int radius = 1;
	SpawnAreaKind kind = SpawnAreaKind::Mixed;
	SpawnAttributeMap attributes;
	std::vector<SpawnEntryData> entries;
};

struct SpawnDocument {
	SpawnFormat format = SpawnFormat::Unknown;
	std::filesystem::path primaryFile;
	std::filesystem::path npcFile;
	std::vector<SpawnAreaData> areas;
	std::vector<std::string> warnings;

	size_t monsterCount() const;
	size_t npcCount() const;
	size_t entryCount() const;
};

struct SpawnDetectionResult {
	SpawnFormat format = SpawnFormat::Unknown;
	std::filesystem::path primaryFile;
	std::filesystem::path npcFile;
	std::string error;
};

struct SpawnWriteResult {
	bool success = false;
	std::vector<std::filesystem::path> files;
	std::vector<std::string> warnings;
	std::string error;
};

class SpawnFormatIO {
public:
	static const char* GetFormatName(SpawnFormat format);
	static SpawnDetectionResult Detect(
		const std::filesystem::path& directory,
		const std::string& embeddedPrimaryFile,
		const std::string& embeddedNpcFile,
		const std::string& mapName
	);
	static bool Load(const SpawnDetectionResult& detection, SpawnDocument& document, std::string& error);
	static bool LoadTfs(const std::filesystem::path& file, SpawnDocument& document, std::string& error);
	static bool LoadCanaryCrystal(const std::filesystem::path& monsterFile, const std::filesystem::path& npcFile, SpawnDocument& document, std::string& error);
	static SpawnWriteResult SaveTfs(const SpawnDocument& document, const std::filesystem::path& file);
	static SpawnWriteResult SaveCanaryCrystal(const SpawnDocument& document, const std::filesystem::path& monsterFile, const std::filesystem::path& npcFile);
	static bool SemanticallyEqual(const SpawnDocument& lhs, const SpawnDocument& rhs, bool compareWeights, std::string& difference);
};

class SpawnMapAdapter {
public:
	static bool Apply(Map& map, const SpawnDocument& document, std::vector<std::string>& warnings);
	static SpawnDocument Capture(Map& map);
};

#endif
