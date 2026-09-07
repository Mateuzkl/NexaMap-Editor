//////////////////////////////////////////////////////////////////////
// Reusable visual selector for the 133 Tibia outfit colors.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_OUTFIT_COLOR_PICKER_H_
#define NEXAMAP_OUTFIT_COLOR_PICKER_H_

#include <wx/panel.h>

#include <array>
#include <functional>

class wxButton;

class OutfitColorPicker final : public wxPanel {
public:
	using ChangeHandler = std::function<void(int channel, int color)>;

	OutfitColorPicker(wxWindow* parent, ChangeHandler handler);

	void SetColors(int head, int body, int legs, int feet);
	void SetChannelEnabled(int channel, bool enabled);
	[[nodiscard]] int GetColor(int channel) const;

private:
	class Palette;
	void selectChannel(int channel);
	void chooseColor(int color);
	void refreshChannels();

	std::array<int, 4> colors {};
	std::array<wxButton*, 4> buttons {};
	std::array<bool, 4> enabled { true, true, true, true };
	Palette* palette = nullptr;
	ChangeHandler handler;
	int activeChannel = 0;
};

#endif // NEXAMAP_OUTFIT_COLOR_PICKER_H_
