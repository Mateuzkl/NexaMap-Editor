#ifndef NEXAMAP_RUNTIME_PATHS_H
#define NEXAMAP_RUNTIME_PATHS_H

#include <cstdlib>
#include <filesystem>
#include <vector>

namespace RuntimePaths {

	// XDG paths must be absolute. Ignore invalid relative environment values.
	inline std::filesystem::path UserDirectory(const char* variable, const char* fallback) {
		if (const char* value = std::getenv(variable); value && *value) {
			std::filesystem::path path(value);
			if (path.is_absolute()) {
				return path / "nexamap";
			}
		}
		if (const char* home = std::getenv("HOME"); home && std::filesystem::path(home).is_absolute()) {
			return std::filesystem::path(home) / fallback / "nexamap";
		}
		return {};
	}

	inline std::vector<std::filesystem::path> DataCandidates(
		const std::filesystem::path& executableDirectory,
		const std::filesystem::path& workingDirectory,
		const std::filesystem::path& installedRelative = "../share/nexamap",
		const std::filesystem::path& installedAbsolute = {}
	) {
		std::vector<std::filesystem::path> paths {
			executableDirectory / "data",
			executableDirectory / installedRelative / "data",
		};
		if (!installedAbsolute.empty()) {
			paths.push_back(installedAbsolute / "data");
		}
		// Preserve developer builds and the existing portable Windows layout.
		for (const auto& base : { executableDirectory.parent_path(), executableDirectory.parent_path().parent_path(), executableDirectory.parent_path().parent_path().parent_path(), workingDirectory, workingDirectory.parent_path() }) {
			paths.push_back(base / "data");
		}
		return paths;
	}

} // namespace RuntimePaths
#endif
