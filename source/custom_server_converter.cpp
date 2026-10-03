//////////////////////////////////////////////////////////////////////
// This file is part of NexaMap Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "custom_server_converter.h"

#include "ext/pugixml.hpp"
#include "file_transaction.h"
#include "filehandle.h"
#include "iomap_otbm.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <system_error>

namespace {
	constexpr std::size_t MaximumReportedMapIssues = 12;
	constexpr std::size_t MaximumReportedResourceIssues = 20;

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

	bool EscapesRoot(const std::filesystem::path& relative) {
		return relative.empty() || (relative.begin() != relative.end() && *relative.begin() == "..");
	}

	bool IsRegularFile(const std::filesystem::path& path) {
		std::error_code error;
		return std::filesystem::is_regular_file(path, error) && !error;
	}

	bool IsGzipFile(const std::filesystem::path& path) {
		std::ifstream input(path, std::ios::binary);
		std::array<uint8_t, 2> magic {};
		return input.read(reinterpret_cast<char*>(magic.data()), magic.size()) && magic[0] == 0x1F && magic[1] == 0x8B;
	}

	bool LoadXml(const std::filesystem::path& path, pugi::xml_document& document, std::string& error) {
		std::ifstream input(path, std::ios::binary);
		if (!input) {
			error = "Could not open items.xml.";
			return false;
		}
		const std::string contents { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
		const pugi::xml_parse_result result = document.load_buffer(contents.data(), contents.size(), pugi::parse_full, pugi::encoding_auto);
		if (!result) {
			error = std::string("Could not parse items.xml: ") + result.description();
			return false;
		}
		return true;
	}

	CustomServerSourceDigest ReadDigest(const std::filesystem::path& path) {
		CustomServerSourceDigest digest;
		digest.path = FileSaveTransaction::NormalizePath(path);
		std::ifstream input(path, std::ios::binary);
		if (!input) {
			return digest;
		}
		uint64_t hash = 14695981039346656037ull;
		std::array<char, 64 * 1024> buffer {};
		uintmax_t size = 0;
		while (input) {
			input.read(buffer.data(), buffer.size());
			const std::streamsize read = input.gcount();
			for (std::streamsize index = 0; index < read; ++index) {
				hash ^= static_cast<uint8_t>(buffer[static_cast<std::size_t>(index)]);
				hash *= 1099511628211ull;
			}
			size += static_cast<uintmax_t>(read);
		}
		if (!input.eof()) {
			return digest;
		}
		digest.size = size;
		digest.hash = hash;
		digest.valid = true;
		return digest;
	}

	std::vector<CustomServerSourceDigest> CaptureSourceDigests(const ServerWorkspace& workspace, const CustomServerConversionScope& scope, std::string& error) {
		std::vector<std::filesystem::path> paths { workspace.itemsOtbPath };
		if (scope.itemsXml) {
			paths.push_back(workspace.itemsXmlPath);
		}
		if (scope.maps) {
			for (const DetectedMap& map : workspace.maps) {
				paths.push_back(map.path);
			}
		}
		std::vector<CustomServerSourceDigest> digests;
		digests.reserve(paths.size());
		for (const std::filesystem::path& path : paths) {
			CustomServerSourceDigest digest = ReadDigest(path);
			if (!digest.valid) {
				error = "Could not read source resource for integrity validation: " + PrintablePath(path);
				return {};
			}
			digests.push_back(std::move(digest));
		}
		return digests;
	}

	bool SourceDigestsMatch(const std::vector<CustomServerSourceDigest>& expected, std::string& error) {
		for (const CustomServerSourceDigest& digest : expected) {
			const CustomServerSourceDigest current = ReadDigest(digest.path);
			if (!current.valid || current.size != digest.size || current.hash != digest.hash) {
				error = "Source changed during conversion; no output was committed. Changed resource: " + PrintablePath(digest.path);
				return false;
			}
		}
		return true;
	}

	bool InvokePhaseCallback(const CustomServerConversionOptions& options, CustomServerConversionPhase phase, std::string& error) {
		if (!options.phaseCallback) {
			return true;
		}
		try {
			options.phaseCallback(phase);
			return true;
		} catch (const std::exception& exception) {
			error = std::string("Conversion phase callback failed: ") + exception.what();
		} catch (...) {
			error = "Conversion phase callback failed with an unknown exception.";
		}
		return false;
	}

	class StagingDirectory final {
	public:
		explicit StagingDirectory(std::filesystem::path destination) :
			destination(std::move(destination)) { }

		StagingDirectory(const StagingDirectory&) = delete;
		StagingDirectory& operator=(const StagingDirectory&) = delete;

		~StagingDirectory() {
			if (owned && !staging.empty()) {
				std::error_code ignored;
				std::filesystem::remove_all(staging, ignored);
			}
		}

		bool create(std::string& error) {
			std::error_code filesystemError;
			const std::filesystem::path parent = destination.parent_path();
			if (!parent.empty()) {
				std::filesystem::create_directories(parent, filesystemError);
				if (filesystemError) {
					error = "Could not create the output parent directory: " + filesystemError.message();
					return false;
				}
			}
			static std::atomic_uint64_t sequence = 0;
			const uint64_t timestamp = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
			for (unsigned int attempt = 0; attempt < 128; ++attempt) {
				std::filesystem::path candidate = destination;
				candidate += ".nexamap-staging." + std::to_string(timestamp) + "." + std::to_string(sequence.fetch_add(1));
				filesystemError.clear();
				if (std::filesystem::create_directory(candidate, filesystemError)) {
					staging = std::move(candidate);
					owned = true;
					return true;
				}
				if (filesystemError && filesystemError != std::errc::file_exists) {
					error = "Could not create a unique conversion staging directory: " + filesystemError.message();
					return false;
				}
			}
			error = "Could not allocate a unique conversion staging directory.";
			return false;
		}

		bool commit(const std::filesystem::path& sourceRoot, std::string& error) {
			if (!owned || staging.empty()) {
				error = "Conversion staging directory is not owned by this operation.";
				return false;
			}
			if (FileSaveTransaction::PathsOverlap(sourceRoot, destination)) {
				error = "Destination overlaps the source server tree.";
				return false;
			}
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
			owned = false;
			return true;
		}

		[[nodiscard]] const std::filesystem::path& path() const noexcept {
			return staging;
		}

	private:
		std::filesystem::path destination;
		std::filesystem::path staging;
		bool owned = false;
	};

	void AppendMapIssues(std::ostringstream& output, const MapItemIdConversionReport& report) {
		const std::size_t count = std::min(MaximumReportedMapIssues, report.issues.size());
		for (std::size_t index = 0; index < count; ++index) {
			const MapItemIdConversionIssue& issue = report.issues[index];
			output << "    - ServerID " << issue.sourceId << ": " << issue.occurrences << " occurrence(s), "
				   << (issue.found ? "ambiguous" : "missing");
			if (!issue.candidates.empty()) {
				output << ", candidates:";
				for (uint16_t candidate : issue.candidates) {
					output << ' ' << candidate;
				}
			}
			output << '\n';
		}
		if (report.issues.size() > count) {
			output << "    - ... " << (report.issues.size() - count) << " additional issue(s) omitted\n";
		}
	}
}

std::string CustomServerAnalysis::format() const {
	std::ostringstream output;
	output << "Custom server analysis\n\n"
		   << "Source: " << PrintablePath(workspace.rootPath) << '\n'
		   << "Server type: " << ServerTypeName(workspace.serverType) << "\n\n"
		   << "items.otb (read-only mapping input" << (scope.itemsOtb ? " and selected ClientID output" : "") << "):\n"
		   << "  entries: " << mappingStats.entries << '\n'
		   << "  unique ServerIDs: " << mappingStats.uniqueServerIds << '\n'
		   << "  unique ClientIDs: " << mappingStats.uniqueClientIds << '\n'
		   << "  duplicate ServerIDs: " << mappingStats.duplicateServerIds << '\n'
		   << "  ClientID aliases: " << mappingStats.duplicateClientIds;
	if (duplicateClientIdsAllowed && mappingStats.duplicateClientIds != 0) {
		output << " (compatibility override: aliases and malformed XML ranges are preserved/allowed)";
	}
	output << "\n\n";
	if (scope.itemsXml) {
		output << "items.xml:\n"
			   << "  declarations: " << itemsXml.declarations << '\n'
			   << "  mapped: " << itemsXml.mappedDeclarations << '\n'
			   << "  missing: " << itemsXml.missingDeclarations << '\n'
			   << "  expanded ranges: " << itemsXml.expandedRanges << "\n\n";
	} else {
		output << "items.xml: skipped\n\n";
	}
	if (scope.maps) {
		output << "maps: " << maps.size() << '\n';
		for (const CustomServerMapAnalysis& map : maps) {
			output << "  " << PrintablePath(map.source.filename()) << ": " << map.preflight.totalItems << " items, "
				   << map.preflight.mappedItems << " mapped, " << map.preflight.changedItems << " changed, "
				   << map.preflight.missingItems << " missing, " << map.preflight.ambiguousItems << " ambiguous\n";
			AppendMapIssues(output, map.preflight);
		}
	} else {
		output << "maps: skipped\n";
	}
	if (!workspace.warnings.empty()) {
		output << "\nDetector warnings:\n";
		for (const std::string& warning : workspace.warnings) {
			output << "  - " << warning << '\n';
		}
	}
	output << "\nStatus: " << (ready ? "READY TO CONVERT" : "BLOCKED") << '\n';
	if (!error.empty()) {
		output << "Reason: " << error << '\n';
	}
	const std::size_t mappingIssueCount = std::min(MaximumReportedResourceIssues, mappingIssues.size());
	for (std::size_t index = 0; index < mappingIssueCount; ++index) {
		output << "  - " << mappingIssues[index].message << '\n';
	}
	if (mappingIssues.size() > mappingIssueCount) {
		output << "  - ... " << (mappingIssues.size() - mappingIssueCount) << " additional items.otb issue(s) omitted\n";
	}
	const std::size_t xmlIssueCount = std::min(MaximumReportedResourceIssues, itemsXml.issues.size());
	for (std::size_t index = 0; index < xmlIssueCount; ++index) {
		output << "  - " << itemsXml.issues[index].context << ": " << itemsXml.issues[index].message << '\n';
	}
	if (itemsXml.issues.size() > xmlIssueCount) {
		output << "  - ... " << (itemsXml.issues.size() - xmlIssueCount) << " additional items.xml issue(s) omitted\n";
	}
	return output.str();
}

CustomServerAnalysis AnalyzeCustomServer(const std::filesystem::path& sourceRoot, const CustomServerConversionScope& scope, bool allowDuplicateClientIds) {
	CustomServerAnalysis analysis;
	analysis.scope = scope;
	analysis.duplicateClientIdsAllowed = allowDuplicateClientIds;
	std::cout << "[CustomConverter] Analyzing source: " << PrintablePath(sourceRoot) << std::endl;
	if (!scope.any()) {
		analysis.error = "Select at least one output: maps, items.xml, or items.otb.";
		return analysis;
	}
	const ServerDetectionResult detection = ServerResourceDetector::Detect(sourceRoot);
	if (!detection.validRoot) {
		analysis.error = detection.error.empty() ? "The selected folder is not a valid server root." : detection.error;
		return analysis;
	}
	analysis.workspace = detection.workspace;
	if (!analysis.workspace.hasItemsOtb()) {
		analysis.error = "items.otb was not found; it is required as the read-only mapping authority.";
		return analysis;
	}
	const std::filesystem::path siblingItemsXml = analysis.workspace.itemsOtbPath.parent_path() / "items.xml";
	if (IsRegularFile(siblingItemsXml)) {
		analysis.workspace.itemsXmlPath = FileSaveTransaction::NormalizePath(siblingItemsXml);
		analysis.workspace.itemsXmlFingerprint = ResourceFingerprint::Read(analysis.workspace.itemsXmlPath);
	}
	if (scope.itemsXml && !analysis.workspace.hasItemsXml()) {
		analysis.error = "items.xml was not found for the selected XML conversion scope.";
		return analysis;
	}
	if (scope.maps && analysis.workspace.maps.empty()) {
		analysis.error = "No maps were found for the selected map conversion scope.";
		return analysis;
	}
	if (scope.maps && analysis.workspace.scanLimitReached) {
		analysis.error = "The bounded server scan reached its directory limit; complete map discovery cannot be guaranteed.";
		return analysis;
	}

	analysis.sourceDigests = CaptureSourceDigests(analysis.workspace, scope, analysis.error);
	if (!analysis.error.empty()) {
		return analysis;
	}
	OtbItemIdMappingLoadResult mapping = OtbItemIdMappingProvider::Load(analysis.workspace.itemsOtbPath, allowDuplicateClientIds);
	analysis.mappingStats = mapping.stats;
	analysis.mappingIssues = mapping.issues;
	analysis.mappingProvider = mapping.provider;
	std::cout << "[CustomConverter] items.otb: " << mapping.stats.entries << " mapping pairs, " << mapping.stats.duplicateClientIds << " ClientID aliases" << std::endl;
	if (!mapping.ready()) {
		analysis.error = mapping.error.empty() ? "items.otb mapping is unsafe." : mapping.error;
		return analysis;
	}

	if (scope.itemsXml) {
		pugi::xml_document document;
		if (!LoadXml(analysis.workspace.itemsXmlPath, document, analysis.error)) {
			return analysis;
		}
		analysis.itemsXml = ConvertItemsXmlDocument(document, *analysis.mappingProvider, true, allowDuplicateClientIds, allowDuplicateClientIds);
		std::cout << "[CustomConverter] items.xml: " << analysis.itemsXml.mappedDeclarations << " mapped, " << analysis.itemsXml.missingDeclarations << " missing" << std::endl;
		if (!analysis.itemsXml.success) {
			analysis.error = analysis.itemsXml.error;
			return analysis;
		}
	}

	if (scope.maps) {
		for (const DetectedMap& detectedMap : analysis.workspace.maps) {
			if (IsGzipFile(detectedMap.path)) {
				analysis.error = "OTGZ maps are not supported by the isolated custom-server converter. Decompress the map to OTBM before conversion.";
				return analysis;
			}
			MapVersion sourceVersion;
			if (!IOMapOTBM::getVersionInfo(FileName(PathToWxString(detectedMap.path)), sourceVersion)) {
				analysis.error = "Could not determine map version for " + PrintablePath(detectedMap.path) + '.';
				return analysis;
			}
			MapItemIdConversionOptions preflightOptions;
			preflightOptions.source = detectedMap.path;
			preflightOptions.direction = ItemIdMapping::Direction::ServerToClient;
			preflightOptions.targetVersion = sourceVersion;
			preflightOptions.mappingProvider = analysis.mappingProvider;
			preflightOptions.requireStreaming = true;
			preflightOptions.preflightOnly = true;
			preflightOptions.preserveSourceVersion = true;
			CustomServerMapAnalysis mapAnalysis { detectedMap.path, ConvertMapItemIds(preflightOptions) };
			analysis.maps.push_back(std::move(mapAnalysis));
			const MapItemIdConversionReport& preflight = analysis.maps.back().preflight;
			if (!preflight.success) {
				analysis.error = preflight.error.empty() ? "Map preflight failed." : preflight.error;
				return analysis;
			}
			if (preflight.missingItems != 0 || preflight.ambiguousItems != 0) {
				analysis.error = "Map preflight found missing or ambiguous item IDs; conversion is blocked.";
				return analysis;
			}
		}
	}
	if (!SourceDigestsMatch(analysis.sourceDigests, analysis.error)) {
		return analysis;
	}
	analysis.ready = true;
	std::cout << "[CustomConverter] Analysis ready" << std::endl;
	return analysis;
}

std::string CustomServerConversionReport::format(const CustomServerConversionOptions& options) const {
	std::ostringstream output;
	output << "CUSTOM SERVER -> CLIENTID CONVERSION " << (success ? "COMPLETE" : "FAILED") << "\n\n"
		   << "Source: " << PrintablePath(options.sourceRoot) << '\n'
		   << "Destination: " << PrintablePath(options.destinationRoot) << "\n\n";
	if (options.scope.itemsOtb) {
		output << "items.otb: " << itemsOtb.writtenItems << " identity items written, " << itemsOtb.aliasesMerged << " aliases merged\n";
	} else {
		output << "items.otb: read-only mapping input; output skipped\n";
	}
	if (options.scope.itemsXml) {
		output << "items.xml: " << itemsXml.mappedDeclarations << " mapped, " << itemsXml.expandedRanges << " ranges expanded, "
			   << itemsXml.missingDeclarations + itemsXml.missingReferences << " missing\n";
	} else {
		output << "items.xml: skipped\n";
	}
	if (options.scope.maps) {
		output << "Maps:\n";
		for (const CustomServerMapConversionReport& map : maps) {
			output << "  " << PrintablePath(map.source.filename()) << ": " << map.conversion.changedItems << " changed, " << map.conversion.missingItems << " missing\n";
			AppendMapIssues(output, map.conversion);
		}
		output << "  Sidecar references were preserved; unrelated sidecar files were not copied.\n";
	} else {
		output << "Maps: skipped\n";
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
	if (!options.scope.any()) {
		report.error = "Select at least one output: maps, items.xml, or items.otb.";
		return report;
	}
	report.analysis = AnalyzeCustomServer(options.sourceRoot, options.scope, options.allowDuplicateClientIds);
	if (!report.analysis.ready) {
		report.error = report.analysis.error;
		return report;
	}
	if (options.destinationRoot.empty()) {
		report.error = "Select a destination folder.";
		return report;
	}
	const std::filesystem::path sourceRoot = FileSaveTransaction::NormalizePath(report.analysis.workspace.rootPath);
	const std::filesystem::path destinationRoot = FileSaveTransaction::NormalizePath(options.destinationRoot);
	if (FileSaveTransaction::PathsOverlap(sourceRoot, destinationRoot)) {
		report.error = "Destination must be a separate folder outside the source server tree.";
		return report;
	}
	if (!InvokePhaseCallback(options, CustomServerConversionPhase::AfterAnalysis, report.error) || !SourceDigestsMatch(report.analysis.sourceDigests, report.error)) {
		return report;
	}

	StagingDirectory staging(destinationRoot);
	if (!staging.create(report.error)) {
		return report;
	}
	if (options.scope.maps) {
		if (!InvokePhaseCallback(options, CustomServerConversionPhase::BeforeMaps, report.error) || !SourceDigestsMatch(report.analysis.sourceDigests, report.error)) {
			return report;
		}
		for (const DetectedMap& detectedMap : report.analysis.workspace.maps) {
			std::cout << "[CustomConverter] Converting map: " << PrintablePath(detectedMap.path) << std::endl;
			if (!FileSaveTransaction::IsSameOrWithin(detectedMap.path, sourceRoot)) {
				report.error = "A detected map is outside the selected server root.";
				return report;
			}
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
			mapOptions.allowCrossDirectoryStreaming = true;
			mapOptions.requireStreaming = true;
			mapOptions.preserveSourceVersion = true;
			CustomServerMapConversionReport mapReport { detectedMap.path, destination, ConvertMapItemIds(mapOptions) };
			report.maps.push_back(std::move(mapReport));
			if (!report.maps.back().conversion.success || !report.maps.back().conversion.outputValidated) {
				report.error = report.maps.back().conversion.error.empty() ? "Map conversion failed validation." : report.maps.back().conversion.error;
				return report;
			}
			if (!SourceDigestsMatch(report.analysis.sourceDigests, report.error)) {
				return report;
			}
		}
	}
	if (options.scope.itemsXml) {
		if (!InvokePhaseCallback(options, CustomServerConversionPhase::BeforeItemsXml, report.error) || !SourceDigestsMatch(report.analysis.sourceDigests, report.error)) {
			return report;
		}
		std::cout << "[CustomConverter] Converting items.xml" << std::endl;
		if (!FileSaveTransaction::IsSameOrWithin(report.analysis.workspace.itemsXmlPath, sourceRoot)) {
			report.error = "Detected items.xml is outside the selected server root.";
			return report;
		}
		std::error_code pathError;
		const std::filesystem::path relative = std::filesystem::relative(report.analysis.workspace.itemsXmlPath, sourceRoot, pathError);
		if (pathError || EscapesRoot(relative)) {
			report.error = "Detected items.xml is outside the selected server root.";
			return report;
		}
		report.itemsXml = ConvertItemsXmlFile(
			report.analysis.workspace.itemsXmlPath,
			staging.path() / relative,
			*report.analysis.mappingProvider,
			true,
			options.allowDuplicateClientIds,
			options.allowDuplicateClientIds
		);
		if (!report.itemsXml.success || !report.itemsXml.outputValidated) {
			report.error = report.itemsXml.error.empty() ? "items.xml conversion failed validation." : report.itemsXml.error;
			return report;
		}
	}
	if (options.scope.itemsOtb) {
		if (!InvokePhaseCallback(options, CustomServerConversionPhase::BeforeItemsOtb, report.error) || !SourceDigestsMatch(report.analysis.sourceDigests, report.error)) {
			return report;
		}
		std::cout << "[CustomConverter] Converting items.otb to ClientID identity mappings" << std::endl;
		if (!FileSaveTransaction::IsSameOrWithin(report.analysis.workspace.itemsOtbPath, sourceRoot)) {
			report.error = "Detected items.otb is outside the selected server root.";
			return report;
		}
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
		if (!SourceDigestsMatch(report.analysis.sourceDigests, report.error)) {
			return report;
		}
	}
	if (!InvokePhaseCallback(options, CustomServerConversionPhase::BeforeCommit, report.error) || !SourceDigestsMatch(report.analysis.sourceDigests, report.error)) {
		return report;
	}
	if (FileSaveTransaction::PathsOverlap(sourceRoot, destinationRoot)) {
		report.error = "Destination path changed and now overlaps the source server tree.";
		return report;
	}
	if (!staging.commit(sourceRoot, report.error)) {
		return report;
	}
	report.outputValidated = true;
	report.success = true;
	std::cout << "[CustomConverter] Conversion validated and committed successfully" << std::endl;
	return report;
}
