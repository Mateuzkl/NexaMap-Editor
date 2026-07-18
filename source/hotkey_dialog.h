#ifndef RME_HOTKEY_DIALOG_H
#define RME_HOTKEY_DIALOG_H

#include "main.h"
#include "main_menubar.h"
#include "hotkey_manager.h"

#include <set>
#include <unordered_map>
#include <vector>
#include <wx/dialog.h>
#include <wx/event.h>

class wxListCtrl;
class wxTextCtrl;

class HotkeyDialog : public wxDialog {
public:
	HotkeyDialog(wxWindow* parent, MainMenuBar* menubar,
		std::unordered_map<MenuBar::ActionID, HotkeyEntry>& entries,
		std::unordered_map<MenuBar::ActionID, HotkeyManager::ActionInfo>& actionInfo);

private:
	struct DisplayItem {
		wxString category;
		MenuBar::ActionID action;
		wxString actionName;
		wxString hotkey;
		wxString help;
	};

	struct MenuHelpEntry {
		wxString menu;
		wxString text;
		wxString action;
		wxString help;
		wxString shortcut;
	};

	void BuildDisplayData();
	void PopulateList();
	void OnKeyDown(wxKeyEvent& event);
	void OnSetButton(wxCommandEvent& event);
	void OnHotkeySearch(wxCommandEvent& event);
	void OnHelpSearch(wxCommandEvent& event);

	static bool ContainsIgnoreCase(const wxString& source, const wxString& search);

	// Key handling helpers
	bool IsModifierKey(int keyCode) const;
	wxString GetModifierString(int keyCode) const;
	wxString FormatKeyCode(int keyCode) const;
	void HandleModifierKey(int keyCode);
	void HandleRegularKey(int keyCode);

	// Button action helpers
	bool GetSelectedAction(long& selectedIndex, MenuBar::ActionID& actionId);
	bool CheckHotkeyConflict(const wxString& hotkey, MenuBar::ActionID currentAction);
	void ApplyHotkeyChange(MenuBar::ActionID actionId, const wxString& hotkey, long selectedIndex);

	// Search helpers
	bool MatchesHotkeySearch(const DisplayItem& item, const wxString& search) const;
	std::set<wxString> BuildHelpSearchMatches(const wxString& search) const;

	MainMenuBar* menubar_;
	std::unordered_map<MenuBar::ActionID, HotkeyEntry>& entriesRef_;
	std::unordered_map<MenuBar::ActionID, HotkeyManager::ActionInfo>& actionInfoRef_;
	std::unordered_map<MenuBar::ActionID, HotkeyEntry> entries_;
	std::unordered_map<MenuBar::ActionID, HotkeyManager::ActionInfo> actionInfo_;
	std::vector<DisplayItem> displayItems_;
	std::vector<MenuHelpEntry> menuHelpEntries_;
	wxListCtrl* hotkeyList_ = nullptr;
	wxTextCtrl* hotkeyEdit_ = nullptr;
	wxTextCtrl* hotkeySearch_ = nullptr;
	wxTextCtrl* helpSearch_ = nullptr;
};

#endif
