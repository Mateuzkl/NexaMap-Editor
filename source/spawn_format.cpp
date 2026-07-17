#include "spawn_format.h"

#include "ext/pugixml.hpp"
#include "file_transaction.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <tuple>

namespace {
	constexpr int TFS_MIN_SPAWN_TIME = 10;
	constexpr int MAX_SPAWN_TIME = 24 * 60 * 60;
	constexpr int MIN_DIRECTION = 0;
	constexpr int MAX_DIRECTION = 3;
	constexpr size_t MAX_REPORTED_WARNINGS = 200;

	void AppendWarning(std::vector<std::string>& warnings, std::string warning) {
		if (warnings.size() < MAX_REPORTED_WARNINGS) {
			warnings.push_back(std::move(warning));
		} else if (warnings.size() == MAX_REPORTED_WARNINGS) {
			warnings.emplace_back("Additional warnings were omitted to keep the report responsive.");
		}
	}

	std::string Lower(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return value;
	}

	std::filesystem::path ResolveFile(const std::filesystem::path& directory, const std::string& filename) {
		if (filename.empty()) {
			return {};
		}
		std::filesystem::path path(filename);
		return path.is_absolute() ? path : directory / path;
	}

	std::string RootName(const std::filesystem::path& file) {
		std::error_code filesystemError;
		if (file.empty() || !std::filesystem::exists(file, filesystemError) || filesystemError) {
			return {};
		}
		pugi::xml_document doc;
		if (!doc.load_file(file.string().c_str())) {
			return {};
		}
		return Lower(doc.document_element().name());
	}

	std::string XmlError(const std::filesystem::path& file, const pugi::xml_parse_result& result) {
		std::ifstream input(file, std::ios::binary);
		size_t line = 1;
		if (input) {
			for (ptrdiff_t index = 0; index < result.offset && input; ++index) {
				if (input.get() == '\n') {
					++line;
				}
			}
		}
		return file.string() + ":" + std::to_string(line) + ": " + result.description();
	}

	SpawnAttributeMap ReadExtraAttributes(const pugi::xml_node& node, const std::set<std::string>& known) {
		SpawnAttributeMap attributes;
		for (const pugi::xml_attribute& attribute : node.attributes()) {
			if (!known.contains(attribute.name())) {
				attributes[attribute.name()] = attribute.value();
			}
		}
		return attributes;
	}

	void AppendExtraAttributes(pugi::xml_node node, const SpawnAttributeMap& attributes, const std::set<std::string>& reserved) {
		for (const auto& [name, value] : attributes) {
			if (!reserved.contains(name) && !node.attribute(name.c_str())) {
				node.append_attribute(name.c_str()) = value.c_str();
			}
		}
	}

	bool ReadInteger(const pugi::xml_attribute& attribute, int& value) {
		if (!attribute) {
			return false;
		}
		const char* begin = attribute.value();
		const char* end = begin + std::strlen(begin);
		const std::from_chars_result result = std::from_chars(begin, end, value);
		return result.ec == std::errc() && result.ptr == end;
	}

	bool ReadAreaPosition(const pugi::xml_node& node, SpawnAreaData& area, const std::filesystem::path& file, std::vector<std::string>& warnings, bool allowZeroRadius) {
		const pugi::xml_attribute centerX = node.attribute("centerx");
		const pugi::xml_attribute centerY = node.attribute("centery");
		const pugi::xml_attribute centerZ = node.attribute("centerz");
		if (!centerX || !centerY || !centerZ) {
			AppendWarning(warnings, file.string() + ": spawn area is missing centerx, centery or centerz and was skipped.");
			return false;
		}
		if (!ReadInteger(centerX, area.centerX) || !ReadInteger(centerY, area.centerY) || !ReadInteger(centerZ, area.centerZ)) {
			AppendWarning(warnings, file.string() + ": spawn area has a non-numeric centerx, centery or centerz and was skipped.");
			return false;
		}
		const pugi::xml_attribute radius = node.attribute("radius");
		if (!radius) {
			if (!allowZeroRadius) {
				AppendWarning(warnings, file.string() + ": TFS spawn area is missing radius and was skipped.");
				return false;
			}
			area.radius = 0;
			AppendWarning(warnings, file.string() + ": Canary/Crystal spawn area has no radius; the editor will use the smallest radius containing its entries.");
		} else if (!ReadInteger(radius, area.radius)) {
			AppendWarning(warnings, file.string() + ": spawn area has a non-numeric radius and was skipped.");
			return false;
		}
		area.attributes = ReadExtraAttributes(node, {"centerx", "centery", "centerz", "radius"});
		if (area.centerX < 0 || area.centerX > 65535 || area.centerY < 0 || area.centerY > 65535 || area.centerZ < 0 || area.centerZ > 255) {
			AppendWarning(warnings, file.string() + ": spawn center is outside the server coordinate range and was skipped.");
			return false;
		}
		const int minimumRadius = allowZeroRadius ? 0 : 1;
		if (area.radius < minimumRadius || area.radius > 255) {
			AppendWarning(warnings, file.string() + ": spawn radius must be between " + std::to_string(minimumRadius) + " and 255 and the area was skipped.");
			return false;
		}
		return true;
	}

