#include "spawn_xml_converter.h"

#include "ext/pugixml.hpp"
#include "file_transaction.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <new>
#include <set>
#include <system_error>
#include <utility>

namespace {
	constexpr size_t MAX_REPORT_WARNINGS = 100;

	const std::set<std::string> AREA_ATTRIBUTES = {"centerx", "centery", "centerz", "radius"};
	const std::set<std::string> CREATURE_ATTRIBUTES = {"name", "x", "y", "z", "spawntime", "direction"};

	void AddWarning(std::vector<std::string>& warnings, std::string warning) {
		if (warnings.size() < MAX_REPORT_WARNINGS) {
			warnings.push_back(std::move(warning));
		} else if (warnings.size() == MAX_REPORT_WARNINGS) {
			warnings.emplace_back("Additional warnings were omitted from the report.");
		}
	}

	bool IsRegularFile(const std::filesystem::path& path) {
		if (path.empty()) {
			return false;
		}
		std::error_code error;
		return std::filesystem::is_regular_file(path, error) && !error;
	}

	std::string LayoutRoot(SpawnXmlLayout layout) {
		switch (layout) {
			case SpawnXmlLayout::TfsCombined: return "spawns";
			case SpawnXmlLayout::CanaryMonsters: return "monsters";
			case SpawnXmlLayout::CanaryNpcs: return "npcs";
			default: return {};
		}
	}

	SpawnXmlLayout LayoutFromRoot(const std::string& root) {
		if (root == "spawns") {
			return SpawnXmlLayout::TfsCombined;
		}
		if (root == "monsters") {
			return SpawnXmlLayout::CanaryMonsters;
		}
		if (root == "npcs") {
			return SpawnXmlLayout::CanaryNpcs;
		}
		return SpawnXmlLayout::Unknown;
	}

	std::string XmlParseError(const std::filesystem::path& file, const pugi::xml_parse_result& parseResult) {
		std::ifstream input(file, std::ios::binary);
		size_t line = 1;
		for (ptrdiff_t index = 0; input && index < parseResult.offset; ++index) {
			if (input.get() == '\n') {
				++line;
			}
		}
		return file.string() + ":" + std::to_string(line) + ": " + parseResult.description();
	}

	size_t NodeLine(const std::filesystem::path& file, const pugi::xml_node& node) {
		const ptrdiff_t offset = node.offset_debug();
		if (offset < 0) {
			return 0;
		}
		std::ifstream input(file, std::ios::binary);
		size_t line = 1;
		for (ptrdiff_t index = 0; input && index < offset; ++index) {
			if (input.get() == '\n') {
				++line;
			}
		}
		return line;
	}

	std::string NodeLocation(const std::filesystem::path& file, const pugi::xml_node& node) {
		const size_t line = NodeLine(file, node);
		return file.string() + (line == 0 ? std::string() : ":" + std::to_string(line));
	}

	bool IsInteger(const char* value) {
		if (value == nullptr || *value == '\0') {
			return false;
		}
		int64_t parsed = 0;
		const char* end = value + std::char_traits<char>::length(value);
		const std::from_chars_result result = std::from_chars(value, end, parsed);
		return result.ec == std::errc() && result.ptr == end;
	}

	bool IsNonWhitespaceText(const pugi::xml_node& node) {
		return (node.type() == pugi::node_pcdata || node.type() == pugi::node_cdata) &&
			std::string(node.value()).find_first_not_of(" \t\r\n") != std::string::npos;
	}

	std::vector<SpawnXmlAttribute> ReadAttributes(const pugi::xml_node& node) {
		std::vector<SpawnXmlAttribute> attributes;
		for (const pugi::xml_attribute& attribute : node.attributes()) {
			attributes.push_back({attribute.name(), attribute.value()});
		}
		return attributes;
	}

	const SpawnXmlAttribute* FindAttribute(const std::vector<SpawnXmlAttribute>& attributes, const std::string& name) {
		const auto iterator = std::find_if(attributes.begin(), attributes.end(), [&](const SpawnXmlAttribute& attribute) {
			return attribute.name == name;
		});
		return iterator == attributes.end() ? nullptr : &*iterator;
	}

	size_t CountAdditionalAttributes(const std::vector<SpawnXmlAttribute>& attributes, const std::set<std::string>& known) {
		return std::count_if(attributes.begin(), attributes.end(), [&](const SpawnXmlAttribute& attribute) {
			return !known.contains(attribute.name);
		});
	}

