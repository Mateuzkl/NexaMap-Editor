#ifndef RME_THEME_H_
#define RME_THEME_H_

#include <wx/colour.h>

class Theme {
public:
	enum class Type {
		System = 0,
		Dark = 1,
		Light = 2,
	};

	enum class Role {
		Surface,
		Background,
		RaisedSurface,
		Text,
		TextSubtle,
		TextOnAccent,
		Border,
		Accent,
		AccentHover,
		SelectionFill,
		TooltipBackground,
		TooltipLabel,
		TooltipValue,
		TooltipBorder,
		Count,
	};

	static void SetType(Type type);
	static Type GetType();
	static bool IsDark();
	static wxColour Get(Role role);
	static wxColour GetDark(Role role);

private:
	static Type currentType;
	static wxColour GetLight(Role role);
};

#endif
