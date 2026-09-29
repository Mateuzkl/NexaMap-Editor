// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_registry.h"

#include <algorithm>

namespace mcp {
	ToolRegistry& ToolRegistry::Instance() {
		static ToolRegistry registry;
		return registry;
	}

	void ToolRegistry::add(Tool tool) {
		if (tool.name.empty() || !tool.handler) {
			throw Error("MCP tools require a name and handler");
		}
		std::lock_guard lock(mutex_);
		if (std::any_of(tools_.begin(), tools_.end(), [&tool](const Tool& existing) { return existing.name == tool.name; })) {
			throw Error("duplicate MCP tool: " + tool.name);
		}
		tools_.push_back(std::move(tool));
	}

	std::vector<Tool> ToolRegistry::all() const {
		std::lock_guard lock(mutex_);
		return tools_;
	}

	std::optional<Tool> ToolRegistry::find(const std::string& name) const {
		std::lock_guard lock(mutex_);
		const auto found = std::find_if(tools_.begin(), tools_.end(), [&name](const Tool& tool) { return tool.name == name; });
		return found == tools_.end() ? std::nullopt : std::optional<Tool>(*found);
	}

	void ToolRegistry::clearForTests() {
		std::lock_guard lock(mutex_);
		tools_.clear();
	}
}
