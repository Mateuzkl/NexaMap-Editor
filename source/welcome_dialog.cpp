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

#include "main.h"
#include "welcome_dialog.h"
#include "settings.h"
#include "preferences.h"
#include "theme.h"
#include "application.h"

wxDEFINE_EVENT(WELCOME_DIALOG_ACTION, wxCommandEvent);

WelcomeDialog::WelcomeDialog(const wxString& title_text, const wxString& version_text, const wxSize& size, const wxBitmap& rme_logo, const std::vector<wxString>& recent_files) :
	wxDialog(nullptr, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
	SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	wxColour const base_colour = Theme::Get(Theme::Role::Surface);
	m_welcome_dialog_panel = newd WelcomeDialogPanel(this, title_text, version_text, base_colour, wxBitmap(rme_logo.ConvertToImage().Scale(FROM_DIP(this, 48), FROM_DIP(this, 48))), recent_files);
	auto* dialog_sizer = newd wxBoxSizer(wxVERTICAL);
	dialog_sizer->Add(m_welcome_dialog_panel, 1, wxEXPAND);
	SetSizer(dialog_sizer);

	const wxSize minimum_client_size = FROM_DIP(this, wxSize(760, 440));
	SetMinClientSize(minimum_client_size);
	SetClientSize(wxSize(std::max(size.x, minimum_client_size.x), std::max(size.y, minimum_client_size.y)));
	Layout();
	Centre();
}

