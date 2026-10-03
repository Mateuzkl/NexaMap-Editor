//////////////////////////////////////////////////////////////////////
// This file is part of NexaMap Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_OTB_ITEM_ID_MAPPING_PROVIDER_H_
#define RME_OTB_ITEM_ID_MAPPING_PROVIDER_H_

#include "item_id_mapping_provider.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct OtbItemIdMappingIssue {
	enum class Kind {
		MissingServerId,
		MissingClientId,
		ZeroServerId,
		ZeroClientId,
		DuplicateServerId,
		DuplicateClientId,
		InvalidAttributeLength,
		CorruptNode,
	};

	Kind kind = Kind::CorruptNode;
	uint16_t serverId = 0;
	uint16_t clientId = 0;
	uint16_t conflictingId = 0;
	std::string message;
};

struct OtbItemIdMappingStats {
	std::size_t entries = 0;
	std::size_t uniqueServerIds = 0;
	std::size_t uniqueClientIds = 0;
	std::size_t duplicateServerIds = 0;
	std::size_t duplicateClientIds = 0;
};

struct OtbItemIdMappingLoadResult;

class OtbItemIdMappingProvider final : public ItemIdMappingProvider {
public:
	using Pair = std::pair<uint16_t, uint16_t>;

	[[nodiscard]] static OtbItemIdMappingLoadResult Load(const std::filesystem::path& path, bool allowDuplicateClientIds = false);
	[[nodiscard]] static OtbItemIdMappingLoadResult FromPairs(const std::vector<Pair>& pairs, std::string source = "in-memory OTB mapping", bool allowDuplicateClientIds = false);

	[[nodiscard]] ItemIdMapping::Result convert(uint16_t id, ItemIdMapping::Direction direction) const noexcept override;
	[[nodiscard]] bool validate() const noexcept override;
	[[nodiscard]] std::string description() const override;
	[[nodiscard]] const OtbItemIdMappingStats& stats() const noexcept;
	OtbItemIdMappingProvider(std::vector<Pair> pairs, std::string source, OtbItemIdMappingStats stats, bool allowDuplicateClientIds);

private:
	struct Entry {
		uint16_t converted = 0;
		bool found = false;
		bool ambiguous = false;
	};

	std::array<Entry, 1u << 16> forward {};
	std::array<Entry, 1u << 16> reverse {};
	std::array<std::vector<uint16_t>, 1u << 16> reverseCandidates;
	std::string sourceDescription;
	OtbItemIdMappingStats mappingStats;
	bool duplicateClientIdsAllowed = false;
};

struct OtbItemIdMappingLoadResult {
	std::shared_ptr<const OtbItemIdMappingProvider> provider;
	OtbItemIdMappingStats stats;
	std::vector<OtbItemIdMappingIssue> issues;
	std::string error;

	[[nodiscard]] bool ready() const noexcept {
		return provider != nullptr && error.empty() && stats.entries != 0;
	}
};

#endif // RME_OTB_ITEM_ID_MAPPING_PROVIDER_H_
