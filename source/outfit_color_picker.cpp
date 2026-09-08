//////////////////////////////////////////////////////////////////////
// Reusable visual selector for the 133 Tibia outfit colors.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "outfit_color_picker.h"

#include "graphics.h"
#include "theme.h"

#include <wx/button.h>
#include <wx/dcbuffer.h>
#include <wx/stattext.h>

#include <algorithm>
#include <array>
#include <random>

namespace {
	wxColour OutfitColour(int color) {
		const uint32_t rgb = GetOutfitColorRgb(static_cast<std::size_t>(std::clamp(color, 0, 132)));
		return { static_cast<unsigned char>((rgb >> 16) & 0xff), static_cast<unsigned char>((rgb >> 8) & 0xff), static_cast<unsigned char>(rgb & 0xff) };
	}
}

class OutfitColorPicker::Palette final : public wxPanel {
public:
	Palette(wxWindow* parent, std::function<void(int)> callback) :
		wxPanel(parent, wxID_ANY), callback(std::move(callback)) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetMinSize(FromDIP(wxSize(19 * 15 + 4, 7 * 15 + 4)));
		Bind(wxEVT_PAINT, [this](wxPaintEvent&) { paint(); });
		Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
			const int cell = FromDIP(15);
			const int column = (event.GetX() - FromDIP(2)) / cell;
			const int row = (event.GetY() - FromDIP(2)) / cell;
			const int color = row * 19 + column;
			if (column >= 0 && column < 19 && row >= 0 && row < 7 && color < 133) {
				selected = color;
				Refresh();
				this->callback(color);
			}
		});
	}

	void SetSelected(int color) {
		selected = std::clamp(color, 0, 132);
		Refresh();
	}

private:
	void paint() {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Surface)));
		dc.Clear();
		const int cell = FromDIP(15);
		for (int color = 0; color < 133; ++color) {
			const int x = FromDIP(2) + (color % 19) * cell;
			const int y = FromDIP(2) + (color / 19) * cell;
			dc.SetBrush(wxBrush(OutfitColour(color)));
			dc.SetPen(wxPen(color == selected ? wxColour(255, 196, 40) : Theme::Get(Theme::Role::Border), color == selected ? FromDIP(2) : 1));
			dc.DrawRectangle(x, y, cell - 1, cell - 1);
		}
	}

	std::function<void(int)> callback;
	int selected = 0;
};

OutfitColorPicker::OutfitColorPicker(wxWindow* parent, ChangeHandler changed) :
	wxPanel(parent, wxID_ANY), handler(std::move(changed)) {
	auto* root = newd wxBoxSizer(wxVERTICAL);
	auto* chooser = newd wxBoxSizer(wxHORIZONTAL);
	auto* channels = newd wxBoxSizer(wxVERTICAL);
	static constexpr std::array<const char*, 4> Labels { "Head", "Primary", "Secondary", "Detail" };
	for (int channel = 0; channel < 4; ++channel) {
		auto* row = newd wxBoxSizer(wxHORIZONTAL);
		buttons[channel] = newd wxButton(this, wxID_ANY, Labels[channel], wxDefaultPosition, FromDIP(wxSize(88, 28)));
		buttons[channel]->Bind(wxEVT_BUTTON, [this, channel](wxCommandEvent&) { selectChannel(channel); });
		row->Add(buttons[channel], 0, wxRIGHT, FromDIP(6));
		auto* swatch = newd wxPanel(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(20, 20)));
		swatch->SetName(wxString::Format("outfit-swatch-%d", channel));
		row->Add(swatch, 0, wxALIGN_CENTER_VERTICAL);
		channels->Add(row, 0, wxBOTTOM, FromDIP(4));
	}
	chooser->Add(channels, 0, wxRIGHT, FromDIP(10));
	palette = newd Palette(this, [this](int color) { chooseColor(color); });
	chooser->Add(palette, 0, wxALIGN_CENTER_VERTICAL);
	root->Add(chooser, 0, wxEXPAND);
	auto* randomize = newd wxButton(this, wxID_ANY, "Randomize Colors");
	randomize->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		static std::mt19937 generator(std::random_device {}());
		std::uniform_int_distribution<int> distribution(0, 132);
		for (int channel = 0; channel < 4; ++channel) {
			if (enabled[channel]) {
				colors[channel] = distribution(generator);
				handler(channel, colors[channel]);
			}
		}
		refreshChannels();
	});
	root->Add(randomize, 0, wxALIGN_CENTER | wxTOP, FromDIP(8));
	SetSizer(root);
	refreshChannels();
}

void OutfitColorPicker::SetColors(int head, int body, int legs, int feet) {
	colors = { std::clamp(head, 0, 132), std::clamp(body, 0, 132), std::clamp(legs, 0, 132), std::clamp(feet, 0, 132) };
	refreshChannels();
}

void OutfitColorPicker::SetChannelEnabled(int channel, bool value) {
	if (channel >= 0 && channel < 4) {
		enabled[channel] = value;
		buttons[channel]->Enable(value);
		if (!enabled[activeChannel]) {
			for (int candidate = 0; candidate < 4; ++candidate) {
				if (enabled[candidate]) {
					activeChannel = candidate;
					break;
				}
			}
		}
		refreshChannels();
	}
}

int OutfitColorPicker::GetColor(int channel) const {
	return channel >= 0 && channel < 4 ? colors[channel] : 0;
}

void OutfitColorPicker::selectChannel(int channel) {
	if (channel >= 0 && channel < 4 && enabled[channel]) {
		activeChannel = channel;
		refreshChannels();
	}
}

void OutfitColorPicker::chooseColor(int color) {
	if (!enabled[activeChannel]) {
		return;
	}
	colors[activeChannel] = color;
	handler(activeChannel, color);
	refreshChannels();
}

void OutfitColorPicker::refreshChannels() {
	for (int channel = 0; channel < 4; ++channel) {
		buttons[channel]->SetBackgroundColour(channel == activeChannel ? wxColour(194, 132, 26) : Theme::Get(Theme::Role::RaisedSurface));
		buttons[channel]->SetForegroundColour(Theme::Get(Theme::Role::Text));
		if (wxWindow* swatch = FindWindowByName(wxString::Format("outfit-swatch-%d", channel), this)) {
			swatch->SetBackgroundColour(OutfitColour(colors[channel]));
			swatch->SetToolTip(wxString::Format("Color %d", colors[channel]));
			swatch->Refresh();
		}
		buttons[channel]->Refresh();
	}
	if (palette) {
		palette->SetSelected(colors[activeChannel]);
		palette->Enable(enabled[activeChannel]);
	}
}
