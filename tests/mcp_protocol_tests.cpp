// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp/mcp_protocol.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
	void Require(bool condition, const std::string& message) {
		if (!condition) {
			std::cerr << "MCP protocol test failed: " << message << '\n';
			std::exit(1);
		}
	}
}

int main() {
	mcp::ToolRegistry registry;
	registry.add({
		"inspect",
		"Inspect the active workspace.",
		{ { "type", "object" } },
		false,
		[](const mcp::Json&) { return mcp::StructuredResult({ { "ok", true } }); },
	});
	registry.add({
		"mutate",
		"Change editor state.",
		{ { "type", "object" } },
		true,
		[](const mcp::Json&) { return mcp::TextResult("changed"); },
	});

	mcp::Protocol protocol(registry);
	const auto initialize = protocol.handle({ { "jsonrpc", "2.0" }, { "id", 1 }, { "method", "initialize" } });
	Require(initialize.has_value(), "initialize should respond");
	Require((*initialize)["result"]["protocolVersion"] == "2025-06-18", "protocol version should be advertised");

	const auto listed = protocol.handle({ { "jsonrpc", "2.0" }, { "id", 2 }, { "method", "tools/list" } });
	Require(listed.has_value() && (*listed)["result"]["tools"].size() == 2, "tools/list should return registered tools");

	const auto inspected = protocol.handle({
		{ "jsonrpc", "2.0" },
		{ "id", 3 },
		{ "method", "tools/call" },
		{ "params", { { "name", "inspect" }, { "arguments", mcp::Json::object() } } },
	});
	Require(inspected.has_value() && (*inspected)["result"]["structuredContent"]["ok"], "read-only tool should run");

	const auto denied = protocol.handle({
		{ "jsonrpc", "2.0" },
		{ "id", 4 },
		{ "method", "tools/call" },
		{ "params", { { "name", "mutate" } } },
	});
	Require(denied.has_value() && (*denied)["result"]["isError"], "write tool should be denied by default");

	protocol.setWriteAllowed(true);
	const auto allowed = protocol.handle({
		{ "jsonrpc", "2.0" },
		{ "id", 5 },
		{ "method", "tools/call" },
		{ "params", { { "name", "mutate" } } },
	});
	Require(allowed.has_value() && !(*allowed)["result"].value("isError", false), "write tool should run after opt-in");

	const auto unknown = protocol.handle({ { "jsonrpc", "2.0" }, { "id", 6 }, { "method", "missing" } });
	Require(unknown.has_value() && (*unknown)["error"]["code"] == -32601, "unknown method should use JSON-RPC error");

	const auto notification = protocol.handle({ { "jsonrpc", "2.0" }, { "method", "notifications/initialized" } });
	Require(!notification.has_value(), "notification should not produce a response");

	return 0;
}
