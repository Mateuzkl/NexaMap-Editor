//////////////////////////////////////////////////////////////////////
// Workspace-specific spell effect and projectile constants.
//////////////////////////////////////////////////////////////////////

#include "server_visual_catalog.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <regex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace {
	constexpr std::uintmax_t MaximumSourceBytes = 4u * 1024u * 1024u;

	std::string Lower(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
		return value;
	}

	std::string Trim(std::string value) {
		const auto first = value.find_first_not_of(" \t\r\n(");
		if (first == std::string::npos) {
			return {};
		}
		const auto last = value.find_last_not_of(" \t\r\n)uUlL");
		return value.substr(first, last - first + 1);
	}

	std::optional<uint32_t> ParseNumber(const std::string& raw) {
		const std::string value = Trim(raw);
		if (value.empty() || value.front() == '-') {
			return std::nullopt;
		}
		uint32_t parsed = 0;
		const int base = value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X') ? 16 : 10;
		const char* begin = value.data() + (base == 16 ? 2 : 0);
		const auto [end, error] = std::from_chars(begin, value.data() + value.size(), parsed, base);
		if (error != std::errc() || end != value.data() + value.size()) {
			return std::nullopt;
		}
		return parsed;
	}

	bool IsUsableConstant(const std::string& name) {
		return name.find("_LAST") == std::string::npos && name.find("WEAPONTYPE") == std::string::npos;
	}

	void ParseConstants(
		const std::filesystem::path& path,
		std::vector<ServerVisualConstant>& effects,
		std::vector<ServerVisualConstant>& projectiles,
		std::unordered_map<std::string, uint32_t>& known,
		bool supplementOnly,
		ServerVisualCatalogStats& stats
	) {
		++stats.filesInspected;
		std::error_code error;
		const std::uintmax_t size = std::filesystem::file_size(path, error);
		if (error || size > MaximumSourceBytes) {
			return;
		}
		std::ifstream stream(path, std::ios::binary);
		if (!stream) {
			return;
		}
		++stats.filesRead;

		static const std::regex declaration(R"(^\s*(CONST_(ME|ANI)_[A-Z0-9_]+)\s*(?:=\s*([^,}\r\n]+))?\s*,?\s*(?://.*)?$)");
		std::string line;
		uint32_t previousEffect = 0;
		uint32_t previousProjectile = 0;
		bool havePreviousEffect = false;
		bool havePreviousProjectile = false;
		while (std::getline(stream, line)) {
			std::smatch match;
			if (!std::regex_match(line, match, declaration)) {
				continue;
			}
			const std::string name = match[1].str();
			const ServerVisualKind kind = match[2].str() == "ME" ? ServerVisualKind::MagicEffect : ServerVisualKind::DistanceEffect;
			uint32_t& previous = kind == ServerVisualKind::MagicEffect ? previousEffect : previousProjectile;
			bool& havePrevious = kind == ServerVisualKind::MagicEffect ? havePreviousEffect : havePreviousProjectile;
			std::optional<uint32_t> id;
			const std::string expression = Trim(match[3].str());
			if (expression.empty()) {
				id = havePrevious ? std::optional<uint32_t>(previous + 1) : std::optional<uint32_t>(0);
			} else {
				id = ParseNumber(expression);
				if (!id) {
					const auto alias = known.find(expression);
					if (alias != known.end()) {
						id = alias->second;
					}
				}
			}
			if (!id) {
				continue;
			}
			previous = *id;
			havePrevious = true;
			if (!IsUsableConstant(name)) {
				continue;
			}
			if (supplementOnly && known.contains(name)) {
				continue;
			}
			known[name] = *id;
			(kind == ServerVisualKind::MagicEffect ? effects : projectiles).push_back({ kind, name, *id, path });
		}
	}

	std::vector<std::filesystem::path> HeaderCandidates(const ServerWorkspace& workspace) {
		const std::filesystem::path& root = workspace.rootPath;
		return {
			root / "src/const.h",
			root / "engine/src/const.h",
			root / "src/utils/utils_definitions.hpp",
			root / "src/utils/definitions.hpp",
			root / "src/const.hpp",
		};
	}

	std::vector<std::filesystem::path> LuaLibraryRoots(const ServerWorkspace& workspace) {
		std::vector<std::filesystem::path> roots;
		const auto add = [&](const std::filesystem::path& path) {
			std::error_code error;
			if (!path.empty() && std::filesystem::is_directory(path, error)
				&& std::find(roots.begin(), roots.end(), path) == roots.end()) {
				roots.push_back(path);
			}
		};
		add(workspace.rootPath / "data/lib");
		add(workspace.rootPath / "data/scripts/lib");
		add(workspace.activeDataDirectory / "lib");
		add(workspace.activeDataDirectory / "scripts/lib");
		return roots;
	}

	void SortAndUnique(std::vector<ServerVisualConstant>& values) {
		std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) {
			return left.id != right.id ? left.id < right.id : left.name < right.name;
		});
		values.erase(std::unique(values.begin(), values.end(), [](const auto& left, const auto& right) {
						 return left.name == right.name;
					 }),
					 values.end());
	}
}

