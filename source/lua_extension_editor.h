// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_LUA_EXTENSION_EDITOR_H_
#define NEXAMAP_LUA_EXTENSION_EDITOR_H_

#include "lua_extension_runtime.h"

class Editor;

namespace LuaExtension {

	// These functions run only on the GUI thread. Runtime::Preview itself never
	// changes the map; Apply revalidates everything against the active session.
	Context CaptureContext(Editor& editor, std::string& error);
	bool ValidatePlan(Editor& editor, const Plan& plan, std::string& error);
	bool ApplyPlan(Editor& editor, const Plan& plan, std::string& error);

} // namespace LuaExtension

#endif // NEXAMAP_LUA_EXTENSION_EDITOR_H_