	bool ReadEntryPosition(const pugi::xml_node& node, const SpawnAreaData& area, SpawnEntryData& entry, const std::filesystem::path& file, std::vector<std::string>& warnings) {
		if (!node.attribute("x") || !node.attribute("y")) {
			AppendWarning(warnings, file.string() + ": creature entry is missing x or y and was skipped.");
			return false;
		}
		int offsetX = 0;
		int offsetY = 0;
		if (!ReadInteger(node.attribute("x"), offsetX) || !ReadInteger(node.attribute("y"), offsetY)) {
			AppendWarning(warnings, file.string() + ": creature entry has a non-numeric x or y offset and was skipped.");
			return false;
		}
		const int64_t absoluteX = static_cast<int64_t>(area.centerX) + static_cast<int64_t>(offsetX);
		const int64_t absoluteY = static_cast<int64_t>(area.centerY) + static_cast<int64_t>(offsetY);
		entry.z = area.centerZ;
		if (node.attribute("z")) {
			int childZ = 0;
			if (!ReadInteger(node.attribute("z"), childZ)) {
				AppendWarning(warnings, file.string() + ": creature has a non-numeric z; centerz was used.");
			} else if (childZ != area.centerZ) {
				AppendWarning(warnings, file.string() + ": creature z differs from centerz; the real server parser uses centerz.");
			}
		}
		if (absoluteX < 0 || absoluteX > 65535 || absoluteY < 0 || absoluteY > 65535 || entry.z < 0 || entry.z > 255) {
			AppendWarning(warnings, file.string() + ": creature position is outside the server coordinate range and was skipped.");
			return false;
		}
		entry.x = static_cast<int>(absoluteX);
		entry.y = static_cast<int>(absoluteY);
		return true;
	}

	bool ExpandAreaToContain(const SpawnEntryData& entry, SpawnAreaData& area, const std::filesystem::path& file, std::vector<std::string>& warnings) {
		const int requiredRadius = std::max(std::abs(entry.x - area.centerX), std::abs(entry.y - area.centerY));
		if (requiredRadius <= area.radius) {
			return true;
		}
		if (requiredRadius > 255) {
			AppendWarning(warnings, file.string() + ": creature '" + entry.name + "' is more than 255 tiles from its spawn center and was skipped.");
			return false;
		}
		AppendWarning(warnings, file.string() + ": creature '" + entry.name + "' is outside the declared radius; the in-memory radius was expanded to " + std::to_string(requiredRadius) + ".");
		area.radius = requiredRadius;
		return true;
	}

	void ReadCommonEntryAttributes(const pugi::xml_node& node, SpawnEntryData& entry, SpawnFormat format, const SpawnLoadDefaults& defaults, const std::filesystem::path& file, std::vector<std::string>& warnings) {
		entry.name = node.attribute("name").as_string();
		const int defaultDirection = format == SpawnFormat::Tfs && entry.isNpc ? 2 : MIN_DIRECTION;
		entry.direction = defaultDirection;
		entry.spawnTime = std::clamp(defaults.spawnTime, 1, MAX_SPAWN_TIME);
		int numericValue = 0;
		if (node.attribute("spawntime")) {
			if (ReadInteger(node.attribute("spawntime"), numericValue) && numericValue >= 1 && numericValue <= MAX_SPAWN_TIME) {
				entry.spawnTime = numericValue;
			} else {
				AppendWarning(warnings, file.string() + ": creature '" + entry.name + "' has invalid spawntime '" + node.attribute("spawntime").value() + "'; editor default " + std::to_string(entry.spawnTime) + " was used.");
			}
		} else if (format == SpawnFormat::CanaryCrystal) {
			AppendWarning(warnings, file.string() + ": creature '" + entry.name + "' has no spawntime; editor default " + std::to_string(entry.spawnTime) + " was used.");
		}
		if (node.attribute("direction")) {
			if (ReadInteger(node.attribute("direction"), numericValue) && numericValue >= MIN_DIRECTION && numericValue <= MAX_DIRECTION) {
				entry.direction = numericValue;
				entry.hasDirection = true;
			} else {
				entry.direction = defaultDirection;
				entry.hasDirection = false;
				AppendWarning(warnings, file.string() + ": creature '" + entry.name + "' has invalid direction '" + node.attribute("direction").value() + "'; format default " + std::to_string(defaultDirection) + " was used.");
			}
		}
		entry.weight = format == SpawnFormat::Tfs ? std::max<uint32_t>(1, defaults.monsterWeight) : 1;
		if (node.attribute("weight")) {
			if (ReadInteger(node.attribute("weight"), numericValue) && numericValue >= 1) {
				entry.weight = static_cast<uint32_t>(numericValue);
				entry.hasWeight = true;
			} else {
				AppendWarning(warnings, file.string() + ": creature '" + entry.name + "' has invalid weight '" + node.attribute("weight").value() + "'; default " + std::to_string(entry.weight) + " was used.");
			}
		}
		entry.attributes = ReadExtraAttributes(node, {"name", "x", "y", "z", "spawntime", "direction", "weight"});
	}

