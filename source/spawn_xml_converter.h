#ifndef RME_SPAWN_XML_CONVERTER_H_
#define RME_SPAWN_XML_CONVERTER_H_

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

enum class SpawnXmlLayout {
	Unknown,
	TfsCombined,
	CanaryMonsters,
	CanaryNpcs,
};

enum class SpawnXmlCreatureKind {
	Monster,
	Npc,
};

struct SpawnXmlAttribute {
	std::string name;
	std::string value;

	bool operator==(const SpawnXmlAttribute&) const = default;
};

struct SpawnXmlCreature {
	SpawnXmlCreatureKind kind = SpawnXmlCreatureKind::Monster;
	std::vector<SpawnXmlAttribute> attributes;

	bool operator==(const SpawnXmlCreature&) const = default;
};

struct SpawnXmlArea {
	std::vector<SpawnXmlAttribute> attributes;
	std::vector<SpawnXmlCreature> creatures;

	bool operator==(const SpawnXmlArea&) const = default;
};

struct SpawnXmlDocument {
	SpawnXmlLayout layout = SpawnXmlLayout::Unknown;
	std::vector<SpawnXmlArea> areas;

	bool operator==(const SpawnXmlDocument&) const = default;
};

struct SpawnXmlInspection {
	SpawnXmlLayout layout = SpawnXmlLayout::Unknown;
	std::string rootName;
	std::string error;
};

struct SpawnXmlSuggestedPaths {
	std::filesystem::path monsterFile;
	std::filesystem::path npcFile;
	std::filesystem::path tfsFile;
};

struct SpawnXmlConversionReport {
	std::string direction;
	std::vector<std::filesystem::path> sourceFiles;
	std::vector<std::filesystem::path> outputFiles;
	size_t monsterAreas = 0;
	size_t npcAreas = 0;
	size_t monsters = 0;
	size_t npcs = 0;
	size_t additionalAttributes = 0;
	bool validationPassed = false;
	std::vector<std::string> warnings;
	std::chrono::milliseconds duration{0};
};

struct SpawnXmlConversionResult {
	bool success = false;
	bool canceled = false;
	bool overwriteRequired = false;
	std::string error;
	SpawnXmlConversionReport report;
};

using SpawnXmlProgressCallback = std::function<bool(int progress, const std::string& phase)>;

class SpawnXmlConverter {
public:
	static SpawnXmlInspection Inspect(const std::filesystem::path& file);
	static SpawnXmlSuggestedPaths SuggestFromCanaryFile(const std::filesystem::path& selectedFile, SpawnXmlLayout selectedLayout);
	static SpawnXmlSuggestedPaths SuggestFromTfsFile(const std::filesystem::path& selectedFile, const std::filesystem::path& outputDirectory, bool pluralMonsterFilename);

	static SpawnXmlConversionResult ConvertCanaryToTfs(
		const std::filesystem::path& monsterFile,
		const std::filesystem::path& npcFile,
		const std::filesystem::path& outputFile,
		bool allowOverwrite,
		const SpawnXmlProgressCallback& progress = {}
	);

	static SpawnXmlConversionResult ConvertTfsToCanary(
		const std::filesystem::path& sourceFile,
		const std::filesystem::path& monsterOutputFile,
		const std::filesystem::path& npcOutputFile,
		bool allowOverwrite,
		const SpawnXmlProgressCallback& progress = {}
	);
};

#endif // RME_SPAWN_XML_CONVERTER_H_
