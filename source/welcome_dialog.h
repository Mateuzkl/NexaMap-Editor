//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#ifndef WELCOME_DIALOG_H
#define WELCOME_DIALOG_H

#include <wx/wx.h>

wxDECLARE_EVENT(WELCOME_DIALOG_ACTION, wxCommandEvent);

constexpr wxWindowID WELCOME_DIALOG_MAP_CONVERTER = wxID_HIGHEST + 7000;
constexpr wxWindowID WELCOME_DIALOG_SPAWN_CONVERTER = wxID_HIGHEST + 7001;

class WelcomeDialogPanel;
class RecentItem;

class WelcomeDialog : public wxDialog {
public:
	WelcomeDialog(const wxString& titleText, const wxString& versionText, const wxSize& size, const wxBitmap& rmeLogo, const std::vector<wxString>& recentFiles);
	void OnButtonClicked(const wxMouseEvent& event);
	void OnCheckboxClicked(const wxCommandEvent& event);
	void OnRecentItemClicked(const wxMouseEvent& event);

private:
	WelcomeDialogPanel* m_welcome_dialog_panel;
};

class WelcomeDialogPanel : public wxPanel {
public:
	WelcomeDialogPanel(WelcomeDialog* parent, const wxString& title_text, const wxString& version_text, const wxColour& base_colour, const wxBitmap& rme_logo, const std::vector<wxString>& recent_files);
	void OnThemeChanged(const wxCommandEvent& event);
	void updateInputs();

private:
	void SetThemeSelection(int theme);
	void UpdateThemeStatus();

	wxColour m_text_colour;
	wxColour m_background_colour;
	wxCheckBox* m_show_welcome_dialog_checkbox;
	wxRadioButton* m_system_theme_radio;
	wxRadioButton* m_dark_theme_radio;
	wxRadioButton* m_light_theme_radio;
	wxStaticText* m_theme_status_label;
	int m_active_theme = 0;
	bool m_theme_prompt_open = false;
};

class WelcomeDialogButton : public wxPanel {
public:
	WelcomeDialogButton(wxWindow* parent, const wxPoint& pos, const wxSize& size, const wxColour& base_colour, const wxString& text);
	void OnPaint(const wxPaintEvent& event);
	void OnMouseEnter(const wxMouseEvent& event);
	void OnMouseLeave(const wxMouseEvent& event);
	wxWindowID GetAction() {
		return m_action;
	};
	void SetAction(wxWindowID action) {
		m_action = action;
	};

private:
	wxWindowID m_action;
	wxString m_text;
	wxColour m_text_colour;
	wxColour m_background;
	wxColour m_background_hover;
	bool m_is_hover;
};

class RecentMapsPanel : public wxPanel {
public:
	RecentMapsPanel(wxWindow* parent, WelcomeDialog* dialog, const wxColour& base_colour, const std::vector<wxString>& recent_files);
	void SetHoveredItem(RecentItem* item);
	void ClearHoveredItem(RecentItem* item);

private:
	void OnMouseLeave(wxMouseEvent& event);

	RecentItem* m_hovered_item = nullptr;
};

class RecentItem : public wxPanel {
public:
	RecentItem(RecentMapsPanel* parent, const wxColour& base_colour, const wxString& item_name);
	void OnMouseEnter(wxMouseEvent& event);
	void OnMouseLeave(wxMouseEvent& event);
	void PropagateItemClicked(wxMouseEvent& event);
	void SetHovered(bool hovered);
	const wxString& GetText() {
		return m_item_text;
	};

private:
	wxColour m_text_colour;
	wxColour m_text_colour_hover;
	wxColour m_background_colour;
	wxColour m_background_colour_hover;
	wxStaticText* m_title;
	wxStaticText* m_file_path;
	wxString m_item_text;
};

#endif // WELCOME_DIALOG_H