	bool ParseModernFile(const std::filesystem::path& file, bool npcs, const SpawnLoadDefaults& defaults, SpawnDocument& document, std::string& error) {
		if (file.empty() || !std::filesystem::exists(file)) {
			return true;
		}
		pugi::xml_document doc;
		const pugi::xml_parse_result result = doc.load_file(file.string().c_str());
		if (!result) {
			error = XmlError(file, result);
			return false;
		}
		const char* rootName = npcs ? "npcs" : "monsters";
		const char* groupName = npcs ? "npc" : "monster";
		pugi::xml_node root = doc.document_element();
		if (!root || std::string(root.name()) != rootName) {
			error = file.string() + ": expected root <" + rootName + ">, but found <" + (root ? std::string(root.name()) : std::string()) + ">.";
			return false;
		}
		for (const pugi::xml_node& group : root.children()) {
			if (Lower(group.name()) != groupName) {
				continue;
			}
			SpawnAreaData area;
			area.kind = npcs ? SpawnAreaKind::Npcs : SpawnAreaKind::Monsters;
			if (!ReadAreaPosition(group, area, file, document.warnings, true)) {
				continue;
			}
			for (const pugi::xml_node& child : group.children()) {
				if (Lower(child.name()) != groupName) {
					continue;
				}
				SpawnEntryData entry;
				entry.isNpc = npcs;
				ReadCommonEntryAttributes(child, entry, SpawnFormat::CanaryCrystal, defaults, file, document.warnings);
				if (entry.name.empty()) {
					AppendWarning(document.warnings, file.string() + ": creature entry without name was skipped.");
					continue;
				}
				if (!ReadEntryPosition(child, area, entry, file, document.warnings)) {
					continue;
				}
				if (!ExpandAreaToContain(entry, area, file, document.warnings)) {
					continue;
				}
				area.entries.push_back(std::move(entry));
			}
			document.areas.push_back(std::move(area));
		}
		return true;
	}

	bool ParseTfsFile(const std::filesystem::path& file, const SpawnLoadDefaults& defaults, SpawnDocument& document, std::string& error) {
		pugi::xml_document doc;
		const pugi::xml_parse_result result = doc.load_file(file.string().c_str());
		if (!result) {
			error = XmlError(file, result);
			return false;
		}
		pugi::xml_node root = doc.document_element();
		if (!root || std::string(root.name()) != "spawns") {
			error = file.string() + ": expected root <spawns>, but found <" + (root ? std::string(root.name()) : std::string()) + ">.";
			return false;
		}
		for (const pugi::xml_node& group : root.children()) {
			if (Lower(group.name()) != "spawn") {
				continue;
			}
			SpawnAreaData area;
			area.kind = SpawnAreaKind::Mixed;
			if (!ReadAreaPosition(group, area, file, document.warnings, false)) {
				continue;
			}
			for (const pugi::xml_node& child : group.children()) {
				const std::string tag = Lower(child.name());
				if (tag != "monster" && tag != "npc" && tag != "monsters") {
					continue;
				}
				SpawnEntryData entry;
				entry.isNpc = tag == "npc";
				ReadCommonEntryAttributes(child, entry, SpawnFormat::Tfs, defaults, file, document.warnings);
				if (!ReadEntryPosition(child, area, entry, file, document.warnings)) {
					continue;
				}
				if (!ExpandAreaToContain(entry, area, file, document.warnings)) {
					continue;
				}
				if (tag == "monsters") {
					entry.name.clear();
					entry.alternativeKind = SpawnAlternativeKind::TfsChance;
					entry.attributes = ReadExtraAttributes(child, {"x", "y", "z", "spawntime", "direction"});
					for (const pugi::xml_node& variantNode : child.children()) {
						if (Lower(variantNode.name()) != "monster" || !variantNode.attribute("name")) {
							continue;
						}
						SpawnVariantData variant;
						variant.name = variantNode.attribute("name").as_string();
						variant.weight = variantNode.attribute("chance").as_uint(1);
						variant.hasWeight = static_cast<bool>(variantNode.attribute("chance"));
						variant.attributes = ReadExtraAttributes(variantNode, {"name", "chance"});
						entry.alternatives.push_back(std::move(variant));
					}
					if (entry.alternatives.empty()) {
						AppendWarning(document.warnings, file.string() + ": empty <monsters> set was skipped.");
						continue;
					}
					entry.name = entry.alternatives.front().name;
				} else if (entry.name.empty()) {
					AppendWarning(document.warnings, file.string() + ": creature entry without name was skipped.");
					continue;
				}
				area.entries.push_back(std::move(entry));
			}
			document.areas.push_back(std::move(area));
		}
		return true;
	}

	bool ValidateEntry(const SpawnAreaData& area, const SpawnEntryData& entry, std::string& error) {
		if (entry.x < 0 || entry.x > 65535 || entry.y < 0 || entry.y > 65535 || entry.z < 0 || entry.z > 255) {
			error = "Spawn entry '" + entry.name + "' is outside the supported server coordinate range.";
			return false;
		}
		if (entry.z != area.centerZ) {
			error = "Spawn entry '" + entry.name + "' is on a different floor than its spawn center.";
			return false;
		}
		if (entry.spawnTime < 1 || entry.spawnTime > MAX_SPAWN_TIME) {
			error = "Spawn entry '" + entry.name + "' has an invalid respawn time.";
			return false;
		}
		return true;
	}

