//////////////////////////////////////////////////////////////////////
// This file is part of NexaMap Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "custom_server_converter.h"

#include "ext/pugixml.hpp"
#include "filehandle.h"
#include "iomap_otbm.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <system_error>

namespace {
	std::string PrintablePath(const std::filesystem::path& path) {
#ifdef _WIN32
		return nstr(wxString(path.wstring()));
#else
		return path.string();
#endif
	}

	wxString PathToWxString(const std::filesystem::path& path) {
#ifdef _WIN32
		return wxString(path.wstring());
#else
		return wxString::FromUTF8(path.string());
#endif
	}

	std::filesystem::path Normalize(const std::filesystem::path& path) {
		std::error_code error;
		const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
		if (!error) {
			return canonical;
		}
		const std::filesystem::path absolute = std::filesystem::absolute(path, error);
		return (error ? path : absolute).lexically_normal();
	}

	bool IsWithin(const std::filesystem::path& candidatePath, const std::filesystem::path& parentPath) {
		const std::filesystem::path candidate = Normalize(candidatePath);
		const std::filesystem::path parent = Normalize(parentPath);
		auto candidatePart = candidate.begin();
		for (auto parentPart = parent.begin(); parentPart != parent.end(); ++parentPart, ++candidatePart) {
			if (candidatePart == candidate.end() || *candidatePart != *parentPart) {
				return false;
			}
		}
		return true;
	}

	bool EscapesRoot(const std::filesystem::path& relative) {
		return relative.empty() || (relative.begin() != relative.end() && *relative.begin() == "..");
	}

