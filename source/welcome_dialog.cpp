//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"
#include "welcome_dialog.h"

#include "application.h"
#include "gui.h"
#include "preferences.h"
#include "settings.h"
#include "theme.h"

#include <wx/access.h>
#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/filename.h>

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <utility>

wxDEFINE_EVENT(WELCOME_DIALOG_ACTION, wxCommandEvent);

namespace {
	constexpr int NAVIGATION_WIDTH_DIP = 300;
	constexpr int NAVIGATION_ITEM_HEIGHT_DIP = 72;

	namespace WelcomeThemeStyle {
		wxColour Get(Theme::Role role) {
			return Theme::GetDark(role);
		}
	}

#if wxUSE_ACCESSIBILITY && defined(__WXMSW__)
	class WelcomeControlAccessible final : public wxWindowAccessible {
	public:
		WelcomeControlAccessible(wxWindow* window, wxAccRole role, const wxString& defaultAction, std::function<void()> action, long selectedState = 0, std::function<bool()> selected = {}) :
			wxWindowAccessible(window),
			m_role(role),
			m_default_action(defaultAction),
			m_action(std::move(action)),
			m_selected_state(selectedState),
			m_selected(std::move(selected)) {}

		wxAccStatus GetName(int childId, wxString* name) override {
			if (childId != wxACC_SELF || !name) {
				return wxACC_INVALID_ARG;
			}
			*name = GetWindow()->GetName();
			return name->empty() ? wxACC_FALSE : wxACC_OK;
		}

		wxAccStatus GetDescription(int childId, wxString* description) override {
			if (childId != wxACC_SELF || !description) {
				return wxACC_INVALID_ARG;
			}
			*description = GetWindow()->GetHelpText();
			return description->empty() ? wxACC_FALSE : wxACC_OK;
		}

		wxAccStatus GetDefaultAction(int childId, wxString* actionName) override {
			if (childId != wxACC_SELF || !actionName) {
				return wxACC_INVALID_ARG;
			}
			*actionName = m_default_action;
			return wxACC_OK;
		}

		wxAccStatus DoDefaultAction(int childId) override {
			if (childId != wxACC_SELF) {
				return wxACC_INVALID_ARG;
			}
			if (!GetWindow()->IsEnabled()) {
				return wxACC_FAIL;
			}
			GetWindow()->SetFocus();
			m_action();
			return wxACC_OK;
		}

		wxAccStatus GetRole(int childId, wxAccRole* role) override {
			if (childId != wxACC_SELF || !role) {
				return wxACC_INVALID_ARG;
			}
			*role = m_role;
			return wxACC_OK;
		}

		wxAccStatus GetState(int childId, long* state) override {
			if (childId != wxACC_SELF || !state) {
				return wxACC_INVALID_ARG;
			}

			*state = wxACC_STATE_SYSTEM_FOCUSABLE;
			if (GetWindow()->HasFocus()) {
				*state |= wxACC_STATE_SYSTEM_FOCUSED;
			}
			if (!GetWindow()->IsEnabled()) {
				*state |= wxACC_STATE_SYSTEM_UNAVAILABLE;
			}
			if (m_selected_state != 0) {
				*state |= wxACC_STATE_SYSTEM_SELECTABLE;
				if (m_selected && m_selected()) {
					*state |= m_selected_state;
				}
			}
			return wxACC_OK;
		}

	private:
		wxAccRole m_role;
		wxString m_default_action;
		std::function<void()> m_action;
		long m_selected_state = 0;
		std::function<bool()> m_selected;
	};
#endif

	wxString WelcomeAssetsFromDataDirectory(const wxString& dataDirectory) {
		if (dataDirectory.empty()) {
			return {};
		}

		wxFileName assetsDirectory = wxFileName::DirName(dataDirectory);
		assetsDirectory.AppendDir("images");
		assetsDirectory.AppendDir("welcome");
		if (!assetsDirectory.DirExists()) {
			return {};
		}
		return assetsDirectory.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
	}

	wxString WelcomeAssetsFromRepository(const wxString& startDirectory) {
		if (startDirectory.empty()) {
			return {};
		}

		wxFileName repositoryDirectory = wxFileName::DirName(startDirectory);
		repositoryDirectory.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
		while (repositoryDirectory.GetDirCount() > 0) {
			const wxString repositoryPath = repositoryDirectory.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
			const wxFileName repositoryMarker(repositoryPath + "CMakeLists.txt");

			wxFileName dataDirectory = repositoryDirectory;
			dataDirectory.AppendDir("data");
			const wxString assetsDirectory = WelcomeAssetsFromDataDirectory(dataDirectory.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR));
			if (repositoryMarker.FileExists() && !assetsDirectory.empty()) {
				return assetsDirectory;
			}

			repositoryDirectory.RemoveLastDir();
		}
		return {};
	}

	const wxString& WelcomeAssetDirectory() {
		static const wxString directory = [] {
			const wxString developmentRoots[] = { GUI::GetExecDirectory(), wxGetCwd() };
			for (const wxString& root : developmentRoots) {
				const wxString assetsDirectory = WelcomeAssetsFromRepository(root);
				if (!assetsDirectory.empty()) {
					return assetsDirectory;
				}
			}

			const wxString dataDirectories[] = { g_gui.getFoundDataDirectory(), GUI::GetDataDirectory() };
			for (const wxString& dataDirectory : dataDirectories) {
				const wxString assetsDirectory = WelcomeAssetsFromDataDirectory(dataDirectory);
				if (!assetsDirectory.empty()) {
					return assetsDirectory;
				}
			}

			wxFileName fallbackDirectory = wxFileName::DirName(GUI::GetDataDirectory());
			fallbackDirectory.AppendDir("images");
			fallbackDirectory.AppendDir("welcome");
			return fallbackDirectory.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
		}();
		return directory;
	}

	wxString WelcomeAssetPath(const wxString& fileName) {
		wxFileName path(WelcomeAssetDirectory(), fileName);
		return path.GetFullPath();
	}

	wxBitmap LoadWelcomeBitmap(wxWindow* window, const wxString& fileName, const wxSize& dipSize, const wxColour& tint = wxNullColour) {
		static std::map<std::string, wxBitmap> cache;

		const wxSize targetSize = FROM_DIP(window, dipSize);
		const wxString path = WelcomeAssetPath(fileName);
		const unsigned long tintValue = tint.IsOk() ? tint.GetRGBA() : 0;
		const std::string key = nstr(path) + ":" + std::to_string(targetSize.x) + "x" + std::to_string(targetSize.y) + ":" + std::to_string(tintValue);
		const auto cached = cache.find(key);
		if (cached != cache.end()) {
			return cached->second;
		}

		wxImage image(path, wxBITMAP_TYPE_PNG);
		if (!image.IsOk() || targetSize.x <= 0 || targetSize.y <= 0) {
			return wxNullBitmap;
		}

		image = image.Scale(targetSize.x, targetSize.y, wxIMAGE_QUALITY_HIGH);
		if (tint.IsOk()) {
			if (!image.HasAlpha()) {
				image.InitAlpha();
				std::fill(image.GetAlpha(), image.GetAlpha() + targetSize.x * targetSize.y, 255);
			}
			unsigned char* pixels = image.GetData();
			for (int index = 0; index < targetSize.x * targetSize.y; ++index) {
				pixels[index * 3] = tint.Red();
				pixels[index * 3 + 1] = tint.Green();
				pixels[index * 3 + 2] = tint.Blue();
			}
		}

		wxBitmap bitmap(image);
		cache.emplace(key, bitmap);
		return bitmap;
	}

	wxFont FontWithPointSize(const wxFont& base, int pointSize, bool bold = false) {
		wxFont font(base);
		font.SetPointSize(std::max(7, pointSize));
		font.SetWeight(bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
		return font;
	}

	void DrawFocusRing(wxDC& dc, wxWindow* window, const wxRect& rect, int radius = 5) {
		if (!window->HasFocus()) {
			return;
		}

		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.SetPen(wxPen(WelcomeThemeStyle::Get(Theme::Role::Accent), FROM_DIP(window, 1), wxPENSTYLE_DOT));
		dc.DrawRoundedRectangle(rect, FROM_DIP(window, radius));
	}

	wxString Ellipsize(const wxString& text, wxDC& dc, wxEllipsizeMode mode, int width) {
		return wxControl::Ellipsize(text, dc, mode, std::max(0, width), wxELLIPSIZE_FLAGS_EXPAND_TABS);
	}

	wxRect Deflated(wxRect rect, int amount) {
		return rect.Deflate(amount);
	}
}

