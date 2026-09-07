//////////////////////////////////////////////////////////////////////
// Advanced monster editor pages: defenses, loot, summons and voices.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "monster_editor_dialog.h"

#include "find_item_window.h"
#include "graphics.h"
#include "gui.h"
#include "items.h"
#include "workspace_session.h"

#include <wx/imaglist.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/treectrl.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace {
	wxString Utf8(const std::string& value) {
		return wxString::FromUTF8(value);
	}

	std::string Narrow(const wxString& value) {
		return value.ToStdString(wxConvUTF8);
	}

	class RecordDialog final : public wxDialog {
	public:
		RecordDialog(wxWindow* parent, const wxString& title) :
			wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
			root = newd wxBoxSizer(wxVERTICAL);
			grid = newd wxFlexGridSizer(2, 8, 12);
			grid->AddGrowableCol(1, 1);
			root->Add(grid, 1, wxEXPAND | wxALL, FromDIP(12));
		}

		wxTextCtrl* text(const wxString& label, const std::string& value) {
			grid->Add(newd wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
			auto* control = newd wxTextCtrl(this, wxID_ANY, Utf8(value));
			grid->Add(control, 1, wxEXPAND);
			return control;
		}

		wxSpinCtrl* number(const wxString& label, int value, int minimum, int maximum) {
			grid->Add(newd wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
			auto* control = newd wxSpinCtrl(this, wxID_ANY, wxString::Format("%d", value), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, minimum, maximum, value);
			grid->Add(control, 1, wxEXPAND);
			return control;
		}

		wxCheckBox* boolean(const wxString& label, bool value) {
			grid->Add(newd wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
			auto* control = newd wxCheckBox(this, wxID_ANY, "Enabled");
			control->SetValue(value);
			grid->Add(control, 1, wxEXPAND);
			return control;
		}

		wxComboBox* combo(const wxString& label, const std::string& value, const wxArrayString& choices) {
			grid->Add(newd wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
			auto* control = newd wxComboBox(this, wxID_ANY, Utf8(value), wxDefaultPosition, wxDefaultSize, choices);
			grid->Add(control, 1, wxEXPAND);
			return control;
		}

		void finish(const wxSize& minimum = wxSize(420, 250)) {
			root->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
			SetSizerAndFit(root);
			SetMinSize(FromDIP(minimum));
			CentreOnParent();
		}

	private:
		wxBoxSizer* root = nullptr;
		wxFlexGridSizer* grid = nullptr;
	};

	std::string FormatProperties(const std::vector<MonsterCustomProperty>& properties) {
		std::string result;
		for (const MonsterCustomProperty& property : properties) {
			if (!result.empty()) {
				result += '\n';
			}
			result += property.name + "=" + property.value;
		}
		return result;
	}

	std::vector<MonsterCustomProperty> ParseProperties(const wxString& text, bool rawValues) {
		std::vector<MonsterCustomProperty> properties;
		std::istringstream lines(Narrow(text));
		std::string line;
		while (std::getline(lines, line)) {
			const std::size_t equals = line.find('=');
			if (equals == std::string::npos || equals == 0) {
				continue;
			}
			properties.push_back({ line.substr(0, equals), line.substr(equals + 1), rawValues });
		}
		return properties;
	}

	void AddColumn(wxListCtrl* list, int column, const wxString& title, int width) {
		list->InsertColumn(column, title);
		list->SetColumnWidth(column, width);
	}

	long ListSelection(wxListCtrl* list) {
		return list ? list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) : -1;
	}

	wxBoxSizer* ActionButtons(
		wxWindow* parent,
		const std::function<void()>& add,
		const std::function<void()>& edit,
		const std::function<void()>& remove,
		const std::function<void()>& up,
		const std::function<void()>& down
	) {
		auto* buttons = newd wxBoxSizer(wxHORIZONTAL);
		const auto append = [&](const wxString& label, const std::function<void()>& action) {
			auto* button = newd wxButton(parent, wxID_ANY, label);
			button->Bind(wxEVT_BUTTON, [action](wxCommandEvent&) { action(); });
			buttons->Add(button, 0, wxRIGHT, parent->FromDIP(6));
		};
		append("Add", add);
		append("Edit", edit);
		append("Remove", remove);
		buttons->AddStretchSpacer();
		append("Up", up);
		append("Down", down);
		return buttons;
	}

	class LootTreeData final : public wxTreeItemData {
	public:
		explicit LootTreeData(std::vector<std::size_t> value) :
			path(std::move(value)) { }

		std::vector<std::size_t> path;
	};

	MonsterLootEntry* LootAt(std::vector<MonsterLootEntry>& root, const std::vector<std::size_t>& path) {
		std::vector<MonsterLootEntry>* current = &root;
		MonsterLootEntry* entry = nullptr;
		for (const std::size_t index : path) {
			if (index >= current->size()) {
				return nullptr;
			}
			entry = &(*current)[index];
			current = &entry->children;
		}
		return entry;
	}

	std::vector<MonsterLootEntry>* LootSiblings(std::vector<MonsterLootEntry>& root, const std::vector<std::size_t>& path) {
		std::vector<MonsterLootEntry>* current = &root;
		for (std::size_t depth = 0; depth + 1 < path.size(); ++depth) {
			if (path[depth] >= current->size()) {
				return nullptr;
			}
			current = &(*current)[path[depth]].children;
		}
		return current;
	}

	wxString LootLabel(const MonsterLootEntry& entry, int itemId) {
		wxString identity;
		if (entry.usesName) {
			identity = Utf8(entry.itemName);
			if (itemId > 0) {
				identity += wxString::Format("  [Server %d | Client %u]", itemId, g_items[itemId].clientID);
			}
		} else if (itemId > 0 && g_items.typeExists(itemId)) {
			const ItemType& type = g_items[itemId];
			identity = wxString::Format("%d  %s  [Client %u]", itemId, Utf8(type.name), type.clientID);
		} else {
			identity = wxString::Format("%d  (not found in active client)", entry.itemId);
		}
		return wxString::Format("%s  | chance %d  | max %d", identity, entry.chance, entry.maxCount);
	}

	int ResolveLootItemId(const MonsterLootEntry& entry) {
		if (!entry.usesName) {
			return entry.itemId;
		}
		const wxString wanted = Utf8(entry.itemName).Lower();
		for (int id = 1; id <= g_items.getMaxID(); ++id) {
			if (g_items.typeExists(id) && Utf8(g_items[id].name).Lower() == wanted) {
				return id;
			}
		}
		return 0;
	}

	wxBitmap EmptyItemBitmap(int size) {
		wxImage image(size, size, true);
		image.InitAlpha();
		std::memset(image.GetData(), 0, static_cast<std::size_t>(size) * size * 3);
		std::memset(image.GetAlpha(), 0, static_cast<std::size_t>(size) * size);
		return wxBitmap(image);
	}

	wxBitmap ItemBitmap(int itemId, int size) {
		if (itemId <= 0 || !g_items.typeExists(itemId)) {
			return EmptyItemBitmap(size);
		}
		auto* sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(g_items[itemId].clientID));
		if (!sprite) {
			return EmptyItemBitmap(size);
		}
		std::vector<uint8_t> rgba;
		int width = 0;
		int height = 0;
		bool pending = false;
		if (!sprite->getVisualPreviewRGBA(rgba, width, height, pending, false) || width <= 0 || height <= 0) {
			return EmptyItemBitmap(size);
		}
		const double scale = std::min(static_cast<double>(size) / width, static_cast<double>(size) / height);
		const int scaledWidth = std::max(1, static_cast<int>(width * scale));
		const int scaledHeight = std::max(1, static_cast<int>(height * scale));
		const int offsetX = (size - scaledWidth) / 2;
		const int offsetY = (size - scaledHeight) / 2;
		wxImage image(size, size, true);
		image.InitAlpha();
		std::memset(image.GetData(), 0, static_cast<std::size_t>(size) * size * 3);
		std::memset(image.GetAlpha(), 0, static_cast<std::size_t>(size) * size);
		for (int y = 0; y < scaledHeight; ++y) {
			const int sourceY = std::min(height - 1, y * height / scaledHeight);
			for (int x = 0; x < scaledWidth; ++x) {
				const int sourceX = std::min(width - 1, x * width / scaledWidth);
				const std::size_t source = (static_cast<std::size_t>(sourceY) * width + sourceX) * 4;
				const std::size_t target = static_cast<std::size_t>(offsetY + y) * size + offsetX + x;
				std::copy_n(rgba.data() + source, 3, image.GetData() + target * 3);
				image.GetAlpha()[target] = rgba[source + 3];
			}
		}
		return wxBitmap(image);
	}

	void AppendLootNodes(
		wxTreeCtrl* tree,
		wxImageList* images,
		std::unordered_map<int, int>& imageIndexes,
		int imageSize,
		const wxTreeItemId& parent,
		const std::vector<MonsterLootEntry>& entries,
		std::vector<std::size_t> path
	) {
		for (std::size_t index = 0; index < entries.size(); ++index) {
			path.push_back(index);
			const int itemId = ResolveLootItemId(entries[index]);
			auto image = imageIndexes.find(itemId);
			if (image == imageIndexes.end()) {
				image = imageIndexes.emplace(itemId, images->Add(ItemBitmap(itemId, imageSize))).first;
			}
			const wxTreeItemId item = tree->AppendItem(parent, LootLabel(entries[index], itemId), image->second, image->second, newd LootTreeData(path));
			AppendLootNodes(tree, images, imageIndexes, imageSize, item, entries[index].children, path);
			path.pop_back();
		}
	}

	wxArrayString MonsterNames() {
		std::vector<std::string> names;
		for (const ServerContentSource& source : g_workspace.getServerContent().entries()) {
			if (source.kind == ServerContentKind::Monster) {
				names.push_back(source.name);
			}
		}
		std::sort(names.begin(), names.end());
		names.erase(std::unique(names.begin(), names.end()), names.end());
		wxArrayString choices;
		for (const std::string& name : names) {
			choices.Add(Utf8(name));
		}
		return choices;
	}
}

