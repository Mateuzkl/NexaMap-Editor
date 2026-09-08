//////////////////////////////////////////////////////////////////////
// Native source-preserving editor for indexed global spells.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "spell_editor_dialog.h"

#include "monster_spell_preview.h"
#include "spell_area_editor_dialog.h"
#include "spell_area_resolver.h"
#include "spell_visual_browser_dialog.h"
#include "server_vocation_catalog.h"
#include "source_compare_dialog.h"
#include "theme.h"
#include "workspace_session.h"

#include <wx/checkbox.h>
#include <wx/checklst.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

#include <algorithm>
#include <cctype>
#include <limits>

namespace {
	constexpr int ID_SPELL_BACK = wxID_HIGHEST + 921;
	constexpr int ID_SPELL_BROWSE = wxID_HIGHEST + 922;

	wxString Utf8(const std::string& value) {
		return wxString::FromUTF8(value);
	}

	std::string Narrow(const wxString& value) {
		return value.ToStdString(wxConvUTF8);
	}

	std::size_t Index(SpellField field) {
		return static_cast<std::size_t>(field);
	}

	bool IsVisualField(SpellField field) {
		return field == SpellField::CombatType || field == SpellField::Effect || field == SpellField::Projectile || field == SpellField::Area || field == SpellField::Range || field == SpellField::NeedTarget || field == SpellField::NeedDirection;
	}

	std::string LowerAscii(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
		return value;
	}

	std::string VocationName(std::string value) {
		if (const auto flag = value.find(';'); flag != std::string::npos) {
			value.resize(flag);
		}
		return LowerAscii(value);
	}

