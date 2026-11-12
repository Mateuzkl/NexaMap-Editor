// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_common.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace mcp {
	namespace {
		thread_local std::stop_token requestStopToken;

		// Only counts nesting; values are discarded, so nothing recursive is built.
		class DepthLimitSax final : public nlohmann::json_sax<Json> {
		public:
			explicit DepthLimitSax(size_t maximumDepth) :
				maximumDepth_(maximumDepth) { }

			bool null() override {
				return true;
			}
			bool boolean(bool) override {
				return true;
			}
			bool number_integer(number_integer_t) override {
				return true;
			}
			bool number_unsigned(number_unsigned_t) override {
				return true;
			}
			bool number_float(number_float_t, const string_t&) override {
				return true;
			}
			bool string(string_t&) override {
				return true;
			}
			bool binary(binary_t&) override {
				return true;
			}
			bool key(string_t&) override {
				return true;
			}
			bool start_object(std::size_t) override {
				return enter();
			}
			bool end_object() override {
				--depth_;
				return true;
			}
			bool start_array(std::size_t) override {
				return enter();
			}
			bool end_array() override {
				--depth_;
				return true;
			}
			bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override {
				return false;
			}

			[[nodiscard]] bool exceeded() const noexcept {
				return exceeded_;
			}

		private:
			bool enter() {
				exceeded_ = ++depth_ > maximumDepth_;
				return !exceeded_;
			}

			size_t maximumDepth_ = 0;
			size_t depth_ = 0;
			bool exceeded_ = false;
		};

		uint32_t ReadLittleEndian32(const std::string& bytes, size_t offset) {
			const auto byte = [&bytes, offset](size_t index) { return static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + index])); };
			return byte(0) | (byte(1) << 8) | (byte(2) << 16) | (byte(3) << 24);
		}

		uint32_t ReadBigEndian32(const std::string& bytes, size_t offset) {
			const auto byte = [&bytes, offset](size_t index) { return static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + index])); };
			return (byte(0) << 24) | (byte(1) << 16) | (byte(2) << 8) | byte(3);
		}

		std::string LowerOrigin(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
				return static_cast<char>(std::tolower(character));
			});
			return value;
		}
	}

	bool IsAllowedOrigin(const std::string& origin) {
		if (origin.empty()) {
			return true;
		}
		const std::string value = LowerOrigin(origin);
		constexpr std::array prefixes {
			"http://localhost",
			"https://localhost",
			"http://127.0.0.1",
			"https://127.0.0.1",
			"http://[::1]",
			"https://[::1]",
		};
		for (const char* prefix : prefixes) {
			const std::string candidate(prefix);
			if (value.rfind(candidate, 0) == 0 && (value.size() == candidate.size() || value[candidate.size()] == ':' || value[candidate.size()] == '/')) {
				return true;
			}
		}
		return false;
	}

	bool IsAcceptedOrigin(const std::string& origin, bool allowLoopbackBrowsers) {
		return origin.empty() || (allowLoopbackBrowsers && IsAllowedOrigin(origin));
	}

	bool IsAllowedHost(const std::string& host, uint16_t port) {
		const std::string value = LowerOrigin(host);
		const std::string suffix = ":" + std::to_string(port);
		return value == "127.0.0.1" + suffix || value == "localhost" + suffix || value == "[::1]" + suffix;
	}

	bool IsJsonContentType(const std::string& contentType) {
		std::string value = LowerOrigin(contentType.substr(0, contentType.find(';')));
		const size_t last = value.find_last_not_of(" \t");
		value.erase(last == std::string::npos ? 0 : last + 1);
		return value == "application/json";
	}

	bool ExceedsJsonDepth(const std::string& text, size_t maximumDepth) {
		DepthLimitSax limiter(maximumDepth);
		Json::sax_parse(text, &limiter);
		return limiter.exceeded();
	}

	std::string DumpJson(const Json& value, int indent) {
		return value.dump(indent, ' ', false, Json::error_handler_t::replace);
	}

	bool IsSafeExportName(const std::string& name) {
		if (name.empty() || name.size() > 128 || name.front() == '.' || name.front() == '-') {
			return false;
		}
		return std::all_of(name.begin(), name.end(), [](unsigned char character) {
			return std::isalnum(character) || character == '.' || character == '_' || character == '-';
		});
	}

	bool IsUncPath(const std::string& path) {
		const auto isSeparator = [](char character) { return character == '\\' || character == '/'; };
		return path.size() >= 2 && isSeparator(path[0]) && isSeparator(path[1]);
	}

	std::optional<std::pair<uint32_t, uint32_t>> ProbeImageSize(const std::string& bytes) {
		static constexpr char PngMagic[] = "\x89PNG\r\n\x1a\n";
		constexpr size_t PngMagicSize = sizeof(PngMagic) - 1;
		if (bytes.size() >= PngMagicSize + 16 && std::memcmp(bytes.data(), PngMagic, PngMagicSize) == 0 && bytes.compare(12, 4, "IHDR") == 0) {
			return std::pair(ReadBigEndian32(bytes, 16), ReadBigEndian32(bytes, 20));
		}
		if (bytes.size() >= 26 && bytes.compare(0, 2, "BM") == 0) {
			// BMP stores height as a signed value: negative means a top-down image.
			const int32_t height = static_cast<int32_t>(ReadLittleEndian32(bytes, 22));
			const uint32_t magnitude = height < 0 ? 0U - static_cast<uint32_t>(height) : static_cast<uint32_t>(height);
			return std::pair(ReadLittleEndian32(bytes, 18), magnitude);
		}
		return std::nullopt;
	}

	std::string SanitizeDialogText(const std::string& text, size_t maximumBytes) {
		// Paths and map names reach this from the client; they must not fake dialog lines.
		std::string value = text.substr(0, maximumBytes);
		std::replace_if(
			value.begin(), value.end(), [](unsigned char character) { return character < 0x20 || character == 0x7F; }, ' '
		);
		return value;
	}

	void PrepareExportRoot(const std::filesystem::path& root) {
		std::error_code error;
		std::filesystem::create_directories(root.parent_path(), error);
		std::filesystem::create_directory(root, error);
		if (std::filesystem::symlink_status(root, error).type() != std::filesystem::file_type::directory) {
			throw Error("the MCP export directory is missing or is a link");
		}
		std::filesystem::permissions(root, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, error);
	}

	void RequireFreeExportTarget(const std::filesystem::path& target) {
		std::error_code error;
		// symlink_status reports the link itself, so a dangling link counts as taken too.
		if (std::filesystem::symlink_status(target, error).type() != std::filesystem::file_type::not_found) {
			throw Error("export target already exists: " + target.filename().string());
		}
	}

	void SetRequestStopToken(std::stop_token token) {
		requestStopToken = std::move(token);
	}

	std::stop_token RequestStopToken() {
		return requestStopToken;
	}

	Json TextResult(const std::string& text) {
		return { { "content", Json::array({ { { "type", "text" }, { "text", text } } }) } };
	}

	Json StructuredResult(Json value) {
		Json result = TextResult(DumpJson(value, 2));
		result["structuredContent"] = std::move(value);
		return result;
	}

	Json ImageResult(const std::string& mimeType, const std::string& base64Data, Json metadata) {
		Json result {
			{ "content", Json::array({
							 { { "type", "text" }, { "text", DumpJson(metadata, 2) } },
							 { { "type", "image" }, { "mimeType", mimeType }, { "data", base64Data } },
						 }) },
			{ "structuredContent", std::move(metadata) },
		};
		return result;
	}

	Json ErrorResult(const std::string& message) {
		Json result = TextResult(message);
		result["isError"] = true;
		return result;
	}
}