	bool ValidateAreaAttributes(const std::filesystem::path& file, const pugi::xml_node& node, const SpawnXmlArea& area, std::string& error) {
		for (const std::string& name : AREA_ATTRIBUTES) {
			const SpawnXmlAttribute* attribute = FindAttribute(area.attributes, name);
			if (attribute == nullptr) {
				error = NodeLocation(file, node) + ": <" + node.name() + "> is missing required attribute '" + name + "'.";
				return false;
			}
			if (!IsInteger(attribute->value.c_str())) {
				error = NodeLocation(file, node) + ": <" + node.name() + "> attribute '" + name + "' has value '" + attribute->value + "'; expected an integer.";
				return false;
			}
		}
		return true;
	}

	bool ValidateCreatureAttributes(const std::filesystem::path& file, const pugi::xml_node& node, const SpawnXmlCreature& creature, std::string& error) {
		const SpawnXmlAttribute* name = FindAttribute(creature.attributes, "name");
		if (name == nullptr || name->value.empty()) {
			error = NodeLocation(file, node) + ": <" + node.name() + "> creature is missing a non-empty 'name' attribute.";
			return false;
		}
		for (const char* numericName : {"x", "y", "z", "spawntime", "direction"}) {
			if (const SpawnXmlAttribute* attribute = FindAttribute(creature.attributes, numericName); attribute != nullptr && !IsInteger(attribute->value.c_str())) {
				error = NodeLocation(file, node) + ": creature '" + name->value + "' attribute '" + numericName + "' has value '" + attribute->value + "'; expected an integer.";
				return false;
			}
		}
		for (const pugi::xml_node& child : node.children()) {
			if (child.type() == pugi::node_element || IsNonWhitespaceText(child)) {
				error = NodeLocation(file, node) + ": creature '" + name->value + "' contains nested XML content that cannot be represented safely by this converter.";
				return false;
			}
		}
		return true;
	}

	bool LoadDocument(const std::filesystem::path& file, SpawnXmlLayout expectedLayout, SpawnXmlDocument& document, size_t& additionalAttributes, std::string& error) {
		document = {};
		additionalAttributes = 0;
		if (!IsRegularFile(file)) {
			error = "Spawn XML file was not found: " + file.string();
			return false;
		}

		pugi::xml_document xml;
		const pugi::xml_parse_result parseResult = xml.load_file(file.c_str(), pugi::parse_default, pugi::encoding_auto);
		if (!parseResult) {
			error = XmlParseError(file, parseResult);
			return false;
		}
		const pugi::xml_node root = xml.document_element();
		if (!root) {
			error = file.string() + ": XML document is empty; expected <" + LayoutRoot(expectedLayout) + ">.";
			return false;
		}
		const std::string rootName = root.name();
		const SpawnXmlLayout actualLayout = LayoutFromRoot(rootName);
		if (actualLayout != expectedLayout) {
			error = "File '" + file.string() + "' was expected to contain <" + LayoutRoot(expectedLayout) + ">, but its root node is <" + rootName + ">.";
			return false;
		}

		document.layout = actualLayout;
		const std::string areaTag = actualLayout == SpawnXmlLayout::TfsCombined ? "spawn" : (actualLayout == SpawnXmlLayout::CanaryMonsters ? "monster" : "npc");
		for (const pugi::xml_node& areaNode : root.children()) {
			if (areaNode.type() != pugi::node_element) {
				if (IsNonWhitespaceText(areaNode)) {
					error = NodeLocation(file, root) + ": root <" + rootName + "> contains text outside a spawn area.";
					return false;
				}
				continue;
			}
			if (std::string(areaNode.name()) != areaTag) {
				error = NodeLocation(file, areaNode) + ": invalid grouping node <" + areaNode.name() + ">; expected <" + areaTag + ">.";
				return false;
			}
			SpawnXmlArea area;
			area.attributes = ReadAttributes(areaNode);
			if (!ValidateAreaAttributes(file, areaNode, area, error)) {
				return false;
			}
			additionalAttributes += CountAdditionalAttributes(area.attributes, AREA_ATTRIBUTES);
			for (const pugi::xml_node& creatureNode : areaNode.children()) {
				if (creatureNode.type() != pugi::node_element) {
					if (IsNonWhitespaceText(creatureNode)) {
						error = NodeLocation(file, areaNode) + ": grouping node <" + areaTag + "> contains text outside a creature node.";
						return false;
					}
					continue;
				}
				const std::string creatureTag = creatureNode.name();
				const bool monster = creatureTag == "monster";
				const bool npc = creatureTag == "npc";
				const bool allowed = actualLayout == SpawnXmlLayout::TfsCombined
					? monster || npc
					: (actualLayout == SpawnXmlLayout::CanaryMonsters ? monster : npc);
				if (!allowed) {
					error = NodeLocation(file, creatureNode) + ": invalid creature node <" + creatureTag + "> inside <" + areaTag + ">.";
					return false;
				}
				SpawnXmlCreature creature;
				creature.kind = npc ? SpawnXmlCreatureKind::Npc : SpawnXmlCreatureKind::Monster;
				creature.attributes = ReadAttributes(creatureNode);
				if (!ValidateCreatureAttributes(file, creatureNode, creature, error)) {
					return false;
				}
				additionalAttributes += CountAdditionalAttributes(creature.attributes, CREATURE_ATTRIBUTES);
				area.creatures.push_back(std::move(creature));
			}
			document.areas.push_back(std::move(area));
		}
		return true;
	}

