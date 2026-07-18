//////////////////////////////////////////////////////////////////////
// Native procedural generation model. This layer intentionally has no
// wxWidgets, Map, Tile, Item or brush dependencies and is worker-safe.
//////////////////////////////////////////////////////////////////////

#ifndef RME_PROCEDURAL_MAP_GENERATOR_H_
#define RME_PROCEDURAL_MAP_GENERATOR_H_

#include <cstdint>
#include <functional>
#include <stop_token>
#include <string>
#include <vector>

namespace ProceduralMap {

// Bounds the pure plan before any large cell buffers are allocated. This keeps
// preview generation practical on both 32-bit and 64-bit editor builds.
inline constexpr size_t MaximumPlanCells = sizeof(void*) >= 8 ? 4'000'000 : 1'000'000;

enum class Preset : uint8_t {
	NaturalLandscape,
	Forest,
	Mountain,
	Cave,
	Dungeon,
	HuntingArea,
	City,
	Village,
	Desert,
	SnowArea,
	Swamp,
	Island,
	Coastline,
	River,
	Ruins,
	Custom,
	Count,
};

enum class MaterialRole : uint8_t {
	None,
	Base,
	Secondary,
	Water,
	Path,
	Rock,
	Accent,
	Count,
};

enum CellFeature : uint16_t {
	FeatureNone = 0,
	FeatureWall = 1 << 0,
	FeatureDecoration = 1 << 1,
	FeatureEntrance = 1 << 2,
	FeatureExit = 1 << 3,
	FeatureTransition = 1 << 4,
	FeaturePointOfInterest = 1 << 5,
};

enum class ReplacementMode : uint8_t {
	EmptyTilesOnly,
	ReplaceGroundOnly,
	ReplaceTerrainAndBorders,
	ReplaceEverything,
	BlendWithExisting,
};

enum class EdgeBlending : uint8_t {
	None,
	Soft,
	Strong,
};

enum class GenerationStage : uint8_t {
	ReadingSelection,
	BuildingMasks,
	GeneratingTerrain,
	CreatingStructures,
	PlanningDecorations,
	ValidatingConnectivity,
	Completed,
};

enum class IssueSeverity : uint8_t {
	Information,
	Warning,
	Error,
};

struct GenerationParameters {
	double noiseScale = 48.0;
	int octaves = 4;
	double persistence = 0.52;
	double lacunarity = 2.0;
	double waterLevel = -0.18;
	double irregularity = 0.55;
	int smoothingPasses = 3;
	int decorationDensity = 18; // 0..100
	int pathWidth = 2;
	int roomCount = 12;
	int roomMinSize = 5;
	int roomMaxSize = 13;
	int loops = 2;
	int secondaryRoadSpacing = 10;
	int edgeMargin = 2;
	bool autoFixConnectivity = true;
};

struct GenerationRequest {
	int minX = 0;
	int minY = 0;
	int minZ = 7;
	int maxX = 0;
	int maxY = 0;
	int maxZ = 7;
	std::vector<uint8_t> allowedCells;
	// Existing gameplay-critical content is captured on the UI thread before
	// generation. The pure worker treats these positions as holes in the mask,
	// so paths and structures are planned around content that Apply must keep.
	std::vector<uint8_t> protectedCells;
	uint64_t seed = 1;
	Preset preset = Preset::NaturalLandscape;
	ReplacementMode replacementMode = ReplacementMode::ReplaceTerrainAndBorders;
	EdgeBlending edgeBlending = EdgeBlending::Soft;
	GenerationParameters parameters;

	int width() const noexcept;
	int height() const noexcept;
	int floorCount() const noexcept;
	size_t cellCount() const noexcept;
	bool isSelected(int x, int y, int z) const noexcept;
	bool isProtected(int x, int y, int z) const noexcept;
	bool isAllowed(int x, int y, int z) const noexcept;
};

struct GeneratedCell {
	int x = 0;
	int y = 0;
	int z = 7;
	MaterialRole material = MaterialRole::None;
	uint16_t features = FeatureNone;
	uint8_t blendWeight = 255;
	bool walkable = true;
};

struct ValidationIssue {
	IssueSeverity severity = IssueSeverity::Information;
	std::string message;
	int x = -1;
	int y = -1;
	int z = -1;
};

struct GenerationStatistics {
	size_t allowedTiles = 0;
	size_t protectedTiles = 0;
	size_t plannedTiles = 0;
	size_t walkableTiles = 0;
	size_t wallTiles = 0;
	size_t decorationTiles = 0;
	size_t pointOfInterestTiles = 0;
	size_t roomsPlaced = 0;
	size_t connectedRegions = 0;
	size_t repairedConnections = 0;
	size_t approximateBytes = 0;
};

struct GenerationPlan {
	GenerationRequest request;
	std::vector<GeneratedCell> cells;
	std::vector<ValidationIssue> issues;
	GenerationStatistics statistics;
	uint64_t stableHash = 0;

	bool hasErrors() const noexcept;
	const GeneratedCell* find(int x, int y, int z) const noexcept;
};

struct BriefInterpretation {
	bool hasSupportedTerms = false;
	Preset preset = Preset::NaturalLandscape;
	std::vector<std::string> recognized;
	std::vector<std::string> unknown;
	GenerationParameters parameters;
};

using ProgressCallback = std::function<void(int, GenerationStage)>;

const char* PresetName(Preset preset) noexcept;
const char* MaterialRoleName(MaterialRole role) noexcept;
const char* GenerationStageName(GenerationStage stage) noexcept;

bool ValidateRequest(const GenerationRequest& request, std::vector<ValidationIssue>& issues);
GenerationPlan Generate(const GenerationRequest& request, std::stop_token stopToken = {}, const ProgressCallback& progress = {});
uint64_t StablePlanHash(const GenerationPlan& plan) noexcept;
BriefInterpretation InterpretBrief(const std::string& text);
void MergeLockedParts(GenerationPlan& generated, const GenerationPlan& previous, bool lockTerrain, bool lockStructures, bool lockDecorations);

} // namespace ProceduralMap

#endif
