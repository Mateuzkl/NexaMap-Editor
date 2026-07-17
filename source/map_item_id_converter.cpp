//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "map_item_id_converter.h"

#include "complexitem.h"
#include "depot_conversion_validation.h"
#include "file_transaction.h"
#include "gui.h"
#include "iomap_otbm.h"
#include "map.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>

#ifdef __WINDOWS__
	#include <windows.h>
	#include <psapi.h>
	#pragma comment(lib, "psapi.lib")
#else
	#include <unistd.h>
#endif

namespace {
	constexpr std::size_t ItemIdDomainSize = 1u << 16;
	constexpr uint64_t MiB = 1024ull * 1024ull;
	constexpr uint64_t GiB = 1024ull * MiB;

	wxString PathToWxString(const std::filesystem::path& path) {
#ifdef _WIN32
		return wxString(path.wstring());
#else
		return wxString::FromUTF8(path.string());
#endif
	}

	std::string PrintablePath(const std::filesystem::path& path) {
		return nstr(PathToWxString(path));
	}

	uint64_t QueryProcessMemoryBytes() {
#ifdef __WINDOWS__
		PROCESS_MEMORY_COUNTERS_EX counters {};
		if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
			return static_cast<uint64_t>(counters.WorkingSetSize);
		}
#elif defined(_SC_PAGESIZE)
		std::ifstream statm("/proc/self/statm");
		uint64_t totalPages = 0;
		uint64_t residentPages = 0;
		if (statm >> totalPages >> residentPages) {
			return residentPages * static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
		}
#endif
		return 0;
	}

	uint64_t QueryAvailableMemoryBytes() {
#ifdef __WINDOWS__
		MEMORYSTATUSEX status {};
		status.dwLength = sizeof(status);
		if (GlobalMemoryStatusEx(&status)) {
			return static_cast<uint64_t>(status.ullAvailPhys);
		}
#elif defined(_SC_AVPHYS_PAGES) && defined(_SC_PAGESIZE)
		const long availablePages = sysconf(_SC_AVPHYS_PAGES);
		const long pageSize = sysconf(_SC_PAGESIZE);
		if (availablePages > 0 && pageSize > 0) {
			return static_cast<uint64_t>(availablePages) * static_cast<uint64_t>(pageSize);
		}
#endif
		return 0;
	}

	uint64_t QueryTotalMemoryBytes() {
#ifdef __WINDOWS__
		MEMORYSTATUSEX status {};
		status.dwLength = sizeof(status);
		if (GlobalMemoryStatusEx(&status)) {
			return static_cast<uint64_t>(status.ullTotalPhys);
		}
#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
		const long totalPages = sysconf(_SC_PHYS_PAGES);
		const long pageSize = sysconf(_SC_PAGESIZE);
		if (totalPages > 0 && pageSize > 0) {
			return static_cast<uint64_t>(totalPages) * static_cast<uint64_t>(pageSize);
		}
#endif
		return 0;
	}

	struct ResolvedPerformance {
		uint32_t threads = 1;
		uint64_t memoryLimitBytes = GiB;
	};

	ResolvedPerformance ResolvePerformance(const MapItemIdConversionOptions& options, uint64_t sourceSize, std::string& error) {
		const MapItemIdPerformanceLimits limits = GetMapItemIdConverterPerformanceLimits();
		ResolvedPerformance resolved;
		if (options.performance.mode == MapItemIdProcessingMode::Custom) {
			if (options.performance.cpuThreads == 0 || options.performance.cpuThreads > limits.hardwareThreads) {
				error = "CPU thread count is outside the safe range for this system.";
				return {};
			}
			if (options.performance.memoryLimitBytes < GiB || (limits.maximumCustomMemoryLimitBytes != 0 && options.performance.memoryLimitBytes > limits.maximumCustomMemoryLimitBytes)) {
				error = "Memory limit is outside the safe range for this system.";
				return {};
			}
			resolved.threads = options.performance.cpuThreads;
			resolved.memoryLimitBytes = options.performance.memoryLimitBytes;
		} else {
			const uint64_t workingSet = QueryProcessMemoryBytes();
			const uint64_t desiredForFile = sourceSize > std::numeric_limits<uint64_t>::max() / 16 ? std::numeric_limits<uint64_t>::max() : sourceSize * 16;
			const uint64_t desired = std::clamp<uint64_t>(desiredForFile, GiB, 8 * GiB);
			const uint64_t minimumHeadroom = std::max<uint64_t>(256 * MiB, sourceSize > std::numeric_limits<uint64_t>::max() / 2 ? sourceSize : sourceSize * 2);
			const uint64_t workingFloor = workingSet > std::numeric_limits<uint64_t>::max() - minimumHeadroom ? std::numeric_limits<uint64_t>::max() : workingSet + minimumHeadroom;
			resolved.memoryLimitBytes = std::max(desired, workingFloor);
			if (limits.safeMemoryLimitBytes != 0) {
				resolved.memoryLimitBytes = std::min(resolved.memoryLimitBytes, limits.safeMemoryLimitBytes);
			}

			uint32_t usefulThreads = limits.hardwareThreads;
			if (sourceSize < 32 * MiB) {
				usefulThreads = std::min<uint32_t>(usefulThreads, 2);
			} else if (sourceSize < 128 * MiB) {
				usefulThreads = std::min<uint32_t>(usefulThreads, 4);
			} else if (usefulThreads > 1) {
				--usefulThreads;
			}
			resolved.threads = std::max<uint32_t>(1, usefulThreads);
		}

		const uint64_t perWorkerBudget = 2 * MiB;
		resolved.threads = std::min<uint32_t>(resolved.threads, static_cast<uint32_t>(std::max<uint64_t>(1, resolved.memoryLimitBytes / perWorkerBudget)));
		if (sourceSize > resolved.memoryLimitBytes) {
			error = "The selected memory limit is smaller than the source file.";
			return {};
		}
		const uint64_t workingSet = QueryProcessMemoryBytes();
		if (workingSet != 0 && workingSet > resolved.memoryLimitBytes) {
			error = "The RME process is already using more memory than the selected conversion limit.";
			return {};
		}
		return resolved;
	}

	bool CheckMemoryLimit(uint64_t limit, const char* phase, std::string& error, uint64_t pendingBytes = 0) {
		const uint64_t workingSet = QueryProcessMemoryBytes();
		if (workingSet <= limit && pendingBytes <= limit - workingSet) {
			return true;
		}
		const uint64_t projectedBytes = workingSet > std::numeric_limits<uint64_t>::max() - pendingBytes ? std::numeric_limits<uint64_t>::max() : workingSet + pendingBytes;
		std::ostringstream message;
		message << "Memory limit exceeded " << phase << ": " << std::fixed << std::setprecision(2)
				<< static_cast<double>(projectedBytes) / static_cast<double>(GiB) << " GB used or required, "
				<< static_cast<double>(limit) / static_cast<double>(GiB) << " GB allowed.";
		error = message.str();
		return false;
	}

	std::filesystem::path ParentDirectory(const std::filesystem::path& file) {
		std::filesystem::path directory = file.parent_path();
		if (directory.empty()) {
			directory = ".";
		}
		std::error_code error;
		const std::filesystem::path absolute = std::filesystem::absolute(directory, error);
		return (error ? directory : absolute).lexically_normal();
	}

	bool ReferencesSidecars(const Map& map) {
		return !map.getSpawnFilename().empty() || !map.getSpawnNpcFilename().empty() || !map.getHouseFilename().empty() || !map.getZoneFilename().empty();
	}

	struct WorkerMappingStats {
		explicit WorkerMappingStats(bool collectOccurrences) :
			occurrences(collectOccurrences ? ItemIdDomainSize : 0, 0) { }

		void observe(const ItemIdMapping::Result& result) {
			++totalItems;
			if (!occurrences.empty()) {
				++occurrences[result.original];
			}
			if (result.found) {
				++mappedItems;
				if (result.converted != result.original) {
					++changedItems;
				}
			} else {
				++missingItems;
			}
			if (result.ambiguous) {
				++ambiguousItems;
			}
		}

		uint64_t totalItems = 0;
		uint64_t mappedItems = 0;
		uint64_t changedItems = 0;
		uint64_t missingItems = 0;
		uint64_t ambiguousItems = 0;
		std::vector<uint64_t> occurrences;
	};

	class ReportBuilder {
	public:
		explicit ReportBuilder(MapItemIdConversionReport& report) :
			report(report),
			occurrences(ItemIdDomainSize, 0) { }

		void observe(const ItemIdMapping::Result& result) {
			++report.totalItems;
			++occurrences[result.original];
			if (result.found) {
				++report.mappedItems;
				if (result.converted != result.original) {
					++report.changedItems;
				}
			} else {
				++report.missingItems;
			}
			if (result.ambiguous) {
				++report.ambiguousItems;
			}
		}

		void finish(ItemIdMapping::Direction direction) {
			for (std::size_t id = 0; id < occurrences.size(); ++id) {
				if (occurrences[id] == 0) {
					continue;
				}
				const auto result = ItemIdMapping::convert(static_cast<uint16_t>(id), direction);
				if (result.found && !result.ambiguous) {
					continue;
				}

				MapItemIdConversionIssue issue;
				issue.sourceId = result.original;
				issue.preferredId = result.converted;
				issue.occurrences = occurrences[id];
				issue.found = result.found;
				issue.ambiguous = result.ambiguous;
				issue.candidates.assign(result.candidates.begin(), result.candidates.end());
				report.issues.push_back(std::move(issue));
			}
		}

		void merge(const WorkerMappingStats& worker) {
			report.totalItems += worker.totalItems;
			report.mappedItems += worker.mappedItems;
			report.changedItems += worker.changedItems;
			report.missingItems += worker.missingItems;
			report.ambiguousItems += worker.ambiguousItems;
			if (worker.occurrences.size() == occurrences.size()) {
				for (std::size_t id = 0; id < occurrences.size(); ++id) {
					occurrences[id] += worker.occurrences[id];
				}
			}
		}

	private:
		MapItemIdConversionReport& report;
		std::vector<uint64_t> occurrences;
	};

	class MappingCodec final : public ItemIdCodec {
	public:
		MappingCodec(ItemIdMapping::Direction direction, ReportBuilder* reportBuilder = nullptr) :
			direction(direction),
			reportBuilder(reportBuilder) { }

		bool Decode(uint16_t storedId, uint16_t& serverId) const override {
			const auto result = ItemIdMapping::convert(storedId, direction);
			if (reportBuilder) {
				reportBuilder->observe(result);
			}
			serverId = result.converted;
			return true;
		}

		bool Encode(uint16_t serverId, uint16_t& storedId) const override {
			const auto result = ItemIdMapping::convert(serverId, direction);
			if (reportBuilder) {
				reportBuilder->observe(result);
			}
			storedId = result.converted;
			return true;
		}

	private:
		ItemIdMapping::Direction direction;
		ReportBuilder* reportBuilder;
	};

	class StableHash {
	public:
		template <typename Value>
		void add(const Value& value) {
			const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
			for (std::size_t index = 0; index < sizeof(Value); ++index) {
				hash ^= bytes[index];
				hash *= 1099511628211ull;
			}
		}

		void addBytes(const uint8_t* bytes, std::size_t size) {
			for (std::size_t index = 0; index < size; ++index) {
				hash ^= bytes[index];
				hash *= 1099511628211ull;
			}
		}

		void addString(const std::string& value) {
			add(static_cast<uint64_t>(value.size()));
			addBytes(reinterpret_cast<const uint8_t*>(value.data()), value.size());
		}

		[[nodiscard]] uint64_t value() const {
			return hash;
		}

	private:
		uint64_t hash = 14695981039346656037ull;
	};

	struct MapSummary {
		uint16_t width = 0;
		uint16_t height = 0;
		uint64_t tileCount = 0;
		uint64_t itemCount = 0;
		uint64_t containerCount = 0;
		uint64_t hash = 0;

		struct TeleportState {
			Position position;
			uint32_t itemOrdinal = 0;
			Position destination;
			[[nodiscard]] bool operator==(const TeleportState& other) const = default;
		};

		struct HouseDoorState {
			Position position;
			uint32_t itemOrdinal = 0;
			uint32_t houseId = 0;
			uint8_t doorId = 0;
			[[nodiscard]] bool operator==(const HouseDoorState& other) const = default;
		};

		std::vector<TeleportState> teleports;
		std::vector<HouseDoorState> houseDoors;
		std::vector<DepotConversionState> depots;

		[[nodiscard]] bool operator==(const MapSummary& other) const = default;
	};

	struct PendingItem {
		Item* item;
		uint32_t depth;
		uint32_t index;
		uint8_t role;
	};

	uint16_t StoredIdForSummary(uint16_t serverId, bool encodeServerToClient) {
		return encodeServerToClient ? ItemIdMapping::serverToClient(serverId).converted : serverId;
	}

	std::string StoredFilenameForSummary(const std::string& filename) {
		FileName storedName(wxstr(filename));
		return nstr(storedName.GetFullName());
	}

	struct TileAnalysis {
		uint64_t hash = 0;
		uint64_t itemCount = 0;
		uint64_t containerCount = 0;
		std::vector<MapSummary::TeleportState> teleports;
		std::vector<MapSummary::HouseDoorState> houseDoors;
		std::vector<DepotConversionState> depots;
	};

	struct AnalysisWorkerContext {
		AnalysisWorkerContext(const MapVersion& version, bool collectMapping) :
			attributeSerializer(version), mapping(collectMapping) {
			pending.reserve(64);
		}

		IOMapOTBM attributeSerializer;
		MemoryNodeFileWriteHandle attributeStream;
		std::vector<PendingItem> pending;
		WorkerMappingStats mapping;
	};

	TileAnalysis AnalyzeTile(const Tile& tile, bool encodeServerToClient, AnalysisWorkerContext& worker) {
		TileAnalysis result;
		StableHash hash;
		const Position& position = tile.getPosition();
		hash.add(position.x);
		hash.add(position.y);
		hash.add(position.z);
		hash.add(tile.getMapFlags());
		hash.add(tile.getHouseID());
		hash.add(static_cast<uint64_t>(tile.zones.size()));
		for (const unsigned int zoneId : tile.zones) {
			hash.add(zoneId);
		}

		uint32_t itemOrdinal = 0;
		auto analyzeItem = [&](Item* root, uint8_t role, uint32_t rootIndex) {
			worker.pending.clear();
			worker.pending.push_back({ root, 0, rootIndex, role });
			while (!worker.pending.empty()) {
				const PendingItem current = worker.pending.back();
				worker.pending.pop_back();
				if (!current.item) {
					continue;
				}

				const uint32_t currentOrdinal = itemOrdinal++;
				const auto mapping = ItemIdMapping::serverToClient(current.item->getID());
				if (!worker.mapping.occurrences.empty()) {
					worker.mapping.observe(mapping);
				}
				const uint16_t storedId = StoredIdForSummary(current.item->getID(), encodeServerToClient);
				hash.add(current.role);
				hash.add(current.depth);
				hash.add(current.index);
				hash.add(storedId);
				hash.add(current.item->getSubtype());
				hash.add(current.item->getActionID());
				hash.add(current.item->getUniqueID());
				hash.add(current.item->getTier());
				worker.attributeStream.rewind();
				current.item->serializeItemAttributes_OTBM(worker.attributeSerializer, worker.attributeStream);
				const uint64_t attributeSize = static_cast<uint64_t>(worker.attributeStream.getSize());
				hash.add(attributeSize);
				hash.addBytes(worker.attributeStream.getData(), worker.attributeStream.getSize());
				++result.itemCount;

				if (const auto* teleport = dynamic_cast<const Teleport*>(current.item)) {
					result.teleports.push_back({ position, currentOrdinal, teleport->getDestination() });
				}
				if (tile.getHouseID() != 0) {
					if (const auto* door = dynamic_cast<const Door*>(current.item)) {
						result.houseDoors.push_back({ position, currentOrdinal, tile.getHouseID(), door->getDoorID() });
					}
				}
				if (const auto* depot = dynamic_cast<const Depot*>(current.item)) {
					result.depots.push_back({ position.x, position.y, position.z, currentOrdinal, current.item->getID(), storedId, depot->getDepotID(), current.item->getActionID(), current.item->getUniqueID() });
				}

				auto* container = dynamic_cast<Container*>(current.item);
				const uint32_t childCount = container ? static_cast<uint32_t>(container->getItemCount()) : 0;
				hash.add(childCount);
				if (container) {
					++result.containerCount;
					for (uint32_t childIndex = childCount; childIndex > 0; --childIndex) {
						worker.pending.push_back({ container->getItem(childIndex - 1), current.depth + 1, childIndex - 1, 2 });
					}
				}
			}
		};

		const auto shouldSerializeGround = [](const Tile& candidate) {
			Item* ground = candidate.ground;
			if (!ground || ground->isMetaItem()) {
				return false;
			}
			if (!ground->hasBorderEquivalent()) {
				return true;
			}
			return std::none_of(candidate.items.begin(), candidate.items.end(), [ground](const Item* item) {
				return item->getGroundEquivalent() == ground->getID();
			});
		};

		const uint8_t hasGround = shouldSerializeGround(tile) ? 1 : 0;
		hash.add(hasGround);
		if (hasGround != 0) {
			analyzeItem(tile.ground, 0, 0);
		}
		uint32_t serializedItemIndex = 0;
		for (Item* item : tile.items) {
			if (!item->isMetaItem()) {
				analyzeItem(item, 1, serializedItemIndex++);
			}
		}
		result.hash = hash.value();
		return result;
	}

	bool AnalyzeMap(Map& map, bool encodeServerToClient, ReportBuilder* reportBuilder, MapSummary& summary, uint32_t requestedThreads, uint64_t memoryLimitBytes, uint32_t& threadsUsed, std::string& analysisError) {
		analysisError.clear();
		if (!CheckMemoryLimit(memoryLimitBytes, "before map analysis", analysisError)) {
			return false;
		}
		summary.width = static_cast<uint16_t>(map.getWidth());
		summary.height = static_cast<uint16_t>(map.getHeight());
		StableHash hash;
		const MapVersion mapVersion = map.getVersion();
		hash.add(static_cast<int32_t>(mapVersion.otbm));
		hash.add(summary.width);
		hash.add(summary.height);
		hash.addString(map.getMapDescription());
		hash.addString(StoredFilenameForSummary(map.getSpawnFilename()));
		hash.addString(StoredFilenameForSummary(map.getSpawnNpcFilename()));
		hash.addString(StoredFilenameForSummary(map.getHouseFilename()));
		hash.addString(StoredFilenameForSummary(map.getZoneFilename()));

		hash.add(static_cast<uint64_t>(map.towns.count()));
		for (const auto& townEntry : map.towns) {
			Town* town = townEntry.second;
			const Position& position = town->getTemplePosition();
			hash.add(town->getID());
			hash.addString(town->getName());
			hash.add(position.x);
			hash.add(position.y);
			hash.add(position.z);
		}

		hash.add(static_cast<uint64_t>(map.waypoints.waypoints.size()));
		for (const auto& waypointEntry : map.waypoints) {
			Waypoint* waypoint = waypointEntry.second;
			hash.addString(waypoint->name);
			hash.add(waypoint->pos.x);
			hash.add(waypoint->pos.y);
			hash.add(waypoint->pos.z);
		}

		hash.add(static_cast<uint64_t>(map.zones.zones.size()));
		for (const auto& [zoneName, zoneId] : map.zones) {
			hash.addString(zoneName);
			hash.add(zoneId);
		}

		const uint64_t expectedTiles = std::max<uint64_t>(1, map.getTileCount());
		const uint32_t workerCount = std::max<uint32_t>(1, requestedThreads);
		if (!CheckMemoryLimit(memoryLimitBytes, "before allocating map analysis workers", analysisError, static_cast<uint64_t>(workerCount) * 2 * MiB)) {
			return false;
		}
		std::vector<std::unique_ptr<AnalysisWorkerContext>> workers;
		workers.reserve(workerCount);
		for (uint32_t index = 0; index < workerCount; ++index) {
			workers.push_back(std::make_unique<AnalysisWorkerContext>(mapVersion, reportBuilder != nullptr));
		}

		const uint64_t budgetedBatch = std::max<uint64_t>(1024, memoryLimitBytes / 128);
		const size_t batchCapacity = static_cast<size_t>(std::min<uint64_t>(65536, budgetedBatch));
		std::vector<Tile*> tiles;
		std::vector<TileAnalysis> tileResults;
		tiles.reserve(batchCapacity);
		tileResults.reserve(batchCapacity);

		auto processBatch = [&]() {
			if (tiles.empty()) {
				return true;
			}
			tileResults.clear();
			if (!CheckMemoryLimit(memoryLimitBytes, "before allocating map analysis results", analysisError, static_cast<uint64_t>(tiles.size()) * sizeof(TileAnalysis))) {
				return false;
			}
			tileResults.resize(tiles.size());
			const uint32_t activeWorkers = std::min<uint32_t>(workerCount, static_cast<uint32_t>(tiles.size()));
			threadsUsed = std::max(threadsUsed, activeWorkers);
			std::atomic_size_t nextTile { 0 };
			std::atomic_bool stopWorkers { false };
			std::mutex failureMutex;
			std::exception_ptr firstFailure;
			auto recordFailure = [&](std::exception_ptr failure) {
				{
					std::lock_guard<std::mutex> lock(failureMutex);
					if (!firstFailure) {
						firstFailure = failure;
					}
				}
				stopWorkers.store(true, std::memory_order_release);
			};
			auto runWorker = [&](uint32_t workerIndex) {
				try {
					while (!stopWorkers.load(std::memory_order_acquire)) {
						const size_t index = nextTile.fetch_add(1, std::memory_order_relaxed);
						if (index >= tiles.size()) {
							break;
						}
						if (index % 1024 == 0) {
							std::string memoryError;
							if (!CheckMemoryLimit(memoryLimitBytes, "during map analysis", memoryError)) {
								throw std::runtime_error(memoryError);
							}
						}
						tileResults[index] = AnalyzeTile(*tiles[index], encodeServerToClient, *workers[workerIndex]);
					}
				} catch (...) {
					recordFailure(std::current_exception());
				}
			};

			std::vector<std::thread> threads;
			try {
				threads.reserve(activeWorkers > 0 ? activeWorkers - 1 : 0);
				for (uint32_t index = 1; index < activeWorkers; ++index) {
					threads.emplace_back(runWorker, index);
				}
			} catch (...) {
				recordFailure(std::current_exception());
			}
			runWorker(0);
			for (std::thread& thread : threads) {
				if (thread.joinable()) {
					thread.join();
				}
			}
			if (firstFailure) {
				try {
					std::rethrow_exception(firstFailure);
				} catch (const std::bad_alloc&) {
					analysisError = "Map analysis could not allocate memory within the configured limit.";
				} catch (const std::length_error&) {
					analysisError = "Map analysis requested an allocation that is too large.";
				} catch (const std::exception& exception) {
					analysisError = "Map analysis failed: " + std::string(exception.what());
				} catch (...) {
					analysisError = "Map analysis failed because a worker stopped unexpectedly.";
				}
				return false;
			}
			if (!CheckMemoryLimit(memoryLimitBytes, "after a map analysis batch", analysisError)) {
				return false;
			}

			for (TileAnalysis& tileResult : tileResults) {
				++summary.tileCount;
				summary.itemCount += tileResult.itemCount;
				summary.containerCount += tileResult.containerCount;
				hash.add(tileResult.hash);
				summary.teleports.insert(summary.teleports.end(), tileResult.teleports.begin(), tileResult.teleports.end());
				summary.houseDoors.insert(summary.houseDoors.end(), tileResult.houseDoors.begin(), tileResult.houseDoors.end());
				summary.depots.insert(summary.depots.end(), tileResult.depots.begin(), tileResult.depots.end());
			}
			tiles.clear();
			return g_gui.SetLoadDone(static_cast<int32_t>(std::min<uint64_t>(99, summary.tileCount * 100 / expectedTiles)));
		};

		for (MapIterator iterator = map.begin(); iterator != map.end(); ++iterator) {
			Tile* tile = (*iterator)->get();
			if (!tile || (tile->size() == 0 && !tile->hasZone())) {
				continue;
			}
			tiles.push_back(tile);
			if (tiles.size() == batchCapacity && !processBatch()) {
				return false;
			}
		}
		if (!processBatch()) {
			return false;
		}
		if (reportBuilder) {
			for (const auto& worker : workers) {
				reportBuilder->merge(worker->mapping);
			}
		}

		for (DepotConversionState& depot : summary.depots) {
			const auto town = map.towns.find(depot.depotId);
			depot.townExists = town != map.towns.end();
			if (depot.townExists) {
				depot.townName = town->second->getName();
			}
		}

		summary.hash = hash.value();
		return true;
	}

	std::string FormatPosition(const Position& position) {
		std::ostringstream output;
		output << "{x = " << position.x << ", y = " << position.y << ", z = " << position.z << '}';
		return output.str();
	}

	bool ValidateSpecialItemState(const MapSummary& original, const MapSummary& converted, std::string& error) {
		if (!ValidateDepotConversionStates(original.depots, converted.depots, error)) {
			return false;
		}

		if (original.teleports != converted.teleports) {
			std::size_t index = 0;
			while (index < original.teleports.size() && index < converted.teleports.size() && original.teleports[index] == converted.teleports[index]) {
				++index;
			}
			const MapSummary::TeleportState* before = index < original.teleports.size() ? &original.teleports[index] : nullptr;
			const MapSummary::TeleportState* after = index < converted.teleports.size() ? &converted.teleports[index] : nullptr;
			const Position position = before ? before->position : after->position;
			std::ostringstream message;
			message << "Teleport Destination changed at " << FormatPosition(position) << ". Original: ";
			message << (before ? FormatPosition(before->destination) : "<missing>");
			message << "; after conversion: ";
			if (before && after && before->position == after->position && before->itemOrdinal == after->itemOrdinal) {
				message << FormatPosition(after->destination);
			} else {
				message << "<missing or no longer a Teleport>";
			}
			error = message.str();
			return false;
		}

		if (original.houseDoors != converted.houseDoors) {
			std::size_t index = 0;
			while (index < original.houseDoors.size() && index < converted.houseDoors.size() && original.houseDoors[index] == converted.houseDoors[index]) {
				++index;
			}
			const MapSummary::HouseDoorState* before = index < original.houseDoors.size() ? &original.houseDoors[index] : nullptr;
			const MapSummary::HouseDoorState* after = index < converted.houseDoors.size() ? &converted.houseDoors[index] : nullptr;
			const Position position = before ? before->position : after->position;
			const uint32_t houseId = before ? before->houseId : after->houseId;
			std::ostringstream message;
			message << "House Door ID changed at " << FormatPosition(position) << ". House ID: " << houseId << "; original Door ID: ";
			message << (before ? std::to_string(before->doorId) : "<missing>");
			message << "; after conversion: ";
			if (before && after && before->position == after->position && before->itemOrdinal == after->itemOrdinal) {
				message << static_cast<unsigned int>(after->doorId);
			} else {
				message << "<missing or no longer a Door>";
			}
			error = message.str();
			return false;
		}
		return true;
	}

	bool FilesMatch(const std::filesystem::path& leftPath, const std::filesystem::path& rightPath, std::string& error) {
		error.clear();
		std::error_code filesystemError;
		const uintmax_t leftSize = std::filesystem::file_size(leftPath, filesystemError);
		if (filesystemError) {
			error = "Could not inspect first validation file: " + filesystemError.message();
			return false;
		}
		const uintmax_t rightSize = std::filesystem::file_size(rightPath, filesystemError);
		if (filesystemError) {
			error = "Could not inspect second validation file: " + filesystemError.message();
			return false;
		}
		if (leftSize != rightSize) {
			error = "Round-trip output size changed.";
			return false;
		}

		std::ifstream left(leftPath, std::ios::binary);
		std::ifstream right(rightPath, std::ios::binary);
		if (!left || !right) {
			error = "Could not reopen round-trip output for byte comparison.";
			return false;
		}

		std::array<char, 64 * 1024> leftBuffer;
		std::array<char, 64 * 1024> rightBuffer;
		while (left && right) {
			left.read(leftBuffer.data(), leftBuffer.size());
			right.read(rightBuffer.data(), rightBuffer.size());
			const std::streamsize leftRead = left.gcount();
			const std::streamsize rightRead = right.gcount();
			if (leftRead != rightRead || !std::equal(leftBuffer.begin(), leftBuffer.begin() + leftRead, rightBuffer.begin())) {
				error = "Round-trip output bytes changed.";
				return false;
			}
		}
		if (left.bad() || right.bad()) {
			error = "Could not complete round-trip byte comparison.";
			return false;
		}
		return true;
	}

	void AppendWarnings(IOMapOTBM& io, MapItemIdConversionReport& report) {
		for (const wxString& warning : io.getWarnings()) {
			report.warnings.push_back(nstr(warning));
		}
	}

	bool IsCancellationError(const wxString& error) {
		return error.Lower().Contains("cancel");
	}
}