WelcomeDialog::WelcomeDialog(const wxString& titleText, const wxString& versionText, const wxSize& size, const wxBitmap& fallbackLogo, const std::vector<wxString>& recentFiles) :
	wxDialog(nullptr, wxID_ANY, titleText, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
	SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Surface));
	m_welcome_dialog_panel = newd WelcomeDialogPanel(this, titleText, versionText, fallbackLogo, recentFiles);

	auto* dialogSizer = newd wxBoxSizer(wxVERTICAL);
	dialogSizer->Add(m_welcome_dialog_panel, 1, wxEXPAND);
	SetSizer(dialogSizer);

	wxSize minimumClientSize = FROM_DIP(this, wxSize(640, 400));
	wxSize requestedClientSize(std::max(size.x, minimumClientSize.x), std::max(size.y, minimumClientSize.y));
	int displayIndex = g_gui.root ? wxDisplay::GetFromWindow(g_gui.root) : wxNOT_FOUND;
	if (displayIndex == wxNOT_FOUND && wxDisplay::GetCount() > 0) {
		displayIndex = 0;
	}
	if (displayIndex != wxNOT_FOUND) {
		const wxRect workArea = wxDisplay(static_cast<unsigned int>(displayIndex)).GetClientArea();
		const wxSize available(std::max(480, workArea.width - FROM_DIP(this, 32)), std::max(320, workArea.height - FROM_DIP(this, 32)));
		minimumClientSize.DecTo(available);
		requestedClientSize.DecTo(available);
	}
	SetMinClientSize(minimumClientSize);
	SetClientSize(requestedClientSize);
	Layout();
	Centre();
}

void WelcomeDialog::OnNavigationActivated(wxCommandEvent& event) {
	ActivateAction(event.GetId());
}

void WelcomeDialog::OnCheckboxClicked(wxCommandEvent& event) {
	g_settings.setInteger(Config::WELCOME_DIALOG, event.IsChecked() ? 1 : 0);
}

void WelcomeDialog::OnRecentItemActivated(wxCommandEvent& event) {
	ActivateAction(wxID_OPEN, event.GetString());
}

