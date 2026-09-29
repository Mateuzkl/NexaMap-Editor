// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MCP_CONTEXT_H_
#define NEXAMAP_MCP_CONTEXT_H_

#include "../editor_resource_session.h"
#include "../server_workspace.h"
#include "../session_id.h"

#include <cstdint>

class Editor;
class Map;

namespace mcp {
	enum class AssetMode {
		ClassicDatSpr,
		Appearances,
		Unknown,
	};

	struct EditorContext {
		Editor& editor;
		Map& map;
		EditorResourceSessionPtr resources;
		ItemIdMode itemIdMode;
		AssetMode assetMode;
		SessionId mapSessionId;
		uint64_t workspaceGeneration;
	};

	[[nodiscard]] EditorContext ResolveEditorContext();
	[[nodiscard]] const char* AssetModeName(AssetMode mode);
	[[nodiscard]] const char* McpItemIdModeName(ItemIdMode mode);
	[[nodiscard]] const char* ServerTypeNameForMcp(ServerType type);
}

#endif
