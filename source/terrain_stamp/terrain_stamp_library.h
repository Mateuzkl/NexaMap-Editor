//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_TERRAIN_STAMP_LIBRARY_H_
#define RME_TERRAIN_STAMP_LIBRARY_H_

#include "terrain_stamp.h"

#include <filesystem>
#include <string>
#include <vector>

class TerrainStampLibrary {
public:
	static std::filesystem::path GetDirectory();
	static bool EnsureDirectory(std::string& error);

	static std::vector<std::string> ListNames();
	static bool Load(const std::string& name, TerrainStamp& out, std::string& error);
	static bool Save(const TerrainStamp& stamp, std::string& error, bool overwrite = true);
	static bool Rename(const std::string& oldName, const std::string& newName, std::string& error);
	static bool Delete(const std::string& name, std::string& error);

	static bool ExportTo(const std::string& name, const std::filesystem::path& destination, std::string& error);
	static bool ImportFrom(const std::filesystem::path& source, TerrainStamp& out, std::string& error);

	static std::string SanitizeFileStem(const std::string& name);
	static bool IsValidName(const std::string& name);

private:
	static std::filesystem::path PathForName(const std::string& name);
};

#endif
