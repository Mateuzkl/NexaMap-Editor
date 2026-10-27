#ifndef NEXAMAP_MAP_MIGRATION_H_
#define NEXAMAP_MAP_MIGRATION_H_

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// This module owns its catalogs and performs no wx/GL operations. It never
// reads or swaps g_items, g_gui.gfx, brushes or the active resource session.
namespace MapMigration {

	using Path = std::filesystem::path;
	enum class DatDialect { Automatic,
							Classic74,
							Classic755,
							Classic78,
							Classic86,
							Modern1010 };
	enum Role : uint64_t {
		Ground = 1ull << 0,
		Container = 1ull << 1,
		Teleport = 1ull << 2,
		Door = 1ull << 3,
		Depot = 1ull << 4,
		Fluid = 1ull << 5,
		Splash = 1ull << 6,
		Bed = 1ull << 7,
		Podium = 1ull << 8,
		Field = 1ull << 9,
		Key = 1ull << 10,
		Mailbox = 1ull << 11,
		Wall = 1ull << 12,
		Border = 1ull << 13,
		Window = 1ull << 14,
		Decoration = 1ull << 15,
	};
	inline constexpr uint64_t FunctionalRoles = Ground | Container | Teleport | Door | Depot | Fluid | Splash | Bed | Podium | Field | Key | Mailbox;

	struct Control {
		const std::atomic<bool>* cancelled = nullptr;
		std::function<void(int, const std::string&)> progress;
		uint64_t memoryLimitBytes = 512ull * 1024 * 1024;
		void check() const;
		void update(int percent, const std::string& message) const;
	};

	struct ResourceSet {
		Path otb, xml, dat, spr, otfi;
		DatDialect dialect = DatDialect::Automatic;
	};
	struct FileDigest {
		Path path;
		uint64_t size = 0;
		std::string sha256;
	};
	struct Layout {
		uint8_t width = 1, height = 1, layers = 1;
		uint8_t patternX = 1, patternY = 1, patternZ = 1, frames = 1;
		uint16_t offsetX = 0, offsetY = 0, elevation = 0, groundSpeed = 0;
		uint64_t datFlags = 0;
		std::vector<uint32_t> spriteIds;
		std::vector<uint32_t> durations;
		bool operator==(const Layout&) const = default;
	};
	struct Visual {
		Layout layout;
		bool valid = false;
		bool visiblePixels = false;
		bool allVariantsVerified = false;
		uint16_t previewWidth = 0, previewHeight = 0;
		std::vector<uint8_t> preview;
		std::string completePixels, firstPixels;
		std::array<double, 32> histogram {};
		uint64_t shape = 0;
		// One bounded 8x8 RGBA descriptor per composed frame/pattern. Unlike
		// the display preview, these cover every orientation and animation.
		std::vector<std::array<uint8_t, 256>> variantFeatures;
		std::vector<std::string> variantPixels;
		uint64_t luminanceHash = 0;
		std::string error;
	};
	struct CatalogItem {
		uint16_t serverId = 0, clientId = 0;
		uint8_t group = 0, topOrder = 0;
		uint32_t flags = 0;
		uint64_t roles = 0, knownRoles = 0;
		std::string name, type, otbRecordDigest, xmlProperties;
		std::shared_ptr<const Visual> visual;
	};
	struct Catalog {
		ResourceSet resources;
		uint32_t majorVersion = 0, minorVersion = 0;
		uint32_t datSignature = 0, sprSignature = 0;
		std::string fingerprint;
		std::map<uint16_t, CatalogItem> items;
		std::vector<FileDigest> files;
		std::vector<std::string> warnings;
		uint64_t duplicateClientIds = 0;
	};
	struct Position {
		uint16_t x = 0, y = 0;
		uint8_t z = 0;
		bool operator==(const Position&) const = default;
	};
	struct Usage {
		uint64_t occurrences = 0, groundOccurrences = 0, nestedOccurrences = 0;
		uint64_t tiles = 0;
		uint64_t requiredRoles = 0;
		std::vector<Position> positions;
		std::vector<uint32_t> stackIndices;
		// N, NE, E, SE, S, SW, W, NW: source ground IDs around samples.
		std::vector<std::array<uint16_t, 8>> adjacentGrounds;
		std::vector<uint64_t> patternOccurrences;
	};
	struct Sidecar {
		uint8_t attribute = 0;
		std::string reference;
		FileDigest file;
	};
	struct Inventory {
		FileDigest mapFile;
		uint32_t version = 0, itemMajorVersion = 0, itemMinorVersion = 0;
		uint16_t width = 0, height = 0;
		uint64_t tiles = 0, items = 0, grounds = 0, containers = 0, nestedItems = 0;
		uint64_t tilesWithoutGround = 0, opaqueItemAttributes = 0;
		std::vector<Position> groundlessPositions;
		std::array<uint64_t, 16> floors {};
		std::map<uint16_t, Usage> usedIds;
		std::vector<Sidecar> sidecars;
		std::string structureDigest;
	};
	enum class Decision { Identity,
						  Exact,
						  HighConfidence,
						  Suggested,
						  Ambiguous,
						  Missing,
						  Incompatible,
						  Unverifiable,
						  Reviewed,
						  Approximate,
						  ContextualExact,
						  VisualDraft };
	enum class AutomaticPolicy { Safe,
								 CompatibleApproximation,
								 VisualDraft };
	struct Candidate {
		uint16_t serverId = 0, clientId = 0;
		double score = 0, visualSimilarity = 0;
		bool completeExact = false;
		std::vector<std::string> evidence;
	};
	struct MappingRow {
		uint16_t sourceId = 0, sourceClientId = 0, targetId = 0, targetClientId = 0;
		Decision decision = Decision::Missing;
		Usage usage;
		uint64_t roles = 0;
		double confidence = 0;
		std::string reason;
		std::vector<Candidate> candidates;
		uint8_t patternX = 1, patternY = 1, patternZ = 1;
		std::vector<uint16_t> patternTargets;
		[[nodiscard]] bool resolved() const noexcept;
	};
	struct Options {
		Path sourceMap, outputMap;
		ResourceSet source, target;
	};
	struct Plan {
		Options options;
		std::shared_ptr<const Catalog> source, target;
		Inventory inventory;
		std::vector<MappingRow> rows;
		std::vector<std::string> warnings;
		std::string error;
		bool cancelled = false;
		bool visualDraft = false;
		[[nodiscard]] uint64_t unresolvedIds() const noexcept;
		[[nodiscard]] uint64_t unresolvedOccurrences() const noexcept;
		[[nodiscard]] bool ready() const noexcept;
	};
	struct Result {
		bool success = false, cancelled = false, validated = false;
		Path outputMap, jsonReport, csvReport;
		uint64_t changedItems = 0;
		uint64_t outputTilesWithoutGround = 0;
		std::string error;
	};

