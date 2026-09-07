#ifndef NEXAMAP_NPC_DEFINITION_CREATION_H_
#define NEXAMAP_NPC_DEFINITION_CREATION_H_

#include "server_content_index.h"

struct NpcCreationRequest {
	std::string name;
	ServerContentFormat format = ServerContentFormat::Unknown;
	std::filesystem::path destinationDirectory;
};

struct NpcCreationResult {
	ServerContentSource source;
};

[[nodiscard]] bool CreateNpcDefinition(const ServerWorkspace& workspace, const ServerContentIndex& index, const NpcCreationRequest& request, NpcCreationResult& result, std::string& error);

#endif
