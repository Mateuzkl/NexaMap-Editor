//////////////////////////////////////////////////////////////////////
// This file is part of NexaMap Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_ITEMS_OTB_ID_CONVERTER_H_
#define RME_ITEMS_OTB_ID_CONVERTER_H_

#include <cstddef>
#include <filesystem>
#include <string>

struct ItemsOtbIdConversionReport {
	bool success = false;
	bool outputValidated = false;
	std::size_t sourceNodes = 0;
	std::size_t writtenItems = 0;
	std::size_t aliasesMerged = 0;
	std::size_t deprecatedItemsPreserved = 0;
	std::string error;
};

[[nodiscard]] ItemsOtbIdConversionReport ConvertItemsOtbToClientIds(
	const std::filesystem::path& source,
	const std::filesystem::path& destination
);

#endif // RME_ITEMS_OTB_ID_CONVERTER_H_