	void AppendAttributes(pugi::xml_node node, const std::vector<SpawnXmlAttribute>& attributes) {
		for (const SpawnXmlAttribute& attribute : attributes) {
			node.append_attribute(attribute.name.c_str()).set_value(attribute.value.c_str());
		}
	}

	bool SaveStagedDocument(const SpawnXmlDocument& document, const std::filesystem::path& stagedFile, std::string& error) {
		pugi::xml_document xml;
		pugi::xml_node declaration = xml.append_child(pugi::node_declaration);
		declaration.append_attribute("version") = "1.0";
		declaration.append_attribute("encoding") = "UTF-8";
		pugi::xml_node root = xml.append_child(LayoutRoot(document.layout).c_str());
		const char* areaTag = document.layout == SpawnXmlLayout::TfsCombined ? "spawn" : (document.layout == SpawnXmlLayout::CanaryMonsters ? "monster" : "npc");
		for (const SpawnXmlArea& area : document.areas) {
			pugi::xml_node areaNode = root.append_child(areaTag);
			AppendAttributes(areaNode, area.attributes);
			for (const SpawnXmlCreature& creature : area.creatures) {
				pugi::xml_node creatureNode = areaNode.append_child(creature.kind == SpawnXmlCreatureKind::Npc ? "npc" : "monster");
				AppendAttributes(creatureNode, creature.attributes);
			}
		}
		if (!xml.save_file(stagedFile.c_str(), "\t", pugi::format_default, pugi::encoding_utf8)) {
			error = "Could not write temporary spawn XML beside " + stagedFile.parent_path().string() + ". Check permissions and free disk space.";
			return false;
		}
		return true;
	}

	bool EnsureParentDirectory(const std::filesystem::path& file, std::string& error) {
		if (file.empty()) {
			error = "Output spawn XML path is empty.";
			return false;
		}
		if (file.parent_path().empty()) {
			return true;
		}
		std::error_code filesystemError;
		std::filesystem::create_directories(file.parent_path(), filesystemError);
		if (filesystemError) {
			error = "Could not create output directory '" + file.parent_path().string() + "': " + filesystemError.message();
			return false;
		}
		return true;
	}

	bool CheckOverwrite(const std::vector<std::filesystem::path>& outputs, bool allowOverwrite, SpawnXmlConversionResult& result) {
		if (allowOverwrite) {
			return true;
		}
		for (const std::filesystem::path& output : outputs) {
			std::error_code filesystemError;
			if (std::filesystem::exists(output, filesystemError) && !filesystemError) {
				result.overwriteRequired = true;
				result.error = "Output file already exists and overwrite was not confirmed: " + output.string();
				return false;
			}
			if (filesystemError) {
				result.error = "Could not inspect output file '" + output.string() + "': " + filesystemError.message();
				return false;
			}
		}
		return true;
	}

	bool ReportProgress(const SpawnXmlProgressCallback& callback, int value, const char* phase, SpawnXmlConversionResult& result) {
		if (callback && !callback(value, phase)) {
			result.canceled = true;
			result.error = "Conversion canceled. No output files were committed.";
			return false;
		}
		return true;
	}