	void WriteAreaAttributes(pugi::xml_node node, const SpawnAreaData& area) {
		node.append_attribute("centerx") = area.centerX;
		node.append_attribute("centery") = area.centerY;
		node.append_attribute("centerz") = area.centerZ;
		node.append_attribute("radius") = area.radius;
		AppendExtraAttributes(node, area.attributes, {"centerx", "centery", "centerz", "radius"});
	}

	bool ShouldWriteDirection(const SpawnEntryData& entry, SpawnFormat outputFormat) {
		const int defaultDirection = outputFormat == SpawnFormat::Tfs && entry.isNpc ? 2 : MIN_DIRECTION;
		return entry.hasDirection || entry.direction != defaultDirection;
	}

	void WriteCommonEntryAttributes(pugi::xml_node node, const SpawnAreaData& area, const SpawnEntryData& entry, int spawnTime, SpawnFormat outputFormat) {
		node.append_attribute("name") = entry.name.c_str();
		node.append_attribute("x") = entry.x - area.centerX;
		node.append_attribute("y") = entry.y - area.centerY;
		node.append_attribute("z") = area.centerZ;
		node.append_attribute("spawntime") = spawnTime;
		if (ShouldWriteDirection(entry, outputFormat)) {
			node.append_attribute("direction") = entry.direction;
		}
	}

	std::vector<SpawnAreaData> MergeAreas(const SpawnDocument& document) {
		std::vector<SpawnAreaData> merged;
		std::map<std::tuple<int, int, int, int>, size_t> indices;
		for (const SpawnAreaData& area : document.areas) {
			const auto key = std::make_tuple(area.centerX, area.centerY, area.centerZ, area.radius);
			auto [iterator, inserted] = indices.emplace(key, merged.size());
			if (inserted) {
				merged.push_back(area);
				merged.back().kind = SpawnAreaKind::Mixed;
			} else {
				auto& target = merged[iterator->second];
				target.entries.insert(target.entries.end(), area.entries.begin(), area.entries.end());
				for (const auto& [name, value] : area.attributes) {
					target.attributes.try_emplace(name, value);
				}
			}
		}
		return merged;
	}

	bool NormalizeWeights(const std::vector<SpawnVariantData>& variants, std::vector<uint32_t>& chances, std::string& error) {
		chances.clear();
		if (variants.empty()) {
			return true;
		}
		if (variants.size() > 100) {
			error = "Cannot normalize more than 100 spawn alternatives without assigning a zero chance.";
			return false;
		}

		uint64_t total = 0;
		for (const SpawnVariantData& variant : variants) {
			total += std::max<uint32_t>(1, variant.weight);
		}

		const uint32_t remaining = 100 - static_cast<uint32_t>(variants.size());
		chances.assign(variants.size(), 1);
		struct Remainder {
			size_t index;
			uint64_t value;
		};
		std::vector<Remainder> remainders;
		remainders.reserve(variants.size());
		uint32_t assigned = static_cast<uint32_t>(variants.size());
		for (size_t index = 0; index < variants.size(); ++index) {
			const uint64_t numerator = std::max<uint32_t>(1, variants[index].weight) * static_cast<uint64_t>(remaining);
			const uint32_t extra = static_cast<uint32_t>(numerator / total);
			chances[index] += extra;
			assigned += extra;
			remainders.push_back({ index, numerator % total });
		}
		std::stable_sort(remainders.begin(), remainders.end(), [](const Remainder& lhs, const Remainder& rhs) {
			return lhs.value > rhs.value;
		});
		for (uint32_t index = 0; assigned < 100; ++index, ++assigned) {
			++chances[remainders[index].index];
		}
		return true;
	}

	std::vector<std::string> CanonicalEntries(const SpawnDocument& document, bool includeWeights) {
		std::vector<std::string> entries;
		for (const SpawnAreaData& area : document.areas) {
			for (const SpawnEntryData& entry : area.entries) {
				const auto append = [&](const std::string& name, bool npc, uint32_t weight, bool hasWeight) {
					std::ostringstream stream;
					stream << (npc ? "npc" : "monster") << '|' << name << '|' << entry.x << '|' << entry.y << '|' << entry.z << '|' << entry.spawnTime << '|' << entry.direction;
					if (includeWeights && hasWeight) {
						stream << '|' << weight;
					}
					entries.push_back(stream.str());
				};
				if (entry.alternatives.empty()) {
					append(entry.name, entry.isNpc, entry.weight, entry.hasWeight);
				} else {
					for (const SpawnVariantData& variant : entry.alternatives) {
						append(variant.name, variant.isNpc, variant.weight, variant.hasWeight);
					}
				}
			}
		}
		std::sort(entries.begin(), entries.end());
		return entries;
	}
}

size_t SpawnDocument::monsterCount() const {
	size_t count = 0;
	for (const SpawnAreaData& area : areas) {
		for (const SpawnEntryData& entry : area.entries) {
			if (entry.alternatives.empty()) {
				count += entry.isNpc ? 0 : 1;
			} else {
				count += std::count_if(entry.alternatives.begin(), entry.alternatives.end(), [](const SpawnVariantData& variant) { return !variant.isNpc; });
			}
		}
	}
	return count;
}

