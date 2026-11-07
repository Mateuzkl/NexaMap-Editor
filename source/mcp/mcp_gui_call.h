// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MCP_GUI_CALL_H_
#define NEXAMAP_MCP_GUI_CALL_H_

#include "mcp_common.h"

#include <chrono>
#include <functional>
#include <string>

namespace mcp {
	// Tracks modal dialogs; call once on the GUI thread before the server starts.
	void InstallGuiCallGuards();
	[[nodiscard]] bool IsModalDialogOpen();
	[[nodiscard]] Json CallJsonOnGui(std::function<Json()> function, std::chrono::milliseconds timeout = std::chrono::seconds(10));
	// Blocks the calling server thread until the user answers a native dialog; throws Error unless approved.
	void RequireUserConfirmation(const std::string& description);
}

#endif