	void ReportCompletion(const SpawnXmlProgressCallback& callback) noexcept {
		if (!callback) {
			return;
		}
		try {
			// Completion is reported only after commit. A UI callback must not turn
			// an already committed, validated conversion into a reported failure.
			static_cast<void>(callback(100, "Conversion completed"));
		} catch (...) {
		}
	}

	void CountDocument(const SpawnXmlDocument& document, SpawnXmlConversionReport& report) {
		for (const SpawnXmlArea& area : document.areas) {
			bool hasMonster = false;
			bool hasNpc = false;
			for (const SpawnXmlCreature& creature : area.creatures) {
				if (creature.kind == SpawnXmlCreatureKind::Npc) {
					++report.npcs;
					hasNpc = true;
				} else {
					++report.monsters;
					hasMonster = true;
				}
			}
			report.monsterAreas += hasMonster ? 1 : 0;
			report.npcAreas += hasNpc ? 1 : 0;
		}
	}

	bool SameDocument(const SpawnXmlDocument& expected, const SpawnXmlDocument& actual, std::string& difference) {
		if (expected.layout != actual.layout) {
			difference = "output layout changed.";
			return false;
		}
		if (expected.areas.size() != actual.areas.size()) {
			difference = "expected " + std::to_string(expected.areas.size()) + " areas, got " + std::to_string(actual.areas.size()) + ".";
			return false;
		}
		for (size_t areaIndex = 0; areaIndex < expected.areas.size(); ++areaIndex) {
			const SpawnXmlArea& leftArea = expected.areas[areaIndex];
			const SpawnXmlArea& rightArea = actual.areas[areaIndex];
			if (leftArea.attributes != rightArea.attributes) {
				difference = "area " + std::to_string(areaIndex + 1) + " attributes changed.";
				return false;
			}
			if (leftArea.creatures.size() != rightArea.creatures.size()) {
				difference = "area " + std::to_string(areaIndex + 1) + " expected " + std::to_string(leftArea.creatures.size()) + " creatures, got " + std::to_string(rightArea.creatures.size()) + ".";
				return false;
			}
			for (size_t creatureIndex = 0; creatureIndex < leftArea.creatures.size(); ++creatureIndex) {
				if (leftArea.creatures[creatureIndex] != rightArea.creatures[creatureIndex]) {
					difference = "area " + std::to_string(areaIndex + 1) + ", creature " + std::to_string(creatureIndex + 1) + " changed kind, order or attributes.";
					return false;
				}
			}
		}
		return true;
	}

	using NativePathString = std::filesystem::path::string_type;

	NativePathString StripSuffix(NativePathString stem, const std::vector<NativePathString>& suffixes) {
		for (const NativePathString& suffix : suffixes) {
			if (stem == suffix.substr(1)) {
				return {};
			}
			if (stem.size() > suffix.size() && stem.ends_with(suffix)) {
				return stem.substr(0, stem.size() - suffix.size());
			}
		}
		return stem;
	}

	std::filesystem::path NamedFile(const std::filesystem::path& directory, const NativePathString& base, const char* suffix) {
		NativePathString filename = base;
		if (!filename.empty()) {
			filename += std::filesystem::path("-").native();
		}
		filename += std::filesystem::path(suffix).native();
		filename += std::filesystem::path(".xml").native();
		return directory / std::filesystem::path(filename);
	}

	void FinishDuration(SpawnXmlConversionResult& result, const std::chrono::steady_clock::time_point started) {
		result.report.duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
	}
}

SpawnXmlInspection SpawnXmlConverter::Inspect(const std::filesystem::path& file) {
	SpawnXmlInspection inspection;
	if (!IsRegularFile(file)) {
		inspection.error = "Spawn XML file was not found: " + file.string();
		return inspection;
	}
	pugi::xml_document xml;
	const pugi::xml_parse_result parseResult = xml.load_file(file.c_str(), pugi::parse_default, pugi::encoding_auto);
	if (!parseResult) {
		inspection.error = XmlParseError(file, parseResult);
		return inspection;
	}
	const pugi::xml_node root = xml.document_element();
	if (!root) {
		inspection.error = file.string() + ": XML document is empty.";
		return inspection;
	}
	inspection.rootName = root.name();
	inspection.layout = LayoutFromRoot(inspection.rootName);
	if (inspection.layout == SpawnXmlLayout::Unknown) {
		inspection.error = file.string() + ": unsupported root <" + inspection.rootName + ">; expected <spawns>, <monsters> or <npcs>.";
	}
	return inspection;
}

