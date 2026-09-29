// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MCP_ITEM_IDENTITY_H_
#define NEXAMAP_MCP_ITEM_IDENTITY_H_

#include "mcp_common.h"
#include "mcp_context.h"

#include <cstdint>

class ItemType;

namespace mcp {
	[[nodiscard]] Json ItemIdentity(uint16_t activeId, ItemIdMode mode, const ItemType& type);
	[[nodiscard]] Json ItemFlags(const ItemType& type, AssetMode mode);
}

#endif