void WelcomeDialog::ActivateAction(wxWindowID action, const wxString& path) {
	if (action == wxID_PREFERENCES) {
		PreferencesWindow preferencesWindow(m_welcome_dialog_panel, true);
		preferencesWindow.ShowModal();
		m_welcome_dialog_panel->UpdateInputs();
		return;
	}

	wxCommandEvent actionEvent(WELCOME_DIALOG_ACTION);
	if (action == wxID_OPEN) {
		wxString selectedPath = path;
		if (selectedPath.empty()) {
			const wxString wildcard = g_settings.getInteger(Config::USE_OTGZ) != 0
				? "(*.otbm;*.otgz)|*.otbm;*.otgz"
				: "(*.otbm)|*.otbm|Compressed OpenTibia Binary Map (*.otgz)|*.otgz";
			wxFileDialog fileDialog(this, "Open map file", "", "", wildcard, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
			if (fileDialog.ShowModal() != wxID_OK) {
				return;
			}
			selectedPath = fileDialog.GetPath();
		}
		actionEvent.SetString(selectedPath);
	}

	actionEvent.SetId(action);
	ProcessWindowEvent(actionEvent);
}

WelcomeDialogPanel::WelcomeDialogPanel(WelcomeDialog* dialog, const wxString& titleText, const wxString& versionText, const wxBitmap& fallbackLogo, const std::vector<wxString>& recentFiles) :
	wxPanel(dialog) {
	m_active_theme = static_cast<int>(Theme::GetType());
	SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Surface));

	auto* navigationPanel = newd wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE);
	navigationPanel->SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Surface));
	navigationPanel->SetScrollRate(0, FROM_DIP(this, 12));
	navigationPanel->SetMinSize(wxSize(FROM_DIP(this, NAVIGATION_WIDTH_DIP), -1));
	auto* navigationSizer = newd wxBoxSizer(wxVERTICAL);
	navigationSizer->AddSpacer(FROM_DIP(this, 44));

	AddNavigationItem(navigationPanel, navigationSizer, "icon_new_map.png", "New Map", "Start a new project", "Create a new OTBM map.", wxID_NEW, true);
	AddNavigationItem(navigationPanel, navigationSizer, "icon_open_project.png", "Open Project", "Open an existing map", "Open an existing OTBM map.", wxID_OPEN);
	AddNavigationItem(navigationPanel, navigationSizer, "icon_map_converter.png", "Map Converter", "Convert map formats", "Convert maps between ServerID and ClientID formats.", WELCOME_DIALOG_MAP_CONVERTER);
	AddNavigationItem(navigationPanel, navigationSizer, "icon_spawn_npc_converter.png", "Spawn / NPC Converter", "Convert spawns and NPCs", "Convert spawn files between TFS and Canary/Crystal formats.", WELCOME_DIALOG_SPAWN_CONVERTER);
	AddNavigationItem(navigationPanel, navigationSizer, "icon_preferences.png", "Preferences", "Configure the editor", "Configure NexaMap Editor.", wxID_PREFERENCES);

	navigationSizer->AddSpacer(FROM_DIP(this, 18));
	auto* separator = newd wxPanel(navigationPanel, wxID_ANY);
	separator->SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Border));
	separator->SetMinSize(wxSize(-1, FROM_DIP(this, 1)));
	navigationSizer->Add(separator, 0, wxEXPAND | wxLEFT | wxRIGHT, FROM_DIP(this, 24));
	navigationSizer->AddSpacer(FROM_DIP(this, 18));

	auto* themeLabel = newd wxStaticText(navigationPanel, wxID_ANY, "THEME");
	themeLabel->SetFont(FontWithPointSize(GetFont(), std::max(8, GetFont().GetPointSize() - 1), true));
	themeLabel->SetForegroundColour(WelcomeThemeStyle::Get(Theme::Role::TextSubtle));
	themeLabel->SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Surface));
	navigationSizer->Add(themeLabel, 0, wxLEFT | wxRIGHT, FROM_DIP(this, 24));

	auto* themeSizer = newd wxBoxSizer(wxHORIZONTAL);
	m_system_theme_choice = newd WelcomeThemeChoice(navigationPanel, 0, "icon_system.png", "System");
	m_dark_theme_choice = newd WelcomeThemeChoice(navigationPanel, 1, "icon_dark.png", "Dark");
	m_light_theme_choice = newd WelcomeThemeChoice(navigationPanel, 2, "icon_light.png", "Light");
	for (WelcomeThemeChoice* choice : {m_system_theme_choice, m_dark_theme_choice, m_light_theme_choice}) {
		choice->Bind(wxEVT_BUTTON, &WelcomeDialogPanel::OnThemeChanged, this);
		themeSizer->Add(choice, 1, wxEXPAND | wxRIGHT, choice == m_light_theme_choice ? 0 : FROM_DIP(this, 6));
	}
	navigationSizer->Add(themeSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 16));

	m_theme_status_label = newd wxStaticText(navigationPanel, wxID_ANY, "Changes require an application restart.", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	m_theme_status_label->SetFont(FontWithPointSize(GetFont(), std::max(8, GetFont().GetPointSize() - 1)));
	m_theme_status_label->SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Surface));
	navigationSizer->Add(m_theme_status_label, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 24));

	m_show_welcome_dialog_checkbox = newd wxCheckBox(navigationPanel, wxID_ANY, "Show this screen on startup");
	m_show_welcome_dialog_checkbox->SetValue(g_settings.getInteger(Config::WELCOME_DIALOG) == 1);
	m_show_welcome_dialog_checkbox->Bind(wxEVT_CHECKBOX, &WelcomeDialog::OnCheckboxClicked, dialog);
	m_show_welcome_dialog_checkbox->SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Surface));
	m_show_welcome_dialog_checkbox->SetForegroundColour(WelcomeThemeStyle::Get(Theme::Role::Text));
	navigationSizer->AddStretchSpacer();
	navigationSizer->Add(m_show_welcome_dialog_checkbox, 0, wxEXPAND | wxALL, FROM_DIP(this, 24));
	navigationPanel->SetSizer(navigationSizer);
	navigationPanel->FitInside();
	// Native scrollbars inherit the application theme on Windows. Keep them
	// hidden so a Light editor preference cannot introduce a bright strip into
	// the fixed dark welcome surface; mouse-wheel and keyboard scrolling remain.
	navigationPanel->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_NEVER);

	auto* divider = newd wxPanel(this, wxID_ANY);
	divider->SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Border));
	divider->SetMinSize(wxSize(FROM_DIP(this, 1), -1));

	auto* contentPanel = newd wxPanel(this, wxID_ANY);
	contentPanel->SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Background));
	auto* contentSizer = newd wxBoxSizer(wxVERTICAL);
	auto* brandPanel = newd WelcomeBrandPanel(contentPanel, titleText, versionText, fallbackLogo);
	brandPanel->SetMinSize(wxSize(-1, FROM_DIP(this, 180)));
	contentSizer->Add(brandPanel, 1, wxEXPAND);

	auto* recentMapsPanel = newd RecentMapsPanel(contentPanel, dialog, recentFiles);
	contentSizer->Add(recentMapsPanel, 0, wxEXPAND | wxLEFT | wxRIGHT, FROM_DIP(this, 20));

	contentSizer->AddSpacer(FROM_DIP(this, 24));
	auto* featuresPanel = newd WelcomeFeaturesPanel(contentPanel);
	featuresPanel->SetMinSize(wxSize(-1, FROM_DIP(this, 106)));
	contentSizer->Add(featuresPanel, 0, wxEXPAND | wxLEFT | wxRIGHT, FROM_DIP(this, 16));

	auto* creditText = newd wxStaticText(contentPanel, wxID_ANY, "Developed by Mateuzkl and Skyyzyy");
	creditText->SetFont(FontWithPointSize(GetFont(), std::max(7, GetFont().GetPointSize() - 2)));
	creditText->SetForegroundColour(wxColour(246, 196, 69));
	creditText->SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Background));
	contentSizer->Add(creditText, 0, wxALIGN_RIGHT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 16));
	contentPanel->SetSizer(contentSizer);

	auto* rootSizer = newd wxBoxSizer(wxHORIZONTAL);
	rootSizer->Add(navigationPanel, 0, wxEXPAND);
	rootSizer->Add(divider, 0, wxEXPAND);
	rootSizer->Add(contentPanel, 1, wxEXPAND);
	SetSizer(rootSizer);
	UpdateInputs();
}

void WelcomeDialogPanel::AddNavigationItem(wxWindow* parent, wxSizer* sizer, const wxString& iconName, const wxString& title, const wxString& subtitle, const wxString& tooltip, wxWindowID action, bool primary) {
	auto* item = newd WelcomeNavigationItem(parent, iconName, title, subtitle, tooltip, action, primary);
	item->SetMinSize(wxSize(-1, FROM_DIP(this, NAVIGATION_ITEM_HEIGHT_DIP)));
	item->Bind(wxEVT_BUTTON, &WelcomeDialog::OnNavigationActivated, static_cast<WelcomeDialog*>(GetParent()));
	sizer->Add(item, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 8));
}

void WelcomeDialogPanel::UpdateInputs() {
	m_show_welcome_dialog_checkbox->SetValue(g_settings.getInteger(Config::WELCOME_DIALOG) == 1);
	const int selectedTheme = g_settings.getInteger(Config::THEME);
	SetThemeSelection(selectedTheme >= 0 && selectedTheme <= 2 ? selectedTheme : m_active_theme);
	UpdateThemeStatus();
	Layout();
}

void WelcomeDialogPanel::OnThemeChanged(wxCommandEvent& event) {
	if (m_theme_prompt_open) {
		return;
	}

	const int selectedTheme = event.GetInt();
	if (selectedTheme < 0 || selectedTheme > 2) {
		return;
	}

	const int previousTheme = g_settings.getInteger(Config::THEME);
	if (selectedTheme == m_active_theme) {
		if (previousTheme != m_active_theme) {
			g_settings.setInteger(Config::THEME, m_active_theme);
			g_settings.save();
		}
		SetThemeSelection(m_active_theme);
		UpdateThemeStatus();
		return;
	}

	g_settings.setInteger(Config::THEME, selectedTheme);
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
		g_settings.setInteger(Config::THEME, previousTheme);
		g_settings.save();
		SetThemeSelection(previousTheme);
		UpdateThemeStatus();
		return;
	}

	SetThemeSelection(selectedTheme);
	UpdateThemeStatus();
	if (result == wxID_YES) {
		auto& application = static_cast<Application&>(wxGetApp());
		if (!application.RequestApplicationRestart()) {
			UpdateThemeStatus();
		}
	}
}

void WelcomeDialogPanel::SetThemeSelection(int theme) {
	m_system_theme_choice->SetSelected(theme == 0);
	m_dark_theme_choice->SetSelected(theme == 1);
	m_light_theme_choice->SetSelected(theme == 2);
}