SpawnXmlSuggestedPaths SpawnXmlConverter::SuggestFromCanaryFile(const std::filesystem::path& selectedFile, SpawnXmlLayout selectedLayout) {
	SpawnXmlSuggestedPaths paths;
	const std::filesystem::path directory = selectedFile.parent_path();
	const NativePathString base = StripSuffix(selectedFile.stem().native(), {
		std::filesystem::path("-monsters").native(),
		std::filesystem::path("-monster").native(),
		std::filesystem::path("-npcs").native(),
		std::filesystem::path("-npc").native(),
	});
	paths.monsterFile = NamedFile(directory, base, "monster");
	paths.npcFile = NamedFile(directory, base, "npc");
	paths.tfsFile = NamedFile(directory, base, "spawn");
	if (selectedLayout == SpawnXmlLayout::CanaryMonsters) {
		paths.monsterFile = selectedFile;
	} else if (selectedLayout == SpawnXmlLayout::CanaryNpcs) {
		paths.npcFile = selectedFile;
	}
	return paths;
}

SpawnXmlSuggestedPaths SpawnXmlConverter::SuggestFromTfsFile(const std::filesystem::path& selectedFile, const std::filesystem::path& outputDirectory, bool pluralMonsterFilename) {
	SpawnXmlSuggestedPaths paths;
	const std::filesystem::path directory = outputDirectory.empty() ? selectedFile.parent_path() : outputDirectory;
	const NativePathString base = StripSuffix(selectedFile.stem().native(), {std::filesystem::path("-spawn").native()});
	paths.tfsFile = selectedFile;
	paths.monsterFile = NamedFile(directory, base, pluralMonsterFilename ? "monsters" : "monster");
	paths.npcFile = NamedFile(directory, base, "npc");
	return paths;
}