	[[nodiscard]] FileDigest FingerprintFile(const Path&, const Control& = {});
	[[nodiscard]] std::string Sha256(const std::vector<uint8_t>&);
	[[nodiscard]] Catalog LoadCatalog(const ResourceSet&, const Control& = {});
	[[nodiscard]] Inventory InspectMap(const Path&, const Catalog&, const Control& = {}, bool collectContext = true);
	[[nodiscard]] bool Compatible(const CatalogItem&, const CatalogItem&, uint64_t requiredRoles = 0);
	[[nodiscard]] bool CompatibleForVisualDraft(const CatalogItem&, const CatalogItem&, uint64_t requiredRoles = 0);
	[[nodiscard]] uint16_t TargetIdAt(const MappingRow&, Position);
	[[nodiscard]] std::vector<MappingRow> Match(const Inventory&, const Catalog&, const Catalog&, const Control& = {});
	[[nodiscard]] Plan Analyze(const Options&, const Control& = {});
	[[nodiscard]] bool Resolve(Plan&, uint16_t sourceId, uint16_t targetId, std::string& error);
	// Reassesses every source ID in one pass, retaining reviewed decisions.
	// Approximation is an explicit policy and never resolves tied/invalid items.
	void ResolveAutomatically(Plan&, AutomaticPolicy, const Control& = {});
	[[nodiscard]] Result Convert(const Plan&, const Control& = {});
	void WriteReports(const Plan&, const Path& jsonPath, const Path& csvPath, const Result* = nullptr);
	void SaveReviewedMappings(const Plan&, const Path&);
	void LoadReviewedMappings(Plan&, const Path&);
	[[nodiscard]] std::string DecisionName(Decision);
	[[nodiscard]] std::string RoleName(uint64_t);

} // namespace MapMigration
#endif