void WelcomeDialogPanel::UpdateThemeStatus() {
	const bool pending = g_settings.getInteger(Config::THEME) != m_active_theme;
	const wxString text = pending
		? "Theme change pending. Restart to apply it."
		: "Changes require an application restart.";
	m_theme_status_label->SetLabel(text);
	m_theme_status_label->SetToolTip(text);
	m_theme_status_label->SetForegroundColour(pending ? WelcomeThemeStyle::Get(Theme::Role::Accent) : WelcomeThemeStyle::Get(Theme::Role::TextSubtle));
	m_theme_status_label->Refresh();
}

WelcomeNavigationItem::WelcomeNavigationItem(wxWindow* parent, const wxString& iconName, const wxString& title, const wxString& subtitle, const wxString& tooltip, wxWindowID action, bool primary) :
	wxControl(parent, action, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS),
	m_action(action),
	m_icon_name(iconName),
	m_title(title),
	m_subtitle(subtitle),
	m_primary(primary) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetName(title);
	SetHelpText(tooltip);
	SetToolTip(tooltip);
	RebuildBitmaps();

	Bind(wxEVT_PAINT, &WelcomeNavigationItem::OnPaint, this);
	Bind(wxEVT_ENTER_WINDOW, &WelcomeNavigationItem::OnMouseEnter, this);
	Bind(wxEVT_LEAVE_WINDOW, &WelcomeNavigationItem::OnMouseLeave, this);
	Bind(wxEVT_LEFT_DOWN, &WelcomeNavigationItem::OnLeftDown, this);
	Bind(wxEVT_LEFT_UP, &WelcomeNavigationItem::OnLeftUp, this);
	Bind(wxEVT_MOUSE_CAPTURE_LOST, &WelcomeNavigationItem::OnCaptureLost, this);
	Bind(wxEVT_KEY_DOWN, &WelcomeNavigationItem::OnKeyDown, this);
	Bind(wxEVT_SET_FOCUS, &WelcomeNavigationItem::OnFocus, this);
	Bind(wxEVT_KILL_FOCUS, &WelcomeNavigationItem::OnFocus, this);
#if wxCHECK_VERSION(3, 1, 0)
	Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) {
		RebuildBitmaps();
		Refresh();
		event.Skip();
	});
#endif
}

#if wxUSE_ACCESSIBILITY && defined(__WXMSW__)
wxAccessible* WelcomeNavigationItem::CreateAccessible() {
	return newd WelcomeControlAccessible(this, wxROLE_SYSTEM_PUSHBUTTON, "Press", [this] { Activate(); });
}
#endif

void WelcomeNavigationItem::RebuildBitmaps() {
	const wxColour normalTint = m_primary ? WelcomeThemeStyle::Get(Theme::Role::Accent) : WelcomeThemeStyle::Get(Theme::Role::TextSubtle);
	m_normal_bitmap = LoadWelcomeBitmap(this, m_icon_name, wxSize(30, 30), normalTint);
	m_active_bitmap = LoadWelcomeBitmap(this, m_icon_name, wxSize(30, 30), WelcomeThemeStyle::Get(Theme::Role::Accent));
}

void WelcomeNavigationItem::Activate() {
	if (!IsEnabled()) {
		return;
	}
	wxCommandEvent event(wxEVT_BUTTON, m_action);
	event.SetEventObject(this);
	ProcessWindowEvent(event);
}

void WelcomeNavigationItem::OnPaint(wxPaintEvent& WXUNUSED(event)) {
	wxAutoBufferedPaintDC dc(this);
	const wxRect bounds(wxPoint(0, 0), GetClientSize());
	const bool interactive = IsEnabled();
	const wxColour background = m_pressed
		? WelcomeThemeStyle::Get(Theme::Role::SelectionFill)
		: (m_hovered && interactive ? WelcomeThemeStyle::Get(Theme::Role::AccentHover) : WelcomeThemeStyle::Get(Theme::Role::Surface));
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(background));
	dc.DrawRoundedRectangle(bounds, FROM_DIP(this, 4));

	if ((m_hovered && interactive) || HasFocus() || m_primary) {
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(WelcomeThemeStyle::Get(Theme::Role::Accent)));
		dc.DrawRoundedRectangle(wxRect(0, FROM_DIP(this, 9), FROM_DIP(this, 3), std::max(0, bounds.height - FROM_DIP(this, 18))), FROM_DIP(this, 2));
	}

	const wxBitmap& bitmap = (m_hovered || HasFocus() || m_pressed) && interactive ? m_active_bitmap : m_normal_bitmap;
	const int iconX = FROM_DIP(this, 18);
	const int iconY = (bounds.height - bitmap.GetHeight()) / 2;
	if (bitmap.IsOk()) {
		dc.DrawBitmap(bitmap, iconX, iconY, true);
	}

	const int textX = FROM_DIP(this, 64);
	const int availableTextWidth = std::max(0, bounds.width - textX - FROM_DIP(this, 14));
	const wxColour titleColour = interactive ? WelcomeThemeStyle::Get(Theme::Role::Text) : WelcomeThemeStyle::Get(Theme::Role::TextSubtle).ChangeLightness(80);
	dc.SetTextForeground(titleColour);
	dc.SetFont(FontWithPointSize(GetFont(), GetFont().GetPointSize() + 1, true));
	dc.DrawText(Ellipsize(m_title, dc, wxELLIPSIZE_END, availableTextWidth), textX, FROM_DIP(this, 14));
	dc.SetTextForeground(interactive ? WelcomeThemeStyle::Get(Theme::Role::TextSubtle) : WelcomeThemeStyle::Get(Theme::Role::TextSubtle).ChangeLightness(80));
	dc.SetFont(FontWithPointSize(GetFont(), std::max(8, GetFont().GetPointSize() - 1)));
	dc.DrawText(Ellipsize(m_subtitle, dc, wxELLIPSIZE_END, availableTextWidth), textX, FROM_DIP(this, 39));

	DrawFocusRing(dc, this, Deflated(bounds, FROM_DIP(this, 3)));
}

void WelcomeNavigationItem::OnMouseEnter(wxMouseEvent& event) {
	m_hovered = true;
	SetCursor(IsEnabled() ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
	Refresh();
	event.Skip();
}

void WelcomeNavigationItem::OnMouseLeave(wxMouseEvent& event) {
	m_hovered = false;
	if (!HasCapture()) {
		m_pressed = false;
	}
	SetCursor(wxNullCursor);
	Refresh();
	event.Skip();
}

void WelcomeNavigationItem::OnLeftDown(wxMouseEvent& event) {
	if (!IsEnabled()) {
		return;
	}
	SetFocus();
	m_pressed = true;
	if (!HasCapture()) {
		CaptureMouse();
	}
	Refresh();
	event.Skip();
}

void WelcomeNavigationItem::OnLeftUp(wxMouseEvent& event) {
	const bool activate = m_pressed && GetClientRect().Contains(event.GetPosition());
	m_pressed = false;
	if (HasCapture()) {
		ReleaseMouse();
	}
	Refresh();
	if (activate) {
		Activate();
	}
}

void WelcomeNavigationItem::OnCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(event)) {
	m_pressed = false;
	Refresh();
}

void WelcomeNavigationItem::OnKeyDown(wxKeyEvent& event) {
	if (event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_NUMPAD_ENTER || event.GetKeyCode() == WXK_SPACE) {
		Activate();
		return;
	}
	event.Skip();
}

void WelcomeNavigationItem::OnFocus(wxFocusEvent& event) {
	Refresh();
	event.Skip();
}

