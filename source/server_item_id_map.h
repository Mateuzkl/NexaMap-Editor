//////////////////////////////////////////////////////////////////////
// Structural reader for custom TFS serverId/clientId pair tables.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_SERVER_ITEM_ID_MAP_H_
#define NEXAMAP_SERVER_ITEM_ID_MAP_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct ServerItemIdMapping {
	uint32_t serverId = 0;
	uint32_t clientId = 0;

	friend bool operator==(const ServerItemIdMapping&, const ServerItemIdMapping&) = default;
};

struct ServerItemIdMapProbe {
	bool valid = false;
	std::size_t pairCount = 0;
	std::string error;
};

[[nodiscard]] ServerItemIdMapProbe ProbeServerItemIdMap(const std::filesystem::path& path);
[[nodiscard]] bool LoadServerItemIdMap(
	const std::filesystem::path& path,
	std::vector<ServerItemIdMapping>& mappings,
	std::string& error
);

#endif // NEXAMAP_SERVER_ITEM_ID_MAP_H_
