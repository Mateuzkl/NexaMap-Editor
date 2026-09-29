// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MCP_SERVER_H_
#define NEXAMAP_MCP_SERVER_H_

#include "mcp_protocol.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace mcp {
	class Server final {
	public:
		enum class LogLevel {
			Info,
			Error,
		};
		using LogCallback = std::function<void(LogLevel, const std::string&)>;

		static Server& Instance();
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
		void setLogCallback(LogCallback callback);

	private:
		Server();
		void run(std::stop_token stopToken);
		void handleClient(std::intptr_t clientSocket);
		void log(LogLevel level, const std::string& message);

		Protocol protocol_;
		mutable std::mutex lifecycleMutex_;
		mutable std::mutex logMutex_;
		std::jthread thread_;
		std::atomic<bool> running_ { false };
		std::atomic<uint16_t> port_ { 0 };
		std::intptr_t listenSocket_ = -1;
		std::string lastError_;
		LogCallback logCallback_;
		bool socketsInitialized_ = false;
	};
}

#endif
