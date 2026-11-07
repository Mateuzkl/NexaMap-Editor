// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MCP_SERVER_H_
#define NEXAMAP_MCP_SERVER_H_

#include "mcp_protocol.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace mcp {
	class Server final {
	public:
		enum class LogLevel {
			Info,
			Error,
		};
		using LogCallback = std::function<void(LogLevel, const std::string&)>;

		static Server& Instance();
		// Returns the token stored at path, replacing a malformed one and creating it if needed.
		// A file others can read is replaced too, so a leaked token stops working.
		[[nodiscard]] static std::string LoadOrCreateToken(const std::filesystem::path& path);
		~Server();

		Server(const Server&) = delete;
		Server& operator=(const Server&) = delete;

		bool start(uint16_t port);
		void stop();
		[[nodiscard]] bool running() const noexcept;
		[[nodiscard]] uint16_t port() const noexcept;
		[[nodiscard]] std::string endpoint() const;
		[[nodiscard]] std::string lastError() const;

		void setWriteAllowed(bool allowed) noexcept;
		[[nodiscard]] bool writeAllowed() const noexcept;
		void setBrowserOriginsAllowed(bool allowed) noexcept;
		[[nodiscard]] bool browserOriginsAllowed() const noexcept;
		// start() fails closed until a token has been set.
		void setAuthToken(std::string token);
		[[nodiscard]] std::string authToken() const;
		void setLogCallback(LogCallback callback);

	private:
		Server();
		void run(std::stop_token stopToken, std::intptr_t listenSocket);
		void handleClient(std::intptr_t clientSocket, std::stop_token stopToken);
		[[nodiscard]] bool isAuthorized(const std::string& authorization) const;
		void log(LogLevel level, const std::string& message);
		void logRejection(const std::string& message);

		Protocol protocol_;
		mutable std::mutex lifecycleMutex_;
		mutable std::mutex logMutex_;
		mutable std::mutex tokenMutex_;
		std::vector<std::jthread> workers_;
		std::atomic<bool> running_ { false };
		std::atomic<uint16_t> port_ { 0 };
		std::atomic<bool> browserOriginsAllowed_ { false };
		std::atomic<int64_t> lastRejectionLog_ { 0 };
		std::intptr_t listenSocket_ = -1;
		std::string lastError_;
		std::string authToken_;
		LogCallback logCallback_;
		bool socketsInitialized_ = false;
	};
}

#endif