size_t SpawnDocument::npcCount() const {
	size_t count = 0;
	for (const SpawnAreaData& area : areas) {
		for (const SpawnEntryData& entry : area.entries) {
			if (entry.alternatives.empty()) {
				count += entry.isNpc ? 1 : 0;
			} else {
				count += std::count_if(entry.alternatives.begin(), entry.alternatives.end(), [](const SpawnVariantData& variant) { return variant.isNpc; });
			}
		}
	}
	return count;
}

size_t SpawnDocument::entryCount() const {
	return monsterCount() + npcCount();
}

const char* SpawnFormatIO::GetFormatName(SpawnFormat format) {
	switch (format) {
		case SpawnFormat::Tfs: return "TFS 1.8 (8.60)";
		case SpawnFormat::CanaryCrystal: return "Canary/Crystal (11.x)";
		default: return "Unknown";
	}
}

SpawnDetectionResult SpawnFormatIO::Detect(const std::filesystem::path& directory, const std::string& embeddedPrimaryFile, const std::string& embeddedNpcFile, const std::string& mapName) {
	SpawnDetectionResult detection;
	const std::filesystem::path embeddedPrimary = ResolveFile(directory, embeddedPrimaryFile);
	const std::filesystem::path embeddedNpc = ResolveFile(directory, embeddedNpcFile);
	std::map<std::filesystem::path, std::string> rootCache;
	const auto rootName = [&](const std::filesystem::path& file) {
		auto [iterator, inserted] = rootCache.try_emplace(file);
		if (inserted) {
			iterator->second = RootName(file);
		}
		return iterator->second;
	};
	const std::string embeddedPrimaryRoot = rootName(embeddedPrimary);
	const std::string embeddedNpcRoot = rootName(embeddedNpc);

	const auto firstWithRoot = [&](const std::vector<std::filesystem::path>& candidates, const char* expectedRoot) {
		for (const std::filesystem::path& candidate : candidates) {
			if (rootName(candidate) == expectedRoot) {
				return candidate;
			}
		}
		return std::filesystem::path();
	};
	const auto companionNpc = [](const std::filesystem::path& monsterFile) {
		if (monsterFile.empty()) {
			return std::filesystem::path();
		}
		const std::string stem = monsterFile.stem().string();
		std::string npcStem;
		if (stem.ends_with("-monster")) {
			npcStem = stem.substr(0, stem.size() - std::string("-monster").size()) + "-npc";
		} else if (stem == "monster") {
			npcStem = "npc";
		} else if (stem == "monsters") {
			npcStem = "npcs";
		}
		return npcStem.empty() ? std::filesystem::path() : monsterFile.parent_path() / (npcStem + monsterFile.extension().string());
	};
	const auto companionMonster = [](const std::filesystem::path& npcFile) {
		if (npcFile.empty()) {
			return std::filesystem::path();
		}
		const std::string stem = npcFile.stem().string();
		std::string monsterStem;
		if (stem.ends_with("-npc")) {
			monsterStem = stem.substr(0, stem.size() - std::string("-npc").size()) + "-monster";
		} else if (stem == "npc") {
			monsterStem = "monster";
		} else if (stem == "npcs") {
			monsterStem = "monsters";
		}
		return monsterStem.empty() ? std::filesystem::path() : npcFile.parent_path() / (monsterStem + npcFile.extension().string());
	};

	const std::vector<std::filesystem::path> tfsCandidates = {
		directory / (mapName + "-spawn.xml"),
		directory / "world-spawn.xml",
		directory / "spawn.xml",
	};
	struct ModernPair {
		std::filesystem::path monster;
		std::filesystem::path npc;
	};
	const std::vector<ModernPair> modernPairs = {
		{directory / (mapName + "-monster.xml"), directory / (mapName + "-npc.xml")},
		{directory / "world-monster.xml", directory / "world-npc.xml"},
		{directory / "monster.xml", directory / "npc.xml"},
		{directory / "monsters.xml", directory / "npcs.xml"},
	};

	const std::filesystem::path discoveredTfs = firstWithRoot(tfsCandidates, "spawns");
	std::filesystem::path selectedMonster;
	std::filesystem::path selectedNpc;
	bool discoveredCanary = false;

	const bool primaryIsMonster = embeddedPrimaryRoot == "monsters";
	const bool primaryIsNpc = embeddedPrimaryRoot == "npcs";
	const bool secondaryIsMonster = embeddedNpcRoot == "monsters";
	const bool secondaryIsNpc = embeddedNpcRoot == "npcs";
	if (primaryIsMonster) {
		selectedMonster = embeddedPrimary;
		selectedNpc = secondaryIsNpc ? embeddedNpc : companionNpc(embeddedPrimary);
		discoveredCanary = true;
	} else if (primaryIsNpc) {
		selectedNpc = embeddedPrimary;
		selectedMonster = secondaryIsMonster ? embeddedNpc : companionMonster(embeddedPrimary);
		discoveredCanary = true;
	}
	if (!primaryIsMonster && !primaryIsNpc && secondaryIsMonster) {
		selectedMonster = embeddedNpc;
		selectedNpc = companionNpc(embeddedNpc);
		discoveredCanary = true;
	} else if (!primaryIsMonster && !primaryIsNpc && secondaryIsNpc) {
		selectedNpc = embeddedNpc;
		selectedMonster = companionMonster(embeddedNpc);
		discoveredCanary = true;
	} else if (primaryIsMonster && secondaryIsNpc) {
		selectedNpc = embeddedNpc;
	} else if (primaryIsNpc && secondaryIsMonster) {
		selectedMonster = embeddedNpc;
	}

	if (!discoveredCanary) {
		for (const ModernPair& pair : modernPairs) {
			const bool hasMonster = rootName(pair.monster) == "monsters";
			const bool hasNpc = rootName(pair.npc) == "npcs";
			if (hasMonster || hasNpc) {
				selectedMonster = pair.monster;
				selectedNpc = pair.npc;
				discoveredCanary = true;
				break;
			}
		}
	}

	const bool explicitlyReferencesTfs = embeddedPrimaryRoot == "spawns" || embeddedNpcRoot == "spawns";
	const std::filesystem::path explicitlyReferencedTfs = embeddedPrimaryRoot == "spawns" ? embeddedPrimary : embeddedNpc;
	const bool explicitlyReferencesCanary = embeddedPrimaryRoot == "monsters" || embeddedPrimaryRoot == "npcs" || embeddedNpcRoot == "monsters" || embeddedNpcRoot == "npcs";
	if (explicitlyReferencesTfs && explicitlyReferencesCanary) {
		detection.format = SpawnFormat::Tfs;
		detection.primaryFile = explicitlyReferencedTfs;
		detection.alternateFormat = SpawnFormat::CanaryCrystal;
		detection.alternatePrimaryFile = selectedMonster;
		detection.alternateNpcFile = selectedNpc;
		detection.conflict = true;
		detection.error = "The OTBM references both TFS and Canary/Crystal spawn layouts.";
		return detection;
	}
	if (explicitlyReferencesTfs) {
		detection.format = SpawnFormat::Tfs;
		detection.primaryFile = explicitlyReferencedTfs;
		return detection;
	}
	if (explicitlyReferencesCanary) {
		detection.format = SpawnFormat::CanaryCrystal;
		detection.primaryFile = selectedMonster;
		detection.npcFile = selectedNpc;
		return detection;
	}

	if (!discoveredTfs.empty() && discoveredCanary) {
		detection.format = SpawnFormat::Tfs;
		detection.primaryFile = discoveredTfs;
		detection.alternateFormat = SpawnFormat::CanaryCrystal;
		detection.alternatePrimaryFile = selectedMonster;
		detection.alternateNpcFile = selectedNpc;
		detection.conflict = true;
		detection.error = "Both TFS and Canary/Crystal spawn files were found without an unambiguous OTBM reference.";
		return detection;
	}
	if (!discoveredTfs.empty()) {
		detection.format = SpawnFormat::Tfs;
		detection.primaryFile = discoveredTfs;
		return detection;
	}
	if (discoveredCanary) {
		detection.format = SpawnFormat::CanaryCrystal;
		detection.primaryFile = selectedMonster;
		detection.npcFile = selectedNpc;
		return detection;
	}

	std::error_code filesystemError;
	if (!embeddedPrimary.empty() && std::filesystem::exists(embeddedPrimary, filesystemError) && !filesystemError) {
		detection.error = embeddedPrimary.string() + ": expected root <spawns>, <monsters> or <npcs>, but found <" + embeddedPrimaryRoot + ">.";
	} else if (!embeddedPrimary.empty()) {
		detection.error = "The OTBM spawn reference does not exist: " + embeddedPrimary.string();
	} else {
		detection.error = "No TFS <spawns> file or Canary/Crystal <monsters>/<npcs> file was detected beside the map.";
	}
	return detection;
}

