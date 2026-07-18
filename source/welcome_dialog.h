//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef WELCOME_DIALOG_H
#define WELCOME_DIALOG_H

#include <wx/scrolwin.h>
#include <wx/wx.h>

#include <vector>

wxDECLARE_EVENT(WELCOME_DIALOG_ACTION, wxCommandEvent);

constexpr wxWindowID WELCOME_DIALOG_MAP_CONVERTER = wxID_HIGHEST + 7000;
constexpr wxWindowID WELCOME_DIALOG_SPAWN_CONVERTER = wxID_HIGHEST + 7001;

class WelcomeBrandPanel;
class WelcomeFeatureItem;
class WelcomeFeaturesPanel;
class WelcomeNavigationItem;
class WelcomeThemeChoice;
class RecentItem;
class RecentMapsPanel;

class WelcomeDialogPanel;

class WelcomeDialog : public wxDialog {
public:
	WelcomeDialog(const wxString& titleText, const wxString& versionText, const wxSize& size, const wxBitmap& fallbackLogo, const std::vector<wxString>& recentFiles);

	void OnNavigationActivated(wxCommandEvent& event);
	void OnCheckboxClicked(wxCommandEvent& event);
	void OnRecentItemActivated(wxCommandEvent& event);

private:
	void ActivateAction(wxWindowID action, const wxString& path = wxString {});

	WelcomeDialogPanel* m_welcome_dialog_panel = nullptr;
};

class WelcomeDialogPanel : public wxPanel {
public:
	WelcomeDialogPanel(WelcomeDialog* parent, const wxString& titleText, const wxString& versionText, const wxBitmap& fallbackLogo, const std::vector<wxString>& recentFiles);

	void OnThemeChanged(wxCommandEvent& event);
	void UpdateInputs();

private:
	void SetThemeSelection(int theme);
	void UpdateThemeStatus();
	void AddNavigationItem(wxWindow* parent, wxSizer* sizer, const wxString& iconName, const wxString& title, const wxString& subtitle, const wxString& tooltip, wxWindowID action, bool primary = false);

	wxCheckBox* m_show_welcome_dialog_checkbox = nullptr;
	WelcomeThemeChoice* m_system_theme_choice = nullptr;
	WelcomeThemeChoice* m_dark_theme_choice = nullptr;
	WelcomeThemeChoice* m_light_theme_choice = nullptr;
	wxStaticText* m_theme_status_label = nullptr;
	int m_active_theme = 0;
	bool m_theme_prompt_open = false;
};

class WelcomeNavigationItem : public wxControl {
public:
	WelcomeNavigationItem(wxWindow* parent, const wxString& iconName, const wxString& title, const wxString& subtitle, const wxString& tooltip, wxWindowID action, bool primary);

private:
#if wxUSE_ACCESSIBILITY && defined(__WXMSW__)
	wxAccessible* CreateAccessible() override;
#endif
	void RebuildBitmaps();
	void Activate();
	void OnPaint(wxPaintEvent& event);
	void OnMouseEnter(wxMouseEvent& event);
	void OnMouseLeave(wxMouseEvent& event);
	void OnLeftDown(wxMouseEvent& event);
	void OnLeftUp(wxMouseEvent& event);
	void OnCaptureLost(wxMouseCaptureLostEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnFocus(wxFocusEvent& event);

	wxWindowID m_action = wxID_NONE;
	wxString m_icon_name;
	wxString m_title;
	wxString m_subtitle;
	wxBitmap m_normal_bitmap;
	wxBitmap m_active_bitmap;
	bool m_primary = false;
	bool m_hovered = false;
	bool m_pressed = false;
};

class WelcomeThemeChoice : public wxControl {
public:
	WelcomeThemeChoice(wxWindow* parent, int theme, const wxString& iconName, const wxString& label);

	void SetSelected(bool selected);

private:
#if wxUSE_ACCESSIBILITY && defined(__WXMSW__)
	wxAccessible* CreateAccessible() override;
#endif
	void RebuildBitmap();
	void Activate();
	void OnPaint(wxPaintEvent& event);
	void OnLeftDown(wxMouseEvent& event);
	void OnLeftUp(wxMouseEvent& event);
	void OnMouseEnter(wxMouseEvent& event);
	void OnMouseLeave(wxMouseEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnFocus(wxFocusEvent& event);

	int m_theme = 0;
	wxString m_icon_name;
	wxString m_label;
	wxBitmap m_bitmap;
	bool m_selected = false;
	bool m_hovered = false;
	bool m_pressed = false;
};

class WelcomeBrandPanel : public wxPanel {
public:
	WelcomeBrandPanel(wxWindow* parent, const wxString& title, const wxString& version, const wxBitmap& fallbackLogo);

private:
	void RebuildBackground();
	void OnPaint(wxPaintEvent& event);
	void OnSize(wxSizeEvent& event);

	wxString m_title;
	wxString m_version;
	wxImage m_background_source;
	wxBitmap m_background_bitmap;
	wxBitmap m_fallback_logo;
	wxSize m_cached_size;
};

class WelcomeFeatureItem : public wxPanel {
public:
	WelcomeFeatureItem(wxWindow* parent, const wxString& iconName, const wxString& title, const wxString& description, const wxString& tooltip);

	void SetShowDescription(bool show);

private:
	void RebuildBitmap();
	void OnPaint(wxPaintEvent& event);

	wxString m_icon_name;
	wxString m_title;
	wxString m_description;
	wxBitmap m_bitmap;
	bool m_show_description = true;
};

class WelcomeFeaturesPanel : public wxPanel {
public:
	explicit WelcomeFeaturesPanel(wxWindow* parent);

private:
	void OnSize(wxSizeEvent& event);
	void UpdateLayout(const wxSize& size);

	wxGridSizer* m_grid = nullptr;
	std::vector<WelcomeFeatureItem*> m_items;
	int m_columns = 4;
};

class RecentMapsPanel : public wxScrolledWindow {
public:
	RecentMapsPanel(wxWindow* parent, WelcomeDialog* dialog, const std::vector<wxString>& recentFiles);

	void SetHoveredItem(RecentItem* item);
	void ClearHoveredItem(RecentItem* item);
	void SelectItem(RecentItem* item);

private:
	void OnMouseLeave(wxMouseEvent& event);

	RecentItem* m_hovered_item = nullptr;
	RecentItem* m_selected_item = nullptr;
};

class RecentItem : public wxControl {
public:
	RecentItem(RecentMapsPanel* parent, const wxString& itemName);

	void SetHovered(bool hovered);
	void SetSelected(bool selected);

private:
#if wxUSE_ACCESSIBILITY && defined(__WXMSW__)
	wxAccessible* CreateAccessible() override;
#endif
	void Activate();
	void OnPaint(wxPaintEvent& event);
	void OnMouseEnter(wxMouseEvent& event);
	void OnMouseLeave(wxMouseEvent& event);
	void OnLeftDown(wxMouseEvent& event);
	void OnLeftUp(wxMouseEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnFocus(wxFocusEvent& event);

	wxString m_item_text;
	bool m_hovered = false;
	bool m_selected = false;
	bool m_pressed = false;
};

#endif // WELCOME_DIALOG_H
