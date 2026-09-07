//////////////////////////////////////////////////////////////////////
// Vocation definitions discovered from the active Server Workspace.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_SERVER_VOCATION_CATALOG_H_
#define NEXAMAP_SERVER_VOCATION_CATALOG_H_

#include "server_workspace.h"

#include <string>
#include <vector>

struct ServerVocation {
	int id = 0;
	int fromId = 0;
	std::string name;
	bool promoted = false;
};

[[nodiscard]] std::vector<ServerVocation> LoadServerVocations(const ServerWorkspace& workspace);

#endif // NEXAMAP_SERVER_VOCATION_CATALOG_H_