bool SpawnFormatIO::Load(const SpawnDetectionResult& detection, SpawnDocument& document, std::string& error, const SpawnLoadDefaults& defaults) {
	if (detection.format == SpawnFormat::Tfs) {
		return LoadTfs(detection.primaryFile, document, error, defaults);
	}
	if (detection.format == SpawnFormat::CanaryCrystal) {
		return LoadCanaryCrystal(detection.primaryFile, detection.npcFile, document, error, defaults);
	}
	error = detection.error.empty() ? "Unknown spawn format." : detection.error;
	return false;
}

bool SpawnFormatIO::LoadTfs(const std::filesystem::path& file, SpawnDocument& document, std::string& error, const SpawnLoadDefaults& defaults) {
	document = {};
	SpawnDocument parsed;
	parsed.format = SpawnFormat::Tfs;
	if (!ParseTfsFile(file, defaults, parsed, error)) {
		return false;
	}
	document = std::move(parsed);
	return true;
}

bool SpawnFormatIO::LoadCanaryCrystal(const std::filesystem::path& monsterFile, const std::filesystem::path& npcFile, SpawnDocument& document, std::string& error, const SpawnLoadDefaults& defaults) {
	document = {};
	SpawnDocument parsed;
	parsed.format = SpawnFormat::CanaryCrystal;
	std::error_code filesystemError;
	const bool hasMonsterFile = !monsterFile.empty() && std::filesystem::exists(monsterFile, filesystemError) && !filesystemError;
	filesystemError.clear();
	const bool hasNpcFile = !npcFile.empty() && std::filesystem::exists(npcFile, filesystemError) && !filesystemError;
	if (!ParseModernFile(monsterFile, false, defaults, parsed, error)) {
		return false;
	}
	if (!ParseModernFile(npcFile, true, defaults, parsed, error)) {
		return false;
	}
	if (!hasMonsterFile && !hasNpcFile) {
		error = "Neither Canary/Crystal spawn file exists.";
		return false;
	}
	if (!hasMonsterFile) {
		AppendWarning(parsed.warnings, "Canary/Crystal monster spawn file was not found; NPC spawns were loaded from " + npcFile.string() + ".");
	}
	if (!hasNpcFile) {
		AppendWarning(parsed.warnings, "Canary/Crystal NPC spawn file was not found; monster spawns were loaded from " + monsterFile.string() + ".");
	}
	document = std::move(parsed);
	return true;
}

