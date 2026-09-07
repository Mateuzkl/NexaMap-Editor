//////////////////////////////////////////////////////////////////////
// Native source-preserving editor for indexed global spells.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "spell_editor_dialog.h"

#include "monster_spell_preview.h"
#include "theme.h"

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <algorithm>
#include <limits>

namespace {
	wxString Utf8(const std::string& value) {
		return wxString::FromUTF8(value);
	}

	std::string Narrow(const wxString& value) {
		return value.ToStdString(wxConvUTF8);
	}

	std::size_t Index(SpellField field) {
		return static_cast<std::size_t>(field);
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
	document(std::move(value)), edited(document->definition()) {
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
	auto* visualGrid = Grid();
	addText(visualFields->GetStaticBox(), visualGrid, SpellField::CombatType, edited.combatType, { "COMBAT_PHYSICALDAMAGE", "COMBAT_ENERGYDAMAGE", "COMBAT_EARTHDAMAGE", "COMBAT_FIREDAMAGE", "COMBAT_LIFEDRAIN", "COMBAT_MANADRAIN", "COMBAT_HEALING", "COMBAT_ICEDAMAGE", "COMBAT_HOLYDAMAGE", "COMBAT_DEATHDAMAGE" });
	addText(visualFields->GetStaticBox(), visualGrid, SpellField::Effect, edited.effect, { "CONST_ME_NONE", "CONST_ME_DRAWBLOOD", "CONST_ME_EXPLOSIONAREA", "CONST_ME_FIREAREA", "CONST_ME_ENERGYAREA", "CONST_ME_MAGIC_BLUE", "CONST_ME_MAGIC_RED", "CONST_ME_HITBYFIRE", "CONST_ME_PLANTATTACK" });
	addText(visualFields->GetStaticBox(), visualGrid, SpellField::Projectile, edited.projectile, { "CONST_ANI_NONE", "CONST_ANI_SPEAR", "CONST_ANI_FIRE", "CONST_ANI_ENERGY", "CONST_ANI_POISON", "CONST_ANI_ICE", "CONST_ANI_HOLY", "CONST_ANI_DEATH" });
	addText(visualFields->GetStaticBox(), visualGrid, SpellField::Area, edited.areaExpression, { "AREA_CIRCLE2X2", "AREA_CIRCLE3X3", "AREA_SQUARE1X1", "AREA_SQUARE2X2", "AREA_BEAM5", "AREA_WAVE4" });
	visualGrid->Add(newd wxStaticText(visualFields->GetStaticBox(), wxID_ANY, "Preview direction"), 0, wxALIGN_CENTER_VERTICAL);
	auto* directionChoice = newd wxChoice(visualFields->GetStaticBox(), wxID_ANY);
	directionChoice->Append("North");
	directionChoice->Append("East");
	directionChoice->Append("South");
	directionChoice->Append("West");
	directionChoice->SetSelection(0);
	visualGrid->Add(directionChoice, 1, wxEXPAND);
	visualFields->Add(visualGrid, 0, wxEXPAND | wxALL, FromDIP(8));
	areaStatus = newd wxStaticText(visualFields->GetStaticBox(), wxID_ANY, Utf8(edited.areaStatus));
	areaStatus->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	areaStatus->Wrap(FromDIP(360));
	visualFields->Add(areaStatus, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	visualSizer->Add(visualFields, 1, wxEXPAND | wxRIGHT, FromDIP(10));
	preview = newd MonsterSpellPreview(visualPage);
	visualSizer->Add(preview, 1, wxEXPAND);
	visualPage->SetSizer(visualSizer);
	notebook->AddPage(visualPage, "Visual");
	directionChoice->Bind(wxEVT_CHOICE, [this, directionChoice](wxCommandEvent&) { direction = directionChoice->GetSelection(); refreshPreview(); });

	auto* vocationPage = Page(notebook);
	auto* vocationSizer = newd wxBoxSizer(wxVERTICAL);
	vocationSizer->Add(newd wxStaticText(vocationPage, wxID_ANY, "Vocation declarations are displayed from the selected spell and preserved exactly."), 0, wxALL, FromDIP(10));
	auto* vocations = newd wxTextCtrl(vocationPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	wxString vocationText;
	for (const std::string& vocation : edited.vocations) {
		vocationText += Utf8(vocation) + "\n";
	}
	vocations->ChangeValue(vocationText.empty() ? wxString("No literal vocation list was found in this source.") : vocationText);
	vocationSizer->Add(vocations, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
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
	bottom->Add(newd wxButton(this, wxID_OK, "Save"), 0, wxRIGHT, FromDIP(8));
	bottom->Add(newd wxButton(this, wxID_CANCEL, "Close"), 0);
	root->Add(newd wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
	root->Add(bottom, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
	SetSizer(root);
	SetMinSize(FromDIP(wxSize(790, 610)));
	SetSize(FromDIP(wxSize(980, 760)));
	CentreOnParent();
	Bind(wxEVT_BUTTON, &SpellEditorDialog::onSave, this, wxID_OK);
	Bind(wxEVT_BUTTON, &SpellEditorDialog::onCloseButton, this, wxID_CANCEL);
	Bind(wxEVT_CLOSE_WINDOW, &SpellEditorDialog::onClose, this);
	constructing = false;
	refreshPreview();
}

bool SpellEditorDialog::wasSaved() const {
	return saved;
}
const SpellDefinition& SpellEditorDialog::savedDefinition() const {
	return edited;
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
	control->Bind(wxEVT_TEXT, &SpellEditorDialog::onFieldChanged, this);
	control->Bind(wxEVT_COMBOBOX, &SpellEditorDialog::onFieldChanged, this);
	return control;
}

wxWindow* SpellEditorDialog::addNumber(wxWindow* parent, wxFlexGridSizer* grid, SpellField field, int value, int maximum) {
	grid->Add(newd wxStaticText(parent, wxID_ANY, SpellFieldName(field)), 0, wxALIGN_CENTER_VERTICAL);
	auto* control = newd wxSpinCtrl(parent, wxID_ANY, wxString::Format("%d", value), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, maximum, value);
	controls[Index(field)] = control;
	applyCapability(control, field);
	grid->Add(control, 1, wxEXPAND);
	control->Bind(wxEVT_SPINCTRL, &SpellEditorDialog::onFieldChanged, this);
	control->Bind(wxEVT_TEXT, &SpellEditorDialog::onFieldChanged, this);
	return control;
}

wxWindow* SpellEditorDialog::addBoolean(wxWindow* parent, wxFlexGridSizer* grid, SpellField field, bool value) {
	grid->Add(newd wxStaticText(parent, wxID_ANY, SpellFieldName(field)), 0, wxALIGN_CENTER_VERTICAL);
	auto* control = newd wxCheckBox(parent, wxID_ANY, "Enabled");
	control->SetValue(value);
	controls[Index(field)] = control;
	applyCapability(control, field);
	grid->Add(control, 1, wxEXPAND);
	control->Bind(wxEVT_CHECKBOX, &SpellEditorDialog::onFieldChanged, this);
	return control;
}

void SpellEditorDialog::applyCapability(wxWindow* control, SpellField field) {
	const auto& capability = edited.capability(field);
	control->Enable(capability.editable);
	if (!capability.editable && !capability.limitation.empty()) {
		control->SetToolTip(Utf8(capability.limitation));
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

void SpellEditorDialog::refreshPreview() {
	if (!preview) {
		return;
	}
	readControls();
	edited.preview.name = edited.name;
	edited.preview.type = edited.combatType;
	edited.preview.effect = edited.effect;
	edited.preview.projectile = edited.projectile;
	edited.preview.area.range = edited.range;
	edited.preview.area.target = edited.needTarget;
	preview->SetDirection(direction);
	if (!edited.customAreaTiles.empty() && edited.areaExpression == document->definition().areaExpression) {
		preview->SetCustomArea(&edited.preview, edited.customAreaTiles, "Custom " + std::to_string(edited.customAreaTiles.size()) + " tiles");
	} else {
		SpellDefinition resolved = edited;
		resolved.customAreaTiles.clear();
		// Reloading the provider is unnecessary here; reproduce the common static shapes.
		const std::string area = Narrow(Utf8(edited.areaExpression).Lower());
		resolved.preview.area = {};
		resolved.preview.area.range = edited.range;
		resolved.preview.area.target = edited.needTarget;
		if (area.find("circle2x2") != std::string::npos) {
			resolved.preview.area.radius = 2;
		} else if (area.find("circle3x3") != std::string::npos) {
			resolved.preview.area.radius = 3;
		} else if (area.find("square1x1") != std::string::npos) {
			resolved.preview.area.radius = 1;
		} else if (area.find("square2x2") != std::string::npos) {
			resolved.preview.area.radius = 2;
		} else if (area.find("beam") != std::string::npos) {
			resolved.preview.area.length = area.find('8') != std::string::npos ? 8 : 5;
		} else if (area.find("wave") != std::string::npos) {
			resolved.preview.area.length = area.find('8') != std::string::npos ? 8 : 4;
			resolved.preview.area.spread = 3;
		}
		preview->SetAttack(&resolved.preview);
	}
}

void SpellEditorDialog::updateDirtyState() {
	readControls();
	const bool dirty = document->hasChanges(edited);
	saveState->SetLabel(dirty ? "Unsaved changes" : (saved ? "Saved" : "No changes"));
	saveState->SetForegroundColour(dirty ? wxColour(230, 169, 56) : Theme::Get(Theme::Role::TextSubtle));
}

void SpellEditorDialog::onFieldChanged(wxCommandEvent& event) {
	event.Skip();
	if (!constructing) {
		updateDirtyState();
		refreshPreview();
	}
}

void SpellEditorDialog::onSave(wxCommandEvent&) {
	readControls();
	std::string error;
	if (!document->save(edited, error)) {
		saveState->SetLabel("Save failed");
		saveState->SetForegroundColour(wxColour(230, 82, 82));
		wxMessageBox(Utf8(error), "Could not save spell", wxOK | wxICON_ERROR, this);
		return;
	}
	edited = document->definition();
	declarationSource->ChangeValue(Utf8(document->declarationText()));
	if (implementationSource) {
		implementationSource->ChangeValue(Utf8(document->implementationText()));
	}
	saved = true;
	saveState->SetLabel("Saved");
	saveState->SetForegroundColour(wxColour(80, 196, 122));
	refreshPreview();
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
