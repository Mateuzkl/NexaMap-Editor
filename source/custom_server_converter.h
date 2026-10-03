//////////////////////////////////////////////////////////////////////
// This file is part of NexaMap Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_CUSTOM_SERVER_CONVERTER_H_
#define RME_CUSTOM_SERVER_CONVERTER_H_

#include "items_otb_id_converter.h"
#include "items_xml_id_converter.h"
#include "map_item_id_converter.h"
#include "otb_item_id_mapping_provider.h"
#include "server_workspace.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct CustomServerConversionScope {
	bool maps = true;
	bool itemsXml = true;
	bool itemsOtb = true;

	[[nodiscard]] bool any() const noexcept {
		return maps || itemsXml || itemsOtb;
	}
};

enum class CustomServerConversionPhase : uint8_t {
	AfterAnalysis,
	BeforeMaps,
	BeforeItemsXml,
	BeforeItemsOtb,
	BeforeCommit,
};

struct CustomServerSourceDigest {
	std::filesystem::path path;
	uintmax_t size = 0;
	uint64_t hash = 0;
	bool valid = false;

	friend bool operator==(const CustomServerSourceDigest&, const CustomServerSourceDigest&) = default;
};

struct CustomServerMapAnalysis {
	std::filesystem::path source;
	MapItemIdConversionReport preflight;
};

struct CustomServerAnalysis {
	ServerWorkspace workspace;
	CustomServerConversionScope scope;
	std::shared_ptr<const OtbItemIdMappingProvider> mappingProvider;
	OtbItemIdMappingStats mappingStats;
	std::vector<OtbItemIdMappingIssue> mappingIssues;
	ItemsXmlIdConversionReport itemsXml;
	std::vector<CustomServerMapAnalysis> maps;
	std::vector<CustomServerSourceDigest> sourceDigests;
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
	CustomServerConversionScope scope;
	bool allowDuplicateClientIds = false;
	// Invoked synchronously on the calling thread; useful for progress and deterministic tests.
	std::function<void(CustomServerConversionPhase)> phaseCallback;
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

[[nodiscard]] CustomServerAnalysis AnalyzeCustomServer(
	const std::filesystem::path& sourceRoot,
	const CustomServerConversionScope& scope = {},
	bool allowDuplicateClientIds = false
);
[[nodiscard]] CustomServerConversionReport ConvertCustomServer(const CustomServerConversionOptions& options);

#endif // RME_CUSTOM_SERVER_CONVERTER_H_