SpawnWriteResult SpawnFormatIO::SaveTfs(const SpawnDocument& document, const std::filesystem::path& file) {
	SpawnWriteResult result;
	result.files.push_back(file);
	pugi::xml_document xml;
	pugi::xml_node declaration = xml.append_child(pugi::node_declaration);
	declaration.append_attribute("version") = "1.0";
	pugi::xml_node root = xml.append_child("spawns");
	for (const SpawnAreaData& area : MergeAreas(document)) {
		pugi::xml_node areaNode = root.append_child("spawn");
		WriteAreaAttributes(areaNode, area);
		for (const SpawnEntryData& entry : area.entries) {
			if (!ValidateEntry(area, entry, result.error)) {
				return result;
			}
			const int spawnTime = std::max(TFS_MIN_SPAWN_TIME, entry.spawnTime);
			if (spawnTime != entry.spawnTime) {
				AppendWarning(result.warnings, "TFS requires at least 10 seconds respawn; '" + entry.name + "' was clamped to 10 seconds.");
			}
			if (!entry.alternatives.empty() && std::none_of(entry.alternatives.begin(), entry.alternatives.end(), [](const SpawnVariantData& variant) { return variant.isNpc; })) {
				pugi::xml_node setNode = areaNode.append_child("monsters");
				setNode.append_attribute("x") = entry.x - area.centerX;
				setNode.append_attribute("y") = entry.y - area.centerY;
				setNode.append_attribute("z") = area.centerZ;
				setNode.append_attribute("spawntime") = spawnTime;
				if (ShouldWriteDirection(entry, SpawnFormat::Tfs)) {
					setNode.append_attribute("direction") = entry.direction;
				}
				AppendExtraAttributes(setNode, entry.attributes, {"x", "y", "z", "spawntime", "direction"});
				std::vector<uint32_t> chances;
				if (entry.alternativeKind != SpawnAlternativeKind::TfsChance && !NormalizeWeights(entry.alternatives, chances, result.error)) {
					return result;
				}
				for (size_t index = 0; index < entry.alternatives.size(); ++index) {
					const SpawnVariantData& variant = entry.alternatives[index];
					pugi::xml_node variantNode = setNode.append_child("monster");
					variantNode.append_attribute("name") = variant.name.c_str();
					const uint32_t chance = chances.empty() ? variant.weight : chances[index];
					variantNode.append_attribute("chance") = chance;
					AppendExtraAttributes(variantNode, variant.attributes, {"name", "chance"});
				}
				continue;
			}
			if (!entry.alternatives.empty()) {
				for (const SpawnVariantData& variant : entry.alternatives) {
					SpawnEntryData output = entry;
					output.name = variant.name;
					output.isNpc = variant.isNpc;
					output.weight = variant.weight;
					output.hasWeight = variant.hasWeight;
					output.attributes = variant.attributes;
					pugi::xml_node entryNode = areaNode.append_child(output.isNpc ? "npc" : "monster");
					WriteCommonEntryAttributes(entryNode, area, output, spawnTime, SpawnFormat::Tfs);
					if (output.hasWeight) {
						entryNode.append_attribute("weight") = output.weight;
					}
					AppendExtraAttributes(entryNode, output.attributes, {"name", "x", "y", "z", "spawntime", "direction", "weight"});
				}
				continue;
			}
			pugi::xml_node entryNode = areaNode.append_child(entry.isNpc ? "npc" : "monster");
			WriteCommonEntryAttributes(entryNode, area, entry, spawnTime, SpawnFormat::Tfs);
			if (entry.hasWeight) {
				entryNode.append_attribute("weight") = entry.weight;
			}
			AppendExtraAttributes(entryNode, entry.attributes, {"name", "x", "y", "z", "spawntime", "direction", "weight"});
		}
	}
	std::error_code ec;
	if (!file.parent_path().empty()) {
		std::filesystem::create_directories(file.parent_path(), ec);
	}
	if (ec) {
		result.error = "Could not create destination directory for " + file.string() + ": " + ec.message();
		return result;
	}
	FileSaveTransaction transaction;
	const std::filesystem::path stagedFile = transaction.Stage(file);
	if (!xml.save_file(stagedFile.string().c_str(), "\t", pugi::format_default, pugi::encoding_utf8)) {
		result.error = "Could not write " + file.string();
		return result;
	}
	SpawnDocument reloaded;
	std::string loadError;
	if (!LoadTfs(stagedFile, reloaded, loadError)) {
		result.error = "Generated TFS XML failed validation: " + loadError;
		return result;
	}
	SpawnDocument expected = document;
	for (SpawnAreaData& area : expected.areas) {
		for (SpawnEntryData& entry : area.entries) {
			entry.spawnTime = std::max(TFS_MIN_SPAWN_TIME, entry.spawnTime);
		}
	}
	std::string difference;
	if (!SemanticallyEqual(expected, reloaded, false, difference)) {
		result.error = "Generated TFS XML changed spawn data: " + difference;
		return result;
	}
	if (!transaction.Commit(result.error)) {
		return result;
	}
	result.success = true;
	return result;
}

