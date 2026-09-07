//////////////////////////////////////////////////////////////////////
// Mount ID → Client ID resolver for Tibia server mounts.
//////////////////////////////////////////////////////////////////////

#include "mount_id_resolver.h"

#include "ext/pugixml.hpp"

#include <cstdlib>

bool MountIdResolver::load(const std::filesystem::path& mountsXmlPath, std::string& error) {
	mountIdToClientId.clear();

	if (mountsXmlPath.empty()) {
		return true;
	}

	std::error_code ec;
	if (!std::filesystem::exists(mountsXmlPath, ec)) {
		// No mounts.xml is not an error – the resolver simply stays empty.
		return true;
	}

	pugi::xml_document document;
#ifdef __WINDOWS__
	const pugi::xml_parse_result result = document.load_file(mountsXmlPath.wstring().c_str());
#else
	const pugi::xml_parse_result result = document.load_file(mountsXmlPath.string().c_str());
#endif
	if (!result) {
		error = "Could not parse mounts.xml: ";
		error += result.description();
		return false;
	}

	pugi::xml_node root = document.child("mounts");
	if (!root) {
		// Try the file as the root element itself.
		root = document.first_child();
	}
	if (!root) {
		error = "mounts.xml has no root element.";
		return false;
	}

	for (pugi::xml_node mountNode = root.child("mount"); mountNode; mountNode = mountNode.next_sibling("mount")) {
		const pugi::xml_attribute idAttr = mountNode.attribute("id");
		pugi::xml_attribute clientIdAttr = mountNode.attribute("clientid");
		if (!clientIdAttr) {
			clientIdAttr = mountNode.attribute("clientId");
		}
		if (!clientIdAttr) {
			clientIdAttr = mountNode.attribute("client_id");
		}
		if (!clientIdAttr) {
			clientIdAttr = mountNode.attribute("type");
		}
		if (!idAttr || !clientIdAttr) {
			continue;
		}

		const int id = idAttr.as_int(0);
		const int clientId = clientIdAttr.as_int(0);
		if (id > 0 && clientId > 0) {
			mountIdToClientId[id] = clientId;
		}
	}

	return true;
}

void MountIdResolver::clear() {
	mountIdToClientId.clear();
}

int MountIdResolver::resolveClientId(int mountId) const {
	if (mountId <= 0) {
		return 0;
	}

	const auto it = mountIdToClientId.find(mountId);
	if (it != mountIdToClientId.end()) {
		return it->second;
	}

	// If not found in the ID map, it may already be a direct client lookType ID
	// or no mapping exists. Return mountId as fallback.
	return mountId;
}

bool MountIdResolver::empty() const {
	return mountIdToClientId.empty();
}

std::size_t MountIdResolver::size() const {
	return mountIdToClientId.size();
}
