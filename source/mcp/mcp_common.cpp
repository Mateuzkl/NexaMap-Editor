// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_common.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace mcp {
	namespace {
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

	Json TextResult(const std::string& text) {
		return { { "content", Json::array({ { { "type", "text" }, { "text", text } } }) } };
	}

	Json StructuredResult(Json value) {
		Json result = TextResult(value.dump(2));
		result["structuredContent"] = std::move(value);
		return result;
	}

	Json ErrorResult(const std::string& message) {
		Json result = TextResult(message);
		result["isError"] = true;
		return result;
	}
}
