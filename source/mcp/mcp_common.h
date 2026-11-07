// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MCP_COMMON_H_
#define NEXAMAP_MCP_COMMON_H_

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>

namespace mcp {
	using Json = nlohmann::json;

	class Error final : public std::runtime_error {
	public:
		using std::runtime_error::runtime_error;
	};

	constexpr size_t MaximumJsonDepth = 64;

	[[nodiscard]] bool IsAllowedOrigin(const std::string& origin);
	// Browsers are refused outright unless the user opted in from the MCP panel.
	[[nodiscard]] bool IsAcceptedOrigin(const std::string& origin, bool allowLoopbackBrowsers);
	[[nodiscard]] bool IsAllowedHost(const std::string& host, uint16_t port);
	[[nodiscard]] bool IsJsonContentType(const std::string& contentType);
	// SAX pre-pass: building or copying a DOM recurses, so depth must be known first.
	[[nodiscard]] bool ExceedsJsonDepth(const std::string& text, size_t maximumDepth = MaximumJsonDepth);
	// dump() throws on non-UTF-8 strings, and item or map names are raw file bytes.
	[[nodiscard]] std::string DumpJson(const Json& value, int indent = -1);
	[[nodiscard]] bool IsSafeExportName(const std::string& name);
	[[nodiscard]] bool IsUncPath(const std::string& path);
	// Reads only the PNG or BMP header; nullopt means an unsupported or truncated image.
	[[nodiscard]] std::optional<std::pair<uint32_t, uint32_t>> ProbeImageSize(const std::string& bytes);
	[[nodiscard]] std::string SanitizeDialogText(const std::string& text, size_t maximumBytes = 512);
	// Both throw Error rather than following a symlink out of the export root.
	void PrepareExportRoot(const std::filesystem::path& root);
	void RequireFreeExportTarget(const std::filesystem::path& target);
	// The token of the server worker on this thread, so GUI waits end when the server stops.
	void SetRequestStopToken(std::stop_token token);
	[[nodiscard]] std::stop_token RequestStopToken();
	[[nodiscard]] Json TextResult(const std::string& text);
	[[nodiscard]] Json StructuredResult(Json value);
	[[nodiscard]] Json ImageResult(const std::string& mimeType, const std::string& base64Data, Json metadata = Json::object());
	[[nodiscard]] Json ErrorResult(const std::string& message);
}

#endif
