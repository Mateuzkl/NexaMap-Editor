//////////////////////////////////////////////////////////////////////
// Attack/spell page for the native source-preserving monster editor.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "monster_editor_dialog.h"

#include "monster_spell_area.h"
#include "monster_spell_preview.h"
#include "spell_area_editor_dialog.h"
#include "workspace_session.h"

#include <algorithm>
#include <limits>
#include <sstream>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/spinctrl.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tokenzr.h>

namespace {
	wxString Utf8(const std::string& value) {
		return wxString::FromUTF8(value);
	}
	std::string Narrow(const wxString& value) {
		return value.ToStdString(wxConvUTF8);
	}

	wxString FormatProperties(const std::vector<MonsterCustomProperty>& properties) {
		wxString value;
		for (const MonsterCustomProperty& property : properties) {
			if (!value.empty()) {
				value += "\n";
			}
			value += Utf8(property.name + "=" + property.value);
		}
		return value;
	}

	std::vector<MonsterCustomProperty> ParseProperties(const wxString& source, bool rawValues) {
		std::vector<MonsterCustomProperty> properties;
		wxStringTokenizer lines(source, "\n", wxTOKEN_STRTOK);
		while (lines.HasMoreTokens()) {
			const wxString line = lines.GetNextToken();
			const int separator = line.Find('=');
			if (separator <= 0) {
				continue;
			}
			const wxString name = line.Left(separator).Strip(wxString::both);
			const wxString value = line.Mid(separator + 1).Strip(wxString::both);
			if (!name.empty()) {
				properties.push_back({ Narrow(name), Narrow(value), rawValues });
			}
		}
		return properties;
	}

	wxArrayString Values(std::initializer_list<const char*> values) {
		wxArrayString result;
		for (const char* value : values) {
			result.Add(value);
		}
		return result;
	}
}

void MonsterEditorDialog::addAttackPage(wxNotebook* notebook) {
	auto* page = newd wxPanel(notebook);
	auto* root = newd wxBoxSizer(wxVERTICAL);
	root->Add(newd wxStaticText(page, wxID_ANY, "Attacks and spells are normalized from XML or Lua. Unknown fields remain preserved in the source."), 0, wxEXPAND | wxALL, FromDIP(10));
	auto* splitter = newd wxSplitterWindow(page, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);
	splitter->SetMinimumPaneSize(FromDIP(210));
	splitter->SetSashGravity(0.66);
	auto* leftPanel = newd wxPanel(splitter);
	auto* rightPanel = newd wxPanel(splitter);
	auto* left = newd wxBoxSizer(wxVERTICAL);
	attackList = newd wxListCtrl(leftPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	attackList->AppendColumn("Attack", wxLIST_FORMAT_LEFT, FromDIP(120));
	attackList->AppendColumn("Combat", wxLIST_FORMAT_LEFT, FromDIP(145));
	attackList->AppendColumn("Interval", wxLIST_FORMAT_RIGHT, FromDIP(72));
	attackList->AppendColumn("Chance", wxLIST_FORMAT_RIGHT, FromDIP(65));
	attackList->AppendColumn("Damage", wxLIST_FORMAT_LEFT, FromDIP(105));
	attackList->AppendColumn("Area", wxLIST_FORMAT_LEFT, FromDIP(135));
	attackList->AppendColumn("Effect", wxLIST_FORMAT_LEFT, FromDIP(145));
	attackList->AppendColumn("Projectile", wxLIST_FORMAT_LEFT, FromDIP(145));
	left->Add(attackList, 1, wxEXPAND);
	leftPanel->SetSizer(left);

	auto* right = newd wxBoxSizer(wxVERTICAL);
	right->Add(newd wxStaticText(rightPanel, wxID_ANY, "Visual preview"), 0, wxBOTTOM, FromDIP(5));
	attackDirection = newd wxChoice(rightPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, Values({ "North", "East", "South", "West" }));
	attackDirection->SetSelection(0);
	right->Add(attackDirection, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	attackPreview = newd MonsterSpellPreview(rightPanel);
	attackPreview->SetMinSize(FromDIP(wxSize(210, 260)));
	right->Add(attackPreview, 1, wxEXPAND);
	rightPanel->SetSizer(right);
	splitter->SplitVertically(leftPanel, rightPanel, FromDIP(500));
	root->Add(splitter, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));

	auto* buttons = newd wxBoxSizer(wxHORIZONTAL);
	const auto addButton = [&](const wxString& label, const std::function<void()>& action) {
		auto* button = newd wxButton(page, wxID_ANY, label);
		button->Bind(wxEVT_BUTTON, [action](wxCommandEvent&) { action(); });
		buttons->Add(button, 0, wxRIGHT, FromDIP(6));
	};
	addButton("Add", [this]() { MonsterAttackDefinition attack; if (editAttack(attack)) { edited.attacks.push_back(std::move(attack)); refreshAttackList(); } });
	addButton("Edit", [this]() { const long row = attackList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED); if (row >= 0 && editAttack(edited.attacks[static_cast<std::size_t>(row)])){ refreshAttackList();
} });
	addButton("Remove", [this]() { const long row = attackList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED); if (row >= 0) { edited.attacks.erase(edited.attacks.begin() + row); refreshAttackList(); } });
	addButton("Up", [this]() { moveSelected(attackList, edited.attacks.size(), true, [this](std::size_t a, std::size_t b) { std::swap(edited.attacks[a], edited.attacks[b]); refreshAttackList(); }); });
	addButton("Down", [this]() { moveSelected(attackList, edited.attacks.size(), false, [this](std::size_t a, std::size_t b) { std::swap(edited.attacks[a], edited.attacks[b]); refreshAttackList(); }); });
	root->Add(buttons, 0, wxEXPAND | wxALL, FromDIP(10));
	page->SetSizer(root);
	notebook->AddPage(page, "Attacks");
	applySectionCapability(page, MonsterSection::Attacks);

	attackList->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { refreshAttackPreview(); });
	attackList->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) { const std::size_t row = static_cast<std::size_t>(event.GetIndex()); if (row < edited.attacks.size() && editAttack(edited.attacks[row])){ refreshAttackList();
} });
	attackDirection->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { if (attackPreview){ attackPreview->SetDirection(attackDirection->GetSelection());
} });
	refreshAttackList();
}

