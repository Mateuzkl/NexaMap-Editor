// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp/mcp_common.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
	void Require(bool condition, const std::string& message) {
		if (!condition) {
			std::cerr << "MCP security test failed: " << message << '\n';
			std::exit(1);
		}
	}
}

int main() {
	Require(mcp::IsAllowedOrigin(""), "non-browser clients without Origin should be accepted");
	Require(mcp::IsAllowedOrigin("http://localhost:7331"), "localhost should be accepted");
	Require(mcp::IsAllowedOrigin("https://127.0.0.1"), "IPv4 loopback should be accepted");
	Require(mcp::IsAllowedOrigin("http://[::1]:7331"), "IPv6 loopback should be accepted");
	Require(!mcp::IsAllowedOrigin("http://localhost.evil.example"), "localhost suffix attack should be rejected");
	Require(!mcp::IsAllowedOrigin("http://127.0.0.1.evil.example"), "IPv4 suffix attack should be rejected");
	Require(!mcp::IsAllowedOrigin("https://example.com"), "remote origin should be rejected");
	return 0;
}
