// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MCP_COMMON_H_
#define NEXAMAP_MCP_COMMON_H_

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

namespace mcp {
	using Json = nlohmann::json;

	class Error final : public std::runtime_error {
	public:
		using std::runtime_error::runtime_error;
	};

	[[nodiscard]] bool IsAllowedOrigin(const std::string& origin);
	[[nodiscard]] Json TextResult(const std::string& text);
	[[nodiscard]] Json StructuredResult(Json value);
	[[nodiscard]] Json ImageResult(const std::string& mimeType, const std::string& base64Data, Json metadata = Json::object());
	[[nodiscard]] Json ErrorResult(const std::string& message);
}

#endif