void MonsterEditorDialog::refreshAttackList() {
	if (!attackList) {
		return;
	}
	const long previous = attackList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	attackList->DeleteAllItems();
	for (std::size_t index = 0; index < edited.attacks.size(); ++index) {
		const MonsterAttackDefinition& attack = edited.attacks[index];
		const long row = attackList->InsertItem(static_cast<long>(index), Utf8(attack.name.empty() ? attack.type : attack.name));
		attackList->SetItem(row, 1, Utf8(attack.type));
		attackList->SetItem(row, 2, wxString::Format("%d", attack.interval));
		attackList->SetItem(row, 3, wxString::Format("%d%%", attack.chance));
		attackList->SetItem(row, 4, wxString::Format("%d to %d", attack.minDamage, attack.maxDamage));
		attackList->SetItem(row, 5, Utf8(DescribeMonsterArea(attack.area)));
		attackList->SetItem(row, 6, Utf8(attack.effect));
		attackList->SetItem(row, 7, Utf8(attack.projectile));
	}
	if (!edited.attacks.empty()) {
		const long selected = std::clamp<long>(previous < 0 ? 0 : previous, 0, static_cast<long>(edited.attacks.size() - 1));
		attackList->SetItemState(selected, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
		attackList->EnsureVisible(selected);
	}
	refreshAttackPreview();
	scheduleAutosave();
}

void MonsterEditorDialog::refreshAttackPreview() {
	if (!attackPreview) {
		return;
	}
	const long row = attackList ? attackList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) : -1;
	attackPreview->SetDirection(attackDirection ? attackDirection->GetSelection() : 0);
	attackPreview->SetAttack(row >= 0 && static_cast<std::size_t>(row) < edited.attacks.size() ? &edited.attacks[static_cast<std::size_t>(row)] : nullptr);
}