	bool LoadXml(const std::filesystem::path& path, pugi::xml_document& document, std::string& error) {
		std::ifstream input(path, std::ios::binary);
		if (!input) {
			error = "Could not open items.xml.";
			return false;
		}
		const std::string contents { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
		const pugi::xml_parse_result result = document.load_buffer(contents.data(), contents.size(), pugi::parse_default | pugi::parse_comments, pugi::encoding_utf8);
		if (!result) {
			error = std::string("Could not parse items.xml: ") + result.description();
			return false;
		}
		return true;
	}

	class StagingDirectory final {
	public:
		explicit StagingDirectory(const std::filesystem::path& destination) :
			destination(destination) {
			const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
			staging = destination.parent_path() / ("." + destination.filename().string() + ".nexamap-staging-" + std::to_string(nonce));
		}

		~StagingDirectory() {
			if (!committed && !staging.empty()) {
				std::error_code ignored;
				std::filesystem::remove_all(staging, ignored);
			}
		}

		bool create(std::string& error) {
			std::error_code filesystemError;
			std::filesystem::create_directories(staging, filesystemError);
			if (filesystemError) {
				error = "Could not create conversion staging directory: " + filesystemError.message();
				return false;
			}
			return true;
		}

		bool commit(std::string& error) {
			std::error_code filesystemError;
			if (std::filesystem::exists(destination, filesystemError)) {
				if (filesystemError || !std::filesystem::is_directory(destination, filesystemError) || !std::filesystem::is_empty(destination, filesystemError)) {
					error = "Destination folder must not exist or must be empty.";
					return false;
				}
				std::filesystem::remove(destination, filesystemError);
				if (filesystemError) {
					error = "Could not prepare the empty destination folder: " + filesystemError.message();
					return false;
				}
			}
			std::filesystem::rename(staging, destination, filesystemError);
			if (filesystemError) {
				error = "Could not atomically commit converted output: " + filesystemError.message();
				return false;
			}
			committed = true;
			return true;
		}

		[[nodiscard]] const std::filesystem::path& path() const noexcept {
			return staging;
		}

	private:
		std::filesystem::path destination;
		std::filesystem::path staging;
		bool committed = false;
	};
}

std::string CustomServerAnalysis::format() const {
	std::ostringstream output;
	output << "Custom server analysis\n\n"
		   << "Source: " << PrintablePath(workspace.rootPath) << '\n'
		   << "Server type: " << ServerTypeName(workspace.serverType) << "\n\n"
		   << "items.otb:\n"
		   << "  entries: " << mappingStats.entries << '\n'
		   << "  unique ServerIDs: " << mappingStats.uniqueServerIds << '\n'
		   << "  unique ClientIDs: " << mappingStats.uniqueClientIds << '\n'
		   << "  duplicate ServerIDs: " << mappingStats.duplicateServerIds << '\n'
		   << "  duplicate ClientIDs: " << mappingStats.duplicateClientIds;
	if (duplicateClientIdsAllowed && mappingStats.duplicateClientIds != 0) {
		output << " (allowed: aliases will merge)";
	}
	output << "\n\n"
		   << "items.xml:\n"
		   << "  declarations: " << itemsXml.declarations << '\n'
		   << "  mapped: " << itemsXml.mappedDeclarations << '\n'
		   << "  missing: " << itemsXml.missingDeclarations << '\n'
		   << "  expanded ranges: " << itemsXml.expandedRanges << "\n\n"
		   << "maps: " << workspace.maps.size() << '\n';
	for (const DetectedMap& map : workspace.maps) {
		std::error_code errorCode;
		const std::filesystem::path relative = std::filesystem::relative(map.path, workspace.rootPath, errorCode);
		output << "  " << PrintablePath(errorCode ? map.path.filename() : relative) << '\n';
	}
	output << "\nStatus: " << (ready ? "READY TO CONVERT" : "BLOCKED") << '\n';
	if (!error.empty()) {
		output << "Reason: " << error << '\n';
	}
	for (const OtbItemIdMappingIssue& issue : mappingIssues) {
		output << "  - " << issue.message << '\n';
	}
	for (const ItemsXmlIdConversionIssue& issue : itemsXml.issues) {
		output << "  - " << issue.context << ": " << issue.message << '\n';
	}
	return output.str();
}

CustomServerAnalysis AnalyzeCustomServer(const std::filesystem::path& sourceRoot, bool allowDuplicateClientIds) {
	CustomServerAnalysis analysis;
	std::cout << "[CustomConverter] Analyzing source: " << PrintablePath(sourceRoot) << std::endl;
	analysis.duplicateClientIdsAllowed = allowDuplicateClientIds;
	const ServerDetectionResult detection = ServerResourceDetector::Detect(sourceRoot);
	if (!detection.validRoot) {
		analysis.error = detection.error.empty() ? "The selected folder is not a valid server root." : detection.error;
		return analysis;
	}
	analysis.workspace = detection.workspace;
	if (!analysis.workspace.hasItemsOtb()) {
		analysis.error = "items.otb was not found.";
		return analysis;
	}
	if (!analysis.workspace.hasItemsXml()) {
		analysis.error = "items.xml was not found.";
		return analysis;
	}
	if (analysis.workspace.maps.empty()) {
		analysis.error = "No .otbm or .otgz maps were found.";
		return analysis;
	}

	OtbItemIdMappingLoadResult mapping = OtbItemIdMappingProvider::Load(analysis.workspace.itemsOtbPath, allowDuplicateClientIds);
	analysis.mappingStats = mapping.stats;
	std::cout << "[CustomConverter] items.otb: " << mapping.stats.entries << " mapping pairs, " << mapping.stats.duplicateClientIds << " ClientID aliases" << std::endl;
	analysis.mappingIssues = mapping.issues;
	analysis.mappingProvider = mapping.provider;
	if (!mapping.ready()) {
		analysis.error = mapping.error.empty() ? "items.otb mapping is unsafe." : mapping.error;
		return analysis;
	}

	pugi::xml_document document;
	if (!LoadXml(analysis.workspace.itemsXmlPath, document, analysis.error)) {
		return analysis;
	}
	analysis.itemsXml = ConvertItemsXmlDocument(document, *analysis.mappingProvider, true, allowDuplicateClientIds);
	std::cout << "[CustomConverter] items.xml: " << analysis.itemsXml.mappedDeclarations << " mapped, " << analysis.itemsXml.missingDeclarations << " missing" << std::endl;
	if (!analysis.itemsXml.success) {
		analysis.error = analysis.itemsXml.error;
		return analysis;
	}
	analysis.ready = true;
	std::cout << "[CustomConverter] Maps detected: " << analysis.workspace.maps.size() << "; analysis ready" << std::endl;
	return analysis;
}

std::string CustomServerConversionReport::format(const CustomServerConversionOptions& options) const {
	std::ostringstream output;
	output << "CUSTOM SERVER -> CLIENTID CONVERSION " << (success ? "COMPLETE" : "FAILED") << "\n\n"
		   << "Source: " << PrintablePath(options.sourceRoot) << '\n'
		   << "Destination: " << PrintablePath(options.destinationRoot) << "\n\n"
		   << "OTB mapping: " << analysis.mappingStats.entries << " pairs, " << analysis.mappingStats.duplicateClientIds << " collisions\n"
		   << "items.otb: " << itemsOtb.writtenItems << " identity items written, " << itemsOtb.aliasesMerged << " aliases merged\n"
		   << "items.xml: " << itemsXml.mappedDeclarations << " mapped, " << itemsXml.expandedRanges << " ranges expanded, " << itemsXml.missingDeclarations + itemsXml.missingReferences << " missing\n"
		   << "Maps:\n";
	for (const CustomServerMapConversionReport& map : maps) {
		output << "  " << PrintablePath(map.source.filename()) << ": " << map.conversion.changedItems << " changed, " << map.conversion.missingItems << " missing\n";
	}
	output << "\nValidation: " << (outputValidated ? "PASS" : "FAILED") << '\n';
	if (!error.empty()) {
		output << "Error: " << error << '\n';
	}
	output << "\nOriginal server: UNCHANGED\n";
	if (!success) {
		output << "No incomplete converted output was committed.\n";
	}
	return output.str();
}

CustomServerConversionReport ConvertCustomServer(const CustomServerConversionOptions& options) {
	CustomServerConversionReport report;
	std::cout << "[CustomConverter] Destination: " << PrintablePath(options.destinationRoot) << std::endl;
	report.analysis = AnalyzeCustomServer(options.sourceRoot, options.allowDuplicateClientIds);
	if (!report.analysis.ready) {
		report.error = report.analysis.error;
		return report;
	}
	if (options.destinationRoot.empty()) {
		report.error = "Select a destination folder.";
		return report;
	}
	const std::filesystem::path sourceRoot = Normalize(report.analysis.workspace.rootPath);
	const std::filesystem::path destinationRoot = Normalize(options.destinationRoot);
	if (sourceRoot == destinationRoot || IsWithin(destinationRoot, sourceRoot) || IsWithin(sourceRoot, destinationRoot)) {
		report.error = "Destination must be a separate folder outside the source server tree.";
		return report;
	}

	StagingDirectory staging(destinationRoot);
	if (!staging.create(report.error)) {
		return report;
	}
	if (options.convertMaps) {
		for (const DetectedMap& detectedMap : report.analysis.workspace.maps) {
			std::cout << "[CustomConverter] Converting map: " << PrintablePath(detectedMap.path) << std::endl;
			std::error_code pathError;
			const std::filesystem::path relative = std::filesystem::relative(detectedMap.path, sourceRoot, pathError);
			if (pathError || EscapesRoot(relative)) {
				report.error = "A detected map is outside the selected server root.";
				return report;
			}
			const std::filesystem::path destination = staging.path() / relative;
			MapVersion sourceVersion;
			if (!IOMapOTBM::getVersionInfo(FileName(PathToWxString(detectedMap.path)), sourceVersion)) {
				report.error = "Could not determine map version for " + PrintablePath(detectedMap.path) + '.';
				return report;
			}
			MapItemIdConversionOptions mapOptions;
			mapOptions.source = detectedMap.path;
			mapOptions.destination = destination;
			mapOptions.direction = ItemIdMapping::Direction::ServerToClient;
			mapOptions.targetVersion = sourceVersion;
			mapOptions.mappingProvider = report.analysis.mappingProvider;
			mapOptions.strictMapping = true;
			CustomServerMapConversionReport mapReport { detectedMap.path, destination, ConvertMapItemIds(mapOptions) };
			report.maps.push_back(std::move(mapReport));
			if (!report.maps.back().conversion.success || !report.maps.back().conversion.outputValidated) {
				report.error = report.maps.back().conversion.error.empty() ? "Map conversion failed validation." : report.maps.back().conversion.error;
				return report;
			}
		}
	}
	if (options.convertItemsXml) {
		std::cout << "[CustomConverter] Converting items.xml" << std::endl;
		std::error_code pathError;
		const std::filesystem::path relative = std::filesystem::relative(report.analysis.workspace.itemsXmlPath, sourceRoot, pathError);
		if (pathError || EscapesRoot(relative)) {
			report.error = "Detected items.xml is outside the selected server root.";
			return report;
		}
		report.itemsXml = ConvertItemsXmlFile(report.analysis.workspace.itemsXmlPath, staging.path() / relative, *report.analysis.mappingProvider, true, options.allowDuplicateClientIds);
		if (!report.itemsXml.success || !report.itemsXml.outputValidated) {
			report.error = report.itemsXml.error.empty() ? "items.xml conversion failed validation." : report.itemsXml.error;
			return report;
		}
	}
	if (options.convertItemsOtb) {
		std::cout << "[CustomConverter] Normalizing items.otb to ClientID identity mappings" << std::endl;
		std::error_code pathError;
		const std::filesystem::path relative = std::filesystem::relative(report.analysis.workspace.itemsOtbPath, sourceRoot, pathError);
		if (pathError || EscapesRoot(relative)) {
			report.error = "Detected items.otb is outside the selected server root.";
			return report;
		}
		report.itemsOtb = ConvertItemsOtbToClientIds(report.analysis.workspace.itemsOtbPath, staging.path() / relative);
		if (!report.itemsOtb.success || !report.itemsOtb.outputValidated) {
			report.error = report.itemsOtb.error.empty() ? "items.otb conversion failed validation." : report.itemsOtb.error;
			return report;
		}
		std::cout << "[CustomConverter] items.otb: " << report.itemsOtb.writtenItems << " identity items, " << report.itemsOtb.aliasesMerged << " aliases merged" << std::endl;
	}
	if (!staging.commit(report.error)) {
		return report;
	}
	report.outputValidated = true;
	report.success = true;
	std::cout << "[CustomConverter] Conversion validated and committed successfully" << std::endl;
	return report;
}
