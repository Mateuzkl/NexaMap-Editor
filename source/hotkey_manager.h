#ifndef RME_HOTKEY_MANAGER_H
#define RME_HOTKEY_MANAGER_H

#include "main.h"
#include "main_menubar.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <wx/accel.h>

class wxWindow;

struct HotkeyEntry {
	wxString defaultKey;
	std::optional<wxString> overrideKey;

	wxString EffectiveKey() const {
		if (overrideKey.has_value()) {
			return *overrideKey;
		}
		return defaultKey;
	}
};

class HotkeyManager {
public:
	HotkeyManager();
	~HotkeyManager();

	void DiscoverActions(MainMenuBar* menubar);
	void RebuildAccelerators(wxWindow* target);

	void ShowHotkeyDialog(wxWindow* parent, MainMenuBar* menubar);
	wxString GetEffectiveKey(MenuBar::ActionID actionId) const;

	struct ActionInfo {
		wxString name;
		wxString itemName;
		wxString help;
		wxString category;
	};

private:
	struct XmlActionData {
		wxString hotkey;
		wxString help;
		wxString itemName;
	};
	
	struct XmlMenuData {
		std::unordered_map<std::string, XmlActionData> actions;
		std::unordered_map<std::string, wxString> categories;
	};

	std::unordered_map<MenuBar::ActionID, HotkeyEntry> entries_;
	std::unordered_map<MenuBar::ActionID, ActionInfo> actionInfo_;
	
	XmlMenuData ParseMenubarXml();
	void BuildAcceleratorEntries(std::vector<wxAcceleratorEntry>& accelEntries) const;
	void SyncToXml();
};

extern HotkeyManager g_hotkey_manager;

#endif