SpawnXmlConversionResult SpawnXmlConverter::ConvertCanaryToTfs(
	const std::filesystem::path& monsterFile,
	const std::filesystem::path& npcFile,
	const std::filesystem::path& outputFile,
	bool allowOverwrite,
	const SpawnXmlProgressCallback& progress
) {
	const auto started = std::chrono::steady_clock::now();
	SpawnXmlConversionResult result;
	result.report.direction = "Canary / Crystal 11.x -> TFS 1.8 / protocol 8.60";
	result.report.outputFiles = {outputFile};
	const auto finish = [&] { FinishDuration(result, started); };
	try {
		if (!ReportProgress(progress, 0, "Preparing", result)) {
			finish();
			return result;
		}
		const bool hasMonsterFile = IsRegularFile(monsterFile);
		const bool hasNpcFile = IsRegularFile(npcFile);
		if (!hasMonsterFile && !hasNpcFile) {
			result.error = "No valid Canary/Crystal spawn XML was provided. Select at least one existing <monsters> or <npcs> file.";
			finish();
			return result;
		}
		if (outputFile.empty()) {
			result.error = "Select an output TFS spawn XML file.";
			finish();
			return result;
		}
		if ((hasMonsterFile && FileSaveTransaction::PathsReferToSameFile(monsterFile, outputFile)) ||
			(hasNpcFile && FileSaveTransaction::PathsReferToSameFile(npcFile, outputFile))) {
			result.error = "Source and destination spawn XML files must be different.";
			finish();
			return result;
		}
		if (!CheckOverwrite({outputFile}, allowOverwrite, result)) {
			finish();
			return result;
		}
		if (!ReportProgress(progress, 10, "Reading source XML", result)) {
			finish();
			return result;
		}

		SpawnXmlDocument monsters;
		SpawnXmlDocument npcs;
		size_t monsterExtras = 0;
		size_t npcExtras = 0;
		if (hasMonsterFile) {
			result.report.sourceFiles.push_back(monsterFile);
			if (!LoadDocument(monsterFile, SpawnXmlLayout::CanaryMonsters, monsters, monsterExtras, result.error)) {
				finish();
				return result;
			}
		} else {
			AddWarning(result.report.warnings, "Monster spawn file was not found. The output will contain only NPCs.");
		}
		if (hasNpcFile) {
			result.report.sourceFiles.push_back(npcFile);
			if (!LoadDocument(npcFile, SpawnXmlLayout::CanaryNpcs, npcs, npcExtras, result.error)) {
				finish();
				return result;
			}
		} else {
			AddWarning(result.report.warnings, "NPC spawn file was not found. The output will contain only monsters.");
		}
		result.report.additionalAttributes = monsterExtras + npcExtras;
		if (!ReportProgress(progress, 25, "Validating format", result) || !ReportProgress(progress, 40, "Reading spawn areas", result)) {
			finish();
			return result;
		}

		SpawnXmlDocument combined;
		combined.layout = SpawnXmlLayout::TfsCombined;
		combined.areas.reserve(monsters.areas.size() + npcs.areas.size());
		combined.areas.insert(combined.areas.end(), monsters.areas.begin(), monsters.areas.end());
		combined.areas.insert(combined.areas.end(), npcs.areas.begin(), npcs.areas.end());
		CountDocument(combined, result.report);
		if (!ReportProgress(progress, 60, "Converting structure", result)) {
			finish();
			return result;
		}
		if (!EnsureParentDirectory(outputFile, result.error)) {
			finish();
			return result;
		}
		if (!ReportProgress(progress, 80, "Writing temporary files", result)) {
			finish();
			return result;
		}
		FileSaveTransaction transaction;
		const std::filesystem::path stagedFile = transaction.Stage(outputFile);
		if (!SaveStagedDocument(combined, stagedFile, result.error)) {
			finish();
			return result;
		}
		if (!ReportProgress(progress, 90, "Reopening and validating", result)) {
			finish();
			return result;
		}
		SpawnXmlDocument reopened;
		size_t ignoredExtras = 0;
		if (!LoadDocument(stagedFile, SpawnXmlLayout::TfsCombined, reopened, ignoredExtras, result.error)) {
			result.error = "Generated TFS XML failed validation: " + result.error;
			finish();
			return result;
		}
		std::string difference;
		if (!SameDocument(combined, reopened, difference)) {
			result.error = "Generated TFS XML changed spawn data: " + difference;
			finish();
			return result;
		}
		if (!transaction.Commit(result.error)) {
			finish();
			return result;
		}
		result.report.validationPassed = true;
		result.success = true;
		ReportCompletion(progress);
	} catch (const std::bad_alloc&) {
		result.success = false;
		result.error = "Conversion ran out of memory. No output files were committed.";
	} catch (const std::filesystem::filesystem_error& exception) {
		result.success = false;
		result.error = "Spawn file operation failed: " + std::string(exception.what());
	} catch (const std::exception& exception) {
		result.success = false;
		result.error = "Spawn conversion failed safely: " + std::string(exception.what());
	}
	finish();
	return result;
}