WelcomeThemeChoice::WelcomeThemeChoice(wxWindow* parent, int theme, const wxString& iconName, const wxString& label) :
	wxControl(parent, wxID_ANY, wxDefaultPosition, FROM_DIP(parent, wxSize(72, 62)), wxBORDER_NONE | wxWANTS_CHARS),
	m_theme(theme),
	m_icon_name(iconName),
	m_label(label) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetName(label + " theme");
	const wxString tooltip = "Use the " + label.Lower() + " appearance after restarting NexaMap Editor.";
	SetHelpText(tooltip);
	SetToolTip(tooltip);
	RebuildBitmap();
	Bind(wxEVT_PAINT, &WelcomeThemeChoice::OnPaint, this);
	Bind(wxEVT_LEFT_DOWN, &WelcomeThemeChoice::OnLeftDown, this);
	Bind(wxEVT_LEFT_UP, &WelcomeThemeChoice::OnLeftUp, this);
	Bind(wxEVT_ENTER_WINDOW, &WelcomeThemeChoice::OnMouseEnter, this);
	Bind(wxEVT_LEAVE_WINDOW, &WelcomeThemeChoice::OnMouseLeave, this);
	Bind(wxEVT_KEY_DOWN, &WelcomeThemeChoice::OnKeyDown, this);
	Bind(wxEVT_SET_FOCUS, &WelcomeThemeChoice::OnFocus, this);
	Bind(wxEVT_KILL_FOCUS, &WelcomeThemeChoice::OnFocus, this);
#if wxCHECK_VERSION(3, 1, 0)
	Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) {
		RebuildBitmap();
		Refresh();
		event.Skip();
	});
#endif
}

#if wxUSE_ACCESSIBILITY && defined(__WXMSW__)
wxAccessible* WelcomeThemeChoice::CreateAccessible() {
	return newd WelcomeControlAccessible(this, wxROLE_SYSTEM_RADIOBUTTON, "Select", [this] { Activate(); }, wxACC_STATE_SYSTEM_CHECKED, [this] { return m_selected; });
}
#endif

void WelcomeThemeChoice::SetSelected(bool selected) {
	if (m_selected == selected) {
		return;
	}
	m_selected = selected;
#if wxUSE_ACCESSIBILITY && defined(__WXMSW__)
	wxAccessible::NotifyEvent(wxACC_EVENT_OBJECT_STATECHANGE, this, wxOBJID_CLIENT, wxACC_SELF);
#endif
	Refresh();
}

void WelcomeThemeChoice::RebuildBitmap() {
	m_bitmap = LoadWelcomeBitmap(this, m_icon_name, wxSize(20, 20), WelcomeThemeStyle::Get(Theme::Role::Accent));
}

void WelcomeThemeChoice::Activate() {
	wxCommandEvent event(wxEVT_BUTTON, GetId());
	event.SetEventObject(this);
	event.SetInt(m_theme);
	ProcessWindowEvent(event);
}

void WelcomeThemeChoice::OnPaint(wxPaintEvent& WXUNUSED(event)) {
	wxAutoBufferedPaintDC dc(this);
	const wxRect bounds(wxPoint(0, 0), GetClientSize());
	const wxColour background = m_pressed
		? WelcomeThemeStyle::Get(Theme::Role::SelectionFill)
		: (m_selected || m_hovered ? WelcomeThemeStyle::Get(Theme::Role::RaisedSurface) : WelcomeThemeStyle::Get(Theme::Role::Surface));
	dc.SetBrush(wxBrush(background));
	dc.SetPen(wxPen(m_selected ? WelcomeThemeStyle::Get(Theme::Role::Accent) : WelcomeThemeStyle::Get(Theme::Role::Border), FROM_DIP(this, 1)));
	dc.DrawRoundedRectangle(Deflated(bounds, FROM_DIP(this, 1)), FROM_DIP(this, 4));
	if (m_bitmap.IsOk()) {
		dc.DrawBitmap(m_bitmap, (bounds.width - m_bitmap.GetWidth()) / 2, FROM_DIP(this, 8), true);
	}
	dc.SetFont(FontWithPointSize(GetFont(), std::max(8, GetFont().GetPointSize() - 1), m_selected));
	dc.SetTextForeground(m_selected ? WelcomeThemeStyle::Get(Theme::Role::Accent) : WelcomeThemeStyle::Get(Theme::Role::TextSubtle));
	const wxSize textSize = dc.GetTextExtent(m_label);
	dc.DrawText(m_label, (bounds.width - textSize.x) / 2, FROM_DIP(this, 37));
	DrawFocusRing(dc, this, Deflated(bounds, FROM_DIP(this, 3)), 3);
}

void WelcomeThemeChoice::OnLeftDown(wxMouseEvent& event) {
	SetFocus();
	m_pressed = true;
	Refresh();
	event.Skip();
}

void WelcomeThemeChoice::OnLeftUp(wxMouseEvent& event) {
	const bool activate = m_pressed && GetClientRect().Contains(event.GetPosition());
	m_pressed = false;
	Refresh();
	if (activate) {
		Activate();
	}
}

void WelcomeThemeChoice::OnMouseEnter(wxMouseEvent& event) {
	m_hovered = true;
	SetCursor(wxCursor(wxCURSOR_HAND));
	Refresh();
	event.Skip();
}

void WelcomeThemeChoice::OnMouseLeave(wxMouseEvent& event) {
	m_hovered = false;
	m_pressed = false;
	SetCursor(wxNullCursor);
	Refresh();
	event.Skip();
}

void WelcomeThemeChoice::OnKeyDown(wxKeyEvent& event) {
	if (event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_NUMPAD_ENTER || event.GetKeyCode() == WXK_SPACE) {
		Activate();
		return;
	}
	event.Skip();
}

void WelcomeThemeChoice::OnFocus(wxFocusEvent& event) {
	Refresh();
	event.Skip();
}

WelcomeBrandPanel::WelcomeBrandPanel(wxWindow* parent, const wxString& title, const wxString& version, const wxBitmap& fallbackLogo) :
	wxPanel(parent, wxID_ANY),
	m_title(title),
	m_version(version),
	m_background_source(WelcomeAssetPath("background.png"), wxBITMAP_TYPE_PNG),
	m_fallback_logo(fallbackLogo) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	Bind(wxEVT_PAINT, &WelcomeBrandPanel::OnPaint, this);
	Bind(wxEVT_SIZE, &WelcomeBrandPanel::OnSize, this);
}

void WelcomeBrandPanel::RebuildBackground() {
	const wxSize size = GetClientSize();
	if (!m_background_source.IsOk() || size.x <= 0 || size.y <= 0 || size == m_cached_size) {
		return;
	}
	m_cached_size = size;

	const double scale = std::max(
		static_cast<double>(size.x) / m_background_source.GetWidth(),
		static_cast<double>(size.y) / m_background_source.GetHeight()
	);
	const int scaledWidth = std::max(size.x, static_cast<int>(m_background_source.GetWidth() * scale));
	const int scaledHeight = std::max(size.y, static_cast<int>(m_background_source.GetHeight() * scale));
	wxImage scaled = m_background_source.Scale(scaledWidth, scaledHeight, wxIMAGE_QUALITY_HIGH);
	const int cropX = std::max(0, (scaledWidth - size.x) / 2);
	const int cropY = std::min(std::max(0, (scaledHeight - size.y) / 5), std::max(0, scaledHeight - size.y));
	m_background_bitmap = wxBitmap(scaled.GetSubImage(wxRect(cropX, cropY, size.x, size.y)));
}

