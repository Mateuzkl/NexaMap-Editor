//////////////////////////////////////////////////////////////////////
// Vocation definitions discovered from the active Server Workspace.
//////////////////////////////////////////////////////////////////////

#include "server_vocation_catalog.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <set>

namespace {
	std::optional<std::string> Attribute(std::string_view tag, std::string_view name) {
		const std::regex expression("\\b" + std::string(name) + R"(\s*=\s*["']([^"']*)["'])", std::regex::icase);
		std::match_results<std::string_view::const_iterator> match;
		return std::regex_search(tag.begin(), tag.end(), match, expression) ? std::optional<std::string>(match.str(1)) : std::nullopt;
	}

	int Number(const std::optional<std::string>& value) {
		if (!value) {
			return 0;
		}
		int result = 0;
		const auto parsed = std::from_chars(value->data(), value->data() + value->size(), result);
		return parsed.ec == std::errc() ? result : 0;
	}

	void ReadVocations(const std::filesystem::path& path, std::vector<ServerVocation>& vocations, std::set<int>& ids) {
		std::ifstream stream(path, std::ios::binary);
		if (!stream) {
			return;
		}
		const std::string bytes { std::istreambuf_iterator<char>(stream), {} };
		const std::regex tag(R"(<\s*vocation\b[^>]*>)", std::regex::icase);
		for (std::sregex_iterator iterator(bytes.begin(), bytes.end(), tag), end; iterator != end; ++iterator) {
			const auto name = Attribute(iterator->str(), "name");
			const int id = Number(Attribute(iterator->str(), "id"));
			const int fromId = Number(Attribute(iterator->str(), "fromvoc"));
			if (!name || name->empty() || id <= 0 || !ids.insert(id).second) {
				continue;
			}
			vocations.push_back({ id, fromId, *name, fromId > 0 && fromId != id });
		}
	}
}

std::vector<ServerVocation> LoadServerVocations(const ServerWorkspace& workspace) {
	std::vector<std::filesystem::path> candidates;
	const auto addData = [&](const std::filesystem::path& data) {
		candidates.push_back(data / "XML" / "vocations.xml");
		candidates.push_back(data / "xml" / "vocations.xml");
	};
	addData(workspace.activeDataDirectory);
	for (const char* directory : { "data", "data-crystal", "data-global" }) {
		addData(workspace.rootPath / directory);
	}
	std::vector<ServerVocation> result;
	std::set<int> ids;
	for (const auto& path : candidates) {
		std::error_code error;
		if (std::filesystem::is_regular_file(path, error)) {
			ReadVocations(path, result, ids);
		}
	}
	std::sort(result.begin(), result.end(), [](const ServerVocation& left, const ServerVocation& right) {
		return left.id < right.id;
	});
	return result;
}
