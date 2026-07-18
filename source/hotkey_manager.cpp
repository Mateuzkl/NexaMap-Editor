#include "main.h"
#include "hotkey_manager.h"
#include "hotkey_dialog.h"
#include "gui.h"
#include "gui_ids.h"

#include <functional>

HotkeyManager g_hotkey_manager;

HotkeyManager::HotkeyManager() = default;
HotkeyManager::~HotkeyManager() = default;

HotkeyManager::XmlMenuData HotkeyManager::ParseMenubarXml() {
	XmlMenuData data;
	
	wxString path = g_gui.GetDataDirectory() + "menubar.xml";
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
				data.actions[actionStr] = {wxString(hotkey), wxString(help), wxString(name)};
				
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

wxString HotkeyManager::GetEffectiveKey(MenuBar::ActionID actionId) const {
	auto it = entries_.find(actionId);
	if (it != entries_.end()) {
		return it->second.EffectiveKey();
	}
	return "";
}

void HotkeyManager::SyncToXml() {
	wxString path = g_gui.GetDataDirectory() + "menubar.xml";
	pugi::xml_document doc;
	if (!doc.load_file(path.mb_str())) {
		return;
	}

	// Build action name lookup
	std::unordered_map<std::string, MenuBar::ActionID> nameToId;
	for (const auto& [actionId, info] : actionInfo_) {
		nameToId[info.name.ToStdString()] = actionId;
	}

	// Traverse menubar.xml and update nodes in the live document
	std::function<void(pugi::xml_node)> updateNodes = [&](pugi::xml_node node) {
		for (pugi::xml_node item = node.child("item"); item; item = item.next_sibling("item")) {
			std::string actionStr = item.attribute("action").as_string();
			if (actionStr.empty()) {
				continue;
			}

			auto idIt = nameToId.find(actionStr);
			if (idIt == nameToId.end()) {
				continue;
			}

			MenuBar::ActionID actionId = idIt->second;

			// Update hotkey if override exists
			auto entryIt = entries_.find(actionId);
			if (entryIt != entries_.end() && entryIt->second.overrideKey.has_value()) {
				std::string effectiveKey = entryIt->second.EffectiveKey().ToStdString();
				pugi::xml_attribute attr = item.attribute("hotkey");
				if (attr) {
					attr.set_value(effectiveKey.c_str());
				} else {
					// Append hotkey attribute when absent
					item.append_attribute("hotkey") = effectiveKey.c_str();
				}
			}

			// Update help text
			auto infoIt = actionInfo_.find(actionId);
			if (infoIt != actionInfo_.end()) {
				std::string help = infoIt->second.help.ToStdString();
				pugi::xml_attribute attr = item.attribute("help");
				if (attr) {
					attr.set_value(help.c_str());
				} else {
					item.append_attribute("help") = help.c_str();
				}
			}
		}

		// Recurse into submenus
		for (pugi::xml_node menu = node.child("menu"); menu; menu = menu.next_sibling("menu")) {
			updateNodes(menu);
		}
	};

	pugi::xml_node menubarNode = doc.child("menubar");
	if (menubarNode) {
		updateNodes(menubarNode);
	}

	if (!doc.save_file(path.mb_str())) {
		wxLogWarning("Failed to save hotkeys to menubar.xml");
	}
}

void HotkeyManager::ShowHotkeyDialog(wxWindow* parent, MainMenuBar* menubar) {
	HotkeyDialog dlg(parent, menubar, entries_, actionInfo_);
	if (dlg.ShowModal() == wxID_OK) {
		// Dialog commits changes back to entries_ and actionInfo_ on OK
		SyncToXml();
		RebuildAccelerators(g_gui.root);
		menubar->UpdateLabelHotkeys();
		if (g_gui.root) {
			g_gui.root->UpdateMenubar();
		}
	}
	// On Cancel, dialog copies are discarded automatically
}
