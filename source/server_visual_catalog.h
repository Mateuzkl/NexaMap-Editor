//////////////////////////////////////////////////////////////////////
// Workspace-specific spell effect and projectile constants.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_SERVER_VISUAL_CATALOG_H_
#define NEXAMAP_SERVER_VISUAL_CATALOG_H_

#include "server_workspace.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

enum class ServerVisualKind {
	MagicEffect,
	DistanceEffect,
};

struct ServerVisualConstant {
	ServerVisualKind kind = ServerVisualKind::MagicEffect;
	std::string name;
	uint32_t id = 0;
	std::filesystem::path sourcePath;
};

struct ServerVisualCatalogStats {
	std::size_t filesInspected = 0;
	std::size_t filesRead = 0;
};

class ServerVisualCatalog final {
public:
	static ServerVisualCatalog Build(const ServerWorkspace& workspace);

	[[nodiscard]] const std::vector<ServerVisualConstant>& effects() const;
	[[nodiscard]] const std::vector<ServerVisualConstant>& projectiles() const;
	[[nodiscard]] std::optional<uint32_t> resolve(ServerVisualKind kind, const std::string& value) const;
	[[nodiscard]] std::string nameFor(ServerVisualKind kind, uint32_t id) const;
	[[nodiscard]] const ServerVisualCatalogStats& stats() const;

private:
	void rebuildLookups();
	std::vector<ServerVisualConstant> effectValues;
	std::vector<ServerVisualConstant> projectileValues;
	std::unordered_map<std::string, uint32_t> effectIdsByName;
	std::unordered_map<std::string, uint32_t> projectileIdsByName;
	std::unordered_map<uint32_t, std::string> effectNamesById;
	std::unordered_map<uint32_t, std::string> projectileNamesById;
	ServerVisualCatalogStats scanStats;
};

#endif // NEXAMAP_SERVER_VISUAL_CATALOG_H_
