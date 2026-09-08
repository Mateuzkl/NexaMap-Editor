//////////////////////////////////////////////////////////////////////
// Safe, source-preserving creation of reusable Lua combat areas.
//////////////////////////////////////////////////////////////////////

#include "spell_area_library.h"

#include "file_transaction.h"
#include "source_text_utils.h"
#include "spell_area_resolver.h"

#include <algorithm>
#include <fstream>
#include <unordered_map>

namespace {
	bool IsRegularFile(const std::filesystem::path& path) {
		std::error_code error;
		return std::filesystem::is_regular_file(path, error) && !error;
	}

	bool IsSafeRelative(const std::filesystem::path& path) {
		return !path.empty() && !path.is_absolute()
			&& std::none_of(path.begin(), path.end(), [](const std::filesystem::path& component) { return component == ".."; });
	}

	std::filesystem::path RelativeToWorkspace(const std::filesystem::path& path, const ServerWorkspace& workspace) {
		const std::filesystem::path relative = path.lexically_relative(workspace.rootPath);
		return IsSafeRelative(relative) ? relative : path.filename();
	}

	int TargetRank(const std::filesystem::path& path) {
		const std::string name = SourceText::AsciiLower(path.filename().string());
		if (name == "spell_lib.lua") {
			return 0;
		}
		if (name == "register_spells.lua") {
			return 1;
		}
		return 2;
	}

	bool SameTiles(std::vector<MonsterAreaTile> left, std::vector<MonsterAreaTile> right) {
		const auto order = [](const MonsterAreaTile& first, const MonsterAreaTile& second) {
			return first.y != second.y ? first.y < second.y : first.x < second.x;
		};
		std::sort(left.begin(), left.end(), order);
		std::sort(right.begin(), right.end(), order);
		return left == right;
	}

	bool WriteBytes(const std::filesystem::path& path, std::string_view bytes, std::string& error) {
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		if (!stream.is_open()) {
			error = "Could not stage the area library source.";
			return false;
		}
		stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		if (!stream) {
			error = "Could not write the staged area library source.";
			return false;
		}
		return true;
	}
}

std::vector<SpellAreaLibraryTarget> SpellAreaLibrary::DiscoverTargets(const ServerWorkspace& workspace, const SpellAreaResolver& resolver) {
	std::unordered_map<std::string, SpellAreaLibraryTarget> targets;
	const auto add = [&](const std::filesystem::path& path, std::size_t definitions) {
		if (!IsRegularFile(path)) {
			return;
		}
		const std::string key = SourceText::AsciiLower(path.lexically_normal().generic_string());
		auto [iterator, inserted] = targets.emplace(key, SpellAreaLibraryTarget { path.lexically_normal(), RelativeToWorkspace(path, workspace), ResourceFingerprint::Read(path), definitions });
		if (!inserted) {
			iterator->second.existingDefinitions += definitions;
		}
	};

	for (const SpellAreaResolver::Definition& definition : resolver.allDefinitions()) {
		add(definition.path, 1);
	}
	const auto addKnown = [&](const std::filesystem::path& root) {
		for (const char* name : { "spell_lib.lua", "register_spells.lua" }) {
			add(root / name, 0);
		}
	};
	addKnown(workspace.activeDataDirectory / "scripts" / "lib");
	addKnown(workspace.activeDataDirectory / "lib");
	addKnown(workspace.rootPath / "data" / "scripts" / "lib");
	addKnown(workspace.rootPath / "data" / "lib");

	std::vector<SpellAreaLibraryTarget> result;
	result.reserve(targets.size());
	for (auto& [key, target] : targets) {
		result.push_back(std::move(target));
	}
	std::sort(result.begin(), result.end(), [](const SpellAreaLibraryTarget& left, const SpellAreaLibraryTarget& right) {
		const int leftRank = TargetRank(left.path);
		const int rightRank = TargetRank(right.path);
		if (leftRank != rightRank) {
			return leftRank < rightRank;
		}
		if (left.existingDefinitions != right.existingDefinitions) {
			return left.existingDefinitions > right.existingDefinitions;
		}
		return left.relativePath.generic_string() < right.relativePath.generic_string();
	});
	return result;
}

