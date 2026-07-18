#include "main.h"
#include "theme.h"

#include <array>

namespace {
	struct RoleColours {
		wxColour dark;
		wxColour light;
	};

	const std::array<RoleColours, static_cast<size_t>(Theme::Role::Count)> ROLE_COLOURS = { {
		{ wxColour(11, 24, 32), wxColour(255, 255, 255) },
		{ wxColour(7, 18, 23), wxColour(244, 247, 248) },
		{ wxColour(16, 31, 39), wxColour(234, 241, 243) },
		{ wxColour(242, 246, 248), wxColour(23, 33, 38) },
		{ wxColour(145, 164, 174), wxColour(96, 113, 122) },
		{ wxColour(255, 255, 255), wxColour(255, 255, 255) },
		{ wxColour(23, 51, 62), wxColour(216, 226, 230) },
		{ wxColour(0, 221, 235), wxColour(0, 175, 196) },
		{ wxColour(20, 42, 52), wxColour(225, 245, 247) },
		{ wxColour(18, 56, 68), wxColour(207, 236, 239) },
		{ wxColour(11, 24, 32), wxColour(23, 33, 38) },
		{ wxColour(145, 164, 174), wxColour(242, 246, 248) },
		{ wxColour(0, 221, 235), wxColour(0, 175, 196) },
		{ wxColour(23, 51, 62), wxColour(96, 113, 122) },
	} };

	const RoleColours FALLBACK_COLOURS = { wxColour(232, 234, 238), wxColour(28, 31, 36) };

	const RoleColours& GetRoleColours(Theme::Role role) {
		const size_t index = static_cast<size_t>(role);
		return index < ROLE_COLOURS.size() ? ROLE_COLOURS[index] : FALLBACK_COLOURS;
	}
}

Theme::Type Theme::currentType = Theme::Type::System;

void Theme::SetType(Type type) {
	currentType = type;
}

Theme::Type Theme::GetType() {
	return currentType;
}

bool Theme::IsDark() {
	if (currentType == Type::Dark) {
		return true;
	}
	if (currentType == Type::Light) {
		return false;
	}
	return wxSystemSettings::GetAppearance().IsDark();
}

wxColour Theme::Get(Role role) {
	return IsDark() ? GetDark(role) : GetLight(role);
}

wxColour Theme::GetDark(Role role) {
	return GetRoleColours(role).dark;
}

wxColour Theme::GetLight(Role role) {
	return GetRoleColours(role).light;
}
