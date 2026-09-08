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
#include <unordered_map>
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

struct SpellAreaResolverStats {
	std::size_t filesDiscovered = 0;
	std::size_t filesRead = 0;
	std::size_t definitionsParsed = 0;
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
	[[nodiscard]] const SpellAreaResolverStats& stats() const;
	[[nodiscard]] const std::vector<Definition>& allDefinitions() const;
	[[nodiscard]] static std::vector<Definition> ParseSource(const std::filesystem::path& path, std::string_view text);
	[[nodiscard]] static std::vector<MonsterAreaTile> ParseLiteralMatrix(std::string_view expression);

private:
	std::vector<Definition> definitions;
	std::unordered_map<std::string, std::vector<std::size_t>> definitionsByName;
	SpellAreaResolverStats scanStats;
};

#endif // NEXAMAP_SPELL_AREA_RESOLVER_H_
