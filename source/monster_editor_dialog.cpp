//////////////////////////////////////////////////////////////////////
// Native source-preserving editor for an indexed monster definition.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "monster_editor_dialog.h"

#include "graphics.h"
#include "gui.h"
#include "outfit.h"
#include "outfit_color_picker.h"
#include "source_compare_dialog.h"
#include "theme.h"
#include "workspace_session.h"

#include <algorithm>
#include <filesystem>
#include <vector>

#include <wx/timer.h>

namespace {
	constexpr int ID_MONSTER_BACK = wxID_HIGHEST + 901;
	constexpr int ID_MONSTER_BROWSE = wxID_HIGHEST + 902;

	std::size_t FieldIndex(MonsterField field) {
		return static_cast<std::size_t>(field);
	}

	wxString Utf8(const std::string& value) {
		return wxString::FromUTF8(value);
	}

	wxString DisplayPath(const std::filesystem::path& path) {
#ifdef __WINDOWS__
		return wxString(path.wstring());
#else
		return wxString::FromUTF8(path.string());
#endif
	}

	wxStaticBoxSizer* Section(wxWindow* parent, const wxString& title) {
		return newd wxStaticBoxSizer(wxVERTICAL, parent, title);
	}

	wxFlexGridSizer* FormGrid(int columns = 2) {
		auto* grid = newd wxFlexGridSizer(columns, 8, 12);
		for (int column = 1; column < columns; column += 2) {
			grid->AddGrowableCol(column, 1);
		}
		return grid;
	}
}

