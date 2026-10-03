//////////////////////////////////////////////////////////////////////
// This file is part of NexaMap Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "otb_item_id_mapping_provider.h"

#include "filehandle.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace {
	constexpr uint8_t RootAttributeVersion = 0x01;
	constexpr uint8_t ItemAttributeServerId = 0x10;
	constexpr uint8_t ItemAttributeClientId = 0x11;
	constexpr uint8_t DeprecatedItemGroup = 0x0E;
	constexpr uint16_t VersionAttributeLength = 4 + 4 + 4 + 128;

	std::string PrintablePath(const std::filesystem::path& path) {
#ifdef _WIN32
		return nstr(wxString(path.wstring()));
#else
		return path.string();
#endif
	}

	OtbItemIdMappingLoadResult BuildResult(const std::vector<OtbItemIdMappingProvider::Pair>& pairs, std::string source, std::vector<OtbItemIdMappingIssue> issues, bool allowDuplicateClientIds) {
		OtbItemIdMappingLoadResult result;
		std::array<uint16_t, 1u << 16> serverToClient {};
		std::array<uint16_t, 1u << 16> clientToServer {};
		std::array<bool, 1u << 16> seenServer {};
		std::array<bool, 1u << 16> seenClient {};

		for (const auto& [serverId, clientId] : pairs) {
			if (serverId == 0) {
				issues.push_back({ OtbItemIdMappingIssue::Kind::ZeroServerId, serverId, clientId, 0, "OTB item has ServerID 0." });
				continue;
			}
			if (clientId == 0) {
				issues.push_back({ OtbItemIdMappingIssue::Kind::ZeroClientId, serverId, clientId, 0, "OTB item has ClientID 0." });
				continue;
			}
			if (seenServer[serverId]) {
				++result.stats.duplicateServerIds;
				std::ostringstream message;
				message << "ServerID " << serverId << " appears more than once (ClientIDs " << serverToClient[serverId] << " and " << clientId << ").";
				issues.push_back({ OtbItemIdMappingIssue::Kind::DuplicateServerId, serverId, clientId, serverToClient[serverId], message.str() });
			} else {
				seenServer[serverId] = true;
				serverToClient[serverId] = clientId;
				++result.stats.uniqueServerIds;
			}
			if (seenClient[clientId]) {
				++result.stats.duplicateClientIds;
				std::ostringstream message;
				message << "ClientID " << clientId << " is mapped by ServerIDs " << clientToServer[clientId] << " and " << serverId << ".";
				issues.push_back({ OtbItemIdMappingIssue::Kind::DuplicateClientId, serverId, clientId, clientToServer[clientId], message.str() });
			} else {
				seenClient[clientId] = true;
				clientToServer[clientId] = serverId;
				++result.stats.uniqueClientIds;
			}
		}

		result.stats.entries = pairs.size();
		result.issues = std::move(issues);
		if (pairs.empty()) {
			result.error = "items.otb did not contain any usable ServerID/ClientID mapping pairs.";
			return result;
		}
		const bool hasBlockingIssue = std::any_of(result.issues.begin(), result.issues.end(), [allowDuplicateClientIds](const OtbItemIdMappingIssue& issue) {
			return issue.kind != OtbItemIdMappingIssue::Kind::DuplicateClientId || !allowDuplicateClientIds;
		});
		if (hasBlockingIssue) {
			result.error = "items.otb mapping validation failed.";
			return result;
		}
		result.provider = std::make_shared<const OtbItemIdMappingProvider>(pairs, std::move(source), result.stats, allowDuplicateClientIds);
		return result;
	}
}

OtbItemIdMappingProvider::OtbItemIdMappingProvider(std::vector<Pair> pairs, std::string source, OtbItemIdMappingStats stats, bool allowDuplicateClientIds) :
	sourceDescription(std::move(source)), mappingStats(stats), duplicateClientIdsAllowed(allowDuplicateClientIds) {
	for (const auto& [serverId, clientId] : pairs) {
		forward[serverId] = { clientId, true, false };
		reverseCandidates[clientId].push_back(serverId);
	}
	for (std::size_t clientId = 1; clientId < reverseCandidates.size(); ++clientId) {
		auto& candidates = reverseCandidates[clientId];
		if (candidates.empty()) {
			continue;
		}
		std::sort(candidates.begin(), candidates.end());
		reverse[clientId] = { candidates.front(), true, candidates.size() > 1 };
	}
}