void WelcomeBrandPanel::OnPaint(wxPaintEvent& WXUNUSED(event)) {
	wxAutoBufferedPaintDC dc(this);
	const wxSize size = GetClientSize();
	dc.SetBackground(wxBrush(WelcomeThemeStyle::Get(Theme::Role::Background)));
	dc.Clear();
	if (!m_background_bitmap.IsOk() || m_cached_size != size) {
		RebuildBackground();
	}
	if (m_background_bitmap.IsOk()) {
		dc.DrawBitmap(m_background_bitmap, 0, 0, false);
	} else if (m_fallback_logo.IsOk()) {
		dc.DrawBitmap(m_fallback_logo, (size.x - m_fallback_logo.GetWidth()) / 2, FROM_DIP(this, 18), true);
	}

	const bool compact = size.y < FROM_DIP(this, 260);
	const int maximumTextWidth = std::max(FROM_DIP(this, 180), size.x - FROM_DIP(this, 40));
	int titlePointSize = compact ? 20 : (size.x < FROM_DIP(this, 560) ? 24 : 34);
	dc.SetFont(FontWithPointSize(GetFont(), titlePointSize, true));
	wxString name = m_title;
	wxString suffix;
	const wxString editorSuffix = "Editor";
	if (name.EndsWith(editorSuffix)) {
		suffix = editorSuffix;
		name = name.Left(name.length() - editorSuffix.length());
	}
	while (titlePointSize > 18 && dc.GetTextExtent(name + suffix).x > maximumTextWidth) {
		dc.SetFont(FontWithPointSize(GetFont(), --titlePointSize, true));
	}
	const wxFont fittedTitleFont = dc.GetFont();
	const wxSize nameSize = dc.GetTextExtent(name);
	const wxSize suffixSize = dc.GetTextExtent(suffix);
	const wxSize titleSize(nameSize.x + suffixSize.x, std::max(nameSize.y, suffixSize.y));

	const wxString slogan = "Create. Convert. Build Worlds.";
	int sloganPointSize = compact ? 11 : (size.x < FROM_DIP(this, 560) ? 13 : 16);
	dc.SetFont(FontWithPointSize(GetFont(), sloganPointSize));
	while (sloganPointSize > 9 && dc.GetTextExtent(slogan).x > maximumTextWidth) {
		dc.SetFont(FontWithPointSize(GetFont(), --sloganPointSize));
	}
	const wxFont sloganFont = dc.GetFont();
	const wxSize sloganSize = dc.GetTextExtent(slogan);

	const wxString description = "Next-generation OTBM mapping tools";
	int descriptionPointSize = compact ? 9 : 11;
	dc.SetFont(FontWithPointSize(GetFont(), descriptionPointSize));
	while (descriptionPointSize > 8 && dc.GetTextExtent(description).x > maximumTextWidth) {
		dc.SetFont(FontWithPointSize(GetFont(), --descriptionPointSize));
	}
	const wxFont descriptionFont = dc.GetFont();
	const wxSize descriptionSize = dc.GetTextExtent(description);

	dc.SetFont(FontWithPointSize(GetFont(), 9));
	const wxFont versionFont = dc.GetFont();
	const wxSize versionSize = dc.GetTextExtent(m_version);

	// Use a sizer as the single source of vertical positioning. The first
	// stretch slot reserves the responsive logo/background area; measured text
	// slots and logical-DPI gaps guarantee that no two lines can overlap.
	wxBoxSizer layout(wxVERTICAL);
	layout.AddStretchSpacer(1);
	auto* identitySizer = new wxBoxSizer(wxVERTICAL);
	wxSizerItem* titleSlot = identitySizer->Add(titleSize.x, titleSize.y, 0, wxALIGN_CENTER_HORIZONTAL);
	identitySizer->AddSpacer(FROM_DIP(this, compact ? 10 : 16));
	wxSizerItem* sloganSlot = identitySizer->Add(sloganSize.x, sloganSize.y, 0, wxALIGN_CENTER_HORIZONTAL);
	identitySizer->AddSpacer(FROM_DIP(this, compact ? 8 : 10));
	wxSizerItem* descriptionSlot = identitySizer->Add(descriptionSize.x, descriptionSize.y, 0, wxALIGN_CENTER_HORIZONTAL);
	identitySizer->AddSpacer(FROM_DIP(this, compact ? 8 : 10));
	wxSizerItem* versionSlot = identitySizer->Add(versionSize.x, versionSize.y, 0, wxALIGN_CENTER_HORIZONTAL);
	layout.Add(identitySizer, 0, wxALIGN_CENTER_HORIZONTAL);
	layout.AddSpacer(FROM_DIP(this, compact ? 8 : 12));
	layout.SetDimension(0, 0, size.x, size.y);

	const wxPoint titlePosition = titleSlot->GetPosition();
	dc.SetFont(fittedTitleFont);
	dc.SetTextForeground(wxColour(242, 246, 248));
	dc.DrawText(name, titlePosition.x, titlePosition.y);
	dc.SetTextForeground(WelcomeThemeStyle::Get(Theme::Role::Accent));
	dc.DrawText(suffix, titlePosition.x + nameSize.x, titlePosition.y);

	dc.SetFont(sloganFont);
	dc.DrawText(slogan, sloganSlot->GetPosition());
	dc.SetFont(descriptionFont);
	dc.SetTextForeground(WelcomeThemeStyle::Get(Theme::Role::TextSubtle));
	dc.DrawText(description, descriptionSlot->GetPosition());
	dc.SetFont(versionFont);
	dc.SetTextForeground(WelcomeThemeStyle::Get(Theme::Role::Accent));
	dc.DrawText(m_version, versionSlot->GetPosition());
}

void WelcomeBrandPanel::OnSize(wxSizeEvent& event) {
	RebuildBackground();
	Refresh();
	event.Skip();
}

WelcomeFeatureItem::WelcomeFeatureItem(wxWindow* parent, const wxString& iconName, const wxString& title, const wxString& description, const wxString& tooltip) :
	wxPanel(parent, wxID_ANY),
	m_icon_name(iconName),
	m_title(title),
	m_description(description) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(FROM_DIP(parent, wxSize(130, 92)));
	SetToolTip(tooltip);
	RebuildBitmap();
	Bind(wxEVT_PAINT, &WelcomeFeatureItem::OnPaint, this);
#if wxCHECK_VERSION(3, 1, 0)
	Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) {
		RebuildBitmap();
		Refresh();
		event.Skip();
	});
#endif
}

void WelcomeFeatureItem::SetShowDescription(bool show) {
	if (m_show_description == show) {
		return;
	}
	m_show_description = show;
	Refresh();
}

void WelcomeFeatureItem::RebuildBitmap() {
	m_bitmap = LoadWelcomeBitmap(this, m_icon_name, wxSize(28, 28), WelcomeThemeStyle::Get(Theme::Role::Accent));
}

