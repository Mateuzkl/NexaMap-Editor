#include "main.h"
#include "theme.h"

#include <array>

namespace {
	struct RoleColours {
		wxColour dark;
		wxColour light;
	};

	const std::array<RoleColours, static_cast<size_t>(Theme::Role::Count)> ROLE_COLOURS = { {
		{ wxColour(32, 34, 39), wxColour(245, 246, 248) },
		{ wxColour(24, 26, 30), wxColour(255, 255, 255) },
		{ wxColour(43, 46, 53), wxColour(237, 239, 243) },
		{ wxColour(232, 234, 238), wxColour(28, 31, 36) },
		{ wxColour(164, 169, 179), wxColour(94, 100, 110) },
		{ wxColour(255, 255, 255), wxColour(255, 255, 255) },
		{ wxColour(70, 74, 84), wxColour(194, 198, 206) },
		{ wxColour(45, 112, 190), wxColour(38, 103, 180) },
		{ wxColour(61, 132, 216), wxColour(26, 86, 157) },
		{ wxColour(48, 91, 145), wxColour(47, 109, 185) },
		{ wxColour(29, 31, 37), wxColour(32, 34, 40) },
		{ wxColour(174, 179, 188), wxColour(184, 188, 196) },
		{ wxColour(244, 196, 48), wxColour(247, 199, 46) },
		{ wxColour(86, 91, 103), wxColour(92, 97, 110) },
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
