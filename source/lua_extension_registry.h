// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_LUA_EXTENSION_REGISTRY_H_
#define NEXAMAP_LUA_EXTENSION_REGISTRY_H_

#include "lua_extension_runtime.h"

#include <filesystem>
#include <string>
#include <vector>

namespace LuaExtension {

	struct RegisteredExtension {
		std::string id;
		std::filesystem::path path;
		Metadata metadata;
		std::string error;
	};

	class Registry final {
	public:
		static Registry& Instance();
		void Reload();
		const std::vector<RegisteredExtension>& Entries() const {
			return entries_;
		}
		const RegisteredExtension* Find(const std::string& id) const;
		const RegisteredExtension* Inspect(const std::string& id);
		bool ReadSource(const RegisteredExtension& extension, std::string& source, std::string& error) const;
		std::vector<std::filesystem::path> Directories() const;

	private:
		std::vector<RegisteredExtension> entries_;
	};

} // namespace LuaExtension

#endif // NEXAMAP_LUA_EXTENSION_REGISTRY_H_
