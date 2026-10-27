#include "main.h"
#include "hotkey_manager.h"
#include "hotkey_dialog.h"
#include "gui.h"
#include "gui_ids.h"

#include <functional>
#include <wx/textentry.h>

HotkeyManager g_hotkey_manager;

HotkeyManager::HotkeyManager() = default;
HotkeyManager::~HotkeyManager() = default;

HotkeyManager::XmlMenuData HotkeyManager::ParseMenubarXml() {
	XmlMenuData data;

	wxString path = g_gui.getFoundDataDirectory() + "menubar.xml";
	if (!wxFileExists(path)) {
		path = g_gui.GetDataDirectory() + "menubar.xml";
	}
	pugi::xml_document doc;
	if (!doc.load_file(path.mb_str())) {
		return data;
	}

	pugi::xml_node menubarNode = doc.child("menubar");
	if (!menubarNode) {
		return data;
	}

	std::function<void(pugi::xml_node, wxString)> collectData = [&](pugi::xml_node node, wxString currentMenu) {
		for (pugi::xml_node item = node.child("item"); item; item = item.next_sibling("item")) {
			std::string actionStr = item.attribute("action").as_string();
			if (!actionStr.empty()) {
				// Collect action data
				std::string hotkey = item.attribute("hotkey").as_string();
				std::string help = item.attribute("help").as_string();
				std::string name = item.attribute("name").as_string();
				data.actions[actionStr] = { wxString(hotkey), wxString(help), wxString(name) };

				// Collect category
				data.categories[actionStr] = currentMenu;
			}
		}
		for (pugi::xml_node menu = node.child("menu"); menu; menu = menu.next_sibling("menu")) {
			wxString menuName = wxString(menu.attribute("name").as_string());
			wxString fullPath = currentMenu.empty() ? menuName : currentMenu + " > " + menuName;
			collectData(menu, fullPath);
		}
	};

	collectData(menubarNode, "");
	return data;
}

void HotkeyManager::DiscoverActions(MainMenuBar* menubar) {
	if (!menubar) {
		return;
	}

	actionInfo_.clear();
	entries_.clear();

	XmlMenuData xmlData = ParseMenubarXml();

	// Match XML actions with MainMenuBar ActionID enum
	const auto& actions = menubar->GetActions();
	for (const auto& [actionName, actionPtr] : actions) {
		MenuBar::ActionID actionId = static_cast<MenuBar::ActionID>(actionPtr->id);

		wxString defaultKey;
		wxString description;
		wxString itemName;

		auto xmlIt = xmlData.actions.find(actionName);
		if (xmlIt != xmlData.actions.end()) {
			defaultKey = xmlIt->second.hotkey;
			description = xmlIt->second.help;
			itemName = xmlIt->second.itemName;
		}

		ActionInfo info;
		info.name = wxString(actionName);
		info.itemName = itemName;
		info.help = description;
		auto catIt = xmlData.categories.find(actionName);
		if (catIt != xmlData.categories.end()) {
			info.category = catIt->second;
		}
		actionInfo_[actionId] = info;

		HotkeyEntry entry;
		entry.defaultKey = defaultKey;
		wxString overrideKey;
		auto& config = Settings::getConfigObject();
		const wxString configKey = "/HotkeyOverrides/" + info.name;
		if (config.Read(configKey, &overrideKey)) {
			entry.overrideKey = overrideKey; // An empty override deliberately unbinds the action.
		}
		config.Read("/HotkeyHelp/" + info.name, &actionInfo_[actionId].help, info.help);
		entries_[actionId] = entry;
	}
}

void HotkeyManager::BuildAcceleratorEntries(std::vector<wxAcceleratorEntry>& accelEntries) const {
	for (const auto& [actionId, entry] : entries_) {
		wxString keyStr = entry.EffectiveKey();
		if (keyStr.empty()) {
			continue;
		}

		wxAcceleratorEntry accel;
		if (accel.FromString(wxString("\t") + keyStr)) {
			int eventId = MAIN_FRAME_MENU + static_cast<int>(actionId);
			accel.Set(accel.GetFlags(), accel.GetKeyCode(), eventId);
			accelEntries.push_back(accel);
		}
	}
}