ServerVisualCatalog ServerVisualCatalog::Build(const ServerWorkspace& workspace) {
	ServerVisualCatalog catalog;
	std::unordered_map<std::string, uint32_t> known;
	for (const std::filesystem::path& candidate : HeaderCandidates(workspace)) {
		std::error_code error;
		if (!std::filesystem::is_regular_file(candidate, error)) {
			continue;
		}
		ParseConstants(candidate, catalog.effectValues, catalog.projectileValues, known, false, catalog.scanStats);
		if (!catalog.effectValues.empty() && !catalog.projectileValues.empty()) {
			break;
		}
	}

	std::size_t inspected = 0;
	for (const std::filesystem::path& root : LuaLibraryRoots(workspace)) {
		std::error_code error;
		for (std::filesystem::recursive_directory_iterator iterator(root, std::filesystem::directory_options::skip_permission_denied, error), end;
			 iterator != end && !error && inspected < 300; iterator.increment(error)) {
			if (!iterator->is_regular_file(error) || Lower(iterator->path().extension().string()) != ".lua") {
				continue;
			}
			++inspected;
			ParseConstants(iterator->path(), catalog.effectValues, catalog.projectileValues, known, true, catalog.scanStats);
		}
	}

	SortAndUnique(catalog.effectValues);
	SortAndUnique(catalog.projectileValues);
	catalog.rebuildLookups();
	return catalog;
}

const std::vector<ServerVisualConstant>& ServerVisualCatalog::effects() const {
	return effectValues;
}

const std::vector<ServerVisualConstant>& ServerVisualCatalog::projectiles() const {
	return projectileValues;
}

std::optional<uint32_t> ServerVisualCatalog::resolve(ServerVisualKind kind, const std::string& value) const {
	if (const auto numeric = ParseNumber(value)) {
		return numeric;
	}
	const auto& values = kind == ServerVisualKind::MagicEffect ? effectIdsByName : projectileIdsByName;
	const auto found = values.find(Lower(Trim(value)));
	return found == values.end() ? std::nullopt : std::optional<uint32_t>(found->second);
}

std::string ServerVisualCatalog::nameFor(ServerVisualKind kind, uint32_t id) const {
	const auto& values = kind == ServerVisualKind::MagicEffect ? effectNamesById : projectileNamesById;
	const auto found = values.find(id);
	return found == values.end() ? std::to_string(id) : found->second;
}

const ServerVisualCatalogStats& ServerVisualCatalog::stats() const {
	return scanStats;
}

void ServerVisualCatalog::rebuildLookups() {
	for (const ServerVisualConstant& value : effectValues) {
		effectIdsByName.emplace(Lower(value.name), value.id);
		effectNamesById.emplace(value.id, value.name);
	}
	for (const ServerVisualConstant& value : projectileValues) {
		projectileIdsByName.emplace(Lower(value.name), value.id);
		projectileNamesById.emplace(value.id, value.name);
	}
}
