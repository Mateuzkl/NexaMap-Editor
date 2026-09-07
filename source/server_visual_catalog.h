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

class ServerVisualCatalog final {
public:
	static ServerVisualCatalog Build(const ServerWorkspace& workspace);

	[[nodiscard]] const std::vector<ServerVisualConstant>& effects() const;
	[[nodiscard]] const std::vector<ServerVisualConstant>& projectiles() const;
	[[nodiscard]] std::optional<uint32_t> resolve(ServerVisualKind kind, const std::string& value) const;
	[[nodiscard]] std::string nameFor(ServerVisualKind kind, uint32_t id) const;

private:
	std::vector<ServerVisualConstant> effectValues;
	std::vector<ServerVisualConstant> projectileValues;
};

#endif // NEXAMAP_SERVER_VISUAL_CATALOG_H_