OtbItemIdMappingLoadResult OtbItemIdMappingProvider::Load(const std::filesystem::path& path, bool allowDuplicateClientIds) {
	OtbItemIdMappingLoadResult failed;
	DiskNodeFileReadHandle file(PrintablePath(path), StringVector(1, "OTBI"));
	if (!file.isOk()) {
		failed.error = "Could not open items.otb: " + file.getErrorMessage();
		return failed;
	}
	BinaryNode* root = file.getRootNode();
	if (!root || !root->skip(5)) {
		failed.error = "items.otb has a missing or truncated root node.";
		return failed;
	}
	uint8_t rootAttribute = 0;
	uint16_t rootLength = 0;
	if (!root->getU8(rootAttribute) || rootAttribute != RootAttributeVersion || !root->getU16(rootLength) || rootLength != VersionAttributeLength || !root->skip(rootLength)) {
		failed.error = "items.otb has an invalid version header.";
		return failed;
	}

	std::vector<Pair> pairs;
	std::vector<OtbItemIdMappingIssue> issues;
	for (BinaryNode* node = root->getChild(); node != nullptr; node = node->advance()) {
		uint8_t group = 0;
		if (!node->getU8(group)) {
			issues.push_back({ OtbItemIdMappingIssue::Kind::CorruptNode, 0, 0, 0, "Truncated OTB item node." });
			continue;
		}
		if (group == DeprecatedItemGroup) {
			continue;
		}
		uint32_t flags = 0;
		if (!node->getU32(flags)) {
			issues.push_back({ OtbItemIdMappingIssue::Kind::CorruptNode, 0, 0, 0, "OTB item is missing its four-byte flags field." });
			continue;
		}
		(void)flags;
		uint16_t serverId = 0;
		uint16_t clientId = 0;
		bool hasServerId = false;
		bool hasClientId = false;
		uint8_t attribute = 0;
		while (node->getU8(attribute)) {
			uint16_t length = 0;
			if (!node->getU16(length)) {
				issues.push_back({ OtbItemIdMappingIssue::Kind::CorruptNode, serverId, clientId, 0, "Truncated OTB attribute length." });
				break;
			}
			if (attribute == ItemAttributeServerId || attribute == ItemAttributeClientId) {
				if (length != sizeof(uint16_t)) {
					issues.push_back({ OtbItemIdMappingIssue::Kind::InvalidAttributeLength, serverId, clientId, 0, "ServerID/ClientID OTB attributes must contain exactly two bytes." });
					if (!node->skip(length)) {
						issues.push_back({ OtbItemIdMappingIssue::Kind::CorruptNode, serverId, clientId, 0, "Truncated OTB ID attribute." });
					}
					continue;
				}
				uint16_t value = 0;
				if (!node->getU16(value)) {
					issues.push_back({ OtbItemIdMappingIssue::Kind::CorruptNode, serverId, clientId, 0, "Truncated OTB ID value." });
					break;
				}
				if (attribute == ItemAttributeServerId) {
					serverId = value;
					hasServerId = true;
				} else {
					clientId = value;
					hasClientId = true;
				}
			} else if (!node->skip(length)) {
				issues.push_back({ OtbItemIdMappingIssue::Kind::CorruptNode, serverId, clientId, 0, "Unknown OTB attribute extends beyond its node." });
				break;
			}
		}
		if (!hasServerId) {
			issues.push_back({ OtbItemIdMappingIssue::Kind::MissingServerId, 0, clientId, 0, "OTB item is missing ServerID." });
		}
		if (!hasClientId) {
			issues.push_back({ OtbItemIdMappingIssue::Kind::MissingClientId, serverId, 0, 0, "OTB item is missing ClientID." });
		}
		if (hasServerId && hasClientId) {
			pairs.emplace_back(serverId, clientId);
		}
	}
	if (!file.isOk()) {
		failed.error = "items.otb is corrupt or truncated: " + file.getErrorMessage();
		failed.issues = std::move(issues);
		return failed;
	}
	return BuildResult(pairs, "items.otb: " + PrintablePath(path), std::move(issues), allowDuplicateClientIds);
}

OtbItemIdMappingLoadResult OtbItemIdMappingProvider::FromPairs(const std::vector<Pair>& pairs, std::string source, bool allowDuplicateClientIds) {
	return BuildResult(pairs, std::move(source), {}, allowDuplicateClientIds);
}

ItemIdMapping::Result OtbItemIdMappingProvider::convert(uint16_t id, ItemIdMapping::Direction direction) const noexcept {
	const Entry& entry = direction == ItemIdMapping::Direction::ServerToClient ? forward[id] : reverse[id];
	const auto& candidates = reverseCandidates[id];
	return { id, entry.found ? entry.converted : id, entry.found, entry.ambiguous, direction == ItemIdMapping::Direction::ClientToServer ? std::span<const uint16_t>(candidates) : std::span<const uint16_t>() };
}

bool OtbItemIdMappingProvider::validate() const noexcept {
	return mappingStats.entries != 0 && mappingStats.duplicateServerIds == 0 && (duplicateClientIdsAllowed || mappingStats.duplicateClientIds == 0);
}

std::string OtbItemIdMappingProvider::description() const {
	return sourceDescription;
}

const OtbItemIdMappingStats& OtbItemIdMappingProvider::stats() const noexcept {
	return mappingStats;
}