MapItemIdPerformanceLimits GetMapItemIdConverterPerformanceLimits() {
	MapItemIdPerformanceLimits limits;
	limits.hardwareThreads = std::max<uint32_t>(1, std::thread::hardware_concurrency());
	const uint64_t workingSet = QueryProcessMemoryBytes();
	const uint64_t available = QueryAvailableMemoryBytes();
	const uint64_t total = QueryTotalMemoryBytes();
	if (available != 0) {
		const uint64_t safeHeadroom = available - available / 4;
		limits.safeMemoryLimitBytes = workingSet > std::numeric_limits<uint64_t>::max() - safeHeadroom ? std::numeric_limits<uint64_t>::max() : workingSet + safeHeadroom;
	}
	limits.maximumCustomMemoryLimitBytes = total != 0 ? total : limits.safeMemoryLimitBytes;
	if (limits.maximumCustomMemoryLimitBytes == 0) {
		limits.maximumCustomMemoryLimitBytes = 8 * GiB;
	}
	const uint64_t customRemainder = limits.maximumCustomMemoryLimitBytes % GiB;
	if (customRemainder != 0 && limits.maximumCustomMemoryLimitBytes <= std::numeric_limits<uint64_t>::max() - (GiB - customRemainder)) {
		limits.maximumCustomMemoryLimitBytes += GiB - customRemainder;
	}
	if (sizeof(void*) < 8) {
		constexpr uint64_t Process32BitLimit = 1536 * MiB;
		limits.safeMemoryLimitBytes = limits.safeMemoryLimitBytes == 0 ? Process32BitLimit : std::min(limits.safeMemoryLimitBytes, Process32BitLimit);
		limits.maximumCustomMemoryLimitBytes = std::min(limits.maximumCustomMemoryLimitBytes, Process32BitLimit);
	}
	return limits;
}

