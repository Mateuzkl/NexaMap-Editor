//////////////////////////////////////////////////////////////////////
// This file is part of NexaMap Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_CUSTOM_SERVER_CONVERTER_H_
#define RME_CUSTOM_SERVER_CONVERTER_H_

#include "items_xml_id_converter.h"
#include "items_otb_id_converter.h"
#include "map_item_id_converter.h"
#include "otb_item_id_mapping_provider.h"
#include "server_workspace.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct CustomServerAnalysis {
	ServerWorkspace workspace;
	std::shared_ptr<const OtbItemIdMappingProvider> mappingProvider;
	OtbItemIdMappingStats mappingStats;
	std::vector<OtbItemIdMappingIssue> mappingIssues;
	ItemsXmlIdConversionReport itemsXml;
	bool duplicateClientIdsAllowed = false;
	bool ready = false;
	std::string error;

	[[nodiscard]] std::string format() const;
};

struct CustomServerMapConversionReport {
	std::filesystem::path source;
	std::filesystem::path destination;
	MapItemIdConversionReport conversion;
};

struct CustomServerConversionOptions {
	std::filesystem::path sourceRoot;
	std::filesystem::path destinationRoot;
	bool convertMaps = true;
	bool convertItemsXml = true;
	bool convertItemsOtb = true;
	bool allowDuplicateClientIds = false;
};

struct CustomServerConversionReport {
	bool success = false;
	bool outputValidated = false;
	CustomServerAnalysis analysis;
	ItemsXmlIdConversionReport itemsXml;
	ItemsOtbIdConversionReport itemsOtb;
	std::vector<CustomServerMapConversionReport> maps;
	std::string error;

	[[nodiscard]] std::string format(const CustomServerConversionOptions& options) const;
};

[[nodiscard]] CustomServerAnalysis AnalyzeCustomServer(const std::filesystem::path& sourceRoot, bool allowDuplicateClientIds = false);
[[nodiscard]] CustomServerConversionReport ConvertCustomServer(const CustomServerConversionOptions& options);

#endif // RME_CUSTOM_SERVER_CONVERTER_H_