void WelcomeDialog::OnButtonClicked(const wxMouseEvent& event) {
	auto* button = dynamic_cast<WelcomeDialogButton*>(event.GetEventObject());
	wxSize const button_size = button->GetSize();
	wxPoint const click_point = event.GetPosition();
	if (click_point.x > 0 && click_point.x < button_size.x && click_point.y > 0 && click_point.y < button_size.x) {
		if (button->GetAction() == wxID_PREFERENCES) {
			PreferencesWindow preferences_window(m_welcome_dialog_panel, true);
			preferences_window.ShowModal();
			m_welcome_dialog_panel->updateInputs();
		} else {
			wxCommandEvent action_event(WELCOME_DIALOG_ACTION);
			if (button->GetAction() == wxID_OPEN) {
				wxString const wildcard = g_settings.getInteger(Config::USE_OTGZ) != 0 ? "(*.otbm;*.otgz)|*.otbm;*.otgz" : "(*.otbm)|*.otbm|Compressed OpenTibia Binary Map (*.otgz)|*.otgz";
				wxFileDialog file_dialog(this, "Open map file", "", "", wildcard, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
				if (file_dialog.ShowModal() == wxID_OK) {
					action_event.SetString(file_dialog.GetPath());
				} else {
					return;
				}
			}
			action_event.SetId(button->GetAction());
			ProcessWindowEvent(action_event);
		}
	}
}

void WelcomeDialog::OnCheckboxClicked(const wxCommandEvent& event) {
	g_settings.setInteger(Config::WELCOME_DIALOG, event.GetInt());
}

void WelcomeDialog::OnRecentItemClicked(const wxMouseEvent& event) {
	auto* recent_item = dynamic_cast<RecentItem*>(event.GetEventObject());
	wxSize const button_size = recent_item->GetSize();
	wxPoint const click_point = event.GetPosition();
	if (click_point.x > 0 && click_point.x < button_size.x && click_point.y > 0 && click_point.y < button_size.x) {
		wxCommandEvent action_event(WELCOME_DIALOG_ACTION);
		action_event.SetString(recent_item->GetText());
		action_event.SetId(wxID_OPEN);
		ProcessWindowEvent(action_event);
	}
}

WelcomeDialogPanel::WelcomeDialogPanel(WelcomeDialog* dialog, const wxString& title_text, const wxString& version_text, const wxColour& base_colour, const wxBitmap& rme_logo, const std::vector<wxString>& recent_files) :
	wxPanel(dialog),
	m_text_colour(Theme::Get(Theme::Role::Text)),
	m_background_colour(Theme::Get(Theme::Role::Surface)) {
	m_active_theme = static_cast<int>(Theme::GetType());
	SetBackgroundColour(m_background_colour);

	auto* recent_maps_panel = newd RecentMapsPanel(this, dialog, base_colour, recent_files);
	recent_maps_panel->SetBackgroundColour(Theme::Get(Theme::Role::Background));
	recent_maps_panel->SetMinSize(wxSize(FROM_DIP(this, 280), -1));

	auto* left_panel = newd wxPanel(this, wxID_ANY);
	left_panel->SetBackgroundColour(m_background_colour);
	auto* left_sizer = newd wxBoxSizer(wxVERTICAL);

	auto* logo = newd wxStaticBitmap(left_panel, wxID_ANY, rme_logo);
	auto* title = newd wxStaticText(left_panel, wxID_ANY, title_text, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL | wxST_NO_AUTORESIZE);
	wxFont title_font = GetFont();
	title_font.SetPointSize(std::max(18, title_font.GetPointSize() + 7));
	title->SetFont(title_font);
	title->SetForegroundColour(m_text_colour);
	auto* version = newd wxStaticText(left_panel, wxID_ANY, version_text, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL | wxST_NO_AUTORESIZE);
	version->SetForegroundColour(m_text_colour.ChangeLightness(110));

	wxSize const button_size = FROM_DIP(this, wxSize(150, 35));
	wxColour const button_base_colour = Theme::Get(Theme::Role::RaisedSurface);

	auto* new_map_button = newd WelcomeDialogButton(left_panel, wxDefaultPosition, button_size, button_base_colour, "New");
	new_map_button->SetAction(wxID_NEW);
	new_map_button->Bind(wxEVT_LEFT_UP, &WelcomeDialog::OnButtonClicked, dialog);

	auto* open_map_button = newd WelcomeDialogButton(left_panel, wxDefaultPosition, button_size, button_base_colour, "Open");
	open_map_button->SetAction(wxID_OPEN);
	open_map_button->Bind(wxEVT_LEFT_UP, &WelcomeDialog::OnButtonClicked, dialog);
	auto* preferences_button = newd WelcomeDialogButton(left_panel, wxDefaultPosition, button_size, button_base_colour, "Preferences");
	preferences_button->SetAction(wxID_PREFERENCES);
	preferences_button->Bind(wxEVT_LEFT_UP, &WelcomeDialog::OnButtonClicked, dialog);

	auto* buttons_sizer = newd wxBoxSizer(wxVERTICAL);
	buttons_sizer->Add(new_map_button, 0, wxALIGN_CENTER | wxTOP, FROM_DIP(this, 10));
	buttons_sizer->Add(open_map_button, 0, wxALIGN_CENTER | wxTOP, FROM_DIP(this, 10));
	buttons_sizer->Add(preferences_button, 0, wxALIGN_CENTER | wxTOP, FROM_DIP(this, 10));

	auto* theme_sizer = newd wxBoxSizer(wxHORIZONTAL);
	auto* theme_label = newd wxStaticText(left_panel, wxID_ANY, "Theme:");
	m_system_theme_radio = newd wxRadioButton(left_panel, wxID_ANY, "System", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	m_dark_theme_radio = newd wxRadioButton(left_panel, wxID_ANY, "Dark");
	m_light_theme_radio = newd wxRadioButton(left_panel, wxID_ANY, "Light");
	const wxString theme_tooltip = "Choose the editor appearance. The selected theme is applied the next time the editor starts.";
	wxWindow* theme_controls[] = {theme_label, m_system_theme_radio, m_dark_theme_radio, m_light_theme_radio};
	for (wxWindow* control : theme_controls) {
		control->SetBackgroundColour(m_background_colour);
		control->SetForegroundColour(m_text_colour);
		control->SetToolTip(theme_tooltip);
	}
	m_system_theme_radio->Bind(wxEVT_RADIOBUTTON, &WelcomeDialogPanel::OnThemeChanged, this);
	m_dark_theme_radio->Bind(wxEVT_RADIOBUTTON, &WelcomeDialogPanel::OnThemeChanged, this);
	m_light_theme_radio->Bind(wxEVT_RADIOBUTTON, &WelcomeDialogPanel::OnThemeChanged, this);
	theme_sizer->Add(m_system_theme_radio, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 8));
	theme_sizer->Add(m_dark_theme_radio, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 8));
	theme_sizer->Add(m_light_theme_radio, 0, wxALIGN_CENTER_VERTICAL);
	m_theme_status_label = newd wxStaticText(left_panel, wxID_ANY, "Changes require an application restart.", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
	m_theme_status_label->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	m_theme_status_label->SetBackgroundColour(m_background_colour);

	m_show_welcome_dialog_checkbox = newd wxCheckBox(left_panel, wxID_ANY, "Show this dialog on startup");

	m_show_welcome_dialog_checkbox->SetValue(g_settings.getInteger(Config::WELCOME_DIALOG) == 1);
	m_show_welcome_dialog_checkbox->Bind(wxEVT_CHECKBOX, &WelcomeDialog::OnCheckboxClicked, dialog);
	m_show_welcome_dialog_checkbox->SetBackgroundColour(m_background_colour);
	m_show_welcome_dialog_checkbox->SetForegroundColour(Theme::Get(Theme::Role::Text));

	left_sizer->AddSpacer(FROM_DIP(this, 30));
	left_sizer->Add(logo, 0, wxALIGN_CENTER_HORIZONTAL);
	left_sizer->AddSpacer(FROM_DIP(this, 16));
	left_sizer->Add(title, 0, wxEXPAND | wxLEFT | wxRIGHT, FROM_DIP(this, 24));
	left_sizer->AddSpacer(FROM_DIP(this, 4));
	left_sizer->Add(version, 0, wxEXPAND | wxLEFT | wxRIGHT, FROM_DIP(this, 24));
	left_sizer->AddStretchSpacer(2);
	left_sizer->Add(buttons_sizer, 0, wxALIGN_CENTER_HORIZONTAL);
	left_sizer->Add(theme_label, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FROM_DIP(this, 16));
	left_sizer->Add(theme_sizer, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FROM_DIP(this, 6));
	left_sizer->Add(m_theme_status_label, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FROM_DIP(this, 6));
	left_sizer->AddStretchSpacer(1);
	left_sizer->Add(m_show_welcome_dialog_checkbox, 0, wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 12));
	left_panel->SetSizer(left_sizer);

	const int left_minimum_width = std::max({
		FROM_DIP(this, 350),
		title->GetBestSize().x + FROM_DIP(this, 64),
		theme_sizer->GetMinSize().x + FROM_DIP(this, 48),
	});
	left_panel->SetMinSize(wxSize(left_minimum_width, -1));

	auto* root_sizer = newd wxBoxSizer(wxHORIZONTAL);
	root_sizer->Add(left_panel, 0, wxEXPAND);
	root_sizer->Add(recent_maps_panel, 1, wxEXPAND);
	SetSizer(root_sizer);
	updateInputs();
}

