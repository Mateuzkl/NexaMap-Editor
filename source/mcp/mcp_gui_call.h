// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MCP_GUI_CALL_H_
#define NEXAMAP_MCP_GUI_CALL_H_

#include "mcp_common.h"

#include <chrono>
#include <functional>

namespace mcp {
	[[nodiscard]] Json CallJsonOnGui(std::function<Json()> function, std::chrono::milliseconds timeout = std::chrono::seconds(10));
}

#endif
