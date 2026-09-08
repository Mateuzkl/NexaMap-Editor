//////////////////////////////////////////////////////////////////////
// Vocation definitions discovered from the active Server Workspace.
//////////////////////////////////////////////////////////////////////

#include "server_vocation_catalog.h"

#include "ext/pugixml.hpp"
#include "source_text_utils.h"

#include <algorithm>
#include <set>

namespace {
	void ReadVocationNodes(const pugi::xml_node& parent, std::vector<ServerVocation>& vocations, std::set<int>& ids) {
		for (const pugi::xml_node& node : parent.children()) {
			if (node.type() != pugi::node_element) {
				continue;
			}
			if (!SourceText::AsciiCaseEqual(node.name(), "vocation")) {
				ReadVocationNodes(node, vocations, ids);
				continue;
			}
			const std::string name = node.attribute("name").as_string();
			const int id = node.attribute("id").as_int();
			const int fromId = node.attribute("fromvoc").as_int();
			if (!name.empty() && id > 0 && ids.insert(id).second) {
				vocations.push_back({ id, fromId, name, fromId > 0 && fromId != id });
			}
		}
	}

	void ReadVocations(const std::filesystem::path& path, std::vector<ServerVocation>& vocations, std::set<int>& ids) {
		const auto bytes = SourceText::ReadBoundedFile(path, 4 * 1024 * 1024);
		if (!bytes) {
			return;
		}
		pugi::xml_document document;
		if (!document.load_buffer(bytes->data(), bytes->size())) {
			return;
		}
		ReadVocationNodes(document, vocations, ids);
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
