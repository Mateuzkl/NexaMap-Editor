// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <sys/stat.h>
	#include <unistd.h>
#endif

#include "mcp/mcp_common.h"
#include "mcp/mcp_registry.h"
#include "mcp/mcp_server.h"
#include "mcp/mcp_tools_brush.h"
#include "mcp/mcp_tools_edit.h"
#include "mcp/mcp_tools_editor.h"
#include "mcp/mcp_tools_entities.h"
#include "mcp/mcp_tools_import_export.h"
#include "mcp/mcp_tools_map.h"
#include "mcp/mcp_tools_orientation.h"
#include "mcp/mcp_tools_reference.h"
#include "mcp/mcp_tools_region.h"
#include "mcp/mcp_tools_render.h"
#include "mcp/mcp_tools_search.h"
#include "mcp/mcp_tools_validate.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

// The server only needs these to exist; the real tools require the wx editor.
namespace mcp {
	void RegisterOrientationTools() { }
	void RegisterMapTools() { }
	void RegisterBrushTools() { }
	void RegisterEditTools() { }
	void RegisterEditorTools() { }
	void RegisterEntityTools() { }
	void RegisterImportExportTools() { }
	void RegisterRenderTools() { }
	void RegisterRegionTools() { }
	void RegisterSearchTools() { }
	void RegisterValidationTools() { }

	void RegisterReferenceTools() {
		ToolRegistry& registry = ToolRegistry::Instance();
		registry.add({ "fake_raw_latin1", "Returns bytes that are not UTF-8.", Json::object(), false, [](const Json&) {
						  return Json { { "name", std::string("po\xE7\xE3o") } };
					  } });
		registry.add({ "fake_mutate", "Requires write permission.", Json::object(), true, [](const Json&) {
						  return TextResult("changed");
					  } });
		registry.add({ "fake_large", "Returns 20 MiB.", Json::object(), false, [](const Json&) {
						  return TextResult(std::string(20 * 1024 * 1024, 'x'));
					  } });
	}
}

namespace {
#ifdef _WIN32
	using TestSocket = SOCKET;
#else
	using TestSocket = int;
#endif

	void Require(bool condition, const std::string& message) {
		if (!condition) {
			std::cerr << "MCP security test failed: " << message << '\n';
			std::exit(1);
		}
	}

	void CloseTestSocket(TestSocket socket) {
#ifdef _WIN32
		closesocket(socket);
#else
		close(socket);
#endif
	}

