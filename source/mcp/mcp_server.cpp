// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <unistd.h>
#endif

#include "mcp_server.h"

#include "mcp_common.h"
#include "mcp_tools_orientation.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace mcp {
	namespace {
#ifdef _WIN32
		using NativeSocket = SOCKET;
		constexpr NativeSocket InvalidSocket = INVALID_SOCKET;
#else
		using NativeSocket = int;
		constexpr NativeSocket InvalidSocket = -1;
#endif
		constexpr size_t MaximumHeaderBytes = 64 * 1024;
		constexpr size_t MaximumBodyBytes = 1024 * 1024;

		NativeSocket ToNative(std::intptr_t socket) {
			return static_cast<NativeSocket>(socket);
		}

		std::intptr_t FromNative(NativeSocket socket) {
			return static_cast<std::intptr_t>(socket);
		}

		void CloseSocket(NativeSocket socket) {
			if (socket == InvalidSocket) {
				return;
			}
#ifdef _WIN32
			shutdown(socket, SD_BOTH);
			closesocket(socket);
#else
			shutdown(socket, SHUT_RDWR);
			close(socket);
#endif
		}

		std::string LowerHeaderName(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
			return value;
		}

		std::string Trim(const std::string& value) {
			const size_t first = value.find_first_not_of(" \t\r\n");
			if (first == std::string::npos) {
				return {};
			}
			return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
		}

		bool SendAll(NativeSocket socket, const std::string& data) {
			size_t sent = 0;
			while (sent < data.size()) {
				const int count = send(socket, data.data() + sent, static_cast<int>((std::min<size_t>)(data.size() - sent, (std::numeric_limits<int>::max)())), 0);
				if (count <= 0) {
					return false;
				}
				sent += static_cast<size_t>(count);
			}
			return true;
		}

		void Respond(NativeSocket socket, int status, const std::string& reason, const std::string& body, const std::string& extraHeaders = {}) {
			std::ostringstream response;
			response << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
					 << "Content-Type: application/json\r\n"
					 << "Content-Length: " << body.size() << "\r\n"
					 << "Connection: close\r\n"
					 << extraHeaders << "\r\n"
					 << body;
			SendAll(socket, response.str());
		}
	}

	Server& Server::Instance() {
		static Server server;
		return server;
	}

	Server::Server() {
		RegisterOrientationTools();
	}

	Server::~Server() {
		stop();
	}

	bool Server::start(uint16_t requestedPort) {
		std::lock_guard lock(lifecycleMutex_);
		if (running_) {
			return true;
		}
		lastError_.clear();
#ifdef _WIN32
		WSADATA data {};
		if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
			lastError_ = "could not initialize Windows sockets";
			return false;
		}
		socketsInitialized_ = true;
