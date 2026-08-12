//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "../main.h"

#include "terrain_stamp_library.h"

#include "../gui.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace {
	constexpr const char* StampFormat = "nexamap-terrain-stamp";
	constexpr int StampSchemaVersion = 1;

	bool IsWindowsDeviceName(std::string_view name) {
		std::string stem(name.substr(0, name.find('.')));
		std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char character) {
			return static_cast<char>(std::toupper(character));
		});
		static const std::unordered_set<std::string> ReservedNames = {
			"CON", "PRN", "AUX", "NUL",
			"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
			"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
		};
		return ReservedNames.contains(stem);
	}

	nlohmann::json ItemToJson(const TerrainStampItem& item) {
		nlohmann::json json = {
			{ "id", item.id },
			{ "count", item.count },
		};
		if (item.actionId != 0) {
			json["aid"] = item.actionId;
		}
		if (item.uniqueId != 0) {
			json["uid"] = item.uniqueId;
		}
		if (!item.text.empty()) {
			json["text"] = item.text;
		}
		if (item.teleportDestination) {
			json["teleport"] = {
				{ "x", item.teleportDestination->x },
				{ "y", item.teleportDestination->y },
				{ "z", item.teleportDestination->z },
			};
		}
		if (item.doorId) {
			json["doorId"] = *item.doorId;
		}
		return json;
	}

	bool ItemFromJson(const nlohmann::json& json, TerrainStampItem& out, std::string& error) {
		if (!json.is_object() || !json.contains("id")) {
			error = "Stamp item is missing id.";
			return false;
		}
		out.id = json.value("id", uint16_t { 0 });
		out.count = json.value("count", uint16_t { 1 });
		out.actionId = json.value("aid", uint16_t { 0 });
		out.uniqueId = json.value("uid", uint16_t { 0 });
		out.text = json.value("text", std::string {});
		if (json.contains("teleport") && json["teleport"].is_object()) {
			const auto& teleport = json["teleport"];
			out.teleportDestination = Position(
				teleport.value("x", 0),
				teleport.value("y", 0),
				teleport.value("z", 0)
			);
		}
		if (json.contains("doorId")) {
			out.doorId = static_cast<uint8_t>(json.value("doorId", 0));
		}
		if (out.id == 0) {
			error = "Stamp item id cannot be 0.";
			return false;
		}
		return true;
	}

	nlohmann::json StampToJson(const TerrainStamp& stamp) {
		nlohmann::json tiles = nlohmann::json::array();
		for (const TerrainStampTile& tile : stamp.tiles) {
			nlohmann::json items = nlohmann::json::array();
			for (const TerrainStampItem& item : tile.items) {
				items.push_back(ItemToJson(item));
			}
			tiles.push_back({
				{ "dx", tile.dx },
				{ "dy", tile.dy },
				{ "dz", tile.dz },
				{ "items", std::move(items) },
			});
		}
		return {
			{ "format", StampFormat },
			{ "schemaVersion", StampSchemaVersion },
			{ "clientVersion", stamp.clientVersion },
			{ "name", stamp.name },
			{ "tiles", std::move(tiles) },
		};
	}

	bool StampFromJson(const nlohmann::json& json, TerrainStamp& out, std::string& error) {
		if (!json.is_object()) {
			error = "Stamp JSON root must be an object.";
			return false;
		}
		if (json.value("format", std::string {}) != StampFormat) {
			error = "Unsupported terrain stamp format.";
			return false;
		}
		if (json.value("schemaVersion", 0) != StampSchemaVersion) {
			error = "Unsupported terrain stamp schema version.";
			return false;
		}

		out = TerrainStamp {};
		out.name = json.value("name", std::string {});
		out.clientVersion = json.value("clientVersion", -1);
		if (!TerrainStampLibrary::IsValidName(out.name)) {
			error = "Stamp name is invalid.";
			return false;
		}
		if (!json.contains("tiles") || !json["tiles"].is_array()) {
			error = "Stamp is missing tiles array.";
			return false;
		}

		for (const auto& tileJson : json["tiles"]) {
			if (!tileJson.is_object()) {
				error = "Stamp tile must be an object.";
				return false;
			}
			TerrainStampTile tile;
			tile.dx = tileJson.value("dx", 0);
			tile.dy = tileJson.value("dy", 0);
			tile.dz = tileJson.value("dz", 0);
			if (!tileJson.contains("items") || !tileJson["items"].is_array()) {
				error = "Stamp tile is missing items array.";
				return false;
			}
			for (const auto& itemJson : tileJson["items"]) {
				TerrainStampItem item;
				if (!ItemFromJson(itemJson, item, error)) {
					return false;
				}
				tile.items.push_back(std::move(item));
			}
			if (!tile.items.empty()) {
				out.tiles.push_back(std::move(tile));
			}
		}
		if (out.empty()) {
			error = "Stamp contains no tiles.";
			return false;
		}
		return true;
	}

	bool WriteJsonFile(const std::filesystem::path& path, const nlohmann::json& json, std::string& error) {
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		if (!output) {
			error = "Failed to open stamp file for writing.";
			return false;
		}
		output << json.dump(2);
		if (!output) {
			error = "Failed to write stamp file.";
			return false;
		}
		return true;
	}

	bool ReadJsonFile(const std::filesystem::path& path, nlohmann::json& json, std::string& error) {
		std::ifstream input(path, std::ios::binary);
		if (!input) {
			error = "Failed to open stamp file.";
			return false;
		}
		try {
			input >> json;
		} catch (const std::exception& exception) {
			error = std::string("Failed to parse stamp JSON: ") + exception.what();
			return false;
		}
		return true;
	}
}

