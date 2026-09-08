//////////////////////////////////////////////////////////////////////
// Structural reader for custom TFS serverId/clientId pair tables.
//////////////////////////////////////////////////////////////////////

#include "server_item_id_map.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>

namespace {
	constexpr std::uintmax_t MaximumTableBytes = 64ULL * 1024ULL * 1024ULL;

	uint32_t ReadLittleEndian32(const std::array<unsigned char, 8>& pair, std::size_t offset) {
		return static_cast<uint32_t>(pair[offset])
			| (static_cast<uint32_t>(pair[offset + 1]) << 8U)
			| (static_cast<uint32_t>(pair[offset + 2]) << 16U)
			| (static_cast<uint32_t>(pair[offset + 3]) << 24U);
	}
}

ServerItemIdMapProbe ProbeServerItemIdMap(const std::filesystem::path& path) {
	std::error_code filesystemError;
	const std::uintmax_t size = std::filesystem::file_size(path, filesystemError);
	if (filesystemError) {
		return { false, 0, "Could not inspect the item ID table: " + filesystemError.message() };
	}
	if (size == 0 || size > MaximumTableBytes || size % 8 != 0) {
		return { false, 0, "The file is not a bounded sequence of 8-byte ID pairs." };
	}

	std::ifstream stream(path, std::ios::binary);
	if (!stream.is_open()) {
		return { false, 0, "Could not open the item ID table." };
	}

	std::array<unsigned char, 8> pair {};
	std::size_t plausiblePairs = 0;
	const std::size_t pairCount = static_cast<std::size_t>(size / 8);
	const std::size_t sampleCount = std::min<std::size_t>(pairCount, 256);
	for (std::size_t index = 0; index < sampleCount; ++index) {
		stream.read(reinterpret_cast<char*>(pair.data()), static_cast<std::streamsize>(pair.size()));
		if (!stream) {
			return { false, 0, "The item ID table ended inside a pair." };
		}
		const uint32_t serverId = ReadLittleEndian32(pair, 0);
		const uint32_t clientId = ReadLittleEndian32(pair, 4);
		if (serverId != 0 && clientId != 0
			&& serverId <= std::numeric_limits<uint16_t>::max()
			&& clientId <= std::numeric_limits<uint16_t>::max()) {
			++plausiblePairs;
		}
	}
	if (plausiblePairs == 0 || plausiblePairs * 4 < sampleCount * 3) {
		return { false, 0, "The file does not contain plausible serverId/clientId pairs." };
	}
	return { true, pairCount, {} };
}

bool LoadServerItemIdMap(const std::filesystem::path& path, std::vector<ServerItemIdMapping>& mappings, std::string& error) {
	mappings.clear();
	const ServerItemIdMapProbe probe = ProbeServerItemIdMap(path);
	if (!probe.valid) {
		error = probe.error;
		return false;
	}

	std::ifstream stream(path, std::ios::binary);
	if (!stream.is_open()) {
		error = "Could not open the item ID table.";
		return false;
	}
	mappings.reserve(probe.pairCount);
	std::array<unsigned char, 8> pair {};
	for (std::size_t index = 0; index < probe.pairCount; ++index) {
		stream.read(reinterpret_cast<char*>(pair.data()), static_cast<std::streamsize>(pair.size()));
		if (!stream) {
			error = "The item ID table ended inside a pair.";
			mappings.clear();
			return false;
		}
		mappings.push_back({ ReadLittleEndian32(pair, 0), ReadLittleEndian32(pair, 4) });
	}
	error.clear();
	return true;
}