	TestSocket Connect(uint16_t port) {
		const TestSocket connection = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		sockaddr_in address {};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = htons(port);
		Require(connect(connection, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0, "test client could not connect");
		return connection;
	}

	void SendText(TestSocket connection, const std::string& text) {
		// The server may answer and close before reading everything; the reply still counts.
		size_t sent = 0;
		while (sent < text.size()) {
			const int count = send(connection, text.data() + sent, static_cast<int>(text.size() - sent), 0);
			if (count <= 0) {
				return;
			}
			sent += static_cast<size_t>(count);
		}
	}

	std::string ReadAll(TestSocket connection) {
		std::string response;
		char buffer[65536];
		for (int count = recv(connection, buffer, sizeof(buffer), 0); count > 0; count = recv(connection, buffer, sizeof(buffer), 0)) {
			response.append(buffer, static_cast<size_t>(count));
		}
		return response;
	}

	struct Exchange {
		uint16_t port = 0;
		std::string token;

		[[nodiscard]] std::string goodHost() const {
			return "Host: 127.0.0.1:" + std::to_string(port) + "\r\n";
		}

		[[nodiscard]] std::string goodAuth() const {
			return "Authorization: Bearer " + token + "\r\n";
		}

		[[nodiscard]] std::string request(const std::string& text) const {
			const TestSocket connection = Connect(port);
			SendText(connection, text);
			std::string response = ReadAll(connection);
			CloseTestSocket(connection);
			return response;
		}

		[[nodiscard]] std::string raw(const std::string& headers, const std::string& body) const {
			return request("POST /mcp HTTP/1.1\r\n" + headers + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
		}

		[[nodiscard]] std::string post(const std::string& body, const std::string& extraHeaders = {}) const {
			return raw(goodHost() + goodAuth() + "Content-Type: application/json\r\n" + extraHeaders, body);
		}
	};

	bool HasStatus(const std::string& response, int status) {
		return response.rfind("HTTP/1.1 " + std::to_string(status) + " ", 0) == 0;
	}

	std::string Body(const std::string& response) {
		const size_t end = response.find("\r\n\r\n");
		return end == std::string::npos ? std::string() : response.substr(end + 4);
	}

	std::string Repeat(const std::string& text, size_t count) {
		std::string result;
		result.reserve(text.size() * count);
		for (size_t index = 0; index < count; ++index) {
			result += text;
		}
		return result;
	}

	std::string Batch(size_t entries) {
		std::string body = "[";
		for (size_t index = 0; index < entries; ++index) {
			body += (index ? "," : "") + std::string("{\"id\":") + std::to_string(index) + ",\"method\":\"ping\"}";
		}
		return body + "]";
	}

	const std::string Ping = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}";

	void RequireAlive(const Exchange& client, const std::string& context) {
		Require(HasStatus(client.post(Ping), 200), "server should still answer after " + context);
	}

	void TestOrigins() {
		Require(mcp::IsAllowedOrigin(""), "non-browser clients without Origin should be accepted");
		Require(mcp::IsAllowedOrigin("http://localhost:7331"), "localhost should be accepted");
		Require(mcp::IsAllowedOrigin("https://127.0.0.1"), "IPv4 loopback should be accepted");
		Require(mcp::IsAllowedOrigin("http://[::1]:7331"), "IPv6 loopback should be accepted");
		Require(!mcp::IsAllowedOrigin("http://localhost.evil.example"), "localhost suffix attack should be rejected");
		Require(!mcp::IsAllowedOrigin("http://127.0.0.1.evil.example"), "IPv4 suffix attack should be rejected");
		Require(!mcp::IsAllowedOrigin("https://example.com"), "remote origin should be rejected");

		Require(mcp::IsAcceptedOrigin("", false), "requests without Origin are not from a browser");
		Require(!mcp::IsAcceptedOrigin("http://localhost:3000", false), "browser pages need the opt-in");
		Require(mcp::IsAcceptedOrigin("http://localhost:3000", true), "the opt-in should admit loopback pages");
		Require(!mcp::IsAcceptedOrigin("https://example.com", true), "the opt-in must not admit remote pages");
		Require(!mcp::IsAcceptedOrigin("null", true), "the opt-in must not admit opaque origins");
	}

	void TestHeaderHelpers() {
		Require(mcp::IsAllowedHost("127.0.0.1:7331", 7331), "IPv4 loopback Host should be accepted");
		Require(mcp::IsAllowedHost("LOCALHOST:7331", 7331), "Host comparison should ignore case");
		Require(mcp::IsAllowedHost("[::1]:7331", 7331), "IPv6 loopback Host should be accepted");
		Require(!mcp::IsAllowedHost("127.0.0.1:7332", 7331), "wrong Host port should be rejected");
		Require(!mcp::IsAllowedHost("attacker.example:7331", 7331), "rebinding Host should be rejected");
		Require(!mcp::IsAllowedHost("localhost.attacker.example:7331", 7331), "Host suffix attack should be rejected");
		Require(!mcp::IsAllowedHost("", 7331), "missing Host should be rejected");

		Require(mcp::IsJsonContentType("application/json"), "JSON content type should be accepted");
		Require(mcp::IsJsonContentType("Application/JSON ; charset=utf-8"), "content type parameters should be accepted");
		Require(!mcp::IsJsonContentType("text/plain"), "text/plain is a CORS-safelisted type and must be rejected");
		Require(!mcp::IsJsonContentType("application/json-patch"), "content type prefix must not match");
		Require(!mcp::IsJsonContentType(""), "missing content type should be rejected");
	}

	void TestJsonHelpers() {
		Require(!mcp::ExceedsJsonDepth(Repeat("[", 64) + Repeat("]", 64)), "depth 64 should pass");
		Require(mcp::ExceedsJsonDepth(Repeat("[", 65) + Repeat("]", 65)), "depth 65 should fail");
		Require(mcp::ExceedsJsonDepth(Repeat("{\"a\":", 65) + "1" + Repeat("}", 65)), "object depth should count");
		Require(mcp::ExceedsJsonDepth(Repeat("[", 1024 * 1024)), "unterminated nesting should fail without recursion");
		Require(!mcp::ExceedsJsonDepth("\"\xff\""), "invalid JSON is the parser's error, not a depth error");

		const std::string dumped = mcp::DumpJson(mcp::Json { { "name", std::string("po\xE7\xE3o") } });
		Require(dumped.find("\xEF\xBF\xBD") != std::string::npos, "invalid UTF-8 should become U+FFFD");
		const mcp::Json structured = mcp::StructuredResult({ { "name", std::string("po\xE7\xE3o") } });
		Require(structured.contains("structuredContent"), "StructuredResult should survive invalid UTF-8");
	}

	void TestPathHelpers() {
		Require(mcp::IsSafeExportName("world-map_1.v2"), "plain export names should pass");
		for (const char* name : { "", "..", ".hidden", "-rf", "a/b", "a\\b", "${HOME}", "~root", "a b", "a:b" }) {
			Require(!mcp::IsSafeExportName(name), std::string("unsafe export name should fail: ") + name);
		}
		Require(!mcp::IsSafeExportName(std::string(129, 'a')), "overlong export name should fail");

		Require(mcp::IsUncPath("\\\\host\\share\\x.otbm"), "backslash UNC path should be detected");
		Require(mcp::IsUncPath("//host/share/x.otbm"), "forward-slash UNC path should be detected");
		Require(mcp::IsUncPath("\\\\?\\UNC\\host\\share"), "extended UNC path should be detected");
		Require(!mcp::IsUncPath("C:\\maps\\x.otbm"), "drive path is not UNC");
		Require(!mcp::IsUncPath("/home/user/x.otbm"), "POSIX path is not UNC");

		Require(mcp::SanitizeDialogText("a\nb\r\tc") == "a b  c", "control characters must not shape the dialog");
		Require(mcp::SanitizeDialogText("abcdef", 3) == "abc", "dialog text should be truncated");
	}

	void TestImageProbe() {
		std::string png("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16);
		png += std::string("\x00\x01\x00\x00\x00\x00\x02\x00", 8);
		const auto pngSize = mcp::ProbeImageSize(png);
		Require(pngSize && pngSize->first == 65536 && pngSize->second == 512, "PNG size should come from IHDR");

		std::string bmp(26, '\0');
		bmp[0] = 'B';
		bmp[1] = 'M';
		bmp[18] = 0x10;
		bmp[22] = static_cast<char>(0xF0);
		bmp[23] = static_cast<char>(0xFF);
		bmp[24] = static_cast<char>(0xFF);
		bmp[25] = static_cast<char>(0xFF);
		const auto bmpSize = mcp::ProbeImageSize(bmp);
		Require(bmpSize && bmpSize->first == 16 && bmpSize->second == 16, "negative BMP height means top-down");

		Require(!mcp::ProbeImageSize(png.substr(0, 20)), "truncated PNG should be refused");
		Require(!mcp::ProbeImageSize("GIF89a-with-enough-bytes-to-be-read"), "other formats should be refused");
	}

	void TestExportRoot(const std::filesystem::path& scratch) {
		const std::filesystem::path root = scratch / "data" / "mcp-exports";
		mcp::PrepareExportRoot(root);
		Require(std::filesystem::is_directory(root), "export root should be created");
		mcp::PrepareExportRoot(root);
		mcp::RequireFreeExportTarget(root / "free_7.png");
		std::ofstream(root / "taken_7.png") << "x";
		bool refused = false;
		try {
			mcp::RequireFreeExportTarget(root / "taken_7.png");
		} catch (const mcp::Error&) {
			refused = true;
		}
		Require(refused, "existing export target should be refused");
#ifndef _WIN32
		std::filesystem::create_symlink(scratch / "nowhere", root / "link_7.png");
		refused = false;
		try {
			mcp::RequireFreeExportTarget(root / "link_7.png");
		} catch (const mcp::Error&) {
			refused = true;
		}
		Require(refused, "dangling symlink target should be refused");

		const std::filesystem::path linkedRoot = scratch / "linked-exports";
		std::filesystem::create_directory_symlink(root, linkedRoot);
		refused = false;
		try {
			mcp::PrepareExportRoot(linkedRoot);
		} catch (const mcp::Error&) {
			refused = true;
		}
		Require(refused, "symlinked export root should be refused");
#endif
	}

	void TestTokenFile(const std::filesystem::path& scratch) {
		const std::filesystem::path path = scratch / "nested" / "mcp-token";
		const std::string token = mcp::Server::LoadOrCreateToken(path);
		Require(token.size() == 64, "token should hold 32 random bytes as hex");
		Require(mcp::Server::LoadOrCreateToken(path) == token, "token should persist across launches");
		Require(mcp::Server::LoadOrCreateToken(scratch / "other-token") != token, "tokens should differ per install");
#ifndef _WIN32
		struct stat info { };
		Require(stat(path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600, "token file should be mode 0600");

		std::filesystem::permissions(path, std::filesystem::perms::group_read | std::filesystem::perms::others_read, std::filesystem::perm_options::add);
		const std::string replaced = mcp::Server::LoadOrCreateToken(path);
		Require(replaced != token, "a token others could read should be replaced");
		Require(stat(path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600, "replaced token file should be mode 0600");

		const std::filesystem::path link = scratch / "linked-token";
		std::filesystem::create_symlink(path, link);
		bool refused = false;
		try {
			(void)mcp::Server::LoadOrCreateToken(link);
		} catch (const mcp::Error&) {
			refused = true;
		}
		Require(refused, "symlinked token file should be refused");
#endif
		std::ofstream(path, std::ios::trunc) << "short";
		Require(mcp::Server::LoadOrCreateToken(path).size() == 64, "malformed token should be replaced");
	}

	void TestAuthAndHeaders(const Exchange& client) {
		Require(HasStatus(client.post(Ping), 200), "valid request should succeed");

		const std::string missing = client.raw(client.goodHost() + "Content-Type: application/json\r\n", Ping);
		Require(HasStatus(missing, 401), "missing token should be rejected");
		Require(missing.find("WWW-Authenticate: Bearer") != std::string::npos, "401 should name the scheme");
		const std::string json = "Content-Type: application/json\r\n";
		Require(HasStatus(client.raw(client.goodHost() + "Authorization: Bearer wrong\r\n" + json, Ping), 401), "wrong token should be rejected");
		Require(HasStatus(client.raw(client.goodHost() + "Authorization: Basic " + client.token + "\r\n" + json, Ping), 401), "other schemes should be rejected");
		Require(HasStatus(client.raw(client.goodHost() + "Authorization: Bearer " + client.token.substr(0, 63) + "\r\n" + json, Ping), 401), "token prefix should be rejected");
		Require(HasStatus(client.raw(client.goodHost() + "Authorization: Bearer\r\n" + json, Ping), 401), "empty bearer should be rejected");
		Require(HasStatus(client.raw(client.goodHost() + "Authorization: bearer " + client.token + "\r\n" + json, Ping), 200), "the scheme is case-insensitive");

		Require(HasStatus(client.raw("Host: attacker.example\r\n" + client.goodAuth() + json, Ping), 403), "foreign Host should be rejected");
		Require(HasStatus(client.raw(client.goodAuth() + json, Ping), 403), "missing Host should be rejected");
		Require(HasStatus(client.raw(client.goodHost() + "Host: 127.0.0.1\r\n" + client.goodAuth() + json, Ping), 400), "duplicate Host should be rejected");

		Require(HasStatus(client.raw(client.goodHost() + client.goodAuth() + "Content-Type: text/plain\r\n", Ping), 415), "text/plain should be rejected");
		Require(HasStatus(client.raw(client.goodHost() + client.goodAuth(), Ping), 415), "missing content type should be rejected");

		Require(HasStatus(client.post(Ping, "Origin: http://localhost:3000\r\n"), 403), "browser pages should be refused by default");
		Require(HasStatus(client.post(Ping, "Origin: http://evil.example\r\nOrigin: http://localhost\r\n"), 400), "duplicate Origin should be rejected");
		mcp::Server::Instance().setBrowserOriginsAllowed(true);
		Require(HasStatus(client.post(Ping, "Origin: http://localhost:3000\r\n"), 200), "opt-in should admit loopback pages");
		Require(HasStatus(client.post(Ping, "Origin: http://evil.example\r\n"), 403), "opt-in should not admit remote pages");
		mcp::Server::Instance().setBrowserOriginsAllowed(false);
	}

	void TestMalformedBodies(const Exchange& client) {
		const std::string invalidUtf8 = client.post("\"\xff\"");
		Require(HasStatus(invalidUtf8, 400), "invalid UTF-8 should be a parse error");
		Require(mcp::Json::parse(Body(invalidUtf8))["error"]["code"] == -32700, "parse errors should stay valid JSON");
		RequireAlive(client, "invalid UTF-8");
		Require(HasStatus(client.post("{\"id\":1,\"method\":\"\xc3(\"}"), 400), "invalid UTF-8 in a member should be a parse error");
		RequireAlive(client, "invalid UTF-8 in a member");

		for (const size_t depth : { size_t(65), size_t(20000), size_t(100000) }) {
			const std::string nested = "{\"id\":" + Repeat("[", depth) + Repeat("]", depth) + ",\"method\":\"ping\"}";
			Require(HasStatus(client.post(nested), 400), "deep JSON should be rejected");
			RequireAlive(client, "deep JSON");
		}
		Require(HasStatus(client.post(Repeat("[", 1024 * 1024)), 400), "unterminated nesting should be rejected");
		RequireAlive(client, "unterminated nesting");

		const std::string allowed = "{\"id\":1,\"method\":\"ping\",\"params\":" + Repeat("[", 60) + Repeat("]", 60) + "}";
		Require(HasStatus(client.post(allowed), 200), "nesting within the limit should pass");

		const mcp::Json structuredId = mcp::Json::parse(Body(client.post("{\"id\":[1],\"method\":\"ping\"}")));
		Require(structuredId["error"]["code"] == -32600 && structuredId["id"].is_null(), "structured id should be invalid");

		// The server rejects the length before reading the body. Sending the body can race the
		// close and cause Windows to reset the connection before the client reads the reply.
		const std::string oversized = "POST /mcp HTTP/1.1\r\n" + client.goodHost() + client.goodAuth() + "Content-Type: application/json\r\nContent-Length: " + std::to_string(1024 * 1024 + 1) + "\r\n\r\n";
		Require(HasStatus(client.request(oversized), 413), "oversized declared body should be rejected");

		const std::string raw = client.post("{\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"fake_raw_latin1\"}}");
		Require(HasStatus(raw, 200), "non-UTF-8 tool output should not fail the request");
		Require(mcp::Json::parse(Body(raw))["result"]["name"].get<std::string>().find("\xEF\xBF\xBD") != std::string::npos, "non-UTF-8 output should be replaced");
		RequireAlive(client, "non-UTF-8 tool output");
	}

	void TestBatches(const Exchange& client) {
		const std::string atLimit = client.post(Batch(32));
		Require(HasStatus(atLimit, 200) && mcp::Json::parse(Body(atLimit)).size() == 32, "a full batch should run");

		const std::string overLimit = client.post(Batch(33));
		Require(HasStatus(overLimit, 400), "an oversized batch should be rejected");
		Require(overLimit.find("\"result\"") == std::string::npos, "no entry of an oversized batch should run");

		const std::string call = "{\"id\":%,\"method\":\"tools/call\",\"params\":{\"name\":\"fake_large\"}}";
		const auto entry = [&call](int id) {
			std::string text = call;
			return text.replace(text.find('%'), 1, std::to_string(id));
		};
		const std::string large = client.post("[" + entry(1) + "," + entry(2) + "," + entry(3) + "]");
		const mcp::Json responses = mcp::Json::parse(Body(large));
		Require(responses.size() == 3 && responses[0].contains("result"), "the first large result should fit");
		Require(responses[1].contains("error") && responses[1]["id"] == 2, "a result over the cap should become an error");
		Require(responses[2].contains("error") && responses[2]["id"] == 3, "entries after the cap should not run");
		Require(HasStatus(client.post("[]"), 200), "an empty batch stays a JSON-RPC error, not a transport error");
	}

	void TestWriteGate(const Exchange& client) {
		const std::string response = client.post("{\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"fake_mutate\"}}");
		Require(mcp::Json::parse(Body(response))["result"].value("isError", false), "writes should be refused by default");
	}

	void TestConcurrency(const Exchange& client) {
		const TestSocket idle = Connect(client.port);
		SendText(idle, "P");
		const auto before = std::chrono::steady_clock::now();
		RequireAlive(client, "an idle connection");
		Require(std::chrono::steady_clock::now() - before < std::chrono::seconds(2), "an idle client must not starve others");

		// Bytes arriving faster than any per-recv timeout must not extend the deadline.
		const auto started = std::chrono::steady_clock::now();
		bool closed = false;
		while (!closed && std::chrono::steady_clock::now() - started < std::chrono::seconds(14)) {
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			char byte = 0;
#ifdef _WIN32
			u_long pending = 1;
			ioctlsocket(idle, FIONBIO, &pending);
			closed = recv(idle, &byte, 1, 0) == 0;
#else
			closed = recv(idle, &byte, 1, MSG_DONTWAIT) == 0;
			send(idle, "X", 1, 0);
#endif
		}
		Require(closed, "a trickling client should be dropped at the request deadline");
		CloseTestSocket(idle);
	}

	void TestStop(const Exchange& client) {
		const TestSocket trickling = Connect(client.port);
		SendText(trickling, "POST /mcp HTTP/1.1\r\n");
		const auto started = std::chrono::steady_clock::now();
		mcp::Server::Instance().stop();
		Require(std::chrono::steady_clock::now() - started < std::chrono::seconds(2), "stop must not wait for a slow client");
		CloseTestSocket(trickling);
		Require(!mcp::Server::Instance().running(), "server should report stopped");
	}
}

int main() {
#ifndef _WIN32
	std::signal(SIGPIPE, SIG_IGN);
#endif
	const std::filesystem::path scratch = std::filesystem::temp_directory_path() / ("nexamap-mcp-security-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(scratch);

	TestOrigins();
	TestHeaderHelpers();
	TestJsonHelpers();
	TestPathHelpers();
	TestImageProbe();
	TestExportRoot(scratch);
	TestTokenFile(scratch);

	mcp::Server& server = mcp::Server::Instance();
	Require(!server.start(0), "the server must refuse to start without a token");
	Exchange client;
	client.token = mcp::Server::LoadOrCreateToken(scratch / "server-token");
	server.setAuthToken(client.token);
	Require(server.start(0), "server should start: " + server.lastError());
	client.port = server.port();

	TestAuthAndHeaders(client);
	TestMalformedBodies(client);
	TestBatches(client);
	TestWriteGate(client);
	TestConcurrency(client);
	TestStop(client);

	std::error_code ignored;
	std::filesystem::remove_all(scratch, ignored);
	return 0;
}