void WelcomeFeatureItem::OnPaint(wxPaintEvent& WXUNUSED(event)) {
	wxAutoBufferedPaintDC dc(this);
	const wxSize size = GetClientSize();
	dc.SetBackground(wxBrush(WelcomeThemeStyle::Get(Theme::Role::Background)));
	dc.Clear();

	dc.SetFont(FontWithPointSize(GetFont(), std::max(8, GetFont().GetPointSize() - 1), true));
	const wxFont titleFont = dc.GetFont();
	const wxString title = Ellipsize(m_title, dc, wxELLIPSIZE_END, size.x - FROM_DIP(this, 12));
	const wxSize titleSize = dc.GetTextExtent(title);

	const bool showDescription = m_show_description && size.y >= FROM_DIP(this, 78);
	wxFont descriptionFont;
	wxString description;
	wxSize descriptionSize;
	if (showDescription) {
		dc.SetFont(FontWithPointSize(GetFont(), std::max(7, GetFont().GetPointSize() - 2)));
		descriptionFont = dc.GetFont();
		description = Ellipsize(m_description, dc, wxELLIPSIZE_END, size.x - FROM_DIP(this, 12));
		descriptionSize = dc.GetTextExtent(description);
	}

	// Measured slots keep every feature aligned at all supported DPI scales and
	// place the complete group slightly lower inside the available panel.
	wxBoxSizer itemLayout(wxVERTICAL);
	itemLayout.AddSpacer(FROM_DIP(this, 12));
	const wxSize iconSize = m_bitmap.IsOk() ? m_bitmap.GetSize() : FROM_DIP(this, wxSize(28, 28));
	wxSizerItem* iconSlot = itemLayout.Add(iconSize.x, iconSize.y, 0, wxALIGN_CENTER_HORIZONTAL);
	itemLayout.AddSpacer(FROM_DIP(this, 7));
	wxSizerItem* titleSlot = itemLayout.Add(titleSize.x, titleSize.y, 0, wxALIGN_CENTER_HORIZONTAL);
	wxSizerItem* descriptionSlot = nullptr;
	if (showDescription) {
		itemLayout.AddSpacer(FROM_DIP(this, 7));
		descriptionSlot = itemLayout.Add(descriptionSize.x, descriptionSize.y, 0, wxALIGN_CENTER_HORIZONTAL);
	}
	itemLayout.SetDimension(0, 0, size.x, size.y);

	if (m_bitmap.IsOk()) {
		dc.DrawBitmap(m_bitmap, iconSlot->GetPosition(), true);
	}
	dc.SetFont(titleFont);
	dc.SetTextForeground(WelcomeThemeStyle::Get(Theme::Role::Text));
	dc.DrawText(title, titleSlot->GetPosition());
	if (descriptionSlot) {
		dc.SetFont(descriptionFont);
		dc.SetTextForeground(WelcomeThemeStyle::Get(Theme::Role::TextSubtle));
		dc.DrawText(description, descriptionSlot->GetPosition());
	}
}

WelcomeFeaturesPanel::WelcomeFeaturesPanel(wxWindow* parent) :
	wxPanel(parent, wxID_ANY) {
	SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Background));
	m_grid = newd wxGridSizer(0, 4, FROM_DIP(this, 4), FROM_DIP(this, 10));
	m_items = {
		newd WelcomeFeatureItem(
			this,
			"icon_powerful_tools.png",
			"Powerful Tools",
			"Advanced editing made easy",
			"Access advanced mapping tools, visual workspaces, brushes and editing features designed to make map creation faster and easier."
		),
		newd WelcomeFeatureItem(
			this,
			"icon_smart_conversion.png",
			"Smart Conversion",
			"Seamless format compatibility",
			"Convert maps, item IDs, spawns and NPC files between supported TFS, Canary and Crystal formats while preserving important map data."
		),
		newd WelcomeFeatureItem(
			this,
			"icon_build_worlds.png",
			"Build Worlds",
			"Design epic maps and experiences",
			"Create and edit cities, hunting areas, dungeons, mountains, castles and complete OpenTibia worlds."
		),
		newd WelcomeFeatureItem(
			this,
			"icon_optimized_performance.png",
			"Optimized Performance",
			"Fast, stable and reliable",
			"Provides fast loading, conversion and editing with optimized CPU and memory usage for large maps."
		),
	};
	for (WelcomeFeatureItem* item : m_items) {
		m_grid->Add(item, 1, wxEXPAND);
	}
	SetSizer(m_grid);
	Bind(wxEVT_SIZE, &WelcomeFeaturesPanel::OnSize, this);
}

void WelcomeFeaturesPanel::OnSize(wxSizeEvent& event) {
	UpdateLayout(event.GetSize());
	event.Skip();
}

void WelcomeFeaturesPanel::UpdateLayout(const wxSize& size) {
	const int columns = size.x < FROM_DIP(this, 680) ? 2 : 4;
	if (columns != m_columns) {
		m_columns = columns;
		m_grid->SetCols(columns);
	}
	const bool showDescription = columns == 4 && size.y >= FROM_DIP(this, 88);
	for (WelcomeFeatureItem* item : m_items) {
		item->SetShowDescription(showDescription);
	}
	Layout();
}

RecentMapsPanel::RecentMapsPanel(wxWindow* parent, WelcomeDialog* dialog, const std::vector<wxString>& recentFiles) :
	wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE) {
	SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Background));
	SetScrollRate(0, FROM_DIP(this, 12));
	Bind(wxEVT_LEAVE_WINDOW, &RecentMapsPanel::OnMouseLeave, this);

	auto* sizer = newd wxBoxSizer(wxVERTICAL);
	auto* header = newd wxStaticText(this, wxID_ANY, "Recent Projects");
	header->SetFont(FontWithPointSize(GetFont(), GetFont().GetPointSize(), true));
	header->SetForegroundColour(WelcomeThemeStyle::Get(Theme::Role::Text));
	header->SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Background));
	sizer->Add(header, 0, wxEXPAND | wxBOTTOM, FROM_DIP(this, 8));

	if (recentFiles.empty()) {
		auto* empty = newd wxStaticText(this, wxID_ANY, "No recent projects yet. Create or open a map to get started.");
		empty->SetForegroundColour(WelcomeThemeStyle::Get(Theme::Role::TextSubtle));
		empty->SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Background));
		sizer->Add(empty, 0, wxEXPAND | wxBOTTOM, FROM_DIP(this, 10));
		const wxSize recentSize(-1, FROM_DIP(this, 58));
		SetMinSize(recentSize);
		SetMaxSize(recentSize);
	} else {
		for (const wxString& file : recentFiles) {
			auto* recentItem = newd RecentItem(this, file);
			recentItem->Bind(wxEVT_BUTTON, &WelcomeDialog::OnRecentItemActivated, dialog);
			sizer->Add(recentItem, 0, wxEXPAND | wxBOTTOM, FROM_DIP(this, 4));
		}
		const int visibleItems = std::min<int>(3, recentFiles.size());
		const wxSize recentSize(-1, FROM_DIP(this, 30 + visibleItems * 58));
		SetMinSize(recentSize);
		SetMaxSize(recentSize);
	}
	SetSizer(sizer);
	// Keep the viewport height capped above, but let the sizer retain the full
	// list height as the scrollable virtual area. FitInside() recalculates that
	// area after every recent-project card has been added.
	Layout();
	FitInside();
	ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_DEFAULT);
}

