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
#include "workspace_session.h"

#include <wx/access.h>
#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/dirdlg.h>
#include <wx/filename.h>

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <utility>

wxDEFINE_EVENT(WELCOME_DIALOG_ACTION, wxCommandEvent);

namespace {
	constexpr int NAVIGATION_WIDTH_DIP = 178;
	constexpr int NAVIGATION_ITEM_HEIGHT_DIP = 42;

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

	class WorkspaceCardPanel final : public wxPanel {
	public:
		explicit WorkspaceCardPanel(wxWindow* parent) :
			wxPanel(parent, wxID_ANY) {
			SetBackgroundStyle(wxBG_STYLE_PAINT);
			Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
				wxAutoBufferedPaintDC dc(this);
				const wxRect bounds(wxPoint(0, 0), GetClientSize());
				dc.SetBackground(wxBrush(WelcomeThemeStyle::Get(Theme::Role::Background)));
				dc.Clear();
				dc.SetBrush(wxBrush(WelcomeThemeStyle::Get(Theme::Role::RaisedSurface)));
				dc.SetPen(wxPen(WelcomeThemeStyle::Get(Theme::Role::Border), FROM_DIP(this, 1)));
				dc.DrawRoundedRectangle(Deflated(bounds, FROM_DIP(this, 1)), FROM_DIP(this, 8));
			});
		}
	};

	class WorkspaceActionButton final : public wxControl {
	public:
		WorkspaceActionButton(wxWindow* parent, const wxString& label, bool primary = false) :
			wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS),
			label(label),
			primary(primary) {
			SetBackgroundStyle(wxBG_STYLE_PAINT);
			SetMinSize(FROM_DIP(parent, wxSize(-1, 34)));
			SetName(label);
			Bind(wxEVT_PAINT, &WorkspaceActionButton::OnPaint, this);
			Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& event) {
				hovered = true;
				SetCursor(IsEnabled() ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
				Refresh();
				event.Skip();
			});
			Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& event) {
				hovered = false;
				pressed = false;
				SetCursor(wxNullCursor);
				Refresh();
				event.Skip();
			});
			Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
				if (!IsEnabled()) {
					return;
				}
				SetFocus();
				pressed = true;
				Refresh();
				event.Skip();
			});
			Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
				const bool activate = pressed && IsEnabled() && GetClientRect().Contains(event.GetPosition());
				pressed = false;
				Refresh();
				if (activate) {
					wxCommandEvent command(wxEVT_BUTTON, GetId());
					command.SetEventObject(this);
					ProcessWindowEvent(command);
				}
			});
			Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
				if (IsEnabled() && (event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_NUMPAD_ENTER || event.GetKeyCode() == WXK_SPACE)) {
					wxCommandEvent command(wxEVT_BUTTON, GetId());
					command.SetEventObject(this);
					ProcessWindowEvent(command);
					return;
				}
				event.Skip();
			});
			Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) { Refresh(); event.Skip(); });
			Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) { Refresh(); event.Skip(); });
		}

	private:
		void OnPaint(wxPaintEvent&) {
			wxAutoBufferedPaintDC dc(this);
			const wxRect bounds(wxPoint(0, 0), GetClientSize());
			const wxColour accent(116, 76, 238);
			const wxColour accentHover(91, 58, 191);
			const wxColour background = !IsEnabled()
				? WelcomeThemeStyle::Get(Theme::Role::Surface)
				: (primary ? (pressed ? accent.ChangeLightness(82) : (hovered ? accentHover : accent))
						 : (pressed ? WelcomeThemeStyle::Get(Theme::Role::SelectionFill) : (hovered ? accentHover : WelcomeThemeStyle::Get(Theme::Role::RaisedSurface))));
			dc.SetBackground(wxBrush(WelcomeThemeStyle::Get(Theme::Role::RaisedSurface)));
			dc.Clear();
			dc.SetBrush(wxBrush(background));
			dc.SetPen(wxPen(IsEnabled() ? accent : WelcomeThemeStyle::Get(Theme::Role::Border), FROM_DIP(this, 1)));
			dc.DrawRoundedRectangle(Deflated(bounds, FROM_DIP(this, 1)), FROM_DIP(this, 7));
			dc.SetFont(FontWithPointSize(GetFont(), GetFont().GetPointSize(), true));
			dc.SetTextForeground(IsEnabled() ? (primary ? WelcomeThemeStyle::Get(Theme::Role::TextOnAccent) : WelcomeThemeStyle::Get(Theme::Role::Text)) : WelcomeThemeStyle::Get(Theme::Role::TextSubtle));
			const wxSize extent = dc.GetTextExtent(label);
			dc.DrawText(label, std::max(0, (bounds.width - extent.x) / 2), std::max(0, (bounds.height - extent.y) / 2));
			DrawFocusRing(dc, this, Deflated(bounds, FROM_DIP(this, 4)), 5);
		}

		wxString label;
		bool primary = false;
		bool hovered = false;
		bool pressed = false;
	};
}