SpawnXmlConversionResult SpawnXmlConverter::ConvertTfsToCanary(
	const std::filesystem::path& sourceFile,
	const std::filesystem::path& monsterOutputFile,
	const std::filesystem::path& npcOutputFile,
	bool allowOverwrite,
	const SpawnXmlProgressCallback& progress
) {
	const auto started = std::chrono::steady_clock::now();
	SpawnXmlConversionResult result;
	result.report.direction = "TFS 1.8 / protocol 8.60 -> Canary / Crystal 11.x";
	result.report.sourceFiles = {sourceFile};
	result.report.outputFiles = {monsterOutputFile, npcOutputFile};
	const auto finish = [&] { FinishDuration(result, started); };
	try {
		if (!ReportProgress(progress, 0, "Preparing", result)) {
			finish();
			return result;
		}
		if (!IsRegularFile(sourceFile)) {
			result.error = "TFS spawn XML file was not found: " + sourceFile.string();
			finish();
			return result;
		}
		if (monsterOutputFile.empty() || npcOutputFile.empty()) {
			result.error = "Select both Canary/Crystal output XML files.";
			finish();
			return result;
		}
		if (FileSaveTransaction::PathsReferToSameFile(monsterOutputFile, npcOutputFile)) {
			result.error = "Monster and NPC output files must be different.";
			finish();
			return result;
		}
		if (FileSaveTransaction::PathsReferToSameFile(sourceFile, monsterOutputFile) || FileSaveTransaction::PathsReferToSameFile(sourceFile, npcOutputFile)) {
			result.error = "Source and destination spawn XML files must be different.";
			finish();
			return result;
		}
		if (!CheckOverwrite({monsterOutputFile, npcOutputFile}, allowOverwrite, result)) {
			finish();
			return result;
		}
		if (!ReportProgress(progress, 10, "Reading source XML", result)) {
			finish();
			return result;
		}
		SpawnXmlDocument source;
		size_t sourceExtras = 0;
		if (!LoadDocument(sourceFile, SpawnXmlLayout::TfsCombined, source, sourceExtras, result.error)) {
			finish();
			return result;
		}
		result.report.additionalAttributes = sourceExtras;
		if (!ReportProgress(progress, 25, "Validating format", result) || !ReportProgress(progress, 40, "Reading spawn areas", result)) {
			finish();
			return result;
		}

		SpawnXmlDocument monsters;
		monsters.layout = SpawnXmlLayout::CanaryMonsters;
		SpawnXmlDocument npcs;
		npcs.layout = SpawnXmlLayout::CanaryNpcs;
		size_t emptyAreas = 0;
		for (const SpawnXmlArea& area : source.areas) {
			SpawnXmlArea monsterArea;
			monsterArea.attributes = area.attributes;
			SpawnXmlArea npcArea;
			npcArea.attributes = area.attributes;
			for (const SpawnXmlCreature& creature : area.creatures) {
				(creature.kind == SpawnXmlCreatureKind::Npc ? npcArea.creatures : monsterArea.creatures).push_back(creature);
			}
			if (!monsterArea.creatures.empty()) {
				monsters.areas.push_back(std::move(monsterArea));
			}
			if (!npcArea.creatures.empty()) {
				npcs.areas.push_back(std::move(npcArea));
			}
			if (area.creatures.empty()) {
				++emptyAreas;
			}
		}
		if (emptyAreas > 0) {
			AddWarning(result.report.warnings, std::to_string(emptyAreas) + " empty TFS <spawn> area(s) were skipped; no empty Canary groups were created.");
		}
		CountDocument(source, result.report);
		if (!ReportProgress(progress, 60, "Converting structure", result)) {
			finish();
			return result;
		}
		if (!EnsureParentDirectory(monsterOutputFile, result.error) || !EnsureParentDirectory(npcOutputFile, result.error)) {
			finish();
			return result;
		}
		if (!ReportProgress(progress, 80, "Writing temporary files", result)) {
			finish();
			return result;
		}
		FileSaveTransaction transaction;
		const std::filesystem::path stagedMonsterFile = transaction.Stage(monsterOutputFile);
		const std::filesystem::path stagedNpcFile = transaction.Stage(npcOutputFile);
		if (!SaveStagedDocument(monsters, stagedMonsterFile, result.error) || !SaveStagedDocument(npcs, stagedNpcFile, result.error)) {
			finish();
			return result;
		}
		if (!ReportProgress(progress, 90, "Reopening and validating", result)) {
			finish();
			return result;
		}
		SpawnXmlDocument reopenedMonsters;
		SpawnXmlDocument reopenedNpcs;
		size_t ignoredExtras = 0;
		if (!LoadDocument(stagedMonsterFile, SpawnXmlLayout::CanaryMonsters, reopenedMonsters, ignoredExtras, result.error)) {
			result.error = "Generated monster XML failed validation: " + result.error;
			finish();
			return result;
		}
		if (!LoadDocument(stagedNpcFile, SpawnXmlLayout::CanaryNpcs, reopenedNpcs, ignoredExtras, result.error)) {
			result.error = "Generated NPC XML failed validation: " + result.error;
			finish();
			return result;
		}
		std::string difference;
		if (!SameDocument(monsters, reopenedMonsters, difference)) {
			result.error = "Generated monster XML changed spawn data: " + difference;
			finish();
			return result;
		}
		if (!SameDocument(npcs, reopenedNpcs, difference)) {
			result.error = "Generated NPC XML changed spawn data: " + difference;
			finish();
			return result;
		}
		if (!transaction.Commit(result.error)) {
			finish();
			return result;
		}
		result.report.validationPassed = true;
		result.success = true;
		ReportCompletion(progress);
	} catch (const std::bad_alloc&) {
		result.success = false;
		result.error = "Conversion ran out of memory. No output files were committed.";
	} catch (const std::filesystem::filesystem_error& exception) {
		result.success = false;
		result.error = "Spawn file operation failed: " + std::string(exception.what());
	} catch (const std::exception& exception) {
		result.success = false;
		result.error = "Spawn conversion failed safely: " + std::string(exception.what());
	}
	finish();
	return result;
}