void RecentMapsPanel::SetHoveredItem(RecentItem* item) {
	if (m_hovered_item == item) {
		return;
	}
	if (m_hovered_item) {
		m_hovered_item->SetHovered(false);
	}
	m_hovered_item = item;
	if (m_hovered_item) {
		m_hovered_item->SetHovered(true);
	}
}

void RecentMapsPanel::ClearHoveredItem(RecentItem* item) {
	if (m_hovered_item == item) {
		SetHoveredItem(nullptr);
	}
}

void RecentMapsPanel::SelectItem(RecentItem* item) {
	if (m_selected_item == item) {
		return;
	}
	if (m_selected_item) {
		m_selected_item->SetSelected(false);
	}
	m_selected_item = item;
	if (m_selected_item) {
		m_selected_item->SetSelected(true);
	}
}

void RecentMapsPanel::OnMouseLeave(wxMouseEvent& event) {
	if (!GetScreenRect().Contains(wxGetMousePosition())) {
		SetHoveredItem(nullptr);
	}
	event.Skip();
}

RecentItem::RecentItem(RecentMapsPanel* parent, const wxString& itemName) :
	wxControl(parent, wxID_ANY, wxDefaultPosition, FROM_DIP(parent, wxSize(-1, 54)), wxBORDER_NONE | wxWANTS_CHARS),
	m_item_text(itemName) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(wxSize(-1, FROM_DIP(this, 54)));
	SetName(wxFileNameFromPath(m_item_text));
	SetHelpText(m_item_text);
	SetToolTip(m_item_text);
	Bind(wxEVT_PAINT, &RecentItem::OnPaint, this);
	Bind(wxEVT_ENTER_WINDOW, &RecentItem::OnMouseEnter, this);
	Bind(wxEVT_LEAVE_WINDOW, &RecentItem::OnMouseLeave, this);
	Bind(wxEVT_LEFT_DOWN, &RecentItem::OnLeftDown, this);
	Bind(wxEVT_LEFT_UP, &RecentItem::OnLeftUp, this);
	Bind(wxEVT_KEY_DOWN, &RecentItem::OnKeyDown, this);
	Bind(wxEVT_SET_FOCUS, &RecentItem::OnFocus, this);
	Bind(wxEVT_KILL_FOCUS, &RecentItem::OnFocus, this);
}

#if wxUSE_ACCESSIBILITY && defined(__WXMSW__)
wxAccessible* RecentItem::CreateAccessible() {
	return newd WelcomeControlAccessible(this, wxROLE_SYSTEM_LISTITEM, "Open", [this] { Activate(); }, wxACC_STATE_SYSTEM_SELECTED, [this] { return m_selected; });
}
#endif

void RecentItem::SetHovered(bool hovered) {
	if (m_hovered == hovered) {
		return;
	}
	m_hovered = hovered;
	Refresh();
}

void RecentItem::SetSelected(bool selected) {
	if (m_selected == selected) {
		return;
	}
	m_selected = selected;
#if wxUSE_ACCESSIBILITY && defined(__WXMSW__)
	wxAccessible::NotifyEvent(wxACC_EVENT_OBJECT_STATECHANGE, this, wxOBJID_CLIENT, wxACC_SELF);
#endif
	Refresh();
}

void RecentItem::Activate() {
	if (!m_selected) {
		static_cast<RecentMapsPanel*>(GetParent())->SelectItem(this);
	}
	wxCommandEvent event(wxEVT_BUTTON, wxID_OPEN);
	event.SetEventObject(this);
	event.SetString(m_item_text);
	ProcessWindowEvent(event);
}

void RecentItem::OnPaint(wxPaintEvent& WXUNUSED(event)) {
	wxAutoBufferedPaintDC dc(this);
	const wxRect bounds(wxPoint(0, 0), GetClientSize());
	const wxColour background = m_pressed
		? WelcomeThemeStyle::Get(Theme::Role::SelectionFill)
		: (m_selected ? WelcomeThemeStyle::Get(Theme::Role::SelectionFill) : (m_hovered ? WelcomeThemeStyle::Get(Theme::Role::RaisedSurface) : WelcomeThemeStyle::Get(Theme::Role::Background)));
	dc.SetBrush(wxBrush(background));
	dc.SetPen(wxPen(m_selected ? WelcomeThemeStyle::Get(Theme::Role::Accent) : WelcomeThemeStyle::Get(Theme::Role::Border), FROM_DIP(this, 1)));
	dc.DrawRoundedRectangle(Deflated(bounds, FROM_DIP(this, 1)), FROM_DIP(this, 4));
	if (m_selected) {
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(WelcomeThemeStyle::Get(Theme::Role::Accent)));
		dc.DrawRectangle(0, FROM_DIP(this, 7), FROM_DIP(this, 3), bounds.height - FROM_DIP(this, 14));
	}

	const int textX = FROM_DIP(this, 12);
	const int width = bounds.width - textX - FROM_DIP(this, 12);
	dc.SetFont(FontWithPointSize(GetFont(), GetFont().GetPointSize(), true));
	dc.SetTextForeground(WelcomeThemeStyle::Get(Theme::Role::Text));
	dc.DrawText(Ellipsize(wxFileNameFromPath(m_item_text), dc, wxELLIPSIZE_END, width), textX, FROM_DIP(this, 8));
	dc.SetFont(FontWithPointSize(GetFont(), std::max(7, GetFont().GetPointSize() - 2)));
	dc.SetTextForeground(WelcomeThemeStyle::Get(Theme::Role::TextSubtle));
	dc.DrawText(Ellipsize(m_item_text, dc, wxELLIPSIZE_START, width), textX, FROM_DIP(this, 30));
	DrawFocusRing(dc, this, Deflated(bounds, FROM_DIP(this, 3)), 3);
}

void RecentItem::OnMouseEnter(wxMouseEvent& event) {
	static_cast<RecentMapsPanel*>(GetParent())->SetHoveredItem(this);
	SetCursor(wxCursor(wxCURSOR_HAND));
	event.Skip();
}

void RecentItem::OnMouseLeave(wxMouseEvent& event) {
	if (!GetScreenRect().Contains(wxGetMousePosition())) {
		const bool wasHovered = m_hovered;
		m_pressed = false;
		static_cast<RecentMapsPanel*>(GetParent())->ClearHoveredItem(this);
		SetCursor(wxNullCursor);
		if (!wasHovered) {
			Refresh();
		}
	}
	event.Skip();
}

void RecentItem::OnLeftDown(wxMouseEvent& event) {
	SetFocus();
	m_pressed = true;
	if (m_selected) {
		Refresh();
	} else {
		static_cast<RecentMapsPanel*>(GetParent())->SelectItem(this);
	}
	event.Skip();
}

void RecentItem::OnLeftUp(wxMouseEvent& event) {
	const bool activate = m_pressed && GetClientRect().Contains(event.GetPosition());
	m_pressed = false;
	Refresh();
	if (activate) {
		Activate();
	}
}

void RecentItem::OnKeyDown(wxKeyEvent& event) {
	if (event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_NUMPAD_ENTER || event.GetKeyCode() == WXK_SPACE) {
		Activate();
		return;
	}
	event.Skip();
}

void RecentItem::OnFocus(wxFocusEvent& event) {
	Refresh();
	event.Skip();
}
