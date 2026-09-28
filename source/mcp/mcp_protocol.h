// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MCP_PROTOCOL_H_
#define NEXAMAP_MCP_PROTOCOL_H_

#include "mcp_registry.h"

#include <atomic>
#include <optional>

namespace mcp {
	class Protocol final {
	public:
		explicit Protocol(ToolRegistry& registry = ToolRegistry::Instance());

		[[nodiscard]] std::optional<Json> handle(const Json& request) const;
		void setWriteAllowed(bool allowed) noexcept;
		[[nodiscard]] bool writeAllowed() const noexcept;

	private:
		[[nodiscard]] Json callTool(const Json& parameters) const;

		ToolRegistry& registry_;
		std::atomic<bool> writeAllowed_ { false };
	};
}

#endif
