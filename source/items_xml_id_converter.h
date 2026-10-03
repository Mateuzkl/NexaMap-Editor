//////////////////////////////////////////////////////////////////////
// This file is part of NexaMap Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_ITEMS_XML_ID_CONVERTER_H_
#define RME_ITEMS_XML_ID_CONVERTER_H_

#include "item_id_mapping_provider.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pugi {
	class xml_document;
}

struct ItemsXmlIdConversionIssue {
	uint16_t sourceId = 0;
	std::string context;
	std::string message;
};

struct ItemsXmlIdConversionReport {
	bool success = false;
	bool outputValidated = false;
	std::size_t declarations = 0;
	std::size_t mappedDeclarations = 0;
	std::size_t changedDeclarations = 0;
	std::size_t missingDeclarations = 0;
	std::size_t convertedReferences = 0;
	std::size_t missingReferences = 0;
	std::size_t expandedRanges = 0;
	std::size_t collisions = 0;
	std::string error;
	std::vector<ItemsXmlIdConversionIssue> issues;
};

[[nodiscard]] ItemsXmlIdConversionReport ConvertItemsXmlDocument(
	pugi::xml_document& document,
	const ItemIdMappingProvider& provider,
	bool strictMapping = true,
	bool allowDestinationCollisions = false
);

[[nodiscard]] ItemsXmlIdConversionReport ConvertItemsXmlFile(
	const std::filesystem::path& source,
	const std::filesystem::path& destination,
	const ItemIdMappingProvider& provider,
	bool strictMapping = true,
	bool allowDestinationCollisions = false
);

#endif // RME_ITEMS_XML_ID_CONVERTER_H_