bool SpellAreaLibrary::Validate(const EditableSpellArea& area, std::string& error) {
	if (area.name.size() < 6 || !area.name.starts_with("AREA_")) {
		error = "Area name must start with AREA_.";
		return false;
	}
	if (!std::all_of(area.name.begin(), area.name.end(), [](unsigned char character) {
			return (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '_';
		})) {
		error = "Area name may contain only uppercase letters, digits, and underscores.";
		return false;
	}
	if (area.width == 0 || area.height == 0 || area.width > 31 || area.height > 31 || area.cells.size() != area.width * area.height) {
		error = "Area grid must be between 1x1 and 31x31.";
		return false;
	}
	std::size_t centers = 0;
	std::size_t affected = 0;
	for (uint8_t cell : area.cells) {
		if (cell > 3) {
			error = "Area cells must be empty, affected, center, or affected center.";
			return false;
		}
		centers += cell == 2 || cell == 3 ? 1 : 0;
		affected += cell == 1 || cell == 3 ? 1 : 0;
	}
	if (centers != 1) {
		error = "Select exactly one caster/target center tile.";
		return false;
	}
	if (affected == 0) {
		error = "Select at least one affected tile.";
		return false;
	}
	error.clear();
	return true;
}

std::string SpellAreaLibrary::Serialize(const EditableSpellArea& area, std::string_view newline) {
	std::string output;
	output.reserve(area.name.size() + area.cells.size() * 4 + area.height * 8 + 16);
	output += area.name;
	output += " = {";
	output += newline;
	for (std::size_t y = 0; y < area.height; ++y) {
		output += "\t{";
		for (std::size_t x = 0; x < area.width; ++x) {
			if (x != 0) {
				output += ", ";
			}
			output += static_cast<char>('0' + area.cells[y * area.width + x]);
		}
		output += "},";
		output += newline;
	}
	output += "}";
	output += newline;
	return output;
}

bool SpellAreaLibrary::Save(
	const ServerWorkspace& workspace,
	const SpellAreaResolver& resolver,
	const SpellAreaLibraryTarget& target,
	const EditableSpellArea& area,
	SpellAreaLibrarySaveResult& result,
	std::string& error
) {
	result = {};
	if (!Validate(area, error)) {
		return false;
	}
	const auto available = DiscoverTargets(workspace, resolver);
	const auto selected = std::find_if(available.begin(), available.end(), [&](const SpellAreaLibraryTarget& candidate) {
		return FileSaveTransaction::PathsReferToSameFile(candidate.path, target.path);
	});
	if (selected == available.end()) {
		error = "The selected file is not an active Server Workspace spell-area library.";
		return false;
	}
	if (target.fingerprint != ResourceFingerprint::Read(target.path)) {
		error = "The area library changed on disk. Reopen the area creator before saving.";
		return false;
	}
	const std::string foldedName = SourceText::AsciiLower(area.name);
	for (const SpellAreaResolver::Definition& definition : resolver.allDefinitions()) {
		if (SourceText::AsciiLower(definition.name) == foldedName) {
			error = "An area named " + area.name + " already exists in the active Server Workspace.";
			return false;
		}
	}

	const auto existing = SourceText::ReadBoundedFile(target.path, 16 * 1024 * 1024);
	if (!existing) {
		error = "Could not read the selected area library.";
		return false;
	}
	const std::string newline(SourceText::Newline(*existing));
	std::string updated = *existing;
	if (!updated.empty() && updated.back() != '\n') {
		updated += newline;
	}
	if (!updated.empty()) {
		updated += newline;
	}
	updated += Serialize(area, newline);

	const auto parsed = SpellAreaResolver::ParseSource(target.path, updated);
	const auto created = std::find_if(parsed.begin(), parsed.end(), [&](const SpellAreaResolver::Definition& definition) {
		return definition.name == area.name;
	});
	if (created == parsed.end()) {
		error = "The generated area could not be parsed back safely.";
		return false;
	}
	std::vector<MonsterAreaTile> expected;
	std::size_t centerX = 0;
	std::size_t centerY = 0;
	for (std::size_t index = 0; index < area.cells.size(); ++index) {
		if (area.cells[index] == 2 || area.cells[index] == 3) {
			centerX = index % area.width;
			centerY = index / area.width;
			break;
		}
	}
	for (std::size_t index = 0; index < area.cells.size(); ++index) {
		if (area.cells[index] == 1 || area.cells[index] == 3) {
			expected.push_back({ static_cast<int>(index % area.width) - static_cast<int>(centerX), static_cast<int>(index / area.width) - static_cast<int>(centerY) });
		}
	}
	if (!SameTiles(expected, SpellAreaResolver::ParseLiteralMatrix(created->expression))) {
		error = "The generated area changed meaning when parsed back.";
		return false;
	}

	FileSaveTransaction transaction;
	const std::filesystem::path staged = transaction.Stage(target.path);
	if (!WriteBytes(staged, updated, error) || !transaction.Commit(error)) {
		return false;
	}
	result = { target.path, area.name, expected.size() };
	return true;
}