void WelcomeDialogPanel::updateInputs() {
	m_show_welcome_dialog_checkbox->SetValue(g_settings.getInteger(Config::WELCOME_DIALOG) == 1);
	const int selected_theme = g_settings.getInteger(Config::THEME);
	SetThemeSelection(selected_theme >= 0 && selected_theme <= 2 ? selected_theme : m_active_theme);
	UpdateThemeStatus();
	Layout();
}

void WelcomeDialogPanel::OnThemeChanged(const wxCommandEvent& event) {
	if (m_theme_prompt_open) {
		return;
	}
	int selected_theme = 0;
	if (event.GetEventObject() == m_dark_theme_radio) {
		selected_theme = 1;
	} else if (event.GetEventObject() == m_light_theme_radio) {
		selected_theme = 2;
	}

	const int previous_theme = g_settings.getInteger(Config::THEME);
	if (selected_theme == m_active_theme) {
		if (previous_theme != m_active_theme) {
			g_settings.setInteger(Config::THEME, m_active_theme);
			g_settings.save();
		}
		SetThemeSelection(m_active_theme);
		UpdateThemeStatus();
		return;
	}

	// Persist the selection immediately. Cancel restores the previous value,
	// so the saved configuration always matches the final user decision.
	g_settings.setInteger(Config::THEME, selected_theme);
	g_settings.save();
	m_theme_prompt_open = true;
	wxMessageDialog confirmation(
		this,
		"The selected theme will be applied after restarting the application.",
		"Restart required",
		wxYES_NO | wxCANCEL | wxICON_INFORMATION
	);
	confirmation.SetYesNoLabels("Restart Now", "Restart Later");
	const int result = confirmation.ShowModal();
	m_theme_prompt_open = false;

	if (result == wxID_CANCEL) {
		g_settings.setInteger(Config::THEME, previous_theme);
		g_settings.save();
		SetThemeSelection(previous_theme);
		UpdateThemeStatus();
		return;
	}

	SetThemeSelection(selected_theme);
	UpdateThemeStatus();
	if (result == wxID_YES) {
		auto& application = static_cast<Application&>(wxGetApp());
		if (!application.RequestApplicationRestart()) {
			// Closing was vetoed (for example, the user cancelled a map save).
			// Keep the choice as a safe pending change for the next normal start.
			UpdateThemeStatus();
		}
	}
}

