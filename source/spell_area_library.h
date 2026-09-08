//////////////////////////////////////////////////////////////////////
// Safe, source-preserving creation of reusable Lua combat areas.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_SPELL_AREA_LIBRARY_H_
#define NEXAMAP_SPELL_AREA_LIBRARY_H_

#include "server_workspace.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

class SpellAreaResolver;

struct EditableSpellArea {
	std::string name;
	std::size_t width = 9;
	std::size_t height = 9;
	std::vector<uint8_t> cells;
};

struct SpellAreaLibraryTarget {
	std::filesystem::path path;
	std::filesystem::path relativePath;
	ResourceFingerprint fingerprint;
	std::size_t existingDefinitions = 0;
};

struct SpellAreaLibrarySaveResult {
	std::filesystem::path path;
	std::string name;
	std::size_t affectedTiles = 0;
};

class SpellAreaLibrary {
public:
	[[nodiscard]] static std::vector<SpellAreaLibraryTarget> DiscoverTargets(
		const ServerWorkspace& workspace,
		const SpellAreaResolver& resolver
	);
	[[nodiscard]] static bool Validate(const EditableSpellArea& area, std::string& error);
	[[nodiscard]] static std::string Serialize(const EditableSpellArea& area, std::string_view newline = "\n");
	[[nodiscard]] static bool Save(
		const ServerWorkspace& workspace,
		const SpellAreaResolver& resolver,
		const SpellAreaLibraryTarget& target,
		const EditableSpellArea& area,
		SpellAreaLibrarySaveResult& result,
		std::string& error
	);
};

#endif // NEXAMAP_SPELL_AREA_LIBRARY_H_