WelcomeDialog::WelcomeDialog(const wxString& titleText, const wxString& versionText, const wxSize& size, const wxBitmap& fallbackLogo, const std::vector<wxString>& recentFiles) :
	wxDialog(nullptr, wxID_ANY, titleText, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMINIMIZE_BOX | wxMAXIMIZE_BOX) {
	SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Surface));
	m_welcome_dialog_panel = newd WelcomeDialogPanel(this, titleText, versionText, fallbackLogo, recentFiles);

	auto* dialogSizer = newd wxBoxSizer(wxVERTICAL);
	dialogSizer->Add(m_welcome_dialog_panel, 1, wxEXPAND);
	SetSizer(dialogSizer);

	wxSize minimumClientSize = FROM_DIP(this, wxSize(860, 560));
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
		actionEvent.SetInt(g_workspace.containsMap(selectedPath) ? 1 : 0);
	}

	actionEvent.SetId(action);
	ProcessWindowEvent(actionEvent);
}

WelcomeDialogPanel::WelcomeDialogPanel(WelcomeDialog* dialog, const wxString& titleText, const wxString& versionText, const wxBitmap& fallbackLogo, const std::vector<wxString>& recentFiles) :
	wxPanel(dialog),
	m_recent_files(recentFiles),
	m_version_text(versionText) {
	m_active_theme = static_cast<int>(Theme::GetType());
	SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Surface));

	const wxColour surface = WelcomeThemeStyle::Get(Theme::Role::Surface);
	const wxColour background = WelcomeThemeStyle::Get(Theme::Role::Background);
	const wxColour raised = WelcomeThemeStyle::Get(Theme::Role::RaisedSurface);
	const wxColour text = WelcomeThemeStyle::Get(Theme::Role::Text);
	const wxColour subtle = WelcomeThemeStyle::Get(Theme::Role::TextSubtle);
	const wxColour accent(116, 76, 238);
	const wxColour cyan(38, 211, 230);
	const wxColour green(69, 201, 105);

	auto makeText = [&](wxWindow* parent, const wxString& value, int pointDelta = 0, bool bold = false, const wxColour& colour = wxNullColour) {
		auto* label = newd wxStaticText(parent, wxID_ANY, value, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
		label->SetFont(FontWithPointSize(GetFont(), std::max(8, GetFont().GetPointSize() + pointDelta), bold));
		label->SetForegroundColour(colour.IsOk() ? colour : text);
		label->SetBackgroundColour(dynamic_cast<WorkspaceCardPanel*>(parent) ? raised : parent->GetBackgroundColour());
		return label;
	};

	// Compact fixed sidebar: the startup screen never needs its own scrollbar.
	auto* navigationPanel = newd wxPanel(this, wxID_ANY);
	navigationPanel->SetBackgroundColour(surface);
	navigationPanel->SetMinSize(wxSize(FROM_DIP(this, NAVIGATION_WIDTH_DIP), -1));
	navigationPanel->SetMaxSize(wxSize(FROM_DIP(this, NAVIGATION_WIDTH_DIP), -1));
	auto* navigationSizer = newd wxBoxSizer(wxVERTICAL);

	auto* brandRow = newd wxBoxSizer(wxHORIZONTAL);
	if (fallbackLogo.IsOk()) {
		wxImage image = fallbackLogo.ConvertToImage().Scale(FROM_DIP(this, 34), FROM_DIP(this, 34), wxIMAGE_QUALITY_HIGH);
		brandRow->Add(newd wxStaticBitmap(navigationPanel, wxID_ANY, wxBitmap(image)), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 8));
	}
	auto* brandText = newd wxBoxSizer(wxVERTICAL);
	auto* brandTitle = makeText(navigationPanel, "NexaMap", 2, true);
	auto* brandSuffix = makeText(navigationPanel, "Editor", -1, true, accent);
	brandText->Add(brandTitle, 0, wxEXPAND);
	brandText->Add(brandSuffix, 0, wxEXPAND);
	brandRow->Add(brandText, 1, wxALIGN_CENTER_VERTICAL);
	navigationSizer->Add(brandRow, 0, wxEXPAND | wxALL, FROM_DIP(this, 14));

	auto* navigationLabel = makeText(navigationPanel, "NAVIGATION", -2, true, subtle);
	navigationSizer->Add(navigationLabel, 0, wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 12));
	AddNavigationItem(navigationPanel, navigationSizer, "icon_open_project.png", "Workspace", "Client + server resources", "Open the configured Server Workspace.", WELCOME_DIALOG_OPEN_WORKSPACE, true);
	AddNavigationItem(navigationPanel, navigationSizer, "icon_new_map.png", "New Map", "Create an OTBM map", "Create a new OTBM map.", wxID_NEW);
	AddNavigationItem(navigationPanel, navigationSizer, "icon_open_project.png", "Open Map", "Open an existing map", "Open an existing OTBM map.", wxID_OPEN);
	AddNavigationItem(navigationPanel, navigationSizer, "icon_map_converter.png", "Converters", "Maps, spawns and IDs", "Open the map item ID converter.", WELCOME_DIALOG_MAP_CONVERTER);
	AddNavigationItem(navigationPanel, navigationSizer, "icon_preferences.png", "Preferences", "Configure the editor", "Configure NexaMap Editor.", wxID_PREFERENCES);

	navigationSizer->AddStretchSpacer();
	auto* themeLabel = makeText(navigationPanel, "APPEARANCE", -2, true, subtle);
	navigationSizer->Add(themeLabel, 0, wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 12));
	auto* themeSizer = newd wxBoxSizer(wxHORIZONTAL);
	m_system_theme_choice = newd WelcomeThemeChoice(navigationPanel, 0, "icon_system.png", "System");
	m_dark_theme_choice = newd WelcomeThemeChoice(navigationPanel, 1, "icon_dark.png", "Dark");
	m_light_theme_choice = newd WelcomeThemeChoice(navigationPanel, 2, "icon_light.png", "Light");
	for (WelcomeThemeChoice* choice : {m_system_theme_choice, m_dark_theme_choice, m_light_theme_choice}) {
		choice->Bind(wxEVT_BUTTON, &WelcomeDialogPanel::OnThemeChanged, this);
		themeSizer->Add(choice, 1, wxEXPAND | wxRIGHT, choice == m_light_theme_choice ? 0 : FROM_DIP(this, 4));
	}
	navigationSizer->Add(themeSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 10));

	m_show_welcome_dialog_checkbox = newd wxCheckBox(navigationPanel, wxID_ANY, "Show on startup");
	m_show_welcome_dialog_checkbox->SetValue(g_settings.getInteger(Config::WELCOME_DIALOG) == 1);
	m_show_welcome_dialog_checkbox->Bind(wxEVT_CHECKBOX, &WelcomeDialog::OnCheckboxClicked, dialog);
	m_show_welcome_dialog_checkbox->SetBackgroundColour(surface);
	m_show_welcome_dialog_checkbox->SetForegroundColour(text);
	navigationSizer->Add(m_show_welcome_dialog_checkbox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 8));
	m_theme_status_label = makeText(navigationPanel, versionText, -2, false, subtle);
	navigationSizer->Add(m_theme_status_label, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 12));
	navigationPanel->SetSizer(navigationSizer);

	auto* divider = newd wxPanel(this, wxID_ANY);
	divider->SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Border));
	divider->SetMinSize(wxSize(FROM_DIP(this, 1), -1));

	// Main dashboard: every section fits in the initial 1000 x 650 window.
	auto* contentPanel = newd wxPanel(this, wxID_ANY);
	contentPanel->SetBackgroundColour(background);
	auto* contentSizer = newd wxBoxSizer(wxVERTICAL);

	auto* heading = makeText(contentPanel, "NexaMap Workspace", 3, true);
	auto* subheading = makeText(contentPanel, "Choose the client and server folders. Resources are detected automatically.", -1, false, subtle);
	contentSizer->Add(heading, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 14));
	contentSizer->Add(subheading, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 6));
	contentSizer->AddSpacer(FROM_DIP(this, 10));

	auto* cardsSizer = newd wxBoxSizer(wxHORIZONTAL);

	// Client card.
	auto* clientCard = newd WorkspaceCardPanel(contentPanel);
	clientCard->SetMinSize(FROM_DIP(this, wxSize(0, 124)));
	auto* clientSizer = newd wxBoxSizer(wxVERTICAL);
	auto* clientHeaderRow = newd wxBoxSizer(wxHORIZONTAL);
	auto* clientHeader = makeText(clientCard, "1  CLIENT", 0, true, accent);
	m_client_status_label = makeText(clientCard, "Waiting", -2, true, subtle);
	clientHeaderRow->Add(clientHeader, 0, wxALIGN_CENTER_VERTICAL);
	clientHeaderRow->AddStretchSpacer();
	clientHeaderRow->Add(m_client_status_label, 0, wxALIGN_CENTER_VERTICAL);
	auto* selectClient = newd WorkspaceActionButton(clientCard, "Select Client Folder");
	selectClient->Bind(wxEVT_BUTTON, &WelcomeDialogPanel::OnSelectClient, this);
	m_client_path_label = makeText(clientCard, "No client folder selected", -1, false, cyan);
	clientSizer->Add(clientHeaderRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FROM_DIP(this, 11));
	clientSizer->Add(selectClient, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 11));
	clientSizer->Add(m_client_path_label, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 11));
	clientCard->SetSizer(clientSizer);
	cardsSizer->Add(clientCard, 1, wxEXPAND | wxRIGHT, FROM_DIP(this, 8));

	// Server card mirrors the client card; resources live in their own compact strip.
	auto* serverCard = newd WorkspaceCardPanel(contentPanel);
	serverCard->SetMinSize(FROM_DIP(this, wxSize(0, 124)));
	auto* serverSizer = newd wxBoxSizer(wxVERTICAL);
	auto* serverHeaderRow = newd wxBoxSizer(wxHORIZONTAL);
	serverHeaderRow->Add(makeText(serverCard, "2  SERVER", 0, true, accent), 0, wxALIGN_CENTER_VERTICAL);
	serverHeaderRow->AddStretchSpacer();
	m_server_status_label = makeText(serverCard, "Waiting", -2, true, subtle);
	serverHeaderRow->Add(m_server_status_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 8));
	serverHeaderRow->Add(makeText(serverCard, "AUTO", -2, true, cyan), 0, wxALIGN_CENTER_VERTICAL);
	auto* selectServer = newd WorkspaceActionButton(serverCard, "Select Server Folder", true);
	selectServer->Bind(wxEVT_BUTTON, &WelcomeDialogPanel::OnSelectServer, this);
	m_server_path_label = makeText(serverCard, "No server folder selected", -1, false, cyan);
	serverSizer->Add(serverHeaderRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FROM_DIP(this, 11));
	serverSizer->Add(selectServer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 11));
	serverSizer->Add(m_server_path_label, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 11));
	serverCard->SetSizer(serverSizer);
	cardsSizer->Add(serverCard, 1, wxEXPAND);

	// Detected resources are deliberately one compact horizontal strip.
	auto* resourcesCard = newd WorkspaceCardPanel(contentPanel);
	auto* resourcesSizer = newd wxBoxSizer(wxVERTICAL);
	resourcesSizer->Add(makeText(resourcesCard, "DETECTED RESOURCES", -2, true, subtle), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 10));
	auto* resourceGrid = newd wxFlexGridSizer(1, 5, 0, FROM_DIP(this, 8));
	for (int column = 0; column < 5; ++column) {
		resourceGrid->AddGrowableCol(column, 1);
	}
	m_items_otb_status = makeText(resourcesCard, "Item DB  Not scanned", -2, false, subtle);
	m_items_xml_status = makeText(resourcesCard, "items.xml  Not scanned", -2, false, subtle);
	m_maps_status = makeText(resourcesCard, "Maps  Not scanned", -2, false, subtle);
	m_monsters_status = makeText(resourcesCard, "Monsters  Not scanned", -2, false, subtle);
	m_npcs_status = makeText(resourcesCard, "NPCs  Not scanned", -2, false, subtle);
	for (wxStaticText* row : {m_items_otb_status, m_items_xml_status, m_maps_status, m_monsters_status, m_npcs_status}) {
		resourceGrid->Add(row, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);
	}
	resourcesSizer->Add(resourceGrid, 0, wxEXPAND | wxALL, FROM_DIP(this, 10));
	resourcesCard->SetSizer(resourcesSizer);

	// Workspace summary keeps the primary action next to its state.
	auto* summaryCard = newd WorkspaceCardPanel(contentPanel);
	summaryCard->SetMinSize(FROM_DIP(this, wxSize(0, 92)));
	auto* summarySizer = newd wxBoxSizer(wxVERTICAL);
	auto* summaryHeader = newd wxBoxSizer(wxHORIZONTAL);
	summaryHeader->Add(makeText(summaryCard, "WORKSPACE", -1, true, accent), 0, wxALIGN_CENTER_VERTICAL);
	summaryHeader->AddSpacer(FROM_DIP(this, 14));
	m_ready_description = makeText(summaryCard, "Select a client and server folder to prepare the workspace.", -2, false, subtle);
	summaryHeader->Add(m_ready_description, 1, wxALIGN_CENTER_VERTICAL);
	summarySizer->Add(summaryHeader, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 11));
	auto* summaryBody = newd wxBoxSizer(wxHORIZONTAL);
	auto addSummary = [&](const wxString& label, wxStaticText*& value) {
		auto* metric = newd wxBoxSizer(wxVERTICAL);
		metric->Add(makeText(summaryCard, label, -2, false, subtle), 0, wxEXPAND | wxBOTTOM, FROM_DIP(this, 2));
		value = makeText(summaryCard, "-", -1, true, cyan);
		value->SetWindowStyleFlag(wxST_ELLIPSIZE_END);
		metric->Add(value, 0, wxEXPAND);
		summaryBody->Add(metric, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 10));
	};
	addSummary("Client", m_protocol_value);
	addSummary("ID Mode", m_id_mode_value);
	addSummary("Items", m_items_source_value);
	addSummary("Status", m_workspace_status_value);
	m_open_workspace_button = newd WorkspaceActionButton(summaryCard, "Open Editor", true);
	m_open_workspace_button->Bind(wxEVT_BUTTON, &WelcomeDialogPanel::OnOpenWorkspace, this);
	m_open_workspace_button->SetMinSize(FROM_DIP(this, wxSize(124, 34)));
	summaryBody->Add(m_open_workspace_button, 0, wxALIGN_CENTER_VERTICAL);
	summarySizer->Add(summaryBody, 1, wxEXPAND | wxALL, FROM_DIP(this, 11));
	summaryCard->SetSizer(summarySizer);

	contentSizer->Add(cardsSizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FROM_DIP(this, 14));
	contentSizer->AddSpacer(FROM_DIP(this, 8));
	contentSizer->Add(resourcesCard, 0, wxEXPAND | wxLEFT | wxRIGHT, FROM_DIP(this, 14));
	contentSizer->AddSpacer(FROM_DIP(this, 8));
	contentSizer->Add(summaryCard, 0, wxEXPAND | wxLEFT | wxRIGHT, FROM_DIP(this, 14));
	contentSizer->AddSpacer(FROM_DIP(this, 8));
	m_recent_maps_panel = newd RecentMapsPanel(contentPanel, dialog, recentFiles);
	contentSizer->Add(m_recent_maps_panel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 14));
	contentSizer->AddStretchSpacer();
	auto* footerSizer = newd wxBoxSizer(wxVERTICAL);
	auto* communityText = newd wxStaticText(
		contentPanel,
		wxID_ANY,
		"NexaMap is free and open-source software, shaped by community contributions to advance map editing for everyone."
	);
	communityText->SetFont(FontWithPointSize(GetFont(), std::max(9, GetFont().GetPointSize() - 1)));
	communityText->SetForegroundColour(WelcomeThemeStyle::Get(Theme::Role::TextSubtle));
	communityText->SetBackgroundColour(background);
	footerSizer->Add(communityText, 0, wxALIGN_RIGHT);
	footerSizer->AddSpacer(FROM_DIP(this, 4));
	auto* creditText = newd wxStaticText(contentPanel, wxID_ANY, "Developed by  Mateuzkl  and  Skyyzyy");
	creditText->SetFont(FontWithPointSize(GetFont(), std::max(11, GetFont().GetPointSize() + 1), true));
	creditText->SetForegroundColour(wxColour(255, 211, 77));
	creditText->SetBackgroundColour(background);
	creditText->SetToolTip("NexaMap Editor developers: Mateuzkl and Skyyzyy");
	footerSizer->Add(creditText, 0, wxALIGN_RIGHT);
	contentSizer->Add(footerSizer, 0, wxALIGN_RIGHT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 14));
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
	sizer->Add(item, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 3));
}