#endif
		const NativeSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (socket == InvalidSocket) {
			lastError_ = "could not create the MCP loopback socket";
#ifdef _WIN32
			WSACleanup();
			socketsInitialized_ = false;
#endif
			return false;
		}
		int reuse = 1;
		setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
		sockaddr_in address {};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = htons(requestedPort);
		if (bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || listen(socket, 8) != 0) {
			lastError_ = "could not listen on 127.0.0.1:" + std::to_string(requestedPort) + "; the port may already be in use";
			CloseSocket(socket);
#ifdef _WIN32
			WSACleanup();
			socketsInitialized_ = false;
#endif
			return false;
		}
		if (requestedPort == 0) {
			sockaddr_in bound {};
#ifdef _WIN32
			int length = sizeof(bound);
#else
			socklen_t length = sizeof(bound);
#endif
			if (getsockname(socket, reinterpret_cast<sockaddr*>(&bound), &length) == 0) {
				requestedPort = ntohs(bound.sin_port);
			}
		}
		listenSocket_ = FromNative(socket);
		port_ = requestedPort;
		running_ = true;
		thread_ = std::jthread([this](std::stop_token token) { run(token); });
		log(LogLevel::Info, "MCP server listening on " + endpoint() + (writeAllowed() ? " (writes allowed)" : " (read-only)"));
		return true;
	}

	void Server::stop() {
		std::jthread thread;
		{
			std::lock_guard lock(lifecycleMutex_);
			if (!running_ && !thread_.joinable()) {
				return;
			}
			running_ = false;
			thread_.request_stop();
			CloseSocket(ToNative(listenSocket_));
			listenSocket_ = -1;
			thread = std::move(thread_);
		}
		if (thread.joinable()) {
			thread.join();
		}
#ifdef _WIN32
		if (socketsInitialized_) {
			WSACleanup();
			socketsInitialized_ = false;
		}
#endif
		port_ = 0;
		log(LogLevel::Info, "MCP server stopped");
	}

	bool Server::running() const noexcept {
		return running_;
	}

	uint16_t Server::port() const noexcept {
		return port_;
	}

	std::string Server::endpoint() const {
		return "http://127.0.0.1:" + std::to_string(port_) + "/mcp";
	}

	std::string Server::lastError() const {
		std::lock_guard lock(lifecycleMutex_);
		return lastError_;
	}

	void Server::setWriteAllowed(bool allowed) noexcept {
		protocol_.setWriteAllowed(allowed);
	}

	bool Server::writeAllowed() const noexcept {
		return protocol_.writeAllowed();
	}

	void Server::setLogCallback(LogCallback callback) {
		std::lock_guard lock(logMutex_);
		logCallback_ = std::move(callback);
	}

	void Server::run(std::stop_token stopToken) {
		while (!stopToken.stop_requested() && running_) {
			sockaddr_in remote {};
#ifdef _WIN32
			int length = sizeof(remote);
#else
			socklen_t length = sizeof(remote);
#endif
			const NativeSocket client = accept(ToNative(listenSocket_), reinterpret_cast<sockaddr*>(&remote), &length);
			if (client == InvalidSocket) {
				if (!running_ || stopToken.stop_requested()) {
					break;
				}
				continue;
			}
			if (ntohl(remote.sin_addr.s_addr) != INADDR_LOOPBACK) {
				CloseSocket(client);
				continue;
			}
			handleClient(FromNative(client));
			CloseSocket(client);
		}
	}

	void Server::handleClient(std::intptr_t clientSocket) {
		const NativeSocket socket = ToNative(clientSocket);
#ifdef _WIN32
		DWORD timeout = 5000;
#else
		timeval timeout { 5, 0 };
#endif
		setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
		setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

		std::string request;
		std::array<char, 4096> buffer {};
		size_t headerEnd = std::string::npos;
		while ((headerEnd = request.find("\r\n\r\n")) == std::string::npos) {
			const int count = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
			if (count <= 0) {
				return;
			}
			request.append(buffer.data(), static_cast<size_t>(count));
			if (request.size() > MaximumHeaderBytes) {
				Respond(socket, 431, "Request Header Fields Too Large", "{\"error\":\"request headers too large\"}");
				return;
			}
		}

		std::istringstream headersStream(request.substr(0, headerEnd));
		std::string requestLine;
		std::getline(headersStream, requestLine);
		std::istringstream requestParts(Trim(requestLine));
		std::string method;
		std::string target;
		std::string version;
		requestParts >> method >> target >> version;
		std::unordered_map<std::string, std::string> headers;
		std::string line;
		while (std::getline(headersStream, line)) {
			const size_t colon = line.find(':');
			if (colon != std::string::npos) {
				headers[LowerHeaderName(Trim(line.substr(0, colon)))] = Trim(line.substr(colon + 1));
			}
		}
		if (target != "/mcp") {
			Respond(socket, 404, "Not Found", "{\"error\":\"MCP endpoint is /mcp\"}");
			return;
		}
		if (method != "POST") {
			Respond(socket, 405, "Method Not Allowed", "{\"error\":\"only POST is supported\"}", "Allow: POST\r\n");
			return;
		}
		if (!IsAllowedOrigin(headers["origin"])) {
			Respond(socket, 403, "Forbidden", "{\"error\":\"origin not allowed\"}");
			log(LogLevel::Error, "Rejected MCP request with non-loopback Origin");
			return;
		}
		size_t contentLength = 0;
		const std::string lengthText = headers["content-length"];
		const auto parsed = std::from_chars(lengthText.data(), lengthText.data() + lengthText.size(), contentLength);
		if (lengthText.empty() || parsed.ec != std::errc() || parsed.ptr != lengthText.data() + lengthText.size()) {
			Respond(socket, 400, "Bad Request", "{\"error\":\"invalid Content-Length\"}");
			return;
		}
		if (contentLength > MaximumBodyBytes) {
			Respond(socket, 413, "Payload Too Large", "{\"error\":\"request body too large\"}");
			return;
		}
		std::string body = request.substr(headerEnd + 4);
		while (body.size() < contentLength) {
			const int count = recv(socket, buffer.data(), static_cast<int>((std::min)(buffer.size(), contentLength - body.size())), 0);
			if (count <= 0) {
				return;
			}
			body.append(buffer.data(), static_cast<size_t>(count));
		}
		body.resize(contentLength);

		Json input;
		try {
			input = Json::parse(body);
		} catch (const std::exception& error) {
			const Json response { { "jsonrpc", "2.0" }, { "id", nullptr }, { "error", { { "code", -32700 }, { "message", std::string("invalid JSON: ") + error.what() } } } };
			Respond(socket, 400, "Bad Request", response.dump());
			return;
		}

		Json output;
		bool hasOutput = false;
		if (input.is_array()) {
			output = Json::array();
			for (const Json& entry : input) {
				if (const std::optional<Json> response = protocol_.handle(entry)) {
					output.push_back(*response);
				}
			}
			hasOutput = !output.empty();
		} else if (const std::optional<Json> response = protocol_.handle(input)) {
			output = *response;
			hasOutput = true;
		}
		Respond(socket, hasOutput ? 200 : 202, hasOutput ? "OK" : "Accepted", hasOutput ? output.dump() : std::string());
	}

	void Server::log(LogLevel level, const std::string& message) {
		LogCallback callback;
		{
			std::lock_guard lock(logMutex_);
			callback = logCallback_;
		}
		if (callback) {
			callback(level, message);
		}
	}
}