bool MonsterEditorDialog::editAttack(MonsterAttackDefinition& attack) {
	wxDialog dialog(this, wxID_ANY, "Attack / spell", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	auto* root = newd wxBoxSizer(wxVERTICAL);
	auto* grid = newd wxFlexGridSizer(4, 8, 10);
	grid->AddGrowableCol(1, 1);
	grid->AddGrowableCol(3, 1);
	const auto text = [&](const wxString& label, const std::string& value, const wxArrayString& choices = {}) {
		grid->Add(newd wxStaticText(&dialog, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
		auto* control = newd wxComboBox(&dialog, wxID_ANY, Utf8(value), wxDefaultPosition, wxDefaultSize, choices);
		grid->Add(control, 1, wxEXPAND);
		return control;
	};
	const auto number = [&](const wxString& label, int value, int minimum = 0, int maximum = 2000000000) {
		grid->Add(newd wxStaticText(&dialog, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
		auto* control = newd wxSpinCtrl(&dialog, wxID_ANY, wxString::Format("%d", value), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, minimum, maximum, value);
		grid->Add(control, 1, wxEXPAND);
		return control;
	};
	auto* name = text("Attack", attack.name, Values({ "melee", "combat", "condition", "speed", "outfit", "invisible", "drunk" }));
	auto* type = text("Combat type", attack.type, Values({ "physical", "fire", "earth", "energy", "ice", "holy", "death", "lifedrain", "manadrain", "healing", "COMBAT_PHYSICALDAMAGE", "COMBAT_FIREDAMAGE", "COMBAT_EARTHDAMAGE", "COMBAT_ENERGYDAMAGE", "COMBAT_ICEDAMAGE", "COMBAT_HOLYDAMAGE", "COMBAT_DEATHDAMAGE", "COMBAT_HEALING" }));
	auto* interval = number("Interval (ms)", attack.interval);
	auto* chance = number("Chance (%)", attack.chance, 0, 100000000);
	auto* minimum = number("Minimum", attack.minDamage, std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
	auto* maximum = number("Maximum", attack.maxDamage, std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
	auto* skill = number("Skill", attack.skill);
	auto* attackValue = number("Attack value", attack.attack);
	auto* range = number("Range", attack.area.range, 0, 1000);
	auto* radius = number("Radius", attack.area.radius, 0, 1000);
	auto* ring = number("Ring", attack.area.ring, 0, 1000);
	auto* length = number("Beam length", attack.area.length, 0, 1000);
	auto* spread = number("Beam spread", attack.area.spread, 0, 1000);
	grid->Add(newd wxStaticText(&dialog, wxID_ANY, "Target"), 0, wxALIGN_CENTER_VERTICAL);
	auto* target = newd wxCheckBox(&dialog, wxID_ANY, "Centered on selected target");
	target->SetValue(attack.area.target);
	grid->Add(target, 1, wxEXPAND);
	auto* effect = text("Impact effect", attack.effect, Values({ "none", "firearea", "energyarea", "poff", "explosionarea", "icearea", "holyarea", "CONST_ME_NONE", "CONST_ME_FIREAREA", "CONST_ME_ENERGYAREA", "CONST_ME_POFF", "CONST_ME_EXPLOSIONAREA", "CONST_ME_ICEAREA", "CONST_ME_HOLYAREA" }));
	auto* projectile = text("Projectile", attack.projectile, Values({ "none", "fire", "energy", "poison", "suddendeath", "ice", "holy", "CONST_ANI_NONE", "CONST_ANI_FIRE", "CONST_ANI_ENERGY", "CONST_ANI_POISON", "CONST_ANI_SUDDENDEATH", "CONST_ANI_ICE", "CONST_ANI_HOLY" }));
	root->Add(grid, 0, wxEXPAND | wxALL, dialog.FromDIP(12));
	auto* reusableArea = newd wxButton(&dialog, wxID_ANY, "Create Reusable Spell Area...");
	reusableArea->SetToolTip("Creates a validated AREA_* matrix in the active server library. Registered monster spell scripts can use it without changing this server's inline attack format.");
	root->Add(reusableArea, 0, wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(12));
	reusableArea->Bind(wxEVT_BUTTON, [&dialog](wxCommandEvent&) {
		auto resolver = g_workspace.getSpellAreaResolver();
		SpellAreaEditorDialog areaDialog(&dialog, g_workspace.getServer(), resolver);
		if (areaDialog.ShowModal() != wxID_OK) {
			return;
		}
		const SpellAreaLibrarySaveResult& created = areaDialog.saveResult();
		wxString refreshError;
		g_workspace.refreshServerContentPaths({ created.path }, refreshError);
		wxMessageBox(
			"Created " + Utf8(created.name) + ". Use this constant in the registered monster spell script, then select that spell in the Attack field.",
			"Reusable area created",
			wxOK | wxICON_INFORMATION,
			&dialog
		);
	});
	root->Add(newd wxStaticText(&dialog, wxID_ANY, "Custom key=value (one per line; Lua expressions remain raw)"), 0, wxLEFT | wxRIGHT, dialog.FromDIP(12));
	auto* custom = newd wxTextCtrl(&dialog, wxID_ANY, FormatProperties(attack.customProperties), wxDefaultPosition, dialog.FromDIP(wxSize(-1, 90)), wxTE_MULTILINE);
	root->Add(custom, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(12));
	root->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(12));
	dialog.SetSizerAndFit(root);
	dialog.SetMinSize(dialog.FromDIP(wxSize(760, 620)));
	dialog.CentreOnParent();
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}
	attack.name = Narrow(name->GetValue());
	attack.type = Narrow(type->GetValue());
	attack.interval = interval->GetValue();
	attack.chance = chance->GetValue();
	attack.minDamage = minimum->GetValue();
	attack.maxDamage = maximum->GetValue();
	attack.skill = skill->GetValue();
	attack.attack = attackValue->GetValue();
	attack.area.range = range->GetValue();
	attack.area.radius = radius->GetValue();
	attack.area.ring = ring->GetValue();
	attack.area.length = length->GetValue();
	attack.area.spread = spread->GetValue();
	attack.area.target = target->GetValue();
	attack.area.shape = ResolveMonsterAreaShape(attack.area);
	attack.effect = Narrow(effect->GetValue());
	attack.projectile = Narrow(projectile->GetValue());
	if (attack.effect == "none") {
		attack.effect.clear();
	}
	if (attack.projectile == "none") {
		attack.projectile.clear();
	}
	attack.customProperties = ParseProperties(custom->GetValue(), document->source().format == ServerContentFormat::Lua);
	return true;
}