std::filesystem::path TerrainStampLibrary::GetDirectory() {
	return std::filesystem::path(g_gui.GetLocalDataDirectory().ToStdWstring()) / L"terrain_stamps";
}

bool TerrainStampLibrary::EnsureDirectory(std::string& error) {
	std::error_code ec;
	std::filesystem::create_directories(GetDirectory(), ec);
	if (ec) {
		error = "Failed to create terrain_stamps directory.";
		return false;
	}
	return true;
}

std::string TerrainStampLibrary::SanitizeFileStem(const std::string& name) {
	std::string stem;
	stem.reserve(name.size());
	for (unsigned char character : name) {
		if (std::isalnum(character) || character == '_' || character == '-' || character == ' ') {
			stem.push_back(character == ' ' ? '_' : static_cast<char>(character));
		}
	}
	while (!stem.empty() && (stem.front() == '_' || stem.front() == '-')) {
		stem.erase(stem.begin());
	}
	while (!stem.empty() && (stem.back() == '_' || stem.back() == '-')) {
		stem.pop_back();
	}
	if (stem.empty()) {
		stem = "terrain";
	}
	if (IsWindowsDeviceName(stem)) {
		stem += "_stamp";
	}
	return stem;
}

bool TerrainStampLibrary::IsValidName(const std::string& name) {
	if (name.empty() || name.size() > 64) {
		return false;
	}
	for (unsigned char character : name) {
		if (!(std::isalnum(character) || character == '_' || character == '-' || character == ' ')) {
			return false;
		}
	}
	return !IsWindowsDeviceName(SanitizeFileStem(name));
}

std::filesystem::path TerrainStampLibrary::PathForName(const std::string& name) {
	return GetDirectory() / (SanitizeFileStem(name) + ".json");
}

std::vector<std::string> TerrainStampLibrary::ListNames() {
	std::vector<std::string> names;
	std::string error;
	if (!EnsureDirectory(error)) {
		return names;
	}

	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(GetDirectory(), ec)) {
		if (ec || !entry.is_regular_file()) {
			continue;
		}
		if (entry.path().extension() != ".json") {
			continue;
		}
		nlohmann::json json;
		std::string parseError;
		if (!ReadJsonFile(entry.path(), json, parseError)) {
			continue;
		}
		TerrainStamp stamp;
		if (!StampFromJson(json, stamp, parseError)) {
			continue;
		}
		names.push_back(stamp.name);
	}
	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());
	return names;
}

bool TerrainStampLibrary::Load(const std::string& name, TerrainStamp& out, std::string& error) {
	if (!IsValidName(name)) {
		error = "Invalid stamp name.";
		return false;
	}
	nlohmann::json json;
	if (!ReadJsonFile(PathForName(name), json, error)) {
		return false;
	}
	if (!StampFromJson(json, out, error)) {
		return false;
	}
	return true;
}

bool TerrainStampLibrary::Save(const TerrainStamp& stamp, std::string& error, bool overwrite) {
	if (!IsValidName(stamp.name)) {
		error = "Invalid stamp name.";
		return false;
	}
	if (stamp.empty()) {
		error = "Stamp has no tiles to save.";
		return false;
	}
	if (!EnsureDirectory(error)) {
		return false;
	}

	const std::filesystem::path path = PathForName(stamp.name);
	if (!overwrite && std::filesystem::exists(path)) {
		error = "A stamp with that name already exists.";
		return false;
	}
	return WriteJsonFile(path, StampToJson(stamp), error);
}

bool TerrainStampLibrary::Rename(const std::string& oldName, const std::string& newName, std::string& error) {
	if (!IsValidName(oldName) || !IsValidName(newName)) {
		error = "Invalid stamp name.";
		return false;
	}
	if (SanitizeFileStem(oldName) == SanitizeFileStem(newName)) {
		TerrainStamp stamp;
		if (!Load(oldName, stamp, error)) {
			return false;
		}
		stamp.name = newName;
		return Save(stamp, error, true);
	}

	TerrainStamp stamp;
	if (!Load(oldName, stamp, error)) {
		return false;
	}
	stamp.name = newName;
	if (!Save(stamp, error, false)) {
		return false;
	}
	return Delete(oldName, error);
}

bool TerrainStampLibrary::Delete(const std::string& name, std::string& error) {
	if (!IsValidName(name)) {
		error = "Invalid stamp name.";
		return false;
	}
	std::error_code ec;
	if (!std::filesystem::remove(PathForName(name), ec) || ec) {
		error = "Failed to delete stamp file.";
		return false;
	}
	return true;
}

bool TerrainStampLibrary::ExportTo(const std::string& name, const std::filesystem::path& destination, std::string& error) {
	TerrainStamp stamp;
	if (!Load(name, stamp, error)) {
		return false;
	}
	return WriteJsonFile(destination, StampToJson(stamp), error);
}

bool TerrainStampLibrary::ImportFrom(const std::filesystem::path& source, TerrainStamp& out, std::string& error) {
	nlohmann::json json;
	if (!ReadJsonFile(source, json, error)) {
		return false;
	}
	if (!StampFromJson(json, out, error)) {
		return false;
	}
	return Save(out, error, true);
}
