//////////////////////////////////////////////////////////////////////
// Mount ID → Client ID resolver for Tibia server mounts.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_MOUNT_ID_RESOLVER_H_
#define NEXAMAP_MOUNT_ID_RESOLVER_H_

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

class MountIdResolver {
public:
	// Load mount definitions from a mounts.xml file.
	// Returns true on success or if the file does not exist (empty resolver).
	// On parse failure, sets error and returns false.
	bool load(const std::filesystem::path& mountsXmlPath, std::string& error);

	// Clear all loaded mount definitions.
	void clear();

	// Convert a server mount ID to the corresponding client sprite ID (lookType).
	// Unknown values are returned unchanged because some sources already store
	// the client look type instead of a server mount ID.
	[[nodiscard]] int resolveClientId(int mountId) const;

	// Check whether any mount definitions are loaded.
	[[nodiscard]] bool empty() const;

	// Number of loaded mount definitions.
	[[nodiscard]] std::size_t size() const;

private:
	// Maps server mount ID → client lookType ID.
	std::unordered_map<int, int> mountIdToClientId;
};

#endif // NEXAMAP_MOUNT_ID_RESOLVER_H_
