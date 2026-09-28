// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_context.h"

#include "mcp_common.h"

#include "../editor.h"
#include "../gui.h"
#include "../map.h"
#include "../workspace_session.h"

namespace mcp {
	EditorContext ResolveEditorContext() {
		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor) {
			throw Error("No map is currently open in NexaMap");
		}
		EditorResourceSessionPtr resources = GetActiveEditorResourceSession();
		if (!resources) {
			throw Error("The active map has no resource session");
		}
		AssetMode assetMode = AssetMode::Unknown;
		switch (g_workspace.getClient().mode) {
			case WorkspaceClientMode::Classic:
				assetMode = AssetMode::ClassicDatSpr;
				break;
			case WorkspaceClientMode::Appearances:
				assetMode = AssetMode::Appearances;
				break;
			case WorkspaceClientMode::None:
				break;
		}
		return { *editor, editor->map, std::move(resources), g_workspace.getEffectiveItemIdMode(), assetMode, editor->map.getSessionId(), g_workspace.getGeneration() };
	}

	const char* AssetModeName(AssetMode mode) {
		switch (mode) {
			case AssetMode::ClassicDatSpr:
				return "classic-dat-spr";
			case AssetMode::Appearances:
				return "appearances";
			case AssetMode::Unknown:
			default:
				return "unknown";
		}
	}

	const char* McpItemIdModeName(ItemIdMode mode) {
		switch (mode) {
			case ItemIdMode::ServerId:
				return "server-id";
			case ItemIdMode::ClientId:
				return "client-id";
			case ItemIdMode::Unknown:
			default:
				return "unknown";
		}
	}

	const char* ServerTypeNameForMcp(ServerType type) {
		switch (type) {
			case ServerType::Tfs:
				return "TFS";
			case ServerType::Canary:
				return "Canary";
			case ServerType::Crystal:
				return "Crystal";
			case ServerType::CanaryCrystal:
				return "Canary/Crystal";
			case ServerType::UnknownGeneric:
			default:
				return "Unknown";
		}
	}
}