std::string MapItemIdConversionReport::format(const MapItemIdConversionOptions& options) const {
	std::ostringstream output;
	const ClientVersion* targetClient = ClientVersion::get(options.targetVersion.client);
	const std::string targetName = targetClient ? targetClient->getName() : std::to_string(options.targetVersion.client);
	const uint32_t targetItemMajor = targetClient ? static_cast<uint32_t>(targetClient->getOTBVersion().format_version) : 0;
	output << "Native OTBM Item ID Conversion\n"
		   << "Status: " << (success ? "SUCCESS" : (cancelled ? "CANCELLED" : "FAILED")) << '\n'
		   << "Direction: " << (options.direction == ItemIdMapping::Direction::ServerToClient ? "Server ID -> Client ID" : "Client ID -> Server ID") << '\n'
		   << "Policy: Compatibility\n"
		   << "Target map version: " << targetName << " (OTBM " << (static_cast<int>(options.targetVersion.otbm) + 1) << ", item major " << targetItemMajor << ", item minor " << options.targetVersion.client << ")\n"
		   << "Source: " << PrintablePath(options.source) << '\n'
		   << "Destination: " << PrintablePath(options.destination) << '\n'
		   << "Mapping: " << ItemIdMapping::sourceVersion() << '\n'
		   << "Processing mode: " << (options.performance.mode == MapItemIdProcessingMode::Automatic ? "Automatic" : "Custom") << '\n'
		   << "Threads used: " << threadsUsed << '\n'
		   << "Memory limit: " << std::fixed << std::setprecision(2) << static_cast<double>(memoryLimitBytes) / static_cast<double>(GiB) << " GB\n"
		   << "Loading time: " << loadingSeconds << " s\n"
		   << "Conversion time: " << conversionSeconds << " s\n"
		   << "Saving time: " << savingSeconds << " s\n"
		   << "Total time: " << totalSeconds << " s\n"
		   << "Tiles: " << tileCount << '\n'
		   << "Items scanned: " << totalItems << '\n'
		   << "Mapped: " << mappedItems << '\n'
		   << "Changed: " << changedItems << '\n'
		   << "Missing: " << missingItems << '\n'
		   << "Ambiguous: " << ambiguousItems << '\n'
		   << "Output reopened and validated: " << (outputValidated ? "yes" : "no") << '\n';
	if (success && (missingItems != 0 || ambiguousItems != 0)) {
		output << "Result note: decisions below are expected mapping collisions, not conversion failures.\n";
	}

	if (!error.empty()) {
		output << "\nError: " << error << '\n';
	}
	if (!issues.empty()) {
		output << "\nMapping decisions (source ID, occurrences, resolution):\n";
		for (const auto& issue : issues) {
			output << "  " << issue.sourceId << " x" << issue.occurrences << ": ";
			if (!issue.found) {
				output << "not mapped; kept unchanged";
			} else {
				output << "preferred " << issue.preferredId;
			}
			if (issue.ambiguous) {
				output << "; server candidates [";
				for (std::size_t index = 0; index < issue.candidates.size(); ++index) {
					if (index != 0) {
						output << ", ";
					}
					output << issue.candidates[index];
				}
				output << ']';
			}
			output << '\n';
		}
	}
	if (!warnings.empty()) {
		output << "\nWarnings:\n";
		for (const std::string& warning : warnings) {
			output << "  - " << warning << '\n';
		}
	}
	return output.str();
}