MonsterEditorDialog::MonsterEditorDialog(wxWindow* parent, std::unique_ptr<MonsterDefinitionDocument> definitionDocument) :
	wxDialog(parent, wxID_ANY, "Monster Editor", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	document(std::move(definitionDocument)),
	edited(document->definition()) {
	auto* rootSizer = newd wxBoxSizer(wxVERTICAL);
	auto* header = newd wxPanel(this);
	header->SetBackgroundColour(Theme::Get(Theme::Role::RaisedSurface));
	auto* headerSizer = newd wxBoxSizer(wxVERTICAL);
	auto* title = newd wxStaticText(header, wxID_ANY, Utf8(edited.name));
	wxFont titleFont = title->GetFont();
	titleFont.SetPointSize(titleFont.GetPointSize() + 3);
	titleFont.SetWeight(wxFONTWEIGHT_BOLD);
	title->SetFont(titleFont);
	headerSizer->Add(title, 0, wxBOTTOM, FromDIP(3));
	headerSizer->Add(
		newd wxStaticText(
			header,
			wxID_ANY,
			Utf8(ServerContentFormatName(document->source().format)) + "  |  " + DisplayPath(document->source().declarationPath)
		),
		0,
		wxEXPAND
	);
	header->SetSizer(headerSizer);
	rootSizer->Add(header, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

	auto* notebook = newd wxNotebook(this, wxID_ANY);

	auto* mainPage = newd wxScrolledWindow(notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	mainPage->SetScrollRate(0, FromDIP(12));
	auto* mainSizer = newd wxBoxSizer(wxVERTICAL);
	auto* identity = Section(mainPage, "Identity");
	auto* identityGrid = FormGrid();
	addTextField(identity->GetStaticBox(), identityGrid, MonsterField::Name, edited.name);
	addTextField(identity->GetStaticBox(), identityGrid, MonsterField::Description, edited.description);
	addTextField(identity->GetStaticBox(), identityGrid, MonsterField::Race, edited.race);
	addTextField(identity->GetStaticBox(), identityGrid, MonsterField::Skull, edited.skull);
	identity->Add(identityGrid, 1, wxEXPAND | wxALL, FromDIP(8));
	mainSizer->Add(identity, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

	auto* stats = Section(mainPage, "Stats");
	auto* statsGrid = FormGrid(4);
	addNumberField(stats->GetStaticBox(), statsGrid, MonsterField::Health, edited.health);
	addNumberField(stats->GetStaticBox(), statsGrid, MonsterField::MaxHealth, edited.maxHealth);
	addNumberField(stats->GetStaticBox(), statsGrid, MonsterField::Experience, edited.experience);
	addNumberField(stats->GetStaticBox(), statsGrid, MonsterField::Speed, edited.speed);
	addNumberField(stats->GetStaticBox(), statsGrid, MonsterField::Armor, edited.armor);
	addNumberField(stats->GetStaticBox(), statsGrid, MonsterField::Defense, edited.defense);
	addNumberField(stats->GetStaticBox(), statsGrid, MonsterField::TargetDistance, edited.targetDistance);
	addNumberField(stats->GetStaticBox(), statsGrid, MonsterField::Corpse, edited.corpse);
	addNumberField(stats->GetStaticBox(), statsGrid, MonsterField::ManaCost, edited.manaCost);
	addNumberField(stats->GetStaticBox(), statsGrid, MonsterField::RunOnHealth, edited.runOnHealth);
	addNumberField(stats->GetStaticBox(), statsGrid, MonsterField::LightLevel, edited.lightLevel);
	addNumberField(stats->GetStaticBox(), statsGrid, MonsterField::LightColor, edited.lightColor);
	stats->Add(statsGrid, 1, wxEXPAND | wxALL, FromDIP(8));
	mainSizer->Add(stats, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

	auto* behavior = Section(mainPage, "Targeting and flags");
	auto* behaviorGrid = FormGrid(4);
	addNumberField(behavior->GetStaticBox(), behaviorGrid, MonsterField::TargetChangeInterval, edited.targetChangeInterval);
	addNumberField(behavior->GetStaticBox(), behaviorGrid, MonsterField::TargetChangeChance, edited.targetChangeChance, 0, 100);
	addNumberField(behavior->GetStaticBox(), behaviorGrid, MonsterField::StrategyAttack, edited.strategyAttack, 0, 100);
	addNumberField(behavior->GetStaticBox(), behaviorGrid, MonsterField::StrategyDefense, edited.strategyDefense, 0, 100);
	addNumberField(behavior->GetStaticBox(), behaviorGrid, MonsterField::StaticAttack, edited.staticAttack, 0, 100);
	behaviorGrid->AddSpacer(1);
	behaviorGrid->AddSpacer(1);
	addBooleanField(behavior->GetStaticBox(), behaviorGrid, MonsterField::Attackable, edited.attackable);
	addBooleanField(behavior->GetStaticBox(), behaviorGrid, MonsterField::Hostile, edited.hostile);
	addBooleanField(behavior->GetStaticBox(), behaviorGrid, MonsterField::Summonable, edited.summonable);
	addBooleanField(behavior->GetStaticBox(), behaviorGrid, MonsterField::Convinceable, edited.convinceable);
	addBooleanField(behavior->GetStaticBox(), behaviorGrid, MonsterField::Pushable, edited.pushable);
	addBooleanField(behavior->GetStaticBox(), behaviorGrid, MonsterField::CanPushItems, edited.canPushItems);
	addBooleanField(behavior->GetStaticBox(), behaviorGrid, MonsterField::CanPushCreatures, edited.canPushCreatures);
	behavior->Add(behaviorGrid, 1, wxEXPAND | wxALL, FromDIP(8));
	mainSizer->Add(behavior, 0, wxEXPAND);
	mainPage->SetSizer(mainSizer);
	notebook->AddPage(mainPage, "Main", true);

	auto* lookPage = newd wxScrolledWindow(notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	lookPage->SetScrollRate(0, FromDIP(12));
	auto* lookSizer = newd wxBoxSizer(wxHORIZONTAL);
	auto* previewSection = Section(lookPage, "Active client preview");
	preview = newd wxStaticBitmap(previewSection->GetStaticBox(), wxID_ANY, wxBitmap(), wxDefaultPosition, FromDIP(wxSize(190, 190)));
	preview->SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	previewSection->Add(preview, 1, wxALIGN_CENTER | wxALL, FromDIP(12));
	auto* previewControls = newd wxBoxSizer(wxHORIZONTAL);
	auto* rotate = newd wxButton(previewSection->GetStaticBox(), wxID_ANY, "Rotate");
	rotate->Bind(wxEVT_BUTTON, &MonsterEditorDialog::onRotate, this);
	directionChoice = newd wxChoice(previewSection->GetStaticBox(), wxID_ANY);
	directionChoice->Append("North");
	directionChoice->Append("East");
	directionChoice->Append("South");
	directionChoice->Append("West");
	directionChoice->SetSelection(direction);
	directionChoice->Bind(wxEVT_CHOICE, &MonsterEditorDialog::onLookChanged, this);
	previewControls->Add(directionChoice, 1, wxRIGHT, FromDIP(6));
	previewControls->Add(rotate, 0);
	previewSection->Add(previewControls, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
	auto* frameSizer = newd wxBoxSizer(wxHORIZONTAL);
	frameSizer->Add(newd wxStaticText(previewSection->GetStaticBox(), wxID_ANY, "Animation frame"), 1, wxALIGN_CENTER_VERTICAL);
	frame = newd wxSpinCtrl(previewSection->GetStaticBox(), wxID_ANY, "0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 255, 0);
	frame->Bind(wxEVT_SPINCTRL, &MonsterEditorDialog::onLookChanged, this);
	frameSizer->Add(frame, 0);
	previewSection->Add(frameSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
	lookSizer->Add(previewSection, 1, wxEXPAND | wxRIGHT, FromDIP(10));

	auto* appearance = Section(lookPage, "Outfit");
	auto* appearanceGrid = FormGrid();
	const auto addLook = [&](MonsterField field, int value, int maximum) {
		auto* control = addNumberField(appearance->GetStaticBox(), appearanceGrid, field, value, 0, maximum);
		control->Bind(wxEVT_SPINCTRL, &MonsterEditorDialog::onLookChanged, this);
	};
	addLook(MonsterField::LookType, edited.outfit.lookType, 200000);
	addLook(MonsterField::LookTypeEx, edited.outfit.lookTypeEx, 200000);
	addLook(MonsterField::LookAddons, edited.outfit.addons, 255);
	addLook(MonsterField::LookMount, edited.outfit.mount, 200000);
	appearance->Add(appearanceGrid, 1, wxEXPAND | wxALL, FromDIP(8));
	outfitColors = newd OutfitColorPicker(appearance->GetStaticBox(), [this](int channel, int color) {
		switch (channel) {
			case 0:
				edited.outfit.head = color;
				break;
			case 1:
				edited.outfit.body = color;
				break;
			case 2:
				edited.outfit.legs = color;
				break;
			case 3:
				edited.outfit.feet = color;
				break;
		}
		refreshPreview();
		scheduleAutosave();
	});
	outfitColors->SetColors(edited.outfit.head, edited.outfit.body, edited.outfit.legs, edited.outfit.feet);
	for (int channel = 0; channel < 4; ++channel) {
		const MonsterField field = static_cast<MonsterField>(static_cast<int>(MonsterField::LookHead) + channel);
		outfitColors->SetChannelEnabled(channel, edited.capability(field).editable);
	}
	appearance->Add(outfitColors, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	lookSizer->Add(appearance, 1, wxEXPAND);
	lookPage->SetSizer(lookSizer);
	notebook->AddPage(lookPage, "Look");

	addAdvancedPages(notebook);

	auto* sourcePage = newd wxPanel(notebook);
	auto* sourceSizer = newd wxBoxSizer(wxVERTICAL);
	wxString metadata = "Format: " + Utf8(ServerContentFormatName(document->source().format))
		+ "\nDeclaration: " + DisplayPath(document->source().declarationPath);
	if (document->source().registrationPath) {
		metadata += "\nRegistration: " + DisplayPath(*document->source().registrationPath);
	}
	metadata += wxString::Format("\nIndexed line: %zu", document->source().declarationLine);
	sourceSizer->Add(newd wxStaticText(sourcePage, wxID_ANY, metadata), 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	sourceView = newd wxTextCtrl(
		sourcePage,
		wxID_ANY,
		Utf8(document->sourceText()),
		wxDefaultPosition,
		wxDefaultSize,
		wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP
	);
	wxFont sourceFont = wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE);
	sourceView->SetFont(sourceFont);
	sourceSizer->Add(sourceView, 1, wxEXPAND);
	sourcePage->SetSizer(sourceSizer);
	notebook->AddPage(sourcePage, "Source");

	rootSizer->Add(notebook, 1, wxEXPAND | wxALL, FromDIP(12));
	auto* footer = newd wxBoxSizer(wxHORIZONTAL);
	saveStateLabel = newd wxStaticText(this, wxID_ANY, "No unsaved changes");
	saveStateLabel->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	footer->Add(saveStateLabel, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
	compareButton = newd wxButton(this, wxID_ANY, "Compare External...");
	compareButton->Hide();
	compareButton->Bind(wxEVT_BUTTON, &MonsterEditorDialog::onCompareExternal, this);
	footer->Add(compareButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	footer->Add(newd wxButton(this, ID_MONSTER_BACK, "< Back to Monsters"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	footer->Add(newd wxButton(this, ID_MONSTER_BROWSE, "Browse Monsters..."), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
	auto* buttons = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
	if (buttons) {
		footer->Add(buttons, 0, wxALIGN_CENTER_VERTICAL);
	}
	rootSizer->Add(footer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
	if (wxWindow* saveButton = FindWindow(wxID_OK)) {
		saveButton->SetLabel("Save");
	}
	SetSizer(rootSizer);
	SetMinSize(FromDIP(wxSize(760, 600)));
	SetSize(FromDIP(wxSize(860, 680)));
	CentreOnParent();
	Bind(wxEVT_BUTTON, &MonsterEditorDialog::onSave, this, wxID_OK);
	Bind(wxEVT_BUTTON, &MonsterEditorDialog::onBrowse, this, ID_MONSTER_BACK);
	Bind(wxEVT_BUTTON, &MonsterEditorDialog::onBrowse, this, ID_MONSTER_BROWSE);
	Bind(wxEVT_BUTTON, &MonsterEditorDialog::onCancel, this, wxID_CANCEL);
	Bind(wxEVT_CLOSE_WINDOW, &MonsterEditorDialog::onClose, this);
	Bind(wxEVT_TEXT, &MonsterEditorDialog::onFieldChanged, this);
	Bind(wxEVT_SPINCTRL, &MonsterEditorDialog::onFieldChanged, this);
	Bind(wxEVT_CHECKBOX, &MonsterEditorDialog::onFieldChanged, this);
	autosaveTimer = std::make_unique<wxTimer>(this);
	Bind(wxEVT_TIMER, &MonsterEditorDialog::onAutosave, this, autosaveTimer->GetId());
	sourceWatchTimer = std::make_unique<wxTimer>(this);
	Bind(wxEVT_TIMER, &MonsterEditorDialog::onSourceWatch, this, sourceWatchTimer->GetId());
	resetSourceMonitor();
	sourceWatchTimer->Start(1000);
	constructing = false;
	refreshPreview();
}

bool MonsterEditorDialog::wasSaved() const {
	return saved;
}

const MonsterDefinition& MonsterEditorDialog::savedDefinition() const {
	return edited;
}

bool MonsterEditorDialog::wantsBrowse() const {
	return browseRequested;
}

wxTextCtrl* MonsterEditorDialog::addTextField(wxWindow* parent, wxFlexGridSizer* grid, MonsterField field, const std::string& value) {
	grid->Add(newd wxStaticText(parent, wxID_ANY, MonsterFieldName(field)), 0, wxALIGN_CENTER_VERTICAL);
	auto* control = newd wxTextCtrl(parent, wxID_ANY, Utf8(value));
	grid->Add(control, 1, wxEXPAND);
	controls[FieldIndex(field)] = control;
	applyCapability(control, field);
	return control;
}

wxSpinCtrl* MonsterEditorDialog::addNumberField(wxWindow* parent, wxFlexGridSizer* grid, MonsterField field, int value, int minimum, int maximum) {
	grid->Add(newd wxStaticText(parent, wxID_ANY, MonsterFieldName(field)), 0, wxALIGN_CENTER_VERTICAL);
	auto* control = newd wxSpinCtrl(parent, wxID_ANY, wxString::Format("%d", value), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, minimum, maximum, value);
	grid->Add(control, 1, wxEXPAND);
	controls[FieldIndex(field)] = control;
	applyCapability(control, field);
	return control;
}

wxWindow* MonsterEditorDialog::addBooleanField(wxWindow* parent, wxFlexGridSizer* grid, MonsterField field, bool value) {
	grid->Add(newd wxStaticText(parent, wxID_ANY, MonsterFieldName(field)), 0, wxALIGN_CENTER_VERTICAL);
	auto* control = newd wxCheckBox(parent, wxID_ANY, "Enabled");
	control->SetValue(value);
	grid->Add(control, 1, wxEXPAND);
	controls[FieldIndex(field)] = control;
	applyCapability(control, field);
	return control;
}

void MonsterEditorDialog::applyCapability(wxWindow* control, MonsterField field) {
	const MonsterFieldCapability& capability = edited.capability(field);
	const bool editable = capability.editable && field != MonsterField::Name;
	control->Enable(editable);
	if (field == MonsterField::Name) {
		control->SetToolTip("The existing monster identity is fixed so placed creatures and palette references remain valid.");
		return;
	}
	if (!capability.editable) {
		control->SetToolTip(Utf8(capability.limitation));
	}
}

void MonsterEditorDialog::readControls() {
	const auto text = [this](MonsterField field) {
		if (!edited.capability(field).editable) {
			return std::string();
		}
		return dynamic_cast<wxTextCtrl*>(controls[FieldIndex(field)])->GetValue().ToStdString(wxConvUTF8);
	};
	const auto number = [this](MonsterField field) {
		return dynamic_cast<wxSpinCtrl*>(controls[FieldIndex(field)])->GetValue();
	};
	const auto boolean = [this](MonsterField field) {
		return dynamic_cast<wxCheckBox*>(controls[FieldIndex(field)])->GetValue();
	};
	if (edited.capability(MonsterField::Name).editable) {
		edited.name = text(MonsterField::Name);
	}
	if (edited.capability(MonsterField::Description).editable) {
		edited.description = text(MonsterField::Description);
	}
	if (edited.capability(MonsterField::Race).editable) {
		edited.race = text(MonsterField::Race);
	}
	if (edited.capability(MonsterField::Skull).editable) {
		edited.skull = text(MonsterField::Skull);
	}

	const auto assignNumber = [&](MonsterField field, int& target) {
		if (edited.capability(field).editable) {
			target = number(field);
		}
	};
	assignNumber(MonsterField::Health, edited.health);
	assignNumber(MonsterField::MaxHealth, edited.maxHealth);
	assignNumber(MonsterField::Experience, edited.experience);
	assignNumber(MonsterField::Speed, edited.speed);
	assignNumber(MonsterField::Armor, edited.armor);
	assignNumber(MonsterField::Defense, edited.defense);
	assignNumber(MonsterField::TargetDistance, edited.targetDistance);
	assignNumber(MonsterField::Corpse, edited.corpse);
	assignNumber(MonsterField::ManaCost, edited.manaCost);
	assignNumber(MonsterField::RunOnHealth, edited.runOnHealth);
	assignNumber(MonsterField::LightLevel, edited.lightLevel);
	assignNumber(MonsterField::LightColor, edited.lightColor);
	assignNumber(MonsterField::TargetChangeInterval, edited.targetChangeInterval);
	assignNumber(MonsterField::TargetChangeChance, edited.targetChangeChance);
	assignNumber(MonsterField::StrategyAttack, edited.strategyAttack);
	assignNumber(MonsterField::StrategyDefense, edited.strategyDefense);
	assignNumber(MonsterField::StaticAttack, edited.staticAttack);
	assignNumber(MonsterField::LookType, edited.outfit.lookType);
	assignNumber(MonsterField::LookTypeEx, edited.outfit.lookTypeEx);
	if (outfitColors) {
		if (edited.capability(MonsterField::LookHead).editable) {
			edited.outfit.head = outfitColors->GetColor(0);
		}
		if (edited.capability(MonsterField::LookBody).editable) {
			edited.outfit.body = outfitColors->GetColor(1);
		}
		if (edited.capability(MonsterField::LookLegs).editable) {
			edited.outfit.legs = outfitColors->GetColor(2);
		}
		if (edited.capability(MonsterField::LookFeet).editable) {
			edited.outfit.feet = outfitColors->GetColor(3);
		}
	}
	assignNumber(MonsterField::LookAddons, edited.outfit.addons);
	assignNumber(MonsterField::LookMount, edited.outfit.mount);

	const auto assignBoolean = [&](MonsterField field, bool& target) {
		if (edited.capability(field).editable) {
			target = boolean(field);
		}
	};
	assignBoolean(MonsterField::Summonable, edited.summonable);
	assignBoolean(MonsterField::Convinceable, edited.convinceable);
	assignBoolean(MonsterField::Attackable, edited.attackable);
	assignBoolean(MonsterField::Hostile, edited.hostile);
	assignBoolean(MonsterField::Pushable, edited.pushable);
	assignBoolean(MonsterField::CanPushItems, edited.canPushItems);
	assignBoolean(MonsterField::CanPushCreatures, edited.canPushCreatures);
	if (maxSummons && edited.capability(MonsterSection::Summons).editable) {
		edited.maxSummons = maxSummons->GetValue();
	}
	if (voiceInterval && voiceChance && edited.capability(MonsterSection::Voices).editable) {
		edited.voices.interval = voiceInterval->GetValue();
		edited.voices.chance = voiceChance->GetValue();
	}
}

void MonsterEditorDialog::refreshPreview() {
	direction = directionChoice ? directionChoice->GetSelection() : direction;
	Outfit outfit;
	outfit.lookType = edited.outfit.lookType;
	outfit.lookItem = edited.outfit.lookTypeEx;
	outfit.lookHead = edited.outfit.head;
	outfit.lookBody = edited.outfit.body;
	outfit.lookLegs = edited.outfit.legs;
	outfit.lookFeet = edited.outfit.feet;
	outfit.lookAddon = edited.outfit.addons;
	outfit.lookMount = edited.outfit.mount;
	int mountClientId = outfit.lookMount > 0 ? g_workspace.resolveMountClientId(outfit.lookMount) : 0;
	GameSprite* sprite = nullptr;
	const Outfit* previewOutfit = nullptr;
	if (outfit.lookItem > 0) {
		sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(outfit.lookItem));
	} else if (outfit.lookType > 0) {
		sprite = g_gui.gfx.getCreatureSprite(outfit.lookType);
		previewOutfit = &outfit;
	} else if (outfit.lookMount > 0) {
		sprite = g_gui.gfx.getCreatureSprite(mountClientId);
		outfit.lookType = mountClientId;
		outfit.lookMount = 0;
		mountClientId = 0;
		previewOutfit = &outfit;
	}
	if (!sprite) {
		preview->SetBitmap(wxBitmap());
		preview->SetToolTip("The active client does not contain this lookType/lookTypeEx.");
		return;
	}
	std::vector<uint8_t> rgba;
	int width = 0;
	int height = 0;
	bool pending = false;
	if (!sprite->getVisualPreviewRGBA(rgba, width, height, pending, false, previewOutfit, direction, frame ? frame->GetValue() : 0, 0, 0, 0, mountClientId)) {
		preview->SetBitmap(wxBitmap());
		preview->SetToolTip(pending ? "Preview is loading from the active client." : "This active-client sprite cannot be previewed.");
		return;
	}
	wxImage image(width, height);
	image.InitAlpha();
	for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(width) * height; ++pixel) {
		std::copy_n(rgba.data() + pixel * 4, 3, image.GetData() + pixel * 3);
		image.GetAlpha()[pixel] = rgba[pixel * 4 + 3];
	}
	const int target = FromDIP(160);
	const double scale = static_cast<double>(target) / std::max(width, height);
	preview->SetBitmap(wxBitmap(image.Scale(std::max(1, static_cast<int>(width * scale)), std::max(1, static_cast<int>(height * scale)), wxIMAGE_QUALITY_NEAREST)));
	preview->SetToolTip(wxString::Format("Active client | direction %d | frame %d", direction, frame ? frame->GetValue() : 0));
}

void MonsterEditorDialog::onSave(wxCommandEvent& WXUNUSED(event)) {
	saveDocument(true);
}

void MonsterEditorDialog::onBrowse(wxCommandEvent& WXUNUSED(event)) {
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

bool MonsterEditorDialog::saveDocument(bool showErrors) {
	readControls();
	if (!externalChanges.empty()) {
		if (showErrors) {
			wxCommandEvent event;
			onCompareExternal(event);
		}
		return false;
	}
	if (edited.name.empty()) {
		const std::string error = "Monster name cannot be empty.";
		autosaveState.failed(error);
		updateSaveState(Utf8(error), true);
		if (showErrors) {
			wxMessageBox(Utf8(error), "Monster Editor", wxOK | wxICON_WARNING, this);
		}
		return false;
	}
	std::string error;
	if (!document->save(edited, error)) {
		autosaveState.failed(error);
		updateSaveState("Save error: " + Utf8(error), true);
		if (showErrors) {
			wxMessageBox(Utf8(error), "Could not save monster", wxOK | wxICON_ERROR, this);
		}
		return false;
	}
	edited = document->definition();
	autosaveState.saved();
	saved = true;
	resetSourceMonitor();
	if (sourceView) {
		sourceView->ChangeValue(Utf8(document->sourceText()));
	}
	updateSaveState(showErrors ? "Saved" : "Saved automatically");
	return true;
}

void MonsterEditorDialog::scheduleAutosave() {
	if (constructing || !document || !externalChanges.empty()) {
		return;
	}
	if (!document->hasChanges(edited)) {
		if (!autosaveState.hasError()) {
			updateSaveState(saved ? "Saved" : "No unsaved changes");
		}
		return;
	}
	autosaveState.changed();
	updateSaveState("Unsaved changes - autosave pending");
	if (autosaveTimer) {
		autosaveTimer->StartOnce(650);
	}
}

void MonsterEditorDialog::onFieldChanged(wxCommandEvent& event) {
	readControls();
	scheduleAutosave();
	event.Skip();
}

void MonsterEditorDialog::onAutosave(wxTimerEvent& WXUNUSED(event)) {
	if (autosaveState.ready()) {
		saveDocument(false);
	}
}

void MonsterEditorDialog::resetSourceMonitor() {
	std::vector<EditorSourceSnapshot> sources { { document->source().declarationPath, document->sourceText() } };
	const auto addRelated = [&](const std::optional<std::filesystem::path>& path) {
		if (!path || path->lexically_normal() == document->source().declarationPath.lexically_normal()) {
			return;
		}
		if (const auto text = SourceText::ReadBoundedFile(*path, 32 * 1024 * 1024)) {
			sources.push_back({ *path, *text });
		}
	};
	addRelated(document->source().registrationPath);
	addRelated(document->source().relatedScriptPath);
	sourceMonitor.reset(std::move(sources));
	externalChanges.clear();
	if (compareButton) {
		compareButton->Hide();
		Layout();
	}
}

void MonsterEditorDialog::onSourceWatch(wxTimerEvent&) {
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

void MonsterEditorDialog::onCompareExternal(wxCommandEvent&) {
	if (ShowSourceConflictDialog(this, externalChanges) == SourceConflictChoice::Reopen) {
		browseRequested = true;
		EndModal(wxID_CANCEL);
	}
}

void MonsterEditorDialog::updateSaveState(const wxString& label, bool error) {
	if (!saveStateLabel) {
		return;
	}
	saveStateLabel->SetLabel(label);
	saveStateLabel->SetForegroundColour(error ? wxColour(232, 72, 85) : Theme::Get(Theme::Role::TextSubtle));
	saveStateLabel->SetToolTip(label);
	saveStateLabel->GetParent()->Layout();
}

bool MonsterEditorDialog::confirmDiscard() {
	readControls();
	if (!document->hasChanges(edited)) {
		return true;
	}
	return wxMessageBox(
			   "Discard the unsaved monster changes?",
			   "Monster Editor",
			   wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
			   this
		   )
		== wxYES;
}

void MonsterEditorDialog::onCancel(wxCommandEvent& WXUNUSED(event)) {
	if (confirmDiscard()) {
		EndModal(wxID_CANCEL);
	}
}

void MonsterEditorDialog::onClose(wxCloseEvent& event) {
	if (!IsModal() || confirmDiscard()) {
		event.Skip();
	} else {
		event.Veto();
	}
}

void MonsterEditorDialog::onLookChanged(wxCommandEvent& WXUNUSED(event)) {
	readControls();
	refreshPreview();
	scheduleAutosave();
}

void MonsterEditorDialog::onRotate(wxCommandEvent& WXUNUSED(event)) {
	direction = (directionChoice->GetSelection() + 1) % 4;
	directionChoice->SetSelection(direction);
	refreshPreview();
}