	wxScrolledWindow* Page(wxNotebook* notebook) {
		auto* page = newd wxScrolledWindow(notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
		page->SetScrollRate(0, page->FromDIP(12));
		return page;
	}

	wxFlexGridSizer* Grid(int columns = 2) {
		auto* grid = newd wxFlexGridSizer(columns, 8, 12);
		for (int column = 1; column < columns; column += 2) {
			grid->AddGrowableCol(column, 1);
		}
		return grid;
	}

	wxString PathText(const std::filesystem::path& path) {
#ifdef __WINDOWS__
		return wxString(path.wstring());
#else
		return wxString::FromUTF8(path.string());
#endif
	}
}

SpellEditorDialog::SpellEditorDialog(wxWindow* parent, std::unique_ptr<SpellDefinitionDocument> value) :
	wxDialog(parent, wxID_ANY, "Spell Editor", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	document(std::move(value)), edited(document->definition()), areaResolver(g_workspace.getSpellAreaResolver()), visualCatalog(g_workspace.getServerVisualCatalog()), vocationCatalog(g_workspace.getServerVocations()) {
	SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	auto* root = newd wxBoxSizer(wxVERTICAL);
	auto* header = newd wxPanel(this);
	header->SetBackgroundColour(Theme::Get(Theme::Role::RaisedSurface));
	auto* headerSizer = newd wxBoxSizer(wxVERTICAL);
	auto* title = newd wxStaticText(header, wxID_ANY, Utf8(edited.name));
	wxFont titleFont = title->GetFont();
	titleFont.SetPointSize(titleFont.GetPointSize() + 3);
	titleFont.SetWeight(wxFONTWEIGHT_BOLD);
	title->SetFont(titleFont);
	headerSizer->Add(title, 0, wxBOTTOM, FromDIP(3));
	headerSizer->Add(newd wxStaticText(header, wxID_ANY, Utf8(ServerContentFormatName(document->source().format)) + "  |  " + Utf8(edited.subtype) + "  |  " + PathText(document->source().declarationPath)), 0, wxEXPAND);
	header->SetSizer(headerSizer);
	root->Add(header, 0, wxEXPAND | wxALL, FromDIP(12));

	auto* notebook = newd wxNotebook(this, wxID_ANY);
	auto* mainPage = Page(notebook);
	auto* mainSizer = newd wxBoxSizer(wxVERTICAL);
	auto* identity = newd wxStaticBoxSizer(wxVERTICAL, mainPage, "Identity and registration");
	auto* identityGrid = Grid(4);
	addText(identity->GetStaticBox(), identityGrid, SpellField::Name, edited.name);
	addText(identity->GetStaticBox(), identityGrid, SpellField::Words, edited.words);
	addText(identity->GetStaticBox(), identityGrid, SpellField::Group, edited.group);
	addText(identity->GetStaticBox(), identityGrid, SpellField::Script, edited.script);
	addNumber(identity->GetStaticBox(), identityGrid, SpellField::SpellId, edited.spellId);
	addNumber(identity->GetStaticBox(), identityGrid, SpellField::RuneId, edited.runeId);
	identity->Add(identityGrid, 0, wxEXPAND | wxALL, FromDIP(8));
	mainSizer->Add(identity, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	auto* costs = newd wxStaticBoxSizer(wxVERTICAL, mainPage, "Requirements and cost");
	auto* costGrid = Grid(4);
	addNumber(costs->GetStaticBox(), costGrid, SpellField::Level, edited.level);
	addNumber(costs->GetStaticBox(), costGrid, SpellField::MagicLevel, edited.magicLevel);
	addNumber(costs->GetStaticBox(), costGrid, SpellField::Mana, edited.mana);
	addNumber(costs->GetStaticBox(), costGrid, SpellField::ManaPercent, edited.manaPercent);
	addNumber(costs->GetStaticBox(), costGrid, SpellField::Soul, edited.soul);
	addNumber(costs->GetStaticBox(), costGrid, SpellField::Charges, edited.charges);
	addNumber(costs->GetStaticBox(), costGrid, SpellField::Cooldown, edited.cooldown);
	addNumber(costs->GetStaticBox(), costGrid, SpellField::GroupCooldown, edited.groupCooldown);
	costs->Add(costGrid, 0, wxEXPAND | wxALL, FromDIP(8));
	mainSizer->Add(costs, 0, wxEXPAND);
	mainPage->SetSizer(mainSizer);
	notebook->AddPage(mainPage, "Main");

	auto* targetingPage = Page(notebook);
	auto* targetingSizer = newd wxBoxSizer(wxVERTICAL);
	auto* targeting = newd wxStaticBoxSizer(wxVERTICAL, targetingPage, "Targeting and runtime flags");
	auto* targetingGrid = Grid(4);
	addNumber(targeting->GetStaticBox(), targetingGrid, SpellField::Range, edited.range);
	addBoolean(targeting->GetStaticBox(), targetingGrid, SpellField::Premium, edited.premium);
	addBoolean(targeting->GetStaticBox(), targetingGrid, SpellField::Enabled, edited.enabled);
	addBoolean(targeting->GetStaticBox(), targetingGrid, SpellField::Aggressive, edited.aggressive);
	addBoolean(targeting->GetStaticBox(), targetingGrid, SpellField::NeedTarget, edited.needTarget);
	addBoolean(targeting->GetStaticBox(), targetingGrid, SpellField::NeedDirection, edited.needDirection);
	addBoolean(targeting->GetStaticBox(), targetingGrid, SpellField::BlockWalls, edited.blockWalls);
	addBoolean(targeting->GetStaticBox(), targetingGrid, SpellField::AllowFarUse, edited.allowFarUse);
	addBoolean(targeting->GetStaticBox(), targetingGrid, SpellField::NeedLearn, edited.needLearn);
	addBoolean(targeting->GetStaticBox(), targetingGrid, SpellField::SelfTarget, edited.selfTarget);
	targeting->Add(targetingGrid, 0, wxEXPAND | wxALL, FromDIP(8));
	targetingSizer->Add(targeting, 0, wxEXPAND);
	targetingPage->SetSizer(targetingSizer);
	notebook->AddPage(targetingPage, "Targeting");

	auto* visualPage = Page(notebook);
	auto* visualSizer = newd wxBoxSizer(wxHORIZONTAL);
	auto* visualFields = newd wxStaticBoxSizer(wxVERTICAL, visualPage, "Combat visuals");
	auto* visualGrid = newd wxFlexGridSizer(3, 8, 8);
	visualGrid->AddGrowableCol(1, 1);
	const auto addVisualText = [&](SpellField field, const std::string& value, const wxArrayString& choices, const wxString& buttonLabel, ServerVisualKind kind) {
		visualGrid->Add(newd wxStaticText(visualFields->GetStaticBox(), wxID_ANY, SpellFieldName(field)), 0, wxALIGN_CENTER_VERTICAL);
		auto* control = newd wxComboBox(visualFields->GetStaticBox(), wxID_ANY, Utf8(value), wxDefaultPosition, wxDefaultSize, choices, wxCB_DROPDOWN);
		controls[Index(field)] = control;
		applyCapability(control, field);
		visualGrid->Add(control, 1, wxEXPAND);
		auto* browse = newd wxButton(visualFields->GetStaticBox(), wxID_ANY, buttonLabel);
		browse->Enable(edited.capability(field).editable);
		visualGrid->Add(browse, 0);
		control->Bind(wxEVT_TEXT, [this, field](wxCommandEvent& event) { onFieldChanged(field, event); });
		control->Bind(wxEVT_COMBOBOX, [this, field](wxCommandEvent& event) { onFieldChanged(field, event); });
		browse->Bind(wxEVT_BUTTON, [this, kind](wxCommandEvent&) { chooseVisual(kind); });
	};
	const auto addPlainText = [&](SpellField field, const std::string& value, const wxArrayString& choices) {
		visualGrid->Add(newd wxStaticText(visualFields->GetStaticBox(), wxID_ANY, SpellFieldName(field)), 0, wxALIGN_CENTER_VERTICAL);
		auto* control = newd wxComboBox(visualFields->GetStaticBox(), wxID_ANY, Utf8(value), wxDefaultPosition, wxDefaultSize, choices, wxCB_DROPDOWN);
		controls[Index(field)] = control;
		applyCapability(control, field);
		visualGrid->Add(control, 1, wxEXPAND);
		visualGrid->AddSpacer(1);
		control->Bind(wxEVT_TEXT, [this, field](wxCommandEvent& event) { onFieldChanged(field, event); });
		control->Bind(wxEVT_COMBOBOX, [this, field](wxCommandEvent& event) { onFieldChanged(field, event); });
	};
	addPlainText(SpellField::CombatType, edited.combatType, { "COMBAT_PHYSICALDAMAGE", "COMBAT_ENERGYDAMAGE", "COMBAT_EARTHDAMAGE", "COMBAT_FIREDAMAGE", "COMBAT_LIFEDRAIN", "COMBAT_MANADRAIN", "COMBAT_HEALING", "COMBAT_ICEDAMAGE", "COMBAT_HOLYDAMAGE", "COMBAT_DEATHDAMAGE" });
	wxArrayString effects;
	for (const ServerVisualConstant& entry : visualCatalog->effects()) {
		effects.Add(Utf8(entry.name));
	}
	wxArrayString projectiles;
	for (const ServerVisualConstant& entry : visualCatalog->projectiles()) {
		projectiles.Add(Utf8(entry.name));
	}
	addVisualText(SpellField::Effect, edited.effect, effects, "Browse...", ServerVisualKind::MagicEffect);
	addVisualText(SpellField::Projectile, edited.projectile, projectiles, "Browse...", ServerVisualKind::DistanceEffect);
	visualGrid->Add(newd wxStaticText(visualFields->GetStaticBox(), wxID_ANY, SpellFieldName(SpellField::Area)), 0, wxALIGN_CENTER_VERTICAL);
	auto* areaControl = newd wxComboBox(visualFields->GetStaticBox(), wxID_ANY, Utf8(edited.areaExpression), wxDefaultPosition, wxDefaultSize, wxArrayString { "AREA_CIRCLE2X2", "AREA_CIRCLE3X3", "AREA_SQUARE1X1", "AREA_SQUARE2X2", "AREA_BEAM5", "AREA_WAVE4" }, wxCB_DROPDOWN);
	controls[Index(SpellField::Area)] = areaControl;
	applyCapability(areaControl, SpellField::Area);
	visualGrid->Add(areaControl, 1, wxEXPAND);
	auto* createAreaButton = newd wxButton(visualFields->GetStaticBox(), wxID_ANY, "Create Area...");
	visualGrid->Add(createAreaButton, 0);
	areaControl->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) { onFieldChanged(SpellField::Area, event); });
	areaControl->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& event) { onFieldChanged(SpellField::Area, event); });
	createAreaButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { createReusableArea(); });
	visualGrid->Add(newd wxStaticText(visualFields->GetStaticBox(), wxID_ANY, "Preview direction"), 0, wxALIGN_CENTER_VERTICAL);
	auto* directionChoice = newd wxChoice(visualFields->GetStaticBox(), wxID_ANY);
	directionChoice->Append("North");
	directionChoice->Append("North-East");
	directionChoice->Append("East");
	directionChoice->Append("South-East");
	directionChoice->Append("South");
	directionChoice->Append("South-West");
	directionChoice->Append("West");
	directionChoice->Append("North-West");
	directionChoice->SetSelection(0);
	visualGrid->Add(directionChoice, 1, wxEXPAND);
	visualGrid->AddSpacer(1);
	visualFields->Add(visualGrid, 0, wxEXPAND | wxALL, FromDIP(8));
	auto* playback = newd wxBoxSizer(wxHORIZONTAL);
	auto* play = newd wxButton(visualFields->GetStaticBox(), wxID_ANY, "Play");
	auto* stop = newd wxButton(visualFields->GetStaticBox(), wxID_ANY, "Stop");
	auto* speed = newd wxChoice(visualFields->GetStaticBox(), wxID_ANY);
	speed->Append("Slow");
	speed->Append("Normal");
	speed->Append("Fast");
	speed->SetSelection(1);
	playback->Add(play, 0, wxRIGHT, FromDIP(6));
	playback->Add(stop, 0, wxRIGHT, FromDIP(12));
	playback->Add(newd wxStaticText(visualFields->GetStaticBox(), wxID_ANY, "Animation speed"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	playback->Add(speed, 1);
	visualFields->Add(playback, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	areaStatus = newd wxStaticText(visualFields->GetStaticBox(), wxID_ANY, Utf8(edited.areaStatus));
	areaStatus->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	areaStatus->Wrap(FromDIP(360));
	visualFields->Add(areaStatus, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	visualSizer->Add(visualFields, 1, wxEXPAND | wxRIGHT, FromDIP(10));
	preview = newd MonsterSpellPreview(visualPage);
	visualSizer->Add(preview, 1, wxEXPAND);
	visualPage->SetSizer(visualSizer);
	notebook->AddPage(visualPage, "Visual");
	directionChoice->Bind(wxEVT_CHOICE, [this, directionChoice](wxCommandEvent&) {
		direction = directionChoice->GetSelection();
		schedulePreview();
	});
	play->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { preview->SetPlaying(true); });
	stop->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { preview->SetPlaying(false); });
	speed->Bind(wxEVT_CHOICE, [this, speed](wxCommandEvent&) {
		static constexpr int intervals[] { 250, 140, 75 };
		preview->SetAnimationInterval(intervals[std::clamp(speed->GetSelection(), 0, 2)]);
	});

	auto* vocationPage = Page(notebook);
	auto* vocationSizer = newd wxBoxSizer(wxVERTICAL);
	vocationSizer->Add(newd wxStaticText(vocationPage, wxID_ANY, "Vocations come from the active Server Workspace. Existing custom names and flags remain preserved."), 0, wxALL, FromDIP(10));
	allVocationsCheck = newd wxCheckBox(vocationPage, wxID_ANY, "All vocations (global spell)");
	allVocationsCheck->SetValue(edited.allVocations);
	vocationSizer->Add(allVocationsCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
	vocationList = newd wxCheckListBox(vocationPage, wxID_ANY);
	std::vector<bool> consumed(edited.vocations.size(), false);
	for (const ServerVocation& vocation : *vocationCatalog) {
		const std::string normalized = LowerAscii(vocation.name);
		std::string raw = document->source().format == ServerContentFormat::Lua ? normalized : vocation.name;
		bool checked = false;
		for (std::size_t existing = 0; existing < edited.vocations.size(); ++existing) {
			if (!consumed[existing] && VocationName(edited.vocations[existing]) == normalized) {
				raw = edited.vocations[existing];
				consumed[existing] = true;
				checked = true;
				break;
			}
		}
		const int row = vocationList->Append(Utf8(vocation.name + (vocation.promoted ? "  (promoted)" : "")));
		vocationList->Check(row, checked);
		vocationValues.push_back(std::move(raw));
	}
	for (std::size_t existing = 0; existing < edited.vocations.size(); ++existing) {
		if (!consumed[existing]) {
			const int row = vocationList->Append("Custom: " + Utf8(edited.vocations[existing]));
			vocationList->Check(row, true);
			vocationValues.push_back(edited.vocations[existing]);
		}
	}
	const bool vocationEditable = edited.vocationCapability.editable;
	vocationList->Enable(vocationEditable);
	vocationList->SetToolTip(Utf8(edited.vocationCapability.limitation));
	vocationSizer->Add(vocationList, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
	auto* vocationOptions = newd wxBoxSizer(wxHORIZONTAL);
	showInDescriptionCheck = newd wxCheckBox(vocationPage, wxID_ANY, "Show selected in description");
	showInDescriptionCheck->Enable(document->source().format == ServerContentFormat::Lua && edited.vocationCapability.editable && !edited.allVocations);
	vocationOptions->Add(showInDescriptionCheck, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
	customVocation = newd wxTextCtrl(vocationPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	customVocation->SetHint("Custom vocation name");
	vocationOptions->Add(customVocation, 1, wxRIGHT, FromDIP(6));
	auto* addVocation = newd wxButton(vocationPage, wxID_ADD, "Add");
	auto* removeVocation = newd wxButton(vocationPage, wxID_REMOVE, "Remove selected");
	vocationOptions->Add(addVocation, 0, wxRIGHT, FromDIP(6));
	vocationOptions->Add(removeVocation, 0);
	vocationSizer->Add(vocationOptions, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
	allVocationsCheck->Enable(vocationEditable);
	customVocation->Enable(vocationEditable && !edited.allVocations);
	addVocation->Enable(vocationEditable && !edited.allVocations);
	removeVocation->Enable(vocationEditable && !edited.allVocations);
	allVocationsCheck->Bind(wxEVT_CHECKBOX, [this, addVocation, removeVocation, vocationEditable](wxCommandEvent&) {
		const bool all = allVocationsCheck->GetValue();
		customVocation->Enable(vocationEditable && !all);
		addVocation->Enable(vocationEditable && !all);
		removeVocation->Enable(vocationEditable && !all);
		showInDescriptionCheck->Enable(vocationEditable && !all && document->source().format == ServerContentFormat::Lua);
		if (all) {
			for (unsigned int row = 0; row < vocationList->GetCount(); ++row) {
				vocationList->Check(row, false);
			}
		}
		syncVocations();
	});
	vocationList->Bind(wxEVT_CHECKLISTBOX, [this, addVocation, removeVocation, vocationEditable](wxCommandEvent&) {
		if (allVocationsCheck->GetValue()) {
			allVocationsCheck->SetValue(false);
		}
		syncVocations();
		const bool all = allVocationsCheck->GetValue();
		customVocation->Enable(vocationEditable && !all);
		addVocation->Enable(vocationEditable && !all);
		removeVocation->Enable(vocationEditable && !all);
		refreshVocationControls();
	});
	vocationList->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { refreshVocationControls(); });
	showInDescriptionCheck->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
		const int row = vocationList->GetSelection();
		if (row >= 0 && static_cast<std::size_t>(row) < vocationValues.size()) {
			std::string raw = vocationValues[static_cast<std::size_t>(row)];
			if (const auto separator = raw.find(';'); separator != std::string::npos) {
				raw.resize(separator);
			}
			if (showInDescriptionCheck->GetValue()) {
				raw += ";true";
			}
			vocationValues[static_cast<std::size_t>(row)] = std::move(raw);
			syncVocations();
		}
	});
	const auto addCustom = [this]() {
		const std::string value = Narrow(customVocation->GetValue()).empty() ? std::string() : Narrow(customVocation->GetValue());
		if (value.empty()) {
			return;
		}
		vocationValues.push_back(value);
		const int row = vocationList->Append("Custom: " + Utf8(value));
		vocationList->Check(row, true);
		vocationList->SetSelection(row);
		customVocation->Clear();
		syncVocations();
	};
	addVocation->Bind(wxEVT_BUTTON, [addCustom](wxCommandEvent&) { addCustom(); });
	customVocation->Bind(wxEVT_TEXT_ENTER, [addCustom](wxCommandEvent&) { addCustom(); });
	removeVocation->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		const int row = vocationList->GetSelection();
		if (row < 0) {
			return;
		}
		if (static_cast<std::size_t>(row) < vocationCatalog->size()) {
			vocationList->Check(row, false);
		} else {
			vocationList->Delete(row);
			vocationValues.erase(vocationValues.begin() + row);
		}
		syncVocations();
	});
	vocationPage->SetSizer(vocationSizer);
	notebook->AddPage(vocationPage, "Vocations");

	auto* sourcePage = newd wxPanel(notebook);
	auto* sourceSizer = newd wxBoxSizer(wxVERTICAL);
	auto* sourceNotebook = newd wxNotebook(sourcePage, wxID_ANY);
	auto* declarationPage = newd wxPanel(sourceNotebook);
	auto* declarationSizer = newd wxBoxSizer(wxVERTICAL);
	declarationSizer->Add(newd wxStaticText(declarationPage, wxID_ANY, PathText(document->source().declarationPath)), 0, wxEXPAND | wxALL, FromDIP(6));
	declarationSource = newd wxTextCtrl(declarationPage, wxID_ANY, Utf8(document->declarationText()), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
	declarationSource->SetFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE));
	declarationSizer->Add(declarationSource, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
	declarationPage->SetSizer(declarationSizer);
	sourceNotebook->AddPage(declarationPage, document->source().format == ServerContentFormat::Xml ? "Registration XML" : "Spell Lua");
	if (document->hasSeparateImplementation()) {
		auto* implementationPage = newd wxPanel(sourceNotebook);
		auto* implementationSizer = newd wxBoxSizer(wxVERTICAL);
		implementationSizer->Add(newd wxStaticText(implementationPage, wxID_ANY, PathText(document->implementationPath())), 0, wxEXPAND | wxALL, FromDIP(6));
		implementationSource = newd wxTextCtrl(implementationPage, wxID_ANY, Utf8(document->implementationText()), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
		implementationSource->SetFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE));
		implementationSizer->Add(implementationSource, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
		implementationPage->SetSizer(implementationSizer);
		sourceNotebook->AddPage(implementationPage, "Implementation Lua");
	}
	sourceSizer->Add(sourceNotebook, 1, wxEXPAND);
	sourcePage->SetSizer(sourceSizer);
	notebook->AddPage(sourcePage, "Source");
	root->Add(notebook, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

	auto* bottom = newd wxBoxSizer(wxHORIZONTAL);
	saveState = newd wxStaticText(this, wxID_ANY, "No changes");
	saveState->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	bottom->Add(saveState, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
	compareButton = newd wxButton(this, wxID_ANY, "Compare External...");
	compareButton->Hide();
	compareButton->Bind(wxEVT_BUTTON, &SpellEditorDialog::onCompareExternal, this);
	bottom->Add(compareButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	bottom->Add(newd wxButton(this, ID_SPELL_BACK, "< Back to Spells"), 0, wxRIGHT, FromDIP(6));
	bottom->Add(newd wxButton(this, ID_SPELL_BROWSE, "Browse Spells..."), 0, wxRIGHT, FromDIP(12));
	bottom->Add(newd wxButton(this, wxID_OK, "Save"), 0, wxRIGHT, FromDIP(8));
	bottom->Add(newd wxButton(this, wxID_CANCEL, "Close"), 0);
	root->Add(newd wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
	root->Add(bottom, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
	SetSizer(root);
	SetMinSize(FromDIP(wxSize(790, 610)));
	SetSize(FromDIP(wxSize(980, 760)));
	CentreOnParent();
	Bind(wxEVT_BUTTON, &SpellEditorDialog::onSave, this, wxID_OK);
	Bind(wxEVT_BUTTON, &SpellEditorDialog::onBrowse, this, ID_SPELL_BACK);
	Bind(wxEVT_BUTTON, &SpellEditorDialog::onBrowse, this, ID_SPELL_BROWSE);
	Bind(wxEVT_BUTTON, &SpellEditorDialog::onCloseButton, this, wxID_CANCEL);
	Bind(wxEVT_CLOSE_WINDOW, &SpellEditorDialog::onClose, this);
	autosaveTimer = std::make_unique<wxTimer>(this);
	Bind(wxEVT_TIMER, &SpellEditorDialog::onAutosave, this, autosaveTimer->GetId());
	previewTimer = std::make_unique<wxTimer>(this);
	Bind(wxEVT_TIMER, &SpellEditorDialog::onPreviewTimer, this, previewTimer->GetId());
	sourceWatchTimer = std::make_unique<wxTimer>(this);
	Bind(wxEVT_TIMER, &SpellEditorDialog::onSourceWatch, this, sourceWatchTimer->GetId());
	resetSourceMonitor();
	sourceWatchTimer->Start(1000);
	constructing = false;
	refreshPreview();
}

bool SpellEditorDialog::wasSaved() const {
	return saved;
}
const SpellDefinition& SpellEditorDialog::savedDefinition() const {
	return edited;
}

bool SpellEditorDialog::wantsBrowse() const {
	return browseRequested;
}

wxWindow* SpellEditorDialog::addText(wxWindow* parent, wxFlexGridSizer* grid, SpellField field, const std::string& value, const wxArrayString& choices) {
	grid->Add(newd wxStaticText(parent, wxID_ANY, SpellFieldName(field)), 0, wxALIGN_CENTER_VERTICAL);
	wxWindow* control = nullptr;
	if (choices.empty()) {
		control = newd wxTextCtrl(parent, wxID_ANY, Utf8(value));
	} else {
		control = newd wxComboBox(parent, wxID_ANY, Utf8(value), wxDefaultPosition, wxDefaultSize, choices, wxCB_DROPDOWN);
	}
	controls[Index(field)] = control;
	applyCapability(control, field);
	grid->Add(control, 1, wxEXPAND);
	control->Bind(wxEVT_TEXT, [this, field](wxCommandEvent& event) { onFieldChanged(field, event); });
	control->Bind(wxEVT_COMBOBOX, [this, field](wxCommandEvent& event) { onFieldChanged(field, event); });
	return control;
}

wxWindow* SpellEditorDialog::addNumber(wxWindow* parent, wxFlexGridSizer* grid, SpellField field, int value, int maximum) {
	grid->Add(newd wxStaticText(parent, wxID_ANY, SpellFieldName(field)), 0, wxALIGN_CENTER_VERTICAL);
	auto* control = newd wxSpinCtrl(parent, wxID_ANY, wxString::Format("%d", value), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, maximum, value);
	controls[Index(field)] = control;
	applyCapability(control, field);
	grid->Add(control, 1, wxEXPAND);
	control->Bind(wxEVT_SPINCTRL, [this, field](wxCommandEvent& event) { onFieldChanged(field, event); });
	control->Bind(wxEVT_TEXT, [this, field](wxCommandEvent& event) { onFieldChanged(field, event); });
	return control;
}

wxWindow* SpellEditorDialog::addBoolean(wxWindow* parent, wxFlexGridSizer* grid, SpellField field, bool value) {
	grid->Add(newd wxStaticText(parent, wxID_ANY, SpellFieldName(field)), 0, wxALIGN_CENTER_VERTICAL);
	auto* control = newd wxCheckBox(parent, wxID_ANY, "Enabled");
	control->SetValue(value);
	controls[Index(field)] = control;
	applyCapability(control, field);
	grid->Add(control, 1, wxEXPAND);
	control->Bind(wxEVT_CHECKBOX, [this, field](wxCommandEvent& event) { onFieldChanged(field, event); });
	return control;
}

void SpellEditorDialog::applyCapability(wxWindow* control, SpellField field) {
	const auto& capability = edited.capability(field);
	control->Enable(capability.editable);
	if (!capability.editable && !capability.limitation.empty()) {
		control->SetToolTip(Utf8(capability.limitation));
	}
}

void SpellEditorDialog::createReusableArea() {
	SpellAreaEditorDialog dialog(this, g_workspace.getServer(), areaResolver);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}
	const SpellAreaLibrarySaveResult& created = dialog.saveResult();
	wxString refreshError;
	if (!g_workspace.refreshServerContentPaths({ created.path }, refreshError)) {
		wxMessageBox("The area was saved, but workspace metadata could not be refreshed: " + refreshError, "Area saved", wxOK | wxICON_WARNING, this);
	} else {
		areaResolver = g_workspace.getSpellAreaResolver();
	}
	if (edited.capability(SpellField::Area).editable) {
		if (auto* control = dynamic_cast<wxComboBox*>(controls[Index(SpellField::Area)])) {
			control->ChangeValue(Utf8(created.name));
			edited.areaExpression = created.name;
			autosaveState.changed();
			scheduleAutosave();
			schedulePreview();
		}
	}
	if (areaStatus) {
		areaStatus->SetLabel("Created " + Utf8(created.name) + wxString::Format(" with %zu affected tiles in ", created.affectedTiles) + PathText(created.path));
		areaStatus->Wrap(FromDIP(360));
	}
}

void SpellEditorDialog::syncField(SpellField field) {
	const auto text = [&](std::string& destination) {
		if (auto* control = dynamic_cast<wxTextCtrl*>(controls[Index(field)])) {
			destination = Narrow(control->GetValue());
		} else if (auto* combo = dynamic_cast<wxComboBox*>(controls[Index(field)])) {
			destination = Narrow(combo->GetValue());
		}
	};
	const auto number = [&](int& destination) {
		if (auto* control = dynamic_cast<wxSpinCtrl*>(controls[Index(field)])) {
			destination = control->GetValue();
		}
	};
	const auto boolean = [&](bool& destination) {
		if (auto* control = dynamic_cast<wxCheckBox*>(controls[Index(field)])) {
			destination = control->GetValue();
		}
	};
	switch (field) {
		case SpellField::Name:
			text(edited.name);
			break;
		case SpellField::Words:
			text(edited.words);
			break;
		case SpellField::Group:
			text(edited.group);
			break;
		case SpellField::Script:
			text(edited.script);
			break;
		case SpellField::CombatType:
			text(edited.combatType);
			break;
		case SpellField::Effect:
			text(edited.effect);
			break;
		case SpellField::Projectile:
			text(edited.projectile);
			break;
		case SpellField::Area:
			text(edited.areaExpression);
			break;
		case SpellField::SpellId:
			number(edited.spellId);
			break;
		case SpellField::RuneId:
			number(edited.runeId);
			break;
		case SpellField::Level:
			number(edited.level);
			break;
		case SpellField::MagicLevel:
			number(edited.magicLevel);
			break;
		case SpellField::Mana:
			number(edited.mana);
			break;
		case SpellField::ManaPercent:
			number(edited.manaPercent);
			break;
		case SpellField::Soul:
			number(edited.soul);
			break;
		case SpellField::Cooldown:
			number(edited.cooldown);
			break;
		case SpellField::GroupCooldown:
			number(edited.groupCooldown);
			break;
		case SpellField::Range:
			number(edited.range);
			break;
		case SpellField::Charges:
			number(edited.charges);
			break;
		case SpellField::Premium:
			boolean(edited.premium);
			break;
		case SpellField::Enabled:
			boolean(edited.enabled);
			break;
		case SpellField::Aggressive:
			boolean(edited.aggressive);
			break;
		case SpellField::NeedTarget:
			boolean(edited.needTarget);
			break;
		case SpellField::NeedDirection:
			boolean(edited.needDirection);
			break;
		case SpellField::BlockWalls:
			boolean(edited.blockWalls);
			break;
		case SpellField::AllowFarUse:
			boolean(edited.allowFarUse);
			break;
		case SpellField::NeedLearn:
			boolean(edited.needLearn);
			break;
		case SpellField::SelfTarget:
			boolean(edited.selfTarget);
			break;
		case SpellField::Count:
			break;
	}
}

void SpellEditorDialog::readControls() {
	const auto text = [&](SpellField field, std::string& destination) {
		if (auto* control = dynamic_cast<wxTextCtrl*>(controls[Index(field)])) {
			destination = Narrow(control->GetValue());
		} else if (auto* combo = dynamic_cast<wxComboBox*>(controls[Index(field)])) {
			destination = Narrow(combo->GetValue());
		}
	};
	const auto number = [&](SpellField field, int& destination) { if (auto* control = dynamic_cast<wxSpinCtrl*>(controls[Index(field)])){ destination = control->GetValue();
} };
	const auto boolean = [&](SpellField field, bool& destination) { if (auto* control = dynamic_cast<wxCheckBox*>(controls[Index(field)])){ destination = control->GetValue();
} };
	text(SpellField::Name, edited.name);
	text(SpellField::Words, edited.words);
	text(SpellField::Group, edited.group);
	text(SpellField::Script, edited.script);
	text(SpellField::CombatType, edited.combatType);
	text(SpellField::Effect, edited.effect);
	text(SpellField::Projectile, edited.projectile);
	text(SpellField::Area, edited.areaExpression);
	number(SpellField::SpellId, edited.spellId);
	number(SpellField::RuneId, edited.runeId);
	number(SpellField::Level, edited.level);
	number(SpellField::MagicLevel, edited.magicLevel);
	number(SpellField::Mana, edited.mana);
	number(SpellField::ManaPercent, edited.manaPercent);
	number(SpellField::Soul, edited.soul);
	number(SpellField::Cooldown, edited.cooldown);
	number(SpellField::GroupCooldown, edited.groupCooldown);
	number(SpellField::Range, edited.range);
	number(SpellField::Charges, edited.charges);
	boolean(SpellField::Premium, edited.premium);
	boolean(SpellField::Enabled, edited.enabled);
	boolean(SpellField::Aggressive, edited.aggressive);
	boolean(SpellField::NeedTarget, edited.needTarget);
	boolean(SpellField::NeedDirection, edited.needDirection);
	boolean(SpellField::BlockWalls, edited.blockWalls);
	boolean(SpellField::AllowFarUse, edited.allowFarUse);
	boolean(SpellField::NeedLearn, edited.needLearn);
	boolean(SpellField::SelfTarget, edited.selfTarget);
}

void SpellEditorDialog::syncVocations() {
	edited.vocations.clear();
	if (!allVocationsCheck || !allVocationsCheck->GetValue()) {
		for (unsigned int row = 0; row < vocationList->GetCount() && row < vocationValues.size(); ++row) {
			if (vocationList->IsChecked(row)) {
				edited.vocations.push_back(vocationValues[row]);
			}
		}
	}
	edited.allVocations = edited.vocations.empty();
	if (edited.allVocations && allVocationsCheck && !allVocationsCheck->GetValue()) {
		allVocationsCheck->SetValue(true);
		customVocation->Enable(false);
		showInDescriptionCheck->Enable(false);
	}
	scheduleAutosave();
}

void SpellEditorDialog::refreshVocationControls() {
	if (!showInDescriptionCheck || !vocationList) {
		return;
	}
	const int row = vocationList->GetSelection();
	const bool selected = row >= 0 && static_cast<std::size_t>(row) < vocationValues.size();
	showInDescriptionCheck->Enable(selected && document->source().format == ServerContentFormat::Lua && !edited.allVocations);
	showInDescriptionCheck->SetValue(selected && vocationValues[static_cast<std::size_t>(row)].find(";true") != std::string::npos);
}

void SpellEditorDialog::chooseVisual(ServerVisualKind kind) {
	const SpellField field = kind == ServerVisualKind::MagicEffect ? SpellField::Effect : SpellField::Projectile;
	syncField(field);
	const std::string current = kind == ServerVisualKind::MagicEffect ? edited.effect : edited.projectile;
	SpellVisualBrowserDialog dialog(this, kind, *visualCatalog, current);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}
	const auto selected = dialog.selectedValue();
	if (!selected) {
		return;
	}
	if (auto* control = dynamic_cast<wxComboBox*>(controls[Index(field)])) {
		control->ChangeValue(Utf8(*selected));
	}
	syncField(field);
	scheduleAutosave();
	schedulePreview();
}

void SpellEditorDialog::refreshPreview() {
	if (!preview) {
		return;
	}
	edited.preview.name = edited.name;
	edited.preview.type = edited.combatType;
	edited.preview.effect = edited.effect;
	edited.preview.projectile = edited.projectile;
	edited.preview.area.range = edited.range;
	edited.preview.area.target = edited.needTarget;
	preview->SetDirection(direction);
	const auto effectId = visualCatalog->resolve(ServerVisualKind::MagicEffect, edited.effect);
	const auto projectileId = visualCatalog->resolve(ServerVisualKind::DistanceEffect, edited.projectile);
	preview->SetVisualIds(static_cast<int>(effectId.value_or(0)), static_cast<int>(projectileId.value_or(0)));
	SpellAreaResolution resolution;
	if (edited.areaExpression == document->definition().areaExpression) {
		resolution.state = edited.areaResolutionState;
		resolution.tiles = edited.customAreaTiles;
		resolution.description = edited.areaStatus;
	} else {
		resolution = areaResolver->resolve(edited.areaExpression);
	}
	areaStatus->SetLabel(Utf8(resolution.description));
	areaStatus->Wrap(FromDIP(360));
	if (resolution.state == SpellAreaResolutionState::Resolved) {
		preview->SetCustomArea(&edited.preview, std::move(resolution.tiles), resolution.description);
	} else if (resolution.state == SpellAreaResolutionState::Single) {
		preview->SetAttack(&edited.preview);
	} else {
		preview->SetUnavailableArea(&edited.preview, resolution.description);
	}
}

void SpellEditorDialog::onFieldChanged(SpellField field, wxCommandEvent& event) {
	event.Skip();
	if (!constructing) {
		syncField(field);
		scheduleAutosave();
		if (IsVisualField(field)) {
			schedulePreview();
		}
	}
}

void SpellEditorDialog::schedulePreview() {
	pendingChanges.changed(EditorChangeImpact::Preview);
	if (previewTimer) {
		previewTimer->StartOnce(80);
	}
}

void SpellEditorDialog::onSave(wxCommandEvent&) {
	saveDocument(true);
}

bool SpellEditorDialog::saveDocument(bool showErrors) {
	readControls();
	if (!externalChanges.empty()) {
		if (showErrors) {
			wxCommandEvent event;
			onCompareExternal(event);
		}
		return false;
	}
	std::string error;
	if (!document->save(edited, error)) {
		autosaveState.failed(error);
		updateSaveState("Save error: " + Utf8(error), true);
		if (showErrors) {
			wxMessageBox(Utf8(error), "Could not save spell", wxOK | wxICON_ERROR, this);
		}
		return false;
	}
	edited = document->definition();
	declarationSource->ChangeValue(Utf8(document->declarationText()));
	if (implementationSource) {
		implementationSource->ChangeValue(Utf8(document->implementationText()));
	}
	autosaveState.saved();
	saved = true;
	resetSourceMonitor();
	updateSaveState(showErrors ? "Saved" : "Saved automatically");
	refreshPreview();
	return true;
}

void SpellEditorDialog::scheduleAutosave() {
	if (constructing || !document || !externalChanges.empty()) {
		return;
	}
	if (!document->hasChanges(edited)) {
		if (!autosaveState.hasError()) {
			updateSaveState(saved ? "Saved" : "No changes");
		}
		return;
	}
	autosaveState.changed();
	updateSaveState("Unsaved changes - autosave pending");
	if (autosaveTimer) {
		autosaveTimer->StartOnce(650);
	}
}

void SpellEditorDialog::updateSaveState(const wxString& label, bool error) {
	if (!saveState) {
		return;
	}
	saveState->SetLabel(label);
	saveState->SetForegroundColour(error ? wxColour(232, 72, 85) : Theme::Get(Theme::Role::TextSubtle));
	saveState->SetToolTip(label);
	saveState->GetParent()->Layout();
}

void SpellEditorDialog::onAutosave(wxTimerEvent&) {
	if (autosaveState.ready()) {
		saveDocument(false);
	}
}

void SpellEditorDialog::onPreviewTimer(wxTimerEvent&) {
	if (pendingChanges.takePreview()) {
		refreshPreview();
	}
}

void SpellEditorDialog::resetSourceMonitor() {
	std::vector<EditorSourceSnapshot> sources { { document->source().declarationPath, document->declarationText() } };
	if (document->hasSeparateImplementation()) {
		sources.push_back({ document->implementationPath(), document->implementationText() });
	}
	sourceMonitor.reset(std::move(sources));
	externalChanges.clear();
	if (compareButton) {
		compareButton->Hide();
		Layout();
	}
}

void SpellEditorDialog::onSourceWatch(wxTimerEvent&) {
	if (!externalChanges.empty()) {
		return;
	}
	externalChanges = sourceMonitor.poll();
	if (externalChanges.empty()) {
		return;
	}
	if (autosaveTimer) {
		autosaveTimer->Stop();
	}
	updateSaveState("External source change detected - autosave paused", true);
	compareButton->Show();
	Layout();
}

void SpellEditorDialog::onCompareExternal(wxCommandEvent&) {
	if (ShowSourceConflictDialog(this, externalChanges) == SourceConflictChoice::Reopen) {
		browseRequested = true;
		EndModal(wxID_CANCEL);
	}
}

void SpellEditorDialog::onBrowse(wxCommandEvent&) {
	if (autosaveTimer) {
		autosaveTimer->Stop();
	}
	readControls();
	if (document->hasChanges(edited) && !saveDocument(true)) {
		return;
	}
	browseRequested = true;
	EndModal(wxID_CANCEL);
}

bool SpellEditorDialog::confirmDiscard() {
	readControls();
	return !document->hasChanges(edited) || wxMessageBox("Discard the unsaved spell changes?", "Spell Editor", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) == wxYES;
}

void SpellEditorDialog::onCloseButton(wxCommandEvent&) {
	if (confirmDiscard()) {
		EndModal(wxID_CANCEL);
	}
}

void SpellEditorDialog::onClose(wxCloseEvent& event) {
	if (!IsModal() || confirmDiscard()) {
		event.Skip();
	} else {
		event.Veto();
	}
}