void WelcomeDialogPanel::SetThemeSelection(int theme) {
	m_system_theme_radio->SetValue(theme == 0);
	m_dark_theme_radio->SetValue(theme == 1);
	m_light_theme_radio->SetValue(theme == 2);
}

void WelcomeDialogPanel::UpdateThemeStatus() {
	const bool pending = g_settings.getInteger(Config::THEME) != m_active_theme;
	m_theme_status_label->SetLabel(pending
		? "Theme change pending. Restart the application to apply it."
		: "Changes require an application restart.");
	m_theme_status_label->SetForegroundColour(pending ? Theme::Get(Theme::Role::Accent) : Theme::Get(Theme::Role::TextSubtle));
	m_theme_status_label->Refresh();
}

WelcomeDialogButton::WelcomeDialogButton(wxWindow* parent, const wxPoint& pos, const wxSize& size, const wxColour& base_colour, const wxString& text) :
	wxPanel(parent, wxID_ANY, pos, size),
	m_action(wxID_CLOSE),
	m_text(text),
	m_text_colour(Theme::Get(Theme::Role::Text)),
	m_background(Theme::Get(Theme::Role::RaisedSurface)),
	m_background_hover(Theme::Get(Theme::Role::Accent)),
	m_is_hover(false) {
	Bind(wxEVT_PAINT, &WelcomeDialogButton::OnPaint, this);
	Bind(wxEVT_ENTER_WINDOW, &WelcomeDialogButton::OnMouseEnter, this);
	Bind(wxEVT_LEAVE_WINDOW, &WelcomeDialogButton::OnMouseLeave, this);
}

void WelcomeDialogButton::OnPaint(const wxPaintEvent& event) {
	wxPaintDC dc(this);

	wxColour const colour = m_is_hover ? m_background_hover : m_background;
	dc.SetBrush(wxBrush(colour));
	dc.SetPen(wxPen(colour, 1));
	dc.DrawRectangle(wxRect(wxPoint(0, 0), GetClientSize()));

	dc.SetFont(GetFont());
	dc.SetTextForeground(m_is_hover ? Theme::Get(Theme::Role::TextOnAccent) : m_text_colour);
	wxSize const text_size = dc.GetTextExtent(m_text);
	dc.DrawText(m_text, wxPoint(GetSize().x / 2 - text_size.x / 2, GetSize().y / 2 - text_size.y / 2));
}

void WelcomeDialogButton::OnMouseEnter(const wxMouseEvent& event) {
	m_is_hover = true;
	Refresh();
}

void WelcomeDialogButton::OnMouseLeave(const wxMouseEvent& event) {
	m_is_hover = false;
	Refresh();
}