SpawnWriteResult SpawnFormatIO::SaveCanaryCrystal(const SpawnDocument& document, const std::filesystem::path& monsterFile, const std::filesystem::path& npcFile) {
	SpawnWriteResult result;
	result.files = {monsterFile, npcFile};
	if (FileSaveTransaction::PathsReferToSameFile(monsterFile, npcFile)) {
		result.error = "Monster and NPC output files must be different.";
		return result;
	}
	pugi::xml_document monsterXml;
	pugi::xml_document npcXml;
	for (pugi::xml_document* xml : {&monsterXml, &npcXml}) {
		pugi::xml_node declaration = xml->append_child(pugi::node_declaration);
		declaration.append_attribute("version") = "1.0";
	}
	pugi::xml_node monsterRoot = monsterXml.append_child("monsters");
	pugi::xml_node npcRoot = npcXml.append_child("npcs");
	for (const SpawnAreaData& area : document.areas) {
		pugi::xml_node monsterArea;
		pugi::xml_node npcArea;
		auto ensureArea = [&](bool npc) -> pugi::xml_node {
			pugi::xml_node& node = npc ? npcArea : monsterArea;
			if (!node) {
				node = (npc ? npcRoot : monsterRoot).append_child(npc ? "npc" : "monster");
				WriteAreaAttributes(node, area);
			}
			return node;
		};
		for (const SpawnEntryData& entry : area.entries) {
			if (!ValidateEntry(area, entry, result.error)) {
				return result;
			}
			auto writeOne = [&](const std::string& name, bool npc, uint32_t weight, bool hasWeight, const SpawnAttributeMap& attributes) {
				SpawnEntryData output = entry;
				output.name = name;
				output.isNpc = npc;
				pugi::xml_node entryNode = ensureArea(npc).append_child(npc ? "npc" : "monster");
				WriteCommonEntryAttributes(entryNode, area, output, output.spawnTime, SpawnFormat::CanaryCrystal);
				if (!npc && hasWeight) {
					entryNode.append_attribute("weight") = weight;
				}
				AppendExtraAttributes(entryNode, attributes, {"name", "x", "y", "z", "spawntime", "direction", "weight"});
				if (npc && attributes.contains("instanceId")) {
					AppendWarning(result.warnings, "Crystal does not interpret TFS NPC instanceId for '" + name + "'; the attribute was preserved in XML.");
				}
			};
			if (entry.alternatives.empty()) {
				writeOne(entry.name, entry.isNpc, entry.weight, entry.hasWeight, entry.attributes);
			} else {
				for (const SpawnVariantData& variant : entry.alternatives) {
					writeOne(variant.name, variant.isNpc, variant.weight, variant.hasWeight, variant.attributes);
				}
			}
		}
	}
	std::error_code ec;
	if (!monsterFile.parent_path().empty()) {
		std::filesystem::create_directories(monsterFile.parent_path(), ec);
	}
	if (!ec && !npcFile.parent_path().empty()) {
		std::filesystem::create_directories(npcFile.parent_path(), ec);
	}
	if (ec) {
		result.error = "Could not create spawn destination directory: " + ec.message();
		return result;
	}
	FileSaveTransaction transaction;
	const std::filesystem::path stagedMonsterFile = transaction.Stage(monsterFile);
	const std::filesystem::path stagedNpcFile = transaction.Stage(npcFile);
	if (!monsterXml.save_file(stagedMonsterFile.string().c_str(), "\t", pugi::format_default, pugi::encoding_utf8)) {
		result.error = "Could not write " + monsterFile.string();
		return result;
	}
	if (!npcXml.save_file(stagedNpcFile.string().c_str(), "\t", pugi::format_default, pugi::encoding_utf8)) {
		result.error = "Could not write " + npcFile.string();
		return result;
	}
	SpawnDocument reloaded;
	std::string loadError;
	if (!LoadCanaryCrystal(stagedMonsterFile, stagedNpcFile, reloaded, loadError)) {
		result.error = "Generated Canary/Crystal XML failed validation: " + loadError;
		return result;
	}
	std::string difference;
	if (!SemanticallyEqual(document, reloaded, document.format == SpawnFormat::CanaryCrystal, difference)) {
		result.error = "Generated Canary/Crystal XML changed spawn data: " + difference;
		return result;
	}
	if (!transaction.Commit(result.error)) {
		return result;
	}
	result.success = true;
	return result;
}

bool SpawnFormatIO::SemanticallyEqual(const SpawnDocument& lhs, const SpawnDocument& rhs, bool compareWeights, std::string& difference) {
	const std::vector<std::string> left = CanonicalEntries(lhs, compareWeights);
	const std::vector<std::string> right = CanonicalEntries(rhs, compareWeights);
	if (left == right) {
		return true;
	}
	difference = "expected " + std::to_string(left.size()) + " creature records, got " + std::to_string(right.size()) + ".";
	return false;
}
