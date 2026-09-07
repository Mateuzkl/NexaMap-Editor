//////////////////////////////////////////////////////////////////////
// Workspace-specific resolution of server Lua combat area constants.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_SPELL_AREA_RESOLVER_H_
#define NEXAMAP_SPELL_AREA_RESOLVER_H_

#include "monster_spell_area.h"
#include "server_workspace.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

enum class SpellAreaResolutionState : uint8_t {
	Single = 0,
	Resolved,
	Unresolved,
};

struct SpellAreaResolution {
	SpellAreaResolutionState state = SpellAreaResolutionState::Unresolved;
	std::vector<MonsterAreaTile> tiles;
	std::string description;
	std::filesystem::path sourcePath;
};

class SpellAreaResolver {
public:
	struct Definition {
		std::string name;
		std::string expression;
		std::filesystem::path path;
	};

	explicit SpellAreaResolver(const ServerWorkspace& workspace);

	[[nodiscard]] SpellAreaResolution resolve(std::string_view expression) const;

private:
	std::vector<Definition> definitions;
};

#endif // NEXAMAP_SPELL_AREA_RESOLVER_H_