void HotkeyManager::RebuildAccelerators(wxWindow* target) {
	if (!target) {
		return;
	}
#ifdef __WXGTK__
	if (!g_gui.AreHotkeysEnabled()) {
		target->SetAcceleratorTable(wxNullAcceleratorTable);
		return;
	}
	// GTK checks the frame table before delivering text to an entry. A plain
	// tool key must remain text while editing names, searches or coordinates.
	for (wxWindow* focus = wxWindow::FindFocus(); focus && focus != target; focus = focus->GetParent()) {
		if (dynamic_cast<wxTextEntryBase*>(focus)) {
			target->SetAcceleratorTable(wxNullAcceleratorTable);
			return;
		}
	}
#endif

	std::vector<wxAcceleratorEntry> accelEntries;
	BuildAcceleratorEntries(accelEntries);

	if (!accelEntries.empty()) {
		wxAcceleratorTable table(static_cast<int>(accelEntries.size()), accelEntries.data());
		target->SetAcceleratorTable(table);
	} else {
		// Clear accelerator table when no bindings remain
		target->SetAcceleratorTable(wxNullAcceleratorTable);
	}
}

std::optional<MenuBar::ActionID> HotkeyManager::FindAction(const wxKeyEvent& event) const {
	int flags = wxACCEL_NORMAL;
	if (event.ControlDown()) {
		flags |= wxACCEL_CTRL;
	}
	if (event.ShiftDown()) {
		flags |= wxACCEL_SHIFT;
	}
	if (event.AltDown()) {
		flags |= wxACCEL_ALT;
	}
	if (event.MetaDown()) {
		return std::nullopt;
	}
	int key = event.GetKeyCode();
	if (key >= 'a' && key <= 'z') {
		key += 'A' - 'a';
	}
	std::vector<wxAcceleratorEntry> accelerators;
	BuildAcceleratorEntries(accelerators);
	for (const auto& accelerator : accelerators) {
		if (accelerator.GetFlags() == flags && accelerator.GetKeyCode() == key) {
			return static_cast<MenuBar::ActionID>(accelerator.GetCommand() - MAIN_FRAME_MENU);
		}
	}
	return std::nullopt;
}

void HotkeyManager::HandleKeyHook(wxKeyEvent& event, MainMenuBar& menubar) {
	if (!g_gui.AreHotkeysEnabled()) {
		event.Skip();
		return;
	}
	for (wxWindow* focus = wxWindow::FindFocus(); focus && !focus->IsTopLevel(); focus = focus->GetParent()) {
		if (dynamic_cast<wxTextEntryBase*>(focus)) {
			event.Skip();
			return;
		}
	}
	if (const auto action = FindAction(event)) {
		// Raw frame-table events on wxGTK do not toggle check/radio menu items.
		menubar.DispatchAction(*action);
		return;
	}
	event.Skip();
}

wxString HotkeyManager::GetEffectiveKey(MenuBar::ActionID actionId) const {
	auto it = entries_.find(actionId);
	if (it != entries_.end()) {
		return it->second.EffectiveKey();
	}
	return "";
}

void HotkeyManager::SaveOverrides() {
	auto& config = Settings::getConfigObject();
	for (const auto& [actionId, entry] : entries_) {
		const auto& info = actionInfo_.at(actionId);
		const wxString key = "/HotkeyOverrides/" + info.name;
		if (entry.overrideKey) {
			config.Write(key, *entry.overrideKey);
		} else {
			config.DeleteEntry(key);
		}
		config.Write("/HotkeyHelp/" + info.name, info.help);
	}
	g_settings.save();
}
void HotkeyManager::ShowHotkeyDialog(wxWindow* parent, MainMenuBar* menubar) {
	HotkeyDialog dlg(parent, menubar, entries_, actionInfo_);
	if (dlg.ShowModal() == wxID_OK) {
		// Dialog commits changes back to entries_ and actionInfo_ on OK
		SaveOverrides();
		menubar->UpdateLabelHotkeys();
		RebuildAccelerators(g_gui.root);
		if (g_gui.root) {
			g_gui.root->UpdateMenubar();
		}
	}
	// On Cancel, dialog copies are discarded automatically
}
