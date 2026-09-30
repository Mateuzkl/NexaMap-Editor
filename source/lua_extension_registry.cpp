// SPDX-License-Identifier: GPL-3.0-or-later
#include "lua_extension_registry.h"

#include <wx/stdpaths.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

namespace LuaExtension {

	Registry& Registry::Instance() {
		static Registry registry;
		return registry;
	}

	std::vector<std::filesystem::path> Registry::Directories() const {
		const auto executable = std::filesystem::path(wxStandardPaths::Get().GetExecutablePath().ToStdWstring()).parent_path();
		const auto user = std::filesystem::path(wxStandardPaths::Get().GetUserDataDir().ToStdWstring());
		return { executable / "extensions" / "lua", user / "extensions" / "lua" };
	}

	void Registry::Reload() {
		entries_.clear();
		const auto directories = Directories();
		for (size_t origin = 0; origin < directories.size(); ++origin) {
			std::error_code error;
			if (!std::filesystem::is_directory(directories[origin], error)) {
				continue;
			}
			for (const auto& entry : std::filesystem::directory_iterator(directories[origin], error)) {
				if (error) {
					break;
				}
				if (entry.is_symlink(error) || !entry.is_regular_file(error) || entry.path().extension() != ".lua") {
					continue;
				}
				const auto bytes = entry.file_size(error);
				if (error || bytes > Limits {}.maxSourceBytes) {
					continue;
				}
				RegisteredExtension extension;
				extension.id = (origin == 0 ? "bundled/" : "user/") + entry.path().filename().string();
				extension.path = entry.path();
				extension.metadata.name = entry.path().stem().string();
				entries_.push_back(std::move(extension));
			}
		}
		std::sort(entries_.begin(), entries_.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
	}

	const RegisteredExtension* Registry::Find(const std::string& id) const {
		const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto& extension) { return extension.id == id; });
		return found != entries_.end() ? &*found : nullptr;
	}

	bool Registry::ReadSource(const RegisteredExtension& extension, std::string& source, std::string& error) const {
		source.clear();
		error.clear();
		const RegisteredExtension* registered = Find(extension.id);
		if (!registered || registered->path != extension.path) {
			error = "Extension is not registered. Reload the extension list.";
			return false;
		}
		std::error_code filesystemError;
		if (std::filesystem::is_symlink(extension.path, filesystemError) || !std::filesystem::is_regular_file(extension.path, filesystemError)) {
			error = "Extension file is missing or is a symbolic link.";
			return false;
		}
		const auto size = std::filesystem::file_size(extension.path, filesystemError);
		if (filesystemError || size > Limits {}.maxSourceBytes) {
			error = "Extension source exceeds size limit.";
			return false;
		}
		std::ifstream file(extension.path, std::ios::binary);
		if (!file) {
			error = "Could not open registered extension file.";
			return false;
		}
		source.assign(std::istreambuf_iterator<char>(file), {});
		if (source.size() != size) {
			error = "Extension file changed while it was being read. Reload and preview again.";
			return false;
		}
		return true;
	}

	const RegisteredExtension* Registry::Inspect(const std::string& id) {
		auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto& extension) { return extension.id == id; });
		if (found == entries_.end()) {
			return nullptr;
		}
		std::string source;
		if (!ReadSource(*found, source, found->error)) {
			return &*found;
		}
		found->metadata = Runtime::Inspect(source, found->path.filename().string(), found->error);
		return &*found;
	}

} // namespace LuaExtension