void WelcomeDialogPanel::UpdateInputs() {
	m_show_welcome_dialog_checkbox->SetValue(g_settings.getInteger(Config::WELCOME_DIALOG) == 1);
	const int selectedTheme = g_settings.getInteger(Config::THEME);
	SetThemeSelection(selectedTheme >= 0 && selectedTheme <= 2 ? selectedTheme : m_active_theme);
	UpdateThemeStatus();
	RefreshWorkspaceDashboard();
	Layout();
}

void WelcomeDialogPanel::OnSelectClient(wxCommandEvent& WXUNUSED(event)) {
	const wxString current = g_workspace.getClient().rootPath;
	wxDirDialog dialog(this, "Select the Tibia client folder", current, wxDD_DIR_MUST_EXIST);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	wxString error;
	wxArrayString warnings;
	if (!g_workspace.configureClient(dialog.GetPath(), error, warnings)) {
		wxMessageBox(error, "Client folder not supported", wxOK | wxICON_ERROR, this);
	}
	if (!warnings.empty()) {
		wxString message;
		for (const wxString& warning : warnings) {
			message << warning << "\n";
		}
		wxMessageBox(message, "Client detection warnings", wxOK | wxICON_WARNING, this);
	}
	RefreshWorkspaceDashboard();
}

void WelcomeDialogPanel::OnSelectServer(wxCommandEvent& WXUNUSED(event)) {
	wxString current;
	if (!g_workspace.getServer().rootPath.empty()) {
#ifdef __WINDOWS__
		current = wxString(g_workspace.getServer().rootPath.wstring());
#else
		current = wxString::FromUTF8(g_workspace.getServer().rootPath.string());
#endif
	}
	wxDirDialog dialog(this, "Select the OT server root folder", current, wxDD_DIR_MUST_EXIST);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	wxString error;
	g_workspace.configureServer(dialog.GetPath(), error);
	wxString clientError;
	wxArrayString clientWarnings;
	g_workspace.restoreCompatibleClient(clientError, clientWarnings);
	RefreshWorkspaceDashboard();
	if (!error.empty()) {
		wxMessageBox(error, "Server scan completed", wxOK | wxICON_WARNING, this);
	}
}

