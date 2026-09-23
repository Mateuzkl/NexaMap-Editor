#ifndef NEXAMAP_SERVER_ITEMS_XML_ID_SPACE_H_
#define NEXAMAP_SERVER_ITEMS_XML_ID_SPACE_H_

#include <cstddef>
#include <cstdint>

enum class ServerItemsXmlIdSpace : uint8_t {
	AutoDetect = 0,
	NativeAppearanceId,
	ServerIdMapped,
};

struct ServerItemsXmlLoadReport {
	ServerItemsXmlIdSpace selectedIdSpace = ServerItemsXmlIdSpace::AutoDetect;
	size_t sourceDefinitions = 0;
	size_t directCoverage = 0;
	size_t mappedCoverage = 0;
	size_t nativeTeleportsApplied = 0;
	size_t skippedDefinitions = 0;
	size_t ambiguousMappings = 0;
};

inline constexpr ServerItemsXmlIdSpace SelectServerItemsXmlIdSpace(size_t directCoverage, size_t mappedCoverage) noexcept {
	return mappedCoverage > directCoverage ? ServerItemsXmlIdSpace::ServerIdMapped : ServerItemsXmlIdSpace::NativeAppearanceId;
}

inline constexpr const char* ServerItemsXmlIdSpaceName(ServerItemsXmlIdSpace idSpace) noexcept {
	switch (idSpace) {
		case ServerItemsXmlIdSpace::NativeAppearanceId:
			return "NativeAppearanceId";
		case ServerItemsXmlIdSpace::ServerIdMapped:
			return "ServerIdMapped";
		default:
			return "AutoDetect";
	}
}

#endif // NEXAMAP_SERVER_ITEMS_XML_ID_SPACE_H_
