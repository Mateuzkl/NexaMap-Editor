// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_protocol.h"

#include "../version_info.h"

namespace mcp {
	namespace {
		constexpr int InvalidRequest = -32600;
		constexpr int MethodNotFound = -32601;
		constexpr int InvalidParams = -32602;
		constexpr int InternalError = -32603;

		Json RpcError(const Json& id, int code, const std::string& message) {
			return { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", code }, { "message", message } } } };
		}

		Json RpcResult(const Json& id, Json value) {
			return { { "jsonrpc", "2.0" }, { "id", id }, { "result", std::move(value) } };
		}
	}

	Protocol::Protocol(ToolRegistry& registry) :
		registry_(registry) { }

	std::optional<Json> Protocol::handle(const Json& request) const {
		if (!request.is_object() || !request.contains("method") || !request["method"].is_string()) {
			return RpcError(nullptr, InvalidRequest, "expected a JSON-RPC request object");
		}
		if (request.contains("jsonrpc") && request["jsonrpc"] != "2.0") {
			return RpcError(request.value("id", Json(nullptr)), InvalidRequest, "jsonrpc must be 2.0");
		}

		const bool notification = !request.contains("id");
		if (notification) {
			return std::nullopt;
		}
		const Json id = request["id"];
		const std::string method = request["method"].get<std::string>();
		const Json parameters = request.value("params", Json::object());

		try {
			if (method == "initialize") {
				return RpcResult(id, {
										 { "protocolVersion", "2025-06-18" },
										 { "capabilities", { { "tools", { { "listChanged", false } } } } },
										 { "serverInfo", { { "name", "nexamap-editor" }, { "version", NEXAMAP_VERSION_STRING } } },
										 { "instructions", "Inspect workspace_info, map_info and selection_get first. For blank maps use explicit coordinates. Browse tilesets and brushes, edit directly with brush_apply/tile_edit, then render and validate. NexaMap resolves IDs and brushes from the active resource session. Map writes remain disabled until the user enables them in the MCP panel." },
									 });
			}
			if (method == "ping") {
				return RpcResult(id, Json::object());
			}
			if (method == "tools/list") {
				Json tools = Json::array();
				for (const Tool& tool : registry_.all()) {
					tools.push_back({ { "name", tool.name }, { "description", tool.description }, { "inputSchema", tool.inputSchema } });
				}
				return RpcResult(id, { { "tools", std::move(tools) } });
			}
			if (method == "tools/call") {
				return RpcResult(id, callTool(parameters));
			}
			return RpcError(id, MethodNotFound, "unknown method: " + method);
		} catch (const Error& error) {
			return RpcError(id, InvalidParams, error.what());
		} catch (const std::exception& error) {
			return RpcError(id, InternalError, error.what());
		}
	}

	void Protocol::setWriteAllowed(bool allowed) noexcept {
		writeAllowed_ = allowed;
	}

	bool Protocol::writeAllowed() const noexcept {
		return writeAllowed_;
	}

	Json Protocol::callTool(const Json& parameters) const {
		if (!parameters.is_object() || !parameters.contains("name") || !parameters["name"].is_string()) {
			throw Error("tools/call requires a string name");
		}
		const std::string name = parameters["name"].get<std::string>();
		const std::optional<Tool> tool = registry_.find(name);
		if (!tool) {
			throw Error("unknown tool: " + name);
		}
		if (tool->mutates && !writeAllowed_) {
			return ErrorResult("This tool modifies editor state, but MCP write operations are disabled in NexaMap.");
		}
		const Json arguments = parameters.value("arguments", Json::object());
		if (!arguments.is_object()) {
			throw Error("tool arguments must be an object");
		}
		try {
			return tool->handler(arguments);
		} catch (const Error& error) {
			return ErrorResult(error.what());
		} catch (const std::exception& error) {
			return ErrorResult(std::string("internal tool error: ") + error.what());
		}
	}
}