void WelcomeDialogPanel::OnRescanServer(wxCommandEvent& WXUNUSED(event)) {
	wxString error;
	g_workspace.rescanServer(error);
	RefreshWorkspaceDashboard();
	if (!error.empty()) {
		wxMessageBox(error, "Server scan completed", wxOK | wxICON_WARNING, this);
	}
}

void WelcomeDialogPanel::OnOpenWorkspace(wxCommandEvent& WXUNUSED(event)) {
	wxCommandEvent action(wxEVT_BUTTON, WELCOME_DIALOG_OPEN_WORKSPACE);
	static_cast<WelcomeDialog*>(GetParent())->OnNavigationActivated(action);
}

void WelcomeDialogPanel::RefreshWorkspaceDashboard() {
	if (!m_client_path_label || !m_recent_maps_panel) {
		return;
	}

	const wxColour subtle = WelcomeThemeStyle::Get(Theme::Role::TextSubtle);
	const wxColour cyan(38, 211, 230);
	const wxColour green(69, 201, 105);
	const wxColour warning(241, 184, 55);
	const wxColour danger(236, 92, 92);
	const WorkspaceClientSelection& client = g_workspace.getClient();
	const ServerWorkspace& server = g_workspace.getServer();

	auto pathText = [](const std::filesystem::path& path) {
#ifdef __WINDOWS__
		return wxString(path.wstring());
#else
		return wxString::FromUTF8(path.string());
#endif
	};
	auto setPath = [](wxStaticText* label, const wxString& value) {
		label->SetLabel(value);
		label->SetToolTip(value);
	};
	auto setResource = [&](wxStaticText* label, const wxString& name, bool found, const wxString& foundText = "Found", bool required = false) {
		const wxString status = found ? foundText : wxString("Not found");
		label->SetLabel(name + "  |  " + status);
		label->SetToolTip(label->GetLabel());
		label->SetForegroundColour(found ? green : (required ? danger : warning));
	};

	if (client.valid) {
		setPath(m_client_path_label, client.rootPath);
		m_client_status_label->SetLabel("Ready  |  " + client.versionName);
		m_client_status_label->SetForegroundColour(green);
	} else {
		setPath(m_client_path_label, client.rootPath.empty() ? wxString("No client folder selected") : client.rootPath);
		m_client_status_label->SetLabel(client.rootPath.empty() ? "Waiting" : "Not recognized");
		m_client_status_label->SetForegroundColour(client.rootPath.empty() ? subtle : danger);
	}

	if (!server.rootPath.empty()) {
		setPath(m_server_path_label, pathText(server.rootPath));
		m_server_status_label->SetLabel(server.hasRequiredResources() ? wxString("Ready  |  ") + wxString::FromUTF8(server.serverProfile) : wxString("Item DB missing"));
		m_server_status_label->SetForegroundColour(server.hasRequiredResources() ? green : danger);
	} else {
		setPath(m_server_path_label, "No server folder selected");
		m_server_status_label->SetLabel("Waiting");
		m_server_status_label->SetForegroundColour(subtle);
	}

	if (server.hasAppearances()) {
		setResource(m_items_otb_status, "appearances.dat", true, "Found", true);
	} else {
		setResource(m_items_otb_status, "items.otb", server.hasItemsOtb(), "Found", true);
	}
	setResource(m_items_xml_status, "items.xml", server.itemsXmlFingerprint.exists);
	setResource(m_maps_status, "Maps", !server.maps.empty(), wxString::Format("%llu found", static_cast<unsigned long long>(server.maps.size())));
	setResource(m_monsters_status, "Monsters", !server.monstersDirectory.empty(), "Found");
	setResource(m_npcs_status, "NPCs", !server.npcsDirectory.empty(), "Found");

	m_protocol_value->SetLabel(client.valid ? client.versionName : wxString("-"));
	const ItemIdMode idMode = g_workspace.getEffectiveItemIdMode();
	const ItemIdModePreference idPreference = g_workspace.getItemIdModePreference();
	const wxString preferenceLabel = idPreference == ItemIdModePreference::ServerId
		? wxString("Manual")
		: (idPreference == ItemIdModePreference::ClientId ? wxString("Manual") : wxString("Auto"));
	m_id_mode_value->SetLabel(idMode == ItemIdMode::Unknown ? wxString("Needs review") : preferenceLabel + "  |  " + wxString::FromUTF8(ItemIdModeName(idMode)));
	m_id_mode_value->SetForegroundColour(idMode == ItemIdMode::Unknown ? warning : cyan);
	m_items_source_value->SetLabel(server.hasAppearances() ? wxString("appearances.dat") : (server.hasItemsOtb() ? wxString("items.otb") : wxString("-")));
	m_workspace_status_value->SetLabel(g_workspace.isReady() ? "Ready" : "Setup required");
	m_workspace_status_value->SetForegroundColour(g_workspace.isReady() ? green : warning);
	m_open_workspace_button->Enable(g_workspace.isReady());

	if (g_workspace.isReady()) {
		m_ready_description->SetLabel("Ready - server resources load directly.");
		m_ready_description->SetForegroundColour(green);
	} else if (!client.valid) {
		m_ready_description->SetLabel("Select a supported client folder.");
		m_ready_description->SetForegroundColour(subtle);
	} else {
		m_ready_description->SetLabel("Select a compatible server root containing items.otb or appearances.dat.");
		m_ready_description->SetForegroundColour(subtle);
	}

	const std::vector<wxString> detectedMaps = g_workspace.getDetectedMaps();
	m_recent_maps_panel->SetFiles(detectedMaps.empty() ? m_recent_files : detectedMaps, !detectedMaps.empty());
	Layout();
	if (GetParent()) {
		GetParent()->Layout();
	}
	Refresh();
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
	const wxString text = pending ? wxString("Restart to apply theme") : m_version_text;
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
	m_normal_bitmap = LoadWelcomeBitmap(this, m_icon_name, wxSize(22, 22), normalTint);
	m_active_bitmap = LoadWelcomeBitmap(this, m_icon_name, wxSize(22, 22), WelcomeThemeStyle::Get(Theme::Role::Accent));
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
	const int iconX = FROM_DIP(this, 12);
	const int iconY = (bounds.height - bitmap.GetHeight()) / 2;
	if (bitmap.IsOk()) {
		dc.DrawBitmap(bitmap, iconX, iconY, true);
	}

	const int textX = FROM_DIP(this, 44);
	const int availableTextWidth = std::max(0, bounds.width - textX - FROM_DIP(this, 10));
	const wxColour titleColour = interactive ? WelcomeThemeStyle::Get(Theme::Role::Text) : WelcomeThemeStyle::Get(Theme::Role::TextSubtle).ChangeLightness(80);
	dc.SetTextForeground(titleColour);
	dc.SetFont(FontWithPointSize(GetFont(), GetFont().GetPointSize(), true));
	const wxString title = Ellipsize(m_title, dc, wxELLIPSIZE_END, availableTextWidth);
	const wxSize titleSize = dc.GetTextExtent(title);
	dc.DrawText(title, textX, std::max(0, (bounds.height - titleSize.y) / 2));

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
	wxControl(parent, wxID_ANY, wxDefaultPosition, FROM_DIP(parent, wxSize(48, 32)), wxBORDER_NONE | wxWANTS_CHARS),
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
	dc.SetFont(FontWithPointSize(GetFont(), std::max(8, GetFont().GetPointSize() - 1), m_selected));
	dc.SetTextForeground(m_selected ? WelcomeThemeStyle::Get(Theme::Role::Accent) : WelcomeThemeStyle::Get(Theme::Role::TextSubtle));
	const wxSize textSize = dc.GetTextExtent(m_label);
	dc.DrawText(m_label, (bounds.width - textSize.x) / 2, std::max(0, (bounds.height - textSize.y) / 2));
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
	wxPanel(parent, wxID_ANY),
	m_dialog(dialog) {
	SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Background));
	Bind(wxEVT_LEAVE_WINDOW, &RecentMapsPanel::OnMouseLeave, this);
	m_sizer = newd wxBoxSizer(wxVERTICAL);
	SetSizer(m_sizer);
	SetFiles(recentFiles, false);
}

