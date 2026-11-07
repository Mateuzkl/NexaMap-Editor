// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
#else
	#include <arpa/inet.h>
	#include <fcntl.h>
	#include <netinet/in.h>
	#include <poll.h>
	#include <sys/socket.h>
	#include <sys/stat.h>
	#include <unistd.h>
#endif

#include "mcp_server.h"

#include "mcp_common.h"
#include "mcp_tools_brush.h"
#include "mcp_tools_edit.h"
#include "mcp_tools_editor.h"
#include "mcp_tools_entities.h"
#include "mcp_tools_import_export.h"
#include "mcp_tools_map.h"
#include "mcp_tools_orientation.h"
#include "mcp_tools_reference.h"
#include "mcp_tools_render.h"
#include "mcp_tools_region.h"
#include "mcp_tools_search.h"
#include "mcp_tools_validate.h"

#include "../multiplayer_crypto.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstring>
#include <fstream>
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
		using Clock = std::chrono::steady_clock;

		constexpr size_t MaximumHeaderBytes = 64 * 1024;
		constexpr size_t MaximumBodyBytes = 1024 * 1024;
		constexpr size_t MaximumBatchEntries = 32;
		constexpr size_t MaximumResponseBytes = 32 * 1024 * 1024;
		constexpr size_t TokenBytes = 32;
		constexpr size_t WorkerCount = 4;
		// Deadlines cover the whole transfer, so a client trickling bytes cannot extend them.
		constexpr std::chrono::seconds ReadDeadline { 10 };
		constexpr std::chrono::seconds SendDeadline { 30 };
		constexpr std::chrono::minutes BatchDeadline { 3 };
		constexpr std::chrono::milliseconds PollSlice { 100 };
		constexpr std::chrono::seconds RejectionLogInterval { 1 };

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

		bool LastCallWouldBlock() {
#ifdef _WIN32
			return WSAGetLastError() == WSAEWOULDBLOCK;
#else
			return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
		}

		bool SetNonBlocking(NativeSocket socket) {
#ifdef _WIN32
			u_long enabled = 1;
			return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
			const int flags = fcntl(socket, F_GETFL, 0);
			return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
		}

		// Sliced so that a stop request is noticed quickly in every socket wait.
		bool WaitForSocket(NativeSocket socket, short events, Clock::time_point deadline, const std::stop_token& stopToken) {
			while (!stopToken.stop_requested()) {
				const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
				if (remaining.count() <= 0) {
					return false;
				}
				const int slice = static_cast<int>((std::min)(remaining, PollSlice).count());
#ifdef _WIN32
				WSAPOLLFD descriptor { socket, events, 0 };
				const int ready = WSAPoll(&descriptor, 1, slice);
#else
				pollfd descriptor { socket, events, 0 };
				const int ready = poll(&descriptor, 1, slice);
#endif
				if (ready > 0) {
					return true;
				}
				if (ready < 0 && !LastCallWouldBlock()) {
					return false;
				}
			}
			return false;
		}

		bool SendAll(NativeSocket socket, const std::string& data, const std::stop_token& stopToken) {
#ifdef MSG_NOSIGNAL
			constexpr int SendFlags = MSG_NOSIGNAL;
#else
			constexpr int SendFlags = 0;
#endif
			const Clock::time_point deadline = Clock::now() + SendDeadline;
			size_t sent = 0;
			while (sent < data.size()) {
				if (!WaitForSocket(socket, POLLOUT, deadline, stopToken)) {
					return false;
				}
				const int count = send(socket, data.data() + sent, static_cast<int>((std::min<size_t>)(data.size() - sent, (std::numeric_limits<int>::max)())), SendFlags);
				if (count > 0) {
					sent += static_cast<size_t>(count);
				} else if (!LastCallWouldBlock()) {
					return false;
				}
			}
			return true;
		}

		void Respond(NativeSocket socket, const std::stop_token& stopToken, int status, const std::string& reason, const std::string& body, const std::string& extraHeaders = {}) {
			std::ostringstream response;
			response << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
					 << "Content-Type: application/json\r\n"
					 << "Content-Length: " << body.size() << "\r\n"
					 << "Connection: close\r\n"
					 << extraHeaders << "\r\n"
					 << body;
			SendAll(socket, response.str(), stopToken);
		}

		// Returns the bytes read, or 0 when the peer left, the deadline passed or the server stopped.
		size_t ReceiveSome(NativeSocket socket, char* buffer, size_t size, Clock::time_point deadline, const std::stop_token& stopToken) {
			while (WaitForSocket(socket, POLLIN, deadline, stopToken)) {
				const int count = recv(socket, buffer, static_cast<int>(size), 0);
				if (count > 0) {
					return static_cast<size_t>(count);
				}
				if (count == 0 || !LastCallWouldBlock()) {
					return 0;
				}
			}
			return 0;
		}

		std::string GenerateToken() {
			std::vector<uint8_t> bytes;
			while (bytes.size() < TokenBytes) {
				const Multiplayer::Identity random = Multiplayer::randomIdentity();
				bytes.insert(bytes.end(), random.begin(), random.end());
			}
			bytes.resize(TokenBytes);
			return Multiplayer::hex(bytes);
		}

		bool IsTokenText(const std::string& text) {
			return text.size() == TokenBytes * 2 && std::all_of(text.begin(), text.end(), [](unsigned char character) { return std::isxdigit(character); });
		}

		// Hashing first gives secureEqual equal-sized inputs whatever the client sends.
		bool TokensMatch(const std::string& presented, const std::string& expected) {
			const auto digest = [](const std::string& text) {
				return Multiplayer::sha256(std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
			};
			return Multiplayer::secureEqual(digest(presented), digest(expected));
		}

#ifdef _WIN32
		std::string ReadExistingToken(const std::filesystem::path& path) {
			std::ifstream file(path, std::ios::binary);
			std::string token;
			std::getline(file, token);
			return token;
		}

		void WriteNewToken(const std::filesystem::path& path, const std::string& token) {
			// Relies on the per-user ACL of the data directory; there is no 0600 here.
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file << token << '\n';
			if (!file.flush()) {
				throw Error("could not write the MCP token file");
			}
		}
#else
		// Empty means "no usable token": absent, malformed, or readable by someone else.
		std::string ReadExistingToken(const std::filesystem::path& path) {
			const int descriptor = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
			if (descriptor < 0) {
				if (errno == ENOENT) {
					return {};
				}
				throw Error("could not open the MCP token file, or it is a symlink");
			}
			struct stat info {};
			std::string token(TokenBytes * 2, '\0');
			const bool valid = fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == geteuid();
			const ssize_t count = valid ? read(descriptor, token.data(), token.size()) : -1;
			close(descriptor);
			if (!valid) {
				throw Error("the MCP token file is not a regular file owned by this user");
			}
			if (count != static_cast<ssize_t>(token.size()) || (info.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
				return {};
			}
			return token;
		}

		void WriteNewToken(const std::filesystem::path& path, const std::string& token) {
			const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, S_IRUSR | S_IWUSR);
			if (descriptor < 0) {
				throw Error("could not create the MCP token file");
			}
			const std::string text = token + '\n';
			const bool complete = write(descriptor, text.data(), text.size()) == static_cast<ssize_t>(text.size());
			close(descriptor);
			if (!complete) {
				unlink(path.c_str());
				throw Error("could not write the MCP token file");
			}
		}
#endif
	}

	Server& Server::Instance() {
		static Server server;
		return server;
	}

	Server::Server() {
		RegisterOrientationTools();
		RegisterMapTools();
		RegisterReferenceTools();
		RegisterBrushTools();
		RegisterEditTools();
		RegisterEditorTools();
		RegisterEntityTools();
		RegisterImportExportTools();
		RegisterRenderTools();
		RegisterRegionTools();
		RegisterSearchTools();
		RegisterValidationTools();
	}

	Server::~Server() {
		stop();
	}

	std::string Server::LoadOrCreateToken(const std::filesystem::path& path) {
		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);
		if (const std::string existing = ReadExistingToken(path); IsTokenText(existing)) {
			return existing;
		}
		std::filesystem::remove(path, error);
		std::string token = GenerateToken();
		WriteNewToken(path, token);
		return token;
	}

	bool Server::start(uint16_t requestedPort) {
		std::lock_guard lock(lifecycleMutex_);
		if (running_) {
			return true;
		}
		lastError_.clear();
		if (authToken().empty()) {
			lastError_ = "the MCP authentication token is not available";
			return false;
		}
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
		if (bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || listen(socket, 16) != 0 || !SetNonBlocking(socket)) {
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
		workers_.reserve(WorkerCount);
		for (size_t index = 0; index < WorkerCount; ++index) {
			workers_.emplace_back([this, listener = listenSocket_](std::stop_token token) { run(token, listener); });
		}
		log(LogLevel::Info, "MCP server listening on " + endpoint() + (writeAllowed() ? " (writes allowed)" : " (read-only)"));
		return true;
	}

	void Server::stop() {
		// Runs on the GUI thread, so no worker may depend on the GUI to exit: socket waits
		// are sliced and CallJsonOnGui honours the stop token, which keeps this join short.
		std::vector<std::jthread> workers;
		std::intptr_t listener = -1;
		{
			std::lock_guard lock(lifecycleMutex_);
			if (!running_ && workers_.empty()) {
				return;
			}
			running_ = false;
			for (std::jthread& worker : workers_) {
				worker.request_stop();
			}
			workers = std::move(workers_);
			workers_.clear();
			listener = listenSocket_;
			listenSocket_ = -1;
		}
		workers.clear();
		CloseSocket(ToNative(listener));
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

	void Server::setBrowserOriginsAllowed(bool allowed) noexcept {
		browserOriginsAllowed_ = allowed;
	}

	bool Server::browserOriginsAllowed() const noexcept {
		return browserOriginsAllowed_;
	}

	void Server::setAuthToken(std::string token) {
		std::lock_guard lock(tokenMutex_);
		authToken_ = std::move(token);
	}

	std::string Server::authToken() const {
		std::lock_guard lock(tokenMutex_);
		return authToken_;
	}

	bool Server::isAuthorized(const std::string& authorization) const {
		constexpr size_t SchemeSize = 7;
		if (authorization.size() <= SchemeSize || LowerHeaderName(authorization.substr(0, SchemeSize)) != "bearer ") {
			return false;
		}
		const std::string expected = authToken();
		return !expected.empty() && TokensMatch(Trim(authorization.substr(SchemeSize)), expected);
	}

	void Server::setLogCallback(LogCallback callback) {
		std::lock_guard lock(logMutex_);
		logCallback_ = std::move(callback);
	}

	void Server::run(std::stop_token stopToken, std::intptr_t listenSocket) {
		SetRequestStopToken(stopToken);
		const NativeSocket listener = ToNative(listenSocket);
		while (!stopToken.stop_requested()) {
			if (!WaitForSocket(listener, POLLIN, Clock::now() + PollSlice, stopToken)) {
				continue;
			}
			sockaddr_in remote {};
#ifdef _WIN32
			int length = sizeof(remote);
#else
			socklen_t length = sizeof(remote);
#endif
			const NativeSocket client = accept(listener, reinterpret_cast<sockaddr*>(&remote), &length);
			if (client == InvalidSocket) {
				continue;
			}
			if (ntohl(remote.sin_addr.s_addr) != INADDR_LOOPBACK || !SetNonBlocking(client)) {
				CloseSocket(client);
				continue;
			}
			try {
				handleClient(FromNative(client), stopToken);
			} catch (const std::exception& error) {
				log(LogLevel::Error, std::string("MCP request failed: ") + error.what());
				Respond(client, stopToken, 500, "Internal Server Error", "{\"error\":\"internal server error\"}");
			} catch (...) {
				log(LogLevel::Error, "MCP request failed");
				Respond(client, stopToken, 500, "Internal Server Error", "{\"error\":\"internal server error\"}");
			}
			CloseSocket(client);
		}
	}

	void Server::handleClient(std::intptr_t clientSocket, std::stop_token stopToken) {
		const NativeSocket socket = ToNative(clientSocket);
		const Clock::time_point readDeadline = Clock::now() + ReadDeadline;
		const auto respond = [socket, &stopToken](int status, const std::string& reason, const std::string& body, const std::string& extraHeaders = {}) {
			Respond(socket, stopToken, status, reason, body, extraHeaders);
		};

		std::string request;
		std::array<char, 4096> buffer {};
		size_t headerEnd = std::string::npos;
		while ((headerEnd = request.find("\r\n\r\n")) == std::string::npos) {
			const size_t count = ReceiveSome(socket, buffer.data(), buffer.size(), readDeadline, stopToken);
			if (count == 0) {
				return;
			}
			request.append(buffer.data(), count);
			if (request.size() > MaximumHeaderBytes) {
				respond(431, "Request Header Fields Too Large", "{\"error\":\"request headers too large\"}");
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
			if (colon == std::string::npos) {
				continue;
			}
			const std::string name = LowerHeaderName(Trim(line.substr(0, colon)));
			const bool checked = name == "host" || name == "origin" || name == "authorization" || name == "content-type" || name == "content-length";
			if (checked && headers.contains(name)) {
				respond(400, "Bad Request", "{\"error\":\"duplicate header\"}");
				return;
			}
			headers[name] = Trim(line.substr(colon + 1));
		}
		if (target != "/mcp") {
			respond(404, "Not Found", "{\"error\":\"MCP endpoint is /mcp\"}");
			return;
		}
		if (method != "POST") {
			respond(405, "Method Not Allowed", "{\"error\":\"only POST is supported\"}", "Allow: POST\r\n");
			return;
		}
		if (!IsAllowedHost(headers["host"], port_)) {
			respond(403, "Forbidden", "{\"error\":\"host not allowed\"}");
			logRejection("Rejected MCP request with an unexpected Host header");
			return;
		}
		if (!IsAcceptedOrigin(headers["origin"], browserOriginsAllowed_)) {
			respond(403, "Forbidden", "{\"error\":\"origin not allowed\"}");
			logRejection("Rejected MCP request with a browser Origin");
			return;
		}
		if (!isAuthorized(headers["authorization"])) {
			respond(401, "Unauthorized", "{\"error\":\"missing or invalid bearer token\"}", "WWW-Authenticate: Bearer\r\n");
			logRejection("Rejected MCP request without a valid token");
			return;
		}
		if (!IsJsonContentType(headers["content-type"])) {
			respond(415, "Unsupported Media Type", "{\"error\":\"Content-Type must be application/json\"}");
			return;
		}
		size_t contentLength = 0;
		const std::string lengthText = headers["content-length"];
		const auto parsed = std::from_chars(lengthText.data(), lengthText.data() + lengthText.size(), contentLength);
		if (lengthText.empty() || parsed.ec != std::errc() || parsed.ptr != lengthText.data() + lengthText.size()) {
			respond(400, "Bad Request", "{\"error\":\"invalid Content-Length\"}");
			return;
		}
		if (contentLength > MaximumBodyBytes) {
			respond(413, "Payload Too Large", "{\"error\":\"request body too large\"}");
			return;
		}
		std::string body = request.substr(headerEnd + 4);
		while (body.size() < contentLength) {
			const size_t count = ReceiveSome(socket, buffer.data(), (std::min)(buffer.size(), contentLength - body.size()), readDeadline, stopToken);
			if (count == 0) {
				return;
			}
			body.append(buffer.data(), count);
		}
		body.resize(contentLength);

		const auto rpcError = [](const Json& id, int code, const std::string& message) {
			return Json { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", code }, { "message", message } } } };
		};
		if (ExceedsJsonDepth(body)) {
			respond(400, "Bad Request", DumpJson(rpcError(nullptr, -32600, "JSON nesting is too deep")));
			return;
		}
		Json input;
		try {
			input = Json::parse(body);
		} catch (const std::exception& error) {
			respond(400, "Bad Request", DumpJson(rpcError(nullptr, -32700, std::string("invalid JSON: ") + error.what())));
			return;
		}

		const std::string tooLarge = "response size limit exceeded";
		std::string output;
		if (input.is_array() && !input.empty()) {
			if (input.size() > MaximumBatchEntries) {
				respond(400, "Bad Request", DumpJson(rpcError(nullptr, -32600, "batch exceeds " + std::to_string(MaximumBatchEntries) + " entries")));
				return;
			}
			const Clock::time_point batchDeadline = Clock::now() + BatchDeadline;
			std::string entries;
			for (const Json& entry : input) {
				if (stopToken.stop_requested()) {
					return;
				}
				std::optional<Json> response;
				if (entries.size() >= MaximumResponseBytes || Clock::now() >= batchDeadline) {
					// Stop running tools, but still answer every entry that expects one.
					if (entry.is_object() && entry.contains("id") && !entry["id"].is_structured()) {
						response = rpcError(entry["id"], -32000, "batch limit reached; this entry was not run");
					}
				} else {
					response = protocol_.handle(entry);
				}
				if (!response) {
					continue;
				}
				std::string text = DumpJson(*response);
				if (entries.size() + text.size() > MaximumResponseBytes) {
					text = DumpJson(rpcError(response->value("id", Json(nullptr)), -32000, tooLarge));
				}
				entries += (entries.empty() ? "" : ",") + text;
			}
			if (!entries.empty()) {
				output = "[" + entries + "]";
			}
		} else if (const std::optional<Json> response = protocol_.handle(input)) {
			output = DumpJson(*response);
			if (output.size() > MaximumResponseBytes) {
				output = DumpJson(rpcError(response->value("id", Json(nullptr)), -32000, tooLarge));
			}
		}
		const bool hasOutput = !output.empty();
		respond(hasOutput ? 200 : 202, hasOutput ? "OK" : "Accepted", output);
	}

	void Server::logRejection(const std::string& message) {
		// Anything local can trigger these, so they must not be able to flood the panel.
		const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(Clock::now().time_since_epoch()).count();
		int64_t last = lastRejectionLog_;
		if (now - last >= RejectionLogInterval.count() && lastRejectionLog_.compare_exchange_strong(last, now)) {
			log(LogLevel::Error, message);
		}
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
