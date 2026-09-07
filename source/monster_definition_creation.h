//////////////////////////////////////////////////////////////////////
// Safe creation of new XML and Lua monster definitions.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_MONSTER_DEFINITION_CREATION_H_
#define NEXAMAP_MONSTER_DEFINITION_CREATION_H_

#include "server_content_index.h"

#include <filesystem>
#include <string>

struct MonsterCreationRequest {
	std::string name;
	ServerContentFormat format = ServerContentFormat::Unknown;
	std::filesystem::path destinationDirectory;
};

struct MonsterCreationResult {
	ServerContentSource source;
	std::string provider;
};

enum class MonsterCreationProvider : uint8_t {
	TfsXml,
	TfsLua,
	CanaryLua,
};

[[nodiscard]] std::string MakeMonsterFileStem(const std::string& name);
[[nodiscard]] MonsterCreationProvider DetectMonsterCreationProvider(
	const ServerWorkspace& workspace,
	const ServerContentIndex& index,
	ServerContentFormat format
);
[[nodiscard]] const char* MonsterCreationProviderName(MonsterCreationProvider provider);
[[nodiscard]] bool CreateMonsterDefinition(
	const ServerWorkspace& workspace,
	const ServerContentIndex& index,
	const MonsterCreationRequest& request,
	MonsterCreationResult& result,
	std::string& error
);

#endif // NEXAMAP_MONSTER_DEFINITION_CREATION_H_