RecentMapsPanel::RecentMapsPanel(wxWindow* parent, WelcomeDialog* dialog, const wxColour& base_colour, const std::vector<wxString>& recent_files) :
	wxPanel(parent, wxID_ANY) {
	SetBackgroundColour(Theme::Get(Theme::Role::Background));
	auto* sizer = new wxBoxSizer(wxVERTICAL);
	for (const wxString& file : recent_files) {
		auto* recent_item = newd RecentItem(this, base_colour, file);
		sizer->Add(recent_item, 0, wxEXPAND);
		recent_item->Bind(wxEVT_LEFT_UP, &WelcomeDialog::OnRecentItemClicked, dialog);
	}
	SetSizer(sizer);
}

RecentItem::RecentItem(wxWindow* parent, const wxColour& base_colour, const wxString& item_name) :
	wxPanel(parent, wxID_ANY),
	m_text_colour(Theme::Get(Theme::Role::Text)),
	m_text_colour_hover(Theme::Get(Theme::Role::TextOnAccent)),
	m_background_colour(Theme::Get(Theme::Role::Background)),
	m_background_colour_hover(Theme::Get(Theme::Role::Accent)),
	m_item_text(item_name) {
	SetBackgroundColour(m_background_colour);
	m_title = newd wxStaticText(this, wxID_ANY, wxFileNameFromPath(m_item_text), wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	m_title->SetFont(GetFont().Bold());
	m_title->SetForegroundColour(m_text_colour);
	m_title->SetToolTip(m_item_text);
	m_file_path = newd wxStaticText(this, wxID_ANY, m_item_text, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_START);
	m_file_path->SetToolTip(m_item_text);
	m_file_path->SetFont(GetFont().Smaller());
	m_file_path->SetForegroundColour(m_text_colour);
	auto* mainSizer = newd wxBoxSizer(wxHORIZONTAL);
	auto* sizer = newd wxBoxSizer(wxVERTICAL);
	m_title->SetMinSize(wxSize(0, -1));
	m_file_path->SetMinSize(wxSize(0, -1));
	sizer->Add(m_title, 0, wxEXPAND);
	sizer->Add(m_file_path, 0, wxEXPAND | wxTOP, FROM_DIP(this, 2));
	mainSizer->Add(sizer, 1, wxEXPAND | wxALL, FROM_DIP(this, 8));
	Bind(wxEVT_ENTER_WINDOW, &RecentItem::OnMouseEnter, this);
	Bind(wxEVT_LEAVE_WINDOW, &RecentItem::OnMouseLeave, this);
	m_title->Bind(wxEVT_LEFT_UP, &RecentItem::PropagateItemClicked, this);
	m_file_path->Bind(wxEVT_LEFT_UP, &RecentItem::PropagateItemClicked, this);
	SetMinSize(wxSize(0, FROM_DIP(this, 54)));
	SetSizer(mainSizer);
}

void RecentItem::PropagateItemClicked(wxMouseEvent& event) {
	event.ResumePropagation(1);
	event.SetEventObject(this);
	event.Skip();
}

void RecentItem::OnMouseEnter(const wxMouseEvent& event) {
	if (GetScreenRect().Contains(ClientToScreen(event.GetPosition()))
		&& m_title->GetForegroundColour() != m_text_colour_hover) {
		m_title->SetForegroundColour(m_text_colour_hover);
		m_file_path->SetForegroundColour(m_text_colour_hover);
		SetBackgroundColour(m_background_colour_hover);
		Refresh();
		m_title->Refresh();
		m_file_path->Refresh();
	}
}

void RecentItem::OnMouseLeave(const wxMouseEvent& event) {
	if (!GetScreenRect().Contains(ClientToScreen(event.GetPosition()))
		&& m_title->GetForegroundColour() != m_text_colour) {
		m_title->SetForegroundColour(m_text_colour);
		m_file_path->SetForegroundColour(m_text_colour);
		SetBackgroundColour(m_background_colour);
		Refresh();
		m_title->Refresh();
		m_file_path->Refresh();
	}
}