MapItemIdConversionReport ConvertMapItemIds(const MapItemIdConversionOptions& options) {
	MapItemIdConversionReport report;
	const auto totalStart = std::chrono::steady_clock::now();
	auto finish = [&]() {
		report.totalSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - totalStart).count();
		return report;
	};
	try {
	if (!ItemIdMapping::validateTables()) {
		report.error = "Embedded item ID mapping tables failed validation.";
		return finish();
	}
	if (options.source.empty() || options.destination.empty()) {
		report.error = "Source and destination paths are required.";
		return finish();
	}
	if (FileSaveTransaction::PathsReferToSameFile(options.source, options.destination)) {
		report.error = "Source and destination must be different files.";
		return finish();
	}
	std::error_code filesystemError;
	if (!std::filesystem::is_regular_file(options.source, filesystemError) || filesystemError) {
		report.error = "Source OTBM file does not exist or cannot be accessed.";
		return finish();
	}
	const uint64_t sourceSize = std::filesystem::file_size(options.source, filesystemError);
	if (filesystemError) {
		report.error = "Could not inspect the source OTBM size: " + filesystemError.message();
		return finish();
	}
	const ResolvedPerformance performance = ResolvePerformance(options, sourceSize, report.error);
	if (!report.error.empty()) {
		return finish();
	}
	report.memoryLimitBytes = performance.memoryLimitBytes;
	const OTBMMemoryBudgetCheck memoryBudgetCheck = [limit = performance.memoryLimitBytes, &report](const char* phase, uint64_t pendingBytes, std::string& error) {
		const bool withinBudget = CheckMemoryLimit(limit, phase, error, pendingBytes);
		if (!withinBudget) {
			report.error = error;
		}
		return withinBudget;
	};
	if (!CheckMemoryLimit(performance.memoryLimitBytes, "before conversion setup", report.error)) {
		return finish();
	}

	ClientVersion* targetClient = ClientVersion::get(options.targetVersion.client);
	if (!targetClient) {
		report.error = "The selected target map version is not registered by this RME installation.";
		return finish();
	}
	const MapVersion targetVersion(options.targetVersion.otbm, targetClient->getID());
	if (targetVersion.otbm < MAP_OTBM_1 || targetVersion.otbm > MAP_OTBM_4) {
		report.error = "The selected target OTBM version is not supported.";
		return finish();
	}
	const OtbVersion targetOtb = targetClient->getOTBVersion();
	const uint32_t targetItemMajorVersion = static_cast<uint32_t>(targetOtb.format_version);
	const uint32_t targetItemMinorVersion = static_cast<uint32_t>(targetOtb.id);

	MapVersion sourceVersion;
	if (!IOMapOTBM::getVersionInfo(FileName(PathToWxString(options.source)), sourceVersion, nullptr, memoryBudgetCheck)) {
		if (report.error.empty()) {
			report.error = "Source is not a valid supported OTBM file.";
		}
		return finish();
	}

	ScopedLoadingBar loading("Converting OTBM item IDs...", true);
	ReportBuilder reportBuilder(report);
	std::unique_ptr<Map> map = std::make_unique<Map>();
	MappingCodec reverseReadCodec(ItemIdMapping::Direction::ClientToServer, &reportBuilder);
	IOMapOTBM loader(sourceVersion);
	loader.useMemoryBudgetCheck(memoryBudgetCheck);
	if (options.direction == ItemIdMapping::Direction::ClientToServer) {
		loader.useItemIdCodec(&reverseReadCodec);
	}

	ScopedLoadingBar::SetLoadScale(0, 25);
	const auto loadingStart = std::chrono::steady_clock::now();
	if (!CheckMemoryLimit(performance.memoryLimitBytes, "before loading the source map", report.error)) {
		return finish();
	}
	if (!loader.loadMapData(*map, FileName(PathToWxString(options.source)))) {
		report.loadingSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - loadingStart).count();
		AppendWarnings(loader, report);
		report.cancelled = IsCancellationError(loader.getError());
		if (report.error.empty()) {
			report.error = report.cancelled ? "Conversion cancelled while loading the source map." : nstr(loader.getError());
		}
		return finish();
	}
	report.loadingSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - loadingStart).count();
	AppendWarnings(loader, report);
	if (!CheckMemoryLimit(performance.memoryLimitBytes, "after loading", report.error)) {
		return finish();
	}
	if (!FileSaveTransaction::PathsReferToSameFile(ParentDirectory(options.source), ParentDirectory(options.destination)) && ReferencesSidecars(*map)) {
		report.error = "Cross-directory output is not supported when the OTBM references spawn, NPC spawn, house, or zone sidecar files. Choose a destination in the source map directory.";
		return finish();
	}
	const std::filesystem::path destinationDirectory = options.destination.parent_path();
	if (!destinationDirectory.empty()) {
		std::filesystem::create_directories(destinationDirectory, filesystemError);
		if (filesystemError) {
			report.error = "Could not create destination directory: " + filesystemError.message();
			return finish();
		}
	}
	if (!CheckMemoryLimit(performance.memoryLimitBytes, "before applying the target map version", report.error)) {
		return finish();
	}
	if (!map->convert(targetVersion)) {
		report.error = "Could not apply the selected target map version.";
		return finish();
	}
	if (!CheckMemoryLimit(performance.memoryLimitBytes, "after applying the target map version", report.error)) {
		return finish();
	}

	ScopedLoadingBar::SetLoadScale(25, 50);
	const auto conversionStart = std::chrono::steady_clock::now();
	MapSummary sourceSummary;
	std::string analysisError;
	const bool encodeOutputIds = options.direction == ItemIdMapping::Direction::ServerToClient;
	ReportBuilder* analysisReport = encodeOutputIds ? &reportBuilder : nullptr;
	if (!AnalyzeMap(*map, encodeOutputIds, analysisReport, sourceSummary, performance.threads, performance.memoryLimitBytes, report.threadsUsed, analysisError)) {
		report.conversionSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - conversionStart).count();
		report.cancelled = analysisError.empty();
		report.error = analysisError.empty() ? "Conversion cancelled during map analysis." : analysisError;
		return finish();
	}
	report.tileCount = sourceSummary.tileCount;
	reportBuilder.finish(options.direction);
	report.conversionSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - conversionStart).count();
	if (!CheckMemoryLimit(performance.memoryLimitBytes, "after conversion analysis", report.error)) {
		return finish();
	}

	FileSaveTransaction transaction;
	const std::filesystem::path stagedPath = transaction.Stage(options.destination);
	MappingCodec forwardWriteCodec(ItemIdMapping::Direction::ServerToClient);
	IOMapOTBM saver(targetVersion);
	saver.useMemoryBudgetCheck(memoryBudgetCheck);
	saver.useItemVersionHeader(targetItemMajorVersion, targetItemMinorVersion);
	if (encodeOutputIds) {
		saver.useItemIdCodec(&forwardWriteCodec);
	}

	ScopedLoadingBar::SetLoadScale(50, 72);
	const auto savingStart = std::chrono::steady_clock::now();
	if (!CheckMemoryLimit(performance.memoryLimitBytes, "before writing the output map", report.error)) {
		return finish();
	}
	if (!saver.saveMapData(*map, FileName(PathToWxString(stagedPath)))) {
		report.savingSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - savingStart).count();
		AppendWarnings(saver, report);
		report.cancelled = IsCancellationError(saver.getError());
		if (report.error.empty()) {
			report.error = report.cancelled ? "Conversion cancelled while writing the output map." : nstr(saver.getError());
		}
		return finish();
	}
	report.savingSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - savingStart).count();
	AppendWarnings(saver, report);

	MapVersion stagedVersion;
	uint32_t stagedItemMajorVersion = 0;
	if (!IOMapOTBM::getVersionInfo(FileName(PathToWxString(stagedPath)), stagedVersion, &stagedItemMajorVersion, memoryBudgetCheck) || stagedVersion.otbm != targetVersion.otbm || stagedVersion.client != targetVersion.client || stagedItemMajorVersion != targetItemMajorVersion) {
		if (report.error.empty()) {
			report.error = "Generated OTBM header does not match the selected target map version.";
		}
		return finish();
	}

	map.reset();
	if (!CheckMemoryLimit(performance.memoryLimitBytes, "after saving", report.error)) {
		return finish();
	}
	if (!ScopedLoadingBar::SetLoadDone(99, "Reopening generated OTBM...")) {
		report.cancelled = true;
		report.error = "Conversion cancelled before output validation.";
		return finish();
	}

	ScopedLoadingBar::SetLoadScale(72, 90);
	if (!CheckMemoryLimit(performance.memoryLimitBytes, "before reopening the generated OTBM", report.error)) {
		return finish();
	}
	std::unique_ptr<Map> validationMap = std::make_unique<Map>();
	MappingCodec validationReadCodec(ItemIdMapping::Direction::ClientToServer);
	IOMapOTBM validator(targetVersion);
	validator.useMemoryBudgetCheck(memoryBudgetCheck);
	if (encodeOutputIds) {
		validator.useItemIdCodec(&validationReadCodec);
	}
	if (!validator.loadMapData(*validationMap, FileName(PathToWxString(stagedPath)))) {
		AppendWarnings(validator, report);
		report.cancelled = IsCancellationError(validator.getError());
		if (report.error.empty()) {
			report.error = report.cancelled ? "Conversion cancelled while validating the output map." : "Generated OTBM could not be reopened: " + nstr(validator.getError());
		}
		return finish();
	}
	AppendWarnings(validator, report);
	if (validationMap->getVersion().otbm != targetVersion.otbm || validationMap->getVersion().client != targetVersion.client) {
		report.error = "Generated OTBM reopened with metadata different from the selected target version.";
		return finish();
	}
	if (!CheckMemoryLimit(performance.memoryLimitBytes, "while validating the output", report.error)) {
		return finish();
	}

	ScopedLoadingBar::SetLoadScale(90, 94);
	MapSummary validationSummary;
	analysisError.clear();
	if (!AnalyzeMap(*validationMap, encodeOutputIds, nullptr, validationSummary, performance.threads, performance.memoryLimitBytes, report.threadsUsed, analysisError)) {
		report.cancelled = analysisError.empty();
		report.error = analysisError.empty() ? "Conversion cancelled during output validation." : analysisError;
		return finish();
	}
	if (!ValidateSpecialItemState(sourceSummary, validationSummary, report.error)) {
		return finish();
	}
	if (!(sourceSummary == validationSummary)) {
		report.error = "Generated OTBM failed structural validation; no destination file was replaced.";
		return finish();
	}

	ScopedLoadingBar::SetLoadScale(94, 98);
	FileSaveTransaction roundTripTransaction;
	const std::filesystem::path roundTripPath = roundTripTransaction.Stage(options.destination);
	MappingCodec validationWriteCodec(ItemIdMapping::Direction::ServerToClient);
	IOMapOTBM validationSaver(targetVersion);
	validationSaver.useMemoryBudgetCheck(memoryBudgetCheck);
	validationSaver.useItemVersionHeader(targetItemMajorVersion, targetItemMinorVersion);
	if (encodeOutputIds) {
		validationSaver.useItemIdCodec(&validationWriteCodec);
	}
	if (!CheckMemoryLimit(performance.memoryLimitBytes, "before round-trip serialization", report.error)) {
		return finish();
	}
	if (!validationSaver.saveMapData(*validationMap, FileName(PathToWxString(roundTripPath)))) {
		AppendWarnings(validationSaver, report);
		report.cancelled = IsCancellationError(validationSaver.getError());
		if (report.error.empty()) {
			report.error = report.cancelled ? "Conversion cancelled during round-trip validation." : "Generated OTBM failed native round-trip save: " + nstr(validationSaver.getError());
		}
		return finish();
	}
	AppendWarnings(validationSaver, report);
	std::string comparisonError;
	// Unlike the quick AnalyzeMap gate, native byte-for-byte reserialization covers every persisted OTBM field before commit.
	if (!FilesMatch(stagedPath, roundTripPath, comparisonError)) {
		report.error = "Generated OTBM failed byte-exact round-trip validation: " + comparisonError;
		return finish();
	}
	report.outputValidated = true;
	validationMap.reset();

	std::string commitError;
	if (!transaction.Commit(commitError)) {
		report.error = commitError;
		return finish();
	}

	report.success = true;
	ScopedLoadingBar::SetLoadDone(100, "Conversion complete.");
	return finish();
	} catch (const std::bad_alloc&) {
		report.error = "Conversion stopped because the configured operation could not allocate enough memory.";
		return finish();
	} catch (const std::length_error&) {
		report.error = "Conversion stopped because an allocation request exceeded the supported size.";
		return finish();
	}
}
