// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MCP_REGISTRY_H_
#define NEXAMAP_MCP_REGISTRY_H_

#include "mcp_common.h"

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mcp {
	struct Tool {
		std::string name;
		std::string description;
		Json inputSchema = Json::object();
		bool mutates = false;
		std::function<Json(const Json&)> handler;
	};

	class ToolRegistry final {
	public:
		static ToolRegistry& Instance();

		void add(Tool tool);
		[[nodiscard]] std::vector<Tool> all() const;
		[[nodiscard]] std::optional<Tool> find(const std::string& name) const;
		void clearForTests();

	private:
		mutable std::mutex mutex_;
		std::vector<Tool> tools_;
	};
}

#endif