void MonsterEditorDialog::addAdvancedPages(wxNotebook* notebook) {
	addAttackPage(notebook);
	auto* defensesPage = newd wxScrolledWindow(notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	defensesPage->SetScrollRate(0, FromDIP(12));
	auto* defensesSizer = newd wxBoxSizer(wxVERTICAL);
	defensesSizer->Add(
		newd wxStaticText(defensesPage, wxID_ANY, "Healing and support actions. Armor and base defense remain on Main."),
		0,
		wxBOTTOM,
		FromDIP(8)
	);
	defenseList = newd wxListCtrl(defensesPage, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	AddColumn(defenseList, 0, "Name", FromDIP(120));
	AddColumn(defenseList, 1, "Type", FromDIP(150));
	AddColumn(defenseList, 2, "Interval", FromDIP(75));
	AddColumn(defenseList, 3, "Chance", FromDIP(65));
	AddColumn(defenseList, 4, "Min", FromDIP(65));
	AddColumn(defenseList, 5, "Max", FromDIP(65));
	AddColumn(defenseList, 6, "Effect", FromDIP(145));
	AddColumn(defenseList, 7, "Target", FromDIP(60));
	defensesSizer->Add(defenseList, 1, wxEXPAND | wxBOTTOM, FromDIP(8));
	defensesSizer->Add(
		ActionButtons(
			defensesPage,
			[this]() {
				MonsterDefenseAction action;
				if (editDefense(action)) {
					edited.defenseActions.push_back(std::move(action));
					refreshDefenseList();
				}
			},
			[this]() {
				const long selected = ListSelection(defenseList);
				if (selected >= 0 && editDefense(edited.defenseActions[static_cast<std::size_t>(selected)])) {
					refreshDefenseList();
					defenseList->SetItemState(selected, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
				}
			},
			[this]() {
				const long selected = ListSelection(defenseList);
				if (selected >= 0) {
					edited.defenseActions.erase(edited.defenseActions.begin() + selected);
					refreshDefenseList();
				}
			},
			[this]() {
				moveSelected(defenseList, edited.defenseActions.size(), true, [this](std::size_t left, std::size_t right) {
					std::swap(edited.defenseActions[left], edited.defenseActions[right]);
				});
				refreshDefenseList();
			},
			[this]() {
				moveSelected(defenseList, edited.defenseActions.size(), false, [this](std::size_t left, std::size_t right) {
					std::swap(edited.defenseActions[left], edited.defenseActions[right]);
				});
				refreshDefenseList();
			}
		),
		0,
		wxEXPAND
	);
	defensesPage->SetSizer(defensesSizer);
	notebook->AddPage(defensesPage, "Defenses");
	applySectionCapability(defensesPage, MonsterSection::Defenses);

	auto* resistancePage = newd wxScrolledWindow(notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	resistancePage->SetScrollRate(0, FromDIP(12));
	auto* resistanceSizer = newd wxBoxSizer(wxVERTICAL);
	auto* split = newd wxBoxSizer(wxHORIZONTAL);
	auto* resistanceBox = newd wxStaticBoxSizer(wxVERTICAL, resistancePage, "Elements");
	resistanceList = newd wxListCtrl(resistanceBox->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	AddColumn(resistanceList, 0, "Type", FromDIP(170));
	AddColumn(resistanceList, 1, "Percent", FromDIP(90));
	resistanceBox->Add(resistanceList, 1, wxEXPAND | wxBOTTOM, FromDIP(8));
	resistanceBox->Add(
		ActionButtons(
			resistanceBox->GetStaticBox(),
			[this]() {
				MonsterResistance resistance;
				if (editResistance(resistance)) {
					edited.resistances.push_back(std::move(resistance));
					refreshResistanceLists();
				}
			},
			[this]() {
				const long selected = ListSelection(resistanceList);
				if (selected >= 0 && editResistance(edited.resistances[static_cast<std::size_t>(selected)])) {
					refreshResistanceLists();
				}
			},
			[this]() {
				const long selected = ListSelection(resistanceList);
				if (selected >= 0) {
					edited.resistances.erase(edited.resistances.begin() + selected);
					refreshResistanceLists();
				}
			},
			[this]() {
				moveSelected(resistanceList, edited.resistances.size(), true, [this](std::size_t left, std::size_t right) {
					std::swap(edited.resistances[left], edited.resistances[right]);
				});
				refreshResistanceLists();
			},
			[this]() {
				moveSelected(resistanceList, edited.resistances.size(), false, [this](std::size_t left, std::size_t right) {
					std::swap(edited.resistances[left], edited.resistances[right]);
				});
				refreshResistanceLists();
			}
		),
		0,
		wxEXPAND
	);
	split->Add(resistanceBox, 1, wxEXPAND | wxRIGHT, FromDIP(8));

	auto* immunityBox = newd wxStaticBoxSizer(wxVERTICAL, resistancePage, "Immunities");
	immunityList = newd wxListCtrl(immunityBox->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	AddColumn(immunityList, 0, "Type", FromDIP(150));
	AddColumn(immunityList, 1, "Combat", FromDIP(70));
	AddColumn(immunityList, 2, "Condition", FromDIP(80));
	immunityBox->Add(immunityList, 1, wxEXPAND | wxBOTTOM, FromDIP(8));
	immunityBox->Add(
		ActionButtons(
			immunityBox->GetStaticBox(),
			[this]() {
				MonsterImmunity immunity;
				if (editImmunity(immunity)) {
					edited.immunities.push_back(std::move(immunity));
					refreshResistanceLists();
				}
			},
			[this]() {
				const long selected = ListSelection(immunityList);
				if (selected >= 0 && editImmunity(edited.immunities[static_cast<std::size_t>(selected)])) {
					refreshResistanceLists();
				}
			},
			[this]() {
				const long selected = ListSelection(immunityList);
				if (selected >= 0) {
					edited.immunities.erase(edited.immunities.begin() + selected);
					refreshResistanceLists();
				}
			},
			[this]() {
				moveSelected(immunityList, edited.immunities.size(), true, [this](std::size_t left, std::size_t right) {
					std::swap(edited.immunities[left], edited.immunities[right]);
				});
				refreshResistanceLists();
			},
			[this]() {
				moveSelected(immunityList, edited.immunities.size(), false, [this](std::size_t left, std::size_t right) {
					std::swap(edited.immunities[left], edited.immunities[right]);
				});
				refreshResistanceLists();
			}
		),
		0,
		wxEXPAND
	);
	split->Add(immunityBox, 1, wxEXPAND);
	resistanceSizer->Add(split, 1, wxEXPAND);
	resistancePage->SetSizer(resistanceSizer);
	notebook->AddPage(resistancePage, "Resistances");
	applySectionCapability(resistanceBox->GetStaticBox(), MonsterSection::Resistances);
	applySectionCapability(immunityBox->GetStaticBox(), MonsterSection::Immunities);

	auto* lootPage = newd wxScrolledWindow(notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	lootPage->SetScrollRate(0, FromDIP(12));
	auto* lootSizer = newd wxBoxSizer(wxVERTICAL);
	lootSizer->Add(
		newd wxStaticText(lootPage, wxID_ANY, "Container contents are shown as child rows. Item search uses the active client and server item database."),
		0,
		wxBOTTOM,
		FromDIP(8)
	);
	lootTree = newd wxTreeCtrl(lootPage, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_SINGLE);
	lootTree->SetToolTip("Double-click an item to replace it or edit its loot values.");
	lootTree->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent& event) {
		auto* data = dynamic_cast<LootTreeData*>(lootTree->GetItemData(event.GetItem()));
		MonsterLootEntry* entry = data ? LootAt(edited.loot, data->path) : nullptr;
		if (entry && editLoot(*entry)) {
			refreshLootTree();
		}
	});
	lootSizer->Add(lootTree, 1, wxEXPAND | wxBOTTOM, FromDIP(8));
	auto* lootButtons = newd wxBoxSizer(wxHORIZONTAL);
	const auto lootButton = [&](const wxString& label, const std::function<void()>& action) {
		auto* button = newd wxButton(lootPage, wxID_ANY, label);
		button->Bind(wxEVT_BUTTON, [action](wxCommandEvent&) { action(); });
		lootButtons->Add(button, 0, wxRIGHT, FromDIP(6));
	};
	lootButton("Add item", [this]() {
		MonsterLootEntry entry;
		if (editLoot(entry)) {
			edited.loot.push_back(std::move(entry));
			refreshLootTree();
		}
	});
	lootButton("Add child", [this]() {
		const wxTreeItemId selected = lootTree->GetSelection();
		auto* data = selected.IsOk() ? dynamic_cast<LootTreeData*>(lootTree->GetItemData(selected)) : nullptr;
		MonsterLootEntry* parent = data ? LootAt(edited.loot, data->path) : nullptr;
		if (!parent) {
			return;
		}
		if (!parent->usesName && parent->itemId > 0 && g_items.typeExists(parent->itemId) && !g_items[parent->itemId].isContainer()) {
			wxMessageBox("The selected active-client item is not a container.", "Monster loot", wxOK | wxICON_INFORMATION, this);
			return;
		}
		MonsterLootEntry child;
		if (editLoot(child)) {
			parent->children.push_back(std::move(child));
			refreshLootTree();
		}
	});
	lootButton("Edit", [this]() {
		const wxTreeItemId selected = lootTree->GetSelection();
		auto* data = selected.IsOk() ? dynamic_cast<LootTreeData*>(lootTree->GetItemData(selected)) : nullptr;
		MonsterLootEntry* entry = data ? LootAt(edited.loot, data->path) : nullptr;
		if (entry && editLoot(*entry)) {
			refreshLootTree();
		}
	});
	lootButton("Remove", [this]() {
		const wxTreeItemId selected = lootTree->GetSelection();
		auto* data = selected.IsOk() ? dynamic_cast<LootTreeData*>(lootTree->GetItemData(selected)) : nullptr;
		if (!data || data->path.empty()) {
			return;
		}
		auto* siblings = LootSiblings(edited.loot, data->path);
		if (siblings && data->path.back() < siblings->size()) {
			siblings->erase(siblings->begin() + static_cast<std::ptrdiff_t>(data->path.back()));
			refreshLootTree();
		}
	});
	lootButtons->AddStretchSpacer();
	lootButton("Up", [this]() {
		const wxTreeItemId selected = lootTree->GetSelection();
		auto* data = selected.IsOk() ? dynamic_cast<LootTreeData*>(lootTree->GetItemData(selected)) : nullptr;
		if (!data || data->path.empty() || data->path.back() == 0) {
			return;
		}
		auto* siblings = LootSiblings(edited.loot, data->path);
		if (siblings) {
			const std::size_t index = data->path.back();
			std::swap((*siblings)[index], (*siblings)[index - 1]);
			refreshLootTree();
		}
	});
	lootButton("Down", [this]() {
		const wxTreeItemId selected = lootTree->GetSelection();
		auto* data = selected.IsOk() ? dynamic_cast<LootTreeData*>(lootTree->GetItemData(selected)) : nullptr;
		if (!data || data->path.empty()) {
			return;
		}
		auto* siblings = LootSiblings(edited.loot, data->path);
		const std::size_t index = data->path.back();
		if (siblings && index + 1 < siblings->size()) {
			std::swap((*siblings)[index], (*siblings)[index + 1]);
			refreshLootTree();
		}
	});
	lootSizer->Add(lootButtons, 0, wxEXPAND);
	lootPage->SetSizer(lootSizer);
	notebook->AddPage(lootPage, "Loot");
	applySectionCapability(lootPage, MonsterSection::Loot);

	auto* summonsPage = newd wxScrolledWindow(notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	summonsPage->SetScrollRate(0, FromDIP(12));
	auto* summonsSizer = newd wxBoxSizer(wxVERTICAL);
	auto* maxRow = newd wxBoxSizer(wxHORIZONTAL);
	maxRow->Add(newd wxStaticText(summonsPage, wxID_ANY, "Maximum active summons"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	maxSummons = newd wxSpinCtrl(summonsPage, wxID_ANY, wxString::Format("%d", edited.maxSummons), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 1000, edited.maxSummons);
	maxRow->Add(maxSummons, 0);
	summonsSizer->Add(maxRow, 0, wxBOTTOM, FromDIP(8));
	summonList = newd wxListCtrl(summonsPage, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	AddColumn(summonList, 0, "Creature", FromDIP(250));
	AddColumn(summonList, 1, "Interval", FromDIP(85));
	AddColumn(summonList, 2, "Chance", FromDIP(75));
	AddColumn(summonList, 3, "Max", FromDIP(60));
	AddColumn(summonList, 4, "Force", FromDIP(65));
	summonsSizer->Add(summonList, 1, wxEXPAND | wxBOTTOM, FromDIP(8));
	summonsSizer->Add(
		ActionButtons(
			summonsPage,
			[this]() {
				MonsterSummon summon;
				if (editSummon(summon)) {
					edited.summons.push_back(std::move(summon));
					refreshSummonList();
				}
			},
			[this]() {
				const long selected = ListSelection(summonList);
				if (selected >= 0 && editSummon(edited.summons[static_cast<std::size_t>(selected)])) {
					refreshSummonList();
				}
			},
			[this]() {
				const long selected = ListSelection(summonList);
				if (selected >= 0) {
					edited.summons.erase(edited.summons.begin() + selected);
					refreshSummonList();
				}
			},
			[this]() {
				moveSelected(summonList, edited.summons.size(), true, [this](std::size_t left, std::size_t right) {
					std::swap(edited.summons[left], edited.summons[right]);
				});
				refreshSummonList();
			},
			[this]() {
				moveSelected(summonList, edited.summons.size(), false, [this](std::size_t left, std::size_t right) {
					std::swap(edited.summons[left], edited.summons[right]);
				});
				refreshSummonList();
			}
		),
		0,
		wxEXPAND
	);
	summonsPage->SetSizer(summonsSizer);
	notebook->AddPage(summonsPage, "Summons");
	applySectionCapability(summonsPage, MonsterSection::Summons);

	auto* voicesPage = newd wxScrolledWindow(notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	voicesPage->SetScrollRate(0, FromDIP(12));
	auto* voicesSizer = newd wxBoxSizer(wxVERTICAL);
	auto* voiceSettings = newd wxFlexGridSizer(2, 8, 12);
	voiceSettings->Add(newd wxStaticText(voicesPage, wxID_ANY, "Interval"), 0, wxALIGN_CENTER_VERTICAL);
	voiceInterval = newd wxSpinCtrl(voicesPage, wxID_ANY, wxString::Format("%d", edited.voices.interval), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 2000000000, edited.voices.interval);
	voiceSettings->Add(voiceInterval);
	voiceSettings->Add(newd wxStaticText(voicesPage, wxID_ANY, "Chance"), 0, wxALIGN_CENTER_VERTICAL);
	voiceChance = newd wxSpinCtrl(voicesPage, wxID_ANY, wxString::Format("%d", edited.voices.chance), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 100000000, edited.voices.chance);
	voiceSettings->Add(voiceChance);
	voicesSizer->Add(voiceSettings, 0, wxBOTTOM, FromDIP(8));
	voiceList = newd wxListCtrl(voicesPage, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	AddColumn(voiceList, 0, "Text", FromDIP(500));
	AddColumn(voiceList, 1, "Yell", FromDIP(70));
	voicesSizer->Add(voiceList, 1, wxEXPAND | wxBOTTOM, FromDIP(8));
	voicesSizer->Add(
		ActionButtons(
			voicesPage,
			[this]() {
				MonsterVoice voice;
				if (editVoice(voice)) {
					edited.voices.entries.push_back(std::move(voice));
					refreshVoiceList();
				}
			},
			[this]() {
				const long selected = ListSelection(voiceList);
				if (selected >= 0 && editVoice(edited.voices.entries[static_cast<std::size_t>(selected)])) {
					refreshVoiceList();
				}
			},
			[this]() {
				const long selected = ListSelection(voiceList);
				if (selected >= 0) {
					edited.voices.entries.erase(edited.voices.entries.begin() + selected);
					refreshVoiceList();
				}
			},
			[this]() {
				moveSelected(voiceList, edited.voices.entries.size(), true, [this](std::size_t left, std::size_t right) {
					std::swap(edited.voices.entries[left], edited.voices.entries[right]);
				});
				refreshVoiceList();
			},
			[this]() {
				moveSelected(voiceList, edited.voices.entries.size(), false, [this](std::size_t left, std::size_t right) {
					std::swap(edited.voices.entries[left], edited.voices.entries[right]);
				});
				refreshVoiceList();
			}
		),
		0,
		wxEXPAND
	);
	voicesPage->SetSizer(voicesSizer);
	notebook->AddPage(voicesPage, "Voices");
	applySectionCapability(voicesPage, MonsterSection::Voices);

	refreshDefenseList();
	refreshResistanceLists();
	refreshLootTree();
	refreshSummonList();
	refreshVoiceList();
}

void MonsterEditorDialog::applySectionCapability(wxWindow* page, MonsterSection section) {
	const MonsterSectionCapability& capability = edited.capability(section);
	page->Enable(capability.editable);
	if (!capability.editable) {
		page->SetToolTip(Utf8(capability.limitation));
	}
}

void MonsterEditorDialog::moveSelected(
	wxListCtrl* list,
	std::size_t size,
	bool up,
	const std::function<void(std::size_t, std::size_t)>& move
) {
	const long selected = ListSelection(list);
	if (selected < 0) {
		return;
	}
	const std::size_t index = static_cast<std::size_t>(selected);
	if ((up && index == 0) || (!up && index + 1 >= size)) {
		return;
	}
	move(index, up ? index - 1 : index + 1);
}

void MonsterEditorDialog::refreshDefenseList() {
	if (!defenseList) {
		return;
	}
	defenseList->DeleteAllItems();
	for (std::size_t index = 0; index < edited.defenseActions.size(); ++index) {
		const MonsterDefenseAction& action = edited.defenseActions[index];
		const long row = defenseList->InsertItem(static_cast<long>(index), Utf8(action.name));
		defenseList->SetItem(row, 1, Utf8(action.type));
		defenseList->SetItem(row, 2, wxString::Format("%d", action.interval));
		defenseList->SetItem(row, 3, wxString::Format("%d", action.chance));
		defenseList->SetItem(row, 4, wxString::Format("%d", action.minDamage));
		defenseList->SetItem(row, 5, wxString::Format("%d", action.maxDamage));
		defenseList->SetItem(row, 6, Utf8(action.effect));
		defenseList->SetItem(row, 7, action.target ? "Yes" : "No");
	}
	scheduleAutosave();
}

void MonsterEditorDialog::refreshResistanceLists() {
	if (resistanceList) {
		resistanceList->DeleteAllItems();
		for (std::size_t index = 0; index < edited.resistances.size(); ++index) {
			const MonsterResistance& resistance = edited.resistances[index];
			const long row = resistanceList->InsertItem(static_cast<long>(index), Utf8(resistance.type));
			resistanceList->SetItem(row, 1, wxString::Format("%d%%", resistance.percent));
		}
	}
	if (immunityList) {
		immunityList->DeleteAllItems();
		for (std::size_t index = 0; index < edited.immunities.size(); ++index) {
			const MonsterImmunity& immunity = edited.immunities[index];
			const long row = immunityList->InsertItem(static_cast<long>(index), Utf8(immunity.type));
			immunityList->SetItem(row, 1, immunity.usesCombat ? (immunity.combat ? "Yes" : "No") : "-");
			immunityList->SetItem(row, 2, immunity.usesCondition ? (immunity.condition ? "Yes" : "No") : "-");
		}
	}
	scheduleAutosave();
}

void MonsterEditorDialog::refreshLootTree() {
	if (!lootTree) {
		return;
	}
	lootTree->DeleteAllItems();
	const int imageSize = FromDIP(36);
	auto* images = newd wxImageList(imageSize, imageSize, true, static_cast<int>(std::max<std::size_t>(1, edited.loot.size())));
	std::unordered_map<int, int> imageIndexes;
	imageIndexes.emplace(0, images->Add(EmptyItemBitmap(imageSize)));
	lootTree->AssignImageList(images);
	const wxTreeItemId root = lootTree->AddRoot(wxString::Format("Loot (%zu entries)", edited.loot.size()), 0, 0);
	AppendLootNodes(lootTree, images, imageIndexes, imageSize, root, edited.loot, {});
	lootTree->ExpandAll();
	scheduleAutosave();
}

void MonsterEditorDialog::refreshSummonList() {
	if (!summonList) {
		return;
	}
	summonList->DeleteAllItems();
	for (std::size_t index = 0; index < edited.summons.size(); ++index) {
		const MonsterSummon& summon = edited.summons[index];
		const long row = summonList->InsertItem(static_cast<long>(index), Utf8(summon.name));
		summonList->SetItem(row, 1, wxString::Format("%d", summon.interval));
		summonList->SetItem(row, 2, wxString::Format("%d", summon.chance));
		summonList->SetItem(row, 3, wxString::Format("%d", summon.max));
		summonList->SetItem(row, 4, summon.force ? "Yes" : "No");
	}
	scheduleAutosave();
}

void MonsterEditorDialog::refreshVoiceList() {
	if (!voiceList) {
		return;
	}
	voiceList->DeleteAllItems();
	for (std::size_t index = 0; index < edited.voices.entries.size(); ++index) {
		const MonsterVoice& voice = edited.voices.entries[index];
		const long row = voiceList->InsertItem(static_cast<long>(index), Utf8(voice.text));
		voiceList->SetItem(row, 1, voice.yell ? "Yes" : "No");
	}
	scheduleAutosave();
}

bool MonsterEditorDialog::editDefense(MonsterDefenseAction& action) {
	RecordDialog dialog(this, "Defense action");
	auto* name = dialog.text("Name", action.name);
	auto* type = dialog.text("Type / combat", action.type);
	auto* interval = dialog.number("Interval", action.interval, 0, 2000000000);
	auto* chance = dialog.number("Chance", action.chance, 0, 100000000);
	auto* minimum = dialog.number("Minimum", action.minDamage, std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
	auto* maximum = dialog.number("Maximum", action.maxDamage, std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
	auto* effect = dialog.text("Effect", action.effect);
	auto* target = dialog.boolean("Target", action.target);
	auto* custom = dialog.text("Custom key=value", FormatProperties(action.customProperties));
	custom->SetToolTip("One property per line. Existing custom expressions are preserved.");
	dialog.finish(wxSize(520, 410));
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}
	action.name = Narrow(name->GetValue());
	action.type = Narrow(type->GetValue());
	action.interval = interval->GetValue();
	action.chance = chance->GetValue();
	action.minDamage = minimum->GetValue();
	action.maxDamage = maximum->GetValue();
	action.effect = Narrow(effect->GetValue());
	action.target = target->GetValue();
	action.customProperties = ParseProperties(custom->GetValue(), document->source().format == ServerContentFormat::Lua);
	return true;
}

bool MonsterEditorDialog::editResistance(MonsterResistance& resistance) {
	RecordDialog dialog(this, "Element resistance");
	auto* type = dialog.text("Type", resistance.type);
	type->SetToolTip("Examples: fire for XML, COMBAT_FIREDAMAGE for Lua.");
	auto* percent = dialog.number("Percent", resistance.percent, -100, 100);
	auto* custom = dialog.text("Custom key=value", FormatProperties(resistance.customProperties));
	dialog.finish();
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}
	resistance.type = Narrow(type->GetValue());
	resistance.percent = percent->GetValue();
	resistance.customProperties = ParseProperties(custom->GetValue(), document->source().format == ServerContentFormat::Lua);
	return true;
}

bool MonsterEditorDialog::editImmunity(MonsterImmunity& immunity) {
	RecordDialog dialog(this, "Immunity");
	auto* type = dialog.text("Type", immunity.type);
	auto* combat = dialog.boolean("Combat immunity", immunity.combat);
	auto* condition = dialog.boolean("Condition immunity", immunity.condition);
	auto* custom = dialog.text("Custom key=value", FormatProperties(immunity.customProperties));
	dialog.finish();
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}
	immunity.type = Narrow(type->GetValue());
	immunity.combat = combat->GetValue();
	immunity.condition = condition->GetValue();
	immunity.usesCombat = document->source().format == ServerContentFormat::Lua;
	immunity.usesCondition = document->source().format == ServerContentFormat::Lua;
	immunity.customProperties = ParseProperties(custom->GetValue(), document->source().format == ServerContentFormat::Lua);
	return true;
}

bool MonsterEditorDialog::editLoot(MonsterLootEntry& entry) {
	wxDialog dialog(this, wxID_ANY, "Loot item", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	auto* root = newd wxBoxSizer(wxVERTICAL);
	auto* previewRow = newd wxBoxSizer(wxHORIZONTAL);
	const int previewSize = dialog.FromDIP(64);
	auto* preview = newd wxStaticBitmap(&dialog, wxID_ANY, ItemBitmap(ResolveLootItemId(entry), previewSize));
	auto* previewText = newd wxStaticText(&dialog, wxID_ANY, "Select an item from the active client.");
	previewRow->Add(preview, 0, wxRIGHT, dialog.FromDIP(12));
	previewRow->Add(previewText, 1, wxALIGN_CENTER_VERTICAL);
	root->Add(previewRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(12));
	auto* grid = newd wxFlexGridSizer(2, 8, 12);
	grid->AddGrowableCol(1, 1);
	grid->Add(newd wxStaticText(&dialog, wxID_ANY, "Server item ID"), 0, wxALIGN_CENTER_VERTICAL);
	auto* id = newd wxSpinCtrl(
		&dialog,
		wxID_ANY,
		wxString::Format("%d", ResolveLootItemId(entry)),
		wxDefaultPosition,
		wxDefaultSize,
		wxSP_ARROW_KEYS,
		0,
		std::numeric_limits<uint16_t>::max(),
		ResolveLootItemId(entry)
	);
	grid->Add(id, 1, wxEXPAND);
	grid->Add(newd wxStaticText(&dialog, wxID_ANY, "Item name"), 0, wxALIGN_CENTER_VERTICAL);
	auto* name = newd wxTextCtrl(&dialog, wxID_ANY, Utf8(entry.itemName));
	grid->Add(name, 1, wxEXPAND);
	grid->Add(newd wxStaticText(&dialog, wxID_ANY, "Store by name"), 0, wxALIGN_CENTER_VERTICAL);
	auto* useName = newd wxCheckBox(&dialog, wxID_ANY, "Use the item name in server source");
	useName->SetValue(entry.usesName);
	grid->Add(useName, 1, wxEXPAND);
	auto addNumber = [&](const wxString& label, int value, int minimum, int maximum) {
		grid->Add(newd wxStaticText(&dialog, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
		auto* control = newd wxSpinCtrl(&dialog, wxID_ANY, wxString::Format("%d", value), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, minimum, maximum, value);
		grid->Add(control, 1, wxEXPAND);
		return control;
	};
	auto* chance = addNumber("Chance", entry.chance, 0, 100000000);
	auto* maxCount = addNumber("Maximum count", entry.maxCount, 1, 1000000);
	auto* subtype = addNumber("Subtype", entry.subtype, 0, 1000000);
	auto* actionId = addNumber("Action ID", entry.actionId, 0, 2000000000);
	grid->Add(newd wxStaticText(&dialog, wxID_ANY, "Text"), 0, wxALIGN_CENTER_VERTICAL);
	auto* text = newd wxTextCtrl(&dialog, wxID_ANY, Utf8(entry.text));
	grid->Add(text, 1, wxEXPAND);
	grid->Add(newd wxStaticText(&dialog, wxID_ANY, "Custom key=value"), 0, wxALIGN_CENTER_VERTICAL);
	auto* custom = newd wxTextCtrl(&dialog, wxID_ANY, Utf8(FormatProperties(entry.customProperties)));
	grid->Add(custom, 1, wxEXPAND);
	root->Add(grid, 1, wxEXPAND | wxALL, dialog.FromDIP(12));
	auto* choose = newd wxButton(&dialog, wxID_ANY, "Choose from active items...");
	const auto updatePreview = [&]() {
		const int selected = id->GetValue();
		preview->SetBitmap(ItemBitmap(selected, previewSize));
		if (selected > 0 && g_items.typeExists(selected)) {
			previewText->SetLabel(
				wxString::Format(
					"%s\nServer ID %d  |  Client ID %u",
					Utf8(g_items[selected].name),
					selected,
					g_items[selected].clientID
				)
			);
		} else {
			previewText->SetLabel("This item does not exist in the active client/server item database.");
		}
		previewRow->Layout();
	};
	id->Bind(wxEVT_SPINCTRL, [updatePreview](wxCommandEvent&) { updatePreview(); });
	id->Bind(wxEVT_TEXT, [updatePreview](wxCommandEvent&) { updatePreview(); });
	choose->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
		FindItemDialog chooser(&dialog, "Choose loot item");
		chooser.setSearchMode(FindItemDialog::ServerIDs);
		if (chooser.ShowModal() == wxID_OK) {
			const int selected = chooser.getResultID();
			id->SetValue(selected);
			if (selected > 0 && g_items.typeExists(selected)) {
				name->SetValue(Utf8(g_items[selected].name));
				choose->SetToolTip(
					wxString::Format(
						"Server ID %d | Client ID %u | %s",
						selected,
						g_items[selected].clientID,
						Utf8(g_items[selected].name)
					)
				);
			}
			updatePreview();
		}
	});
	root->Add(choose, 0, wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(12));
	root->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(12));
	dialog.SetSizerAndFit(root);
	dialog.SetMinSize(dialog.FromDIP(wxSize(520, 500)));
	dialog.CentreOnParent();
	updatePreview();
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}
	entry.itemId = id->GetValue();
	entry.itemName = Narrow(name->GetValue());
	entry.usesName = useName->GetValue();
	entry.chance = chance->GetValue();
	entry.maxCount = maxCount->GetValue();
	entry.subtype = subtype->GetValue();
	entry.actionId = actionId->GetValue();
	entry.text = Narrow(text->GetValue());
	entry.customProperties = ParseProperties(custom->GetValue(), document->source().format == ServerContentFormat::Lua);
	return true;
}

bool MonsterEditorDialog::editSummon(MonsterSummon& summon) {
	RecordDialog dialog(this, "Summoned creature");
	auto* name = dialog.combo("Creature", summon.name, MonsterNames());
	name->SetToolTip("Suggestions come from the active workspace ServerContentIndex.");
	auto* interval = dialog.number("Interval", summon.interval, 0, 2000000000);
	auto* chance = dialog.number("Chance", summon.chance, 0, 100000000);
	auto* maximum = dialog.number("Max", summon.max, 0, 1000);
	auto* force = dialog.boolean("Force", summon.force);
	auto* custom = dialog.text("Custom key=value", FormatProperties(summon.customProperties));
	dialog.finish(wxSize(500, 350));
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}
	summon.name = Narrow(name->GetValue());
	summon.interval = interval->GetValue();
	summon.chance = chance->GetValue();
	summon.max = maximum->GetValue();
	summon.force = force->GetValue();
	summon.customProperties = ParseProperties(custom->GetValue(), document->source().format == ServerContentFormat::Lua);
	return true;
}

bool MonsterEditorDialog::editVoice(MonsterVoice& voice) {
	RecordDialog dialog(this, "Monster voice");
	auto* text = dialog.text("Text", voice.text);
	auto* yell = dialog.boolean("Yell", voice.yell);
	auto* custom = dialog.text("Custom key=value", FormatProperties(voice.customProperties));
	dialog.finish();
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}
	voice.text = Narrow(text->GetValue());
	voice.yell = yell->GetValue();
	voice.customProperties = ParseProperties(custom->GetValue(), document->source().format == ServerContentFormat::Lua);
	return true;
}