void RecentMapsPanel::SetFiles(const std::vector<wxString>& files, bool detectedMaps) {
	m_hovered_item = nullptr;
	m_selected_item = nullptr;
	m_sizer->Clear(true);
	auto* header = newd wxStaticText(this, wxID_ANY, detectedMaps ? "DETECTED MAPS" : "RECENT MAPS");
	header->SetFont(FontWithPointSize(GetFont(), GetFont().GetPointSize(), true));
	header->SetForegroundColour(WelcomeThemeStyle::Get(Theme::Role::Accent));
	header->SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Background));
	m_sizer->Add(header, 0, wxEXPAND | wxBOTTOM, FROM_DIP(this, 6));

	if (files.empty()) {
		auto* empty = newd wxStaticText(this, wxID_ANY, "No maps detected yet. Optional map folders will appear here automatically.");
		empty->SetForegroundColour(WelcomeThemeStyle::Get(Theme::Role::TextSubtle));
		empty->SetBackgroundColour(WelcomeThemeStyle::Get(Theme::Role::Background));
		m_sizer->Add(empty, 0, wxEXPAND | wxBOTTOM, FROM_DIP(this, 10));
		const wxSize recentSize(-1, FROM_DIP(this, 46));
		SetMinSize(recentSize);
		SetMaxSize(recentSize);
	} else {
		const size_t visibleItems = std::min<size_t>(2, files.size());
		for (size_t index = 0; index < visibleItems; ++index) {
			const wxString& file = files[index];
			auto* recentItem = newd RecentItem(this, file);
			recentItem->Bind(wxEVT_BUTTON, &WelcomeDialog::OnRecentItemActivated, m_dialog);
			m_sizer->Add(recentItem, 0, wxEXPAND | wxBOTTOM, FROM_DIP(this, 4));
		}
		const wxSize recentSize(-1, FROM_DIP(this, 24 + static_cast<int>(visibleItems) * 44));
		SetMinSize(recentSize);
		SetMaxSize(recentSize);
	}
	Layout();
	Refresh();
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
	wxControl(parent, wxID_ANY, wxDefaultPosition, FROM_DIP(parent, wxSize(-1, 40)), wxBORDER_NONE | wxWANTS_CHARS),
	m_item_text(itemName) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(wxSize(-1, FROM_DIP(this, 40)));
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
	const int actionWidth = FROM_DIP(this, 54);
	const int width = bounds.width - textX - actionWidth - FROM_DIP(this, 12);
	dc.SetFont(FontWithPointSize(GetFont(), GetFont().GetPointSize(), true));
	dc.SetTextForeground(WelcomeThemeStyle::Get(Theme::Role::Text));
	const wxString fileName = Ellipsize(wxFileNameFromPath(m_item_text), dc, wxELLIPSIZE_END, width);
	const wxSize fileSize = dc.GetTextExtent(fileName);
	dc.DrawText(fileName, textX, std::max(0, (bounds.height - fileSize.y) / 2));
	dc.SetFont(FontWithPointSize(GetFont(), std::max(8, GetFont().GetPointSize() - 1), true));
	dc.SetTextForeground(m_hovered || m_selected ? WelcomeThemeStyle::Get(Theme::Role::Accent) : WelcomeThemeStyle::Get(Theme::Role::TextSubtle));
	const wxString action = "Open";
	const wxSize actionSize = dc.GetTextExtent(action);
	dc.DrawText(action, bounds.width - actionSize.x - FROM_DIP(this, 14), std::max(0, (bounds.height - actionSize.y) / 2));
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
