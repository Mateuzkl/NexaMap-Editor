//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_MAP_ITEM_ID_CONVERTER_H_
#define RME_MAP_ITEM_ID_CONVERTER_H_

#include "client_version.h"
#include "item_id_mapping.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class MapItemIdProcessingMode : uint8_t {
	Automatic,
	Custom,
};

struct MapItemIdPerformanceOptions {
	MapItemIdProcessingMode mode = MapItemIdProcessingMode::Automatic;
	uint32_t cpuThreads = 4;
	uint64_t memoryLimitBytes = 8ull * 1024ull * 1024ull * 1024ull;
};

struct MapItemIdPerformanceLimits {
	uint32_t hardwareThreads = 1;
	uint64_t safeMemoryLimitBytes = 0;
	uint64_t maximumCustomMemoryLimitBytes = 0;
};

struct MapItemIdConversionOptions {
	std::filesystem::path source;
	std::filesystem::path destination;
	ItemIdMapping::Direction direction = ItemIdMapping::Direction::ServerToClient;
	MapVersion targetVersion;
	MapItemIdPerformanceOptions performance;
};

struct MapItemIdConversionIssue {
	uint16_t sourceId = 0;
	uint16_t preferredId = 0;
	uint64_t occurrences = 0;
	bool found = false;
	bool ambiguous = false;
	std::vector<uint16_t> candidates;
};

struct MapItemIdConversionReport {
	bool success = false;
	bool cancelled = false;
	bool outputValidated = false;
	bool streamed = false;
	uint64_t tileCount = 0;
	uint64_t totalItems = 0;
	uint64_t mappedItems = 0;
	uint64_t changedItems = 0;
	uint64_t missingItems = 0;
	uint64_t ambiguousItems = 0;
	uint64_t memoryLimitBytes = 0;
	uint32_t threadsUsed = 1;
	double loadingSeconds = 0.0;
	double conversionSeconds = 0.0;
	double savingSeconds = 0.0;
	double totalSeconds = 0.0;
	std::string error;
	std::vector<std::string> warnings;
	std::vector<MapItemIdConversionIssue> issues;

	[[nodiscard]] std::string format(const MapItemIdConversionOptions& options) const;
};

[[nodiscard]] MapItemIdPerformanceLimits GetMapItemIdConverterPerformanceLimits();
[[nodiscard]] MapItemIdConversionReport ConvertMapItemIds(const MapItemIdConversionOptions& options);

#endif // RME_MAP_ITEM_ID_CONVERTER_H_
