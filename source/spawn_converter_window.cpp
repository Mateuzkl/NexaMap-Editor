#include "main.h"

#include "spawn_converter_window.h"

#include "settings.h"
#include "spawn_xml_converter.h"

#include <wx/filepicker.h>
#include <wx/notebook.h>

#include <filesystem>

namespace {
	std::filesystem::path PathFromWx(const wxString& value) {
#ifdef _WIN32
		return std::filesystem::path(value.ToStdWstring());
#else
		return std::filesystem::u8path(value.ToUTF8().data());
#endif
	}

	wxString PathToWx(const std::filesystem::path& value) {
#ifdef _WIN32
		return wxString(value.wstring());
#else
		return wxString::FromUTF8(value.string().c_str());
#endif
	}

	bool PathExists(const std::filesystem::path& path) {
		std::error_code error;
		return !path.empty() && std::filesystem::is_regular_file(path, error) && !error;
	}

	void SetGeneratedFilePath(wxFilePickerCtrl* picker, const std::filesystem::path& path, wxString& generated, bool force = false) {
		const wxString suggested = PathToWx(path);
		if (force || picker->GetPath().empty() || picker->GetPath() == generated) {
			picker->SetPath(suggested);
		}
		generated = suggested;
	}

	wxPanel* CreateCanaryPage(
		wxNotebook* notebook,
		wxFilePickerCtrl*& monsterPicker,
		wxFilePickerCtrl*& npcPicker,
		wxFilePickerCtrl*& outputPicker,
		const wxString& initialDirectory
	) {
		auto* page = newd wxPanel(notebook);
		auto* sizer = newd wxBoxSizer(wxVERTICAL);
		auto* description = newd wxStaticText(page, wxID_ANY, "Separate *-monster.xml and *-npc.xml -> one *-spawn.xml\nSelect either Canary file and the matching filename will be suggested automatically.");
		description->Wrap(700);
		sizer->Add(description, 0, wxEXPAND | wxALL, FROM_DIP(page, 12));

		auto* grid = newd wxFlexGridSizer(2, FROM_DIP(page, 8), FROM_DIP(page, 12));
		grid->AddGrowableCol(1, 1);
		grid->Add(newd wxStaticText(page, wxID_ANY, "Monster spawn file:"), 0, wxALIGN_CENTER_VERTICAL);
		monsterPicker = newd wxFilePickerCtrl(page, wxID_ANY, initialDirectory, "Select Canary/Crystal monster spawn XML", "Spawn XML (*.xml)|*.xml", wxDefaultPosition, wxDefaultSize, wxFLP_OPEN | wxFLP_USE_TEXTCTRL);
		grid->Add(monsterPicker, 1, wxEXPAND);
		grid->Add(newd wxStaticText(page, wxID_ANY, "NPC spawn file:"), 0, wxALIGN_CENTER_VERTICAL);
		npcPicker = newd wxFilePickerCtrl(page, wxID_ANY, wxEmptyString, "Select Canary/Crystal NPC spawn XML", "Spawn XML (*.xml)|*.xml", wxDefaultPosition, wxDefaultSize, wxFLP_OPEN | wxFLP_USE_TEXTCTRL);
		grid->Add(npcPicker, 1, wxEXPAND);
		grid->Add(newd wxStaticText(page, wxID_ANY, "Output TFS spawn file:"), 0, wxALIGN_CENTER_VERTICAL);
		outputPicker = newd wxFilePickerCtrl(page, wxID_ANY, wxEmptyString, "Select output TFS spawn XML", "Spawn XML (*.xml)|*.xml", wxDefaultPosition, wxDefaultSize, wxFLP_SAVE | wxFLP_USE_TEXTCTRL);
		grid->Add(outputPicker, 1, wxEXPAND);
		sizer->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(page, 12));

		auto* note = newd wxStaticText(page, wxID_ANY, "One Canary file may be absent. The converter validates XML roots, preserves every area and creature attribute in source order, and never recalculates coordinates, z or radius.");
		note->Wrap(700);
		sizer->Add(note, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(page, 12));
		page->SetSizer(sizer);
		return page;
	}

	wxPanel* CreateTfsPage(
		wxNotebook* notebook,
		wxFilePickerCtrl*& sourcePicker,
		wxDirPickerCtrl*& directoryPicker,
		wxFilePickerCtrl*& monsterPicker,
		wxFilePickerCtrl*& npcPicker,
		wxCheckBox*& pluralCheck,
		const wxString& initialDirectory
	) {
		auto* page = newd wxPanel(notebook);
		auto* sizer = newd wxBoxSizer(wxVERTICAL);
		auto* description = newd wxStaticText(page, wxID_ANY, "One *-spawn.xml -> separate *-monster.xml and *-npc.xml\nMixed TFS areas are split into matching monster and NPC areas without creating empty groups.");
		description->Wrap(700);
		sizer->Add(description, 0, wxEXPAND | wxALL, FROM_DIP(page, 12));

		auto* grid = newd wxFlexGridSizer(2, FROM_DIP(page, 8), FROM_DIP(page, 12));
		grid->AddGrowableCol(1, 1);
		grid->Add(newd wxStaticText(page, wxID_ANY, "TFS spawn file:"), 0, wxALIGN_CENTER_VERTICAL);
		sourcePicker = newd wxFilePickerCtrl(page, wxID_ANY, wxEmptyString, "Select TFS spawn XML", "Spawn XML (*.xml)|*.xml", wxDefaultPosition, wxDefaultSize, wxFLP_OPEN | wxFLP_FILE_MUST_EXIST | wxFLP_USE_TEXTCTRL);
		grid->Add(sourcePicker, 1, wxEXPAND);
		grid->Add(newd wxStaticText(page, wxID_ANY, "Output directory:"), 0, wxALIGN_CENTER_VERTICAL);
		directoryPicker = newd wxDirPickerCtrl(page, wxID_ANY, initialDirectory, "Select output directory", wxDefaultPosition, wxDefaultSize, wxDIRP_USE_TEXTCTRL | wxDIRP_DIR_MUST_EXIST);
		grid->Add(directoryPicker, 1, wxEXPAND);
		grid->Add(newd wxStaticText(page, wxID_ANY, "Monster output:"), 0, wxALIGN_CENTER_VERTICAL);
		monsterPicker = newd wxFilePickerCtrl(page, wxID_ANY, wxEmptyString, "Select monster output XML", "Spawn XML (*.xml)|*.xml", wxDefaultPosition, wxDefaultSize, wxFLP_SAVE | wxFLP_USE_TEXTCTRL);
		grid->Add(monsterPicker, 1, wxEXPAND);
		grid->Add(newd wxStaticText(page, wxID_ANY, "NPC output:"), 0, wxALIGN_CENTER_VERTICAL);
		npcPicker = newd wxFilePickerCtrl(page, wxID_ANY, wxEmptyString, "Select NPC output XML", "Spawn XML (*.xml)|*.xml", wxDefaultPosition, wxDefaultSize, wxFLP_SAVE | wxFLP_USE_TEXTCTRL);
		grid->Add(npcPicker, 1, wxEXPAND);
		sizer->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(page, 12));

		pluralCheck = newd wxCheckBox(page, wxID_ANY, "Use plural monster filename (*-monsters.xml)");
		sizer->Add(pluralCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(page, 12));
		page->SetSizer(sizer);
		return page;
	}
}

SpawnConverterWindow::SpawnConverterWindow(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Spawn / NPC Converter", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
	auto* topSizer = newd wxBoxSizer(wxVERTICAL);
	auto* intro = newd wxStaticText(this, wxID_ANY, "Convert spawn XML structurally between Canary/Crystal 11.x and TFS 1.8 / protocol 8.60. Source files are never modified; temporary output is reopened and fully compared before commit.");
	intro->Wrap(740);
	topSizer->Add(intro, 0, wxEXPAND | wxALL, FROM_DIP(this, 14));

	notebook = newd wxNotebook(this, wxID_ANY);
	const wxString lastDirectory = wxstr(g_settings.getString(Config::SPAWN_CONVERTER_DIRECTORY));
	notebook->AddPage(CreateCanaryPage(notebook, canaryMonsterPicker, canaryNpcPicker, canaryOutputPicker, wxEmptyString), "Canary / Crystal 11.x", true);
	notebook->AddPage(CreateTfsPage(notebook, tfsSourcePicker, outputDirectoryPicker, monsterOutputPicker, npcOutputPicker, pluralMonsterCheck, lastDirectory), "TFS 1.8 / Protocol 8.60", false);
	topSizer->Add(notebook, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 14));

	wxSizer* buttons = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
	if (buttons != nullptr) {
		if (wxWindow* convert = FindWindow(wxID_OK)) {
			convert->SetLabel("Convert");
		}
		topSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 14));
	}
	SetSizerAndFit(topSizer);
	SetMinClientSize(FROM_DIP(this, wxSize(760, 390)));
	SetClientSize(wxSize(std::max(GetClientSize().x, FROM_DIP(this, 780)), std::max(GetClientSize().y, FROM_DIP(this, 430))));
	CentreOnParent();

	canaryMonsterPicker->Bind(wxEVT_FILEPICKER_CHANGED, &SpawnConverterWindow::OnCanaryMonsterChanged, this);
	canaryNpcPicker->Bind(wxEVT_FILEPICKER_CHANGED, &SpawnConverterWindow::OnCanaryNpcChanged, this);
	tfsSourcePicker->Bind(wxEVT_FILEPICKER_CHANGED, &SpawnConverterWindow::OnTfsSourceChanged, this);
	outputDirectoryPicker->Bind(wxEVT_DIRPICKER_CHANGED, &SpawnConverterWindow::OnOutputDirectoryChanged, this);
	pluralMonsterCheck->Bind(wxEVT_CHECKBOX, &SpawnConverterWindow::OnPluralMonsterChanged, this);
	Bind(wxEVT_BUTTON, &SpawnConverterWindow::OnConvert, this, wxID_OK);
}

void SpawnConverterWindow::OnCanaryMonsterChanged(wxFileDirPickerEvent& WXUNUSED(event)) {
	UpdateCanarySuggestions(canaryMonsterPicker);
}

void SpawnConverterWindow::OnCanaryNpcChanged(wxFileDirPickerEvent& WXUNUSED(event)) {
	UpdateCanarySuggestions(canaryNpcPicker);
}

void SpawnConverterWindow::OnTfsSourceChanged(wxFileDirPickerEvent& WXUNUSED(event)) {
	const std::filesystem::path source = PathFromWx(tfsSourcePicker->GetPath());
	if (!source.empty()) {
		const wxString suggestedDirectory = PathToWx(source.parent_path());
		if (outputDirectoryPicker->GetPath().empty() || outputDirectoryPicker->GetPath() == generatedOutputDirectory) {
			outputDirectoryPicker->SetPath(suggestedDirectory);
		}
		generatedOutputDirectory = suggestedDirectory;
	}
	UpdateTfsSuggestions();
}

void SpawnConverterWindow::OnOutputDirectoryChanged(wxFileDirPickerEvent& WXUNUSED(event)) {
	UpdateTfsSuggestions();
}

void SpawnConverterWindow::OnPluralMonsterChanged(wxCommandEvent& WXUNUSED(event)) {
	UpdateTfsSuggestions(true);
}

void SpawnConverterWindow::UpdateCanarySuggestions(wxFilePickerCtrl* changedPicker) {
	const std::filesystem::path selected = PathFromWx(changedPicker->GetPath());
	if (selected.empty()) {
		return;
	}
	const SpawnXmlLayout selectedLayout = changedPicker == canaryMonsterPicker ? SpawnXmlLayout::CanaryMonsters : SpawnXmlLayout::CanaryNpcs;
	SpawnXmlSuggestedPaths suggestions = SpawnXmlConverter::SuggestFromCanaryFile(selected, selectedLayout);

	if (changedPicker == canaryMonsterPicker && !PathExists(suggestions.npcFile)) {
		std::filesystem::path pluralNpc = suggestions.npcFile;
		pluralNpc.replace_filename(suggestions.npcFile.stem().native() + std::filesystem::path("s.xml").native());
		if (PathExists(pluralNpc)) {
			suggestions.npcFile = pluralNpc;
		}
	}
	if (changedPicker == canaryNpcPicker && !PathExists(suggestions.monsterFile)) {
		std::filesystem::path pluralMonster = suggestions.monsterFile;
		pluralMonster.replace_filename(suggestions.monsterFile.stem().native() + std::filesystem::path("s.xml").native());
		if (PathExists(pluralMonster)) {
			suggestions.monsterFile = pluralMonster;
		}
	}
	if (changedPicker != canaryMonsterPicker) {
		SetGeneratedFilePath(canaryMonsterPicker, suggestions.monsterFile, generatedCanaryMonster);
	}
	if (changedPicker != canaryNpcPicker) {
		SetGeneratedFilePath(canaryNpcPicker, suggestions.npcFile, generatedCanaryNpc);
	}
	SetGeneratedFilePath(canaryOutputPicker, suggestions.tfsFile, generatedCanaryOutput);
}

void SpawnConverterWindow::UpdateTfsSuggestions(bool forceMonsterName) {
	const std::filesystem::path source = PathFromWx(tfsSourcePicker->GetPath());
	if (source.empty()) {
		return;
	}
	const SpawnXmlSuggestedPaths suggestions = SpawnXmlConverter::SuggestFromTfsFile(source, PathFromWx(outputDirectoryPicker->GetPath()), pluralMonsterCheck->GetValue());
	SetGeneratedFilePath(monsterOutputPicker, suggestions.monsterFile, generatedMonsterOutput, forceMonsterName && monsterOutputPicker->GetPath() == generatedMonsterOutput);
	SetGeneratedFilePath(npcOutputPicker, suggestions.npcFile, generatedNpcOutput);
}

bool SpawnConverterWindow::ConfirmOverwrite(const std::vector<std::filesystem::path>& files, bool& allowOverwrite) {
	allowOverwrite = false;
	wxString existing;
	for (const std::filesystem::path& file : files) {
		std::error_code error;
		if (std::filesystem::exists(file, error) && !error) {
			existing << "\n  " << PathToWx(file);
		}
	}
	if (existing.empty()) {
		return true;
	}
	allowOverwrite = wxMessageBox("The following output file(s) already exist:" + existing + "\n\nOverwrite them?", "Confirm spawn XML overwrite", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) == wxYES;
	return allowOverwrite;
}

void SpawnConverterWindow::OnConvert(wxCommandEvent& WXUNUSED(event)) {
	const bool canaryToTfs = notebook->GetSelection() == 0;
	std::vector<std::filesystem::path> outputs;
	if (canaryToTfs) {
		outputs.push_back(PathFromWx(canaryOutputPicker->GetPath()));
	} else {
		outputs.push_back(PathFromWx(monsterOutputPicker->GetPath()));
		outputs.push_back(PathFromWx(npcOutputPicker->GetPath()));
	}
	bool allowOverwrite = false;
	if (!ConfirmOverwrite(outputs, allowOverwrite)) {
		return;
	}

	wxProgressDialog progressDialog("Spawn / NPC Converter", "Preparing (0%)", 100, this, wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_SMOOTH | wxPD_AUTO_HIDE);
	const SpawnXmlProgressCallback progress = [&](int value, const std::string& phase) {
		return progressDialog.Update(value, wxString::FromUTF8(phase.c_str()) + wxString::Format(" (%d%%)", value));
	};
	SpawnXmlConversionResult result;
	if (canaryToTfs) {
		result = SpawnXmlConverter::ConvertCanaryToTfs(
			PathFromWx(canaryMonsterPicker->GetPath()),
			PathFromWx(canaryNpcPicker->GetPath()),
			PathFromWx(canaryOutputPicker->GetPath()),
			allowOverwrite,
			progress
		);
	} else {
		result = SpawnXmlConverter::ConvertTfsToCanary(
			PathFromWx(tfsSourcePicker->GetPath()),
			PathFromWx(monsterOutputPicker->GetPath()),
			PathFromWx(npcOutputPicker->GetPath()),
			allowOverwrite,
			progress
		);
	}
	ShowConversionResult(result);
	if (result.success) {
		const std::filesystem::path rememberedDirectory = canaryToTfs
			? PathFromWx(canaryOutputPicker->GetPath()).parent_path()
			: PathFromWx(outputDirectoryPicker->GetPath());
		g_settings.setString(Config::SPAWN_CONVERTER_DIRECTORY, nstr(PathToWx(rememberedDirectory)));
		g_settings.save();
		EndModal(wxID_OK);
	}
}

void SpawnConverterWindow::ShowConversionResult(const SpawnXmlConversionResult& result) {
	if (!result.success) {
		wxMessageBox(wxString::FromUTF8(result.error.c_str()), result.canceled ? "Conversion canceled" : "Spawn conversion failed", wxOK | (result.canceled ? wxICON_INFORMATION : wxICON_ERROR), this);
		return;
	}
	const SpawnXmlConversionReport& report = result.report;
	wxString message = "Conversion completed successfully.\n\n";
	message << "Direction: " << wxString::FromUTF8(report.direction.c_str()) << "\nSources:";
	for (const std::filesystem::path& file : report.sourceFiles) {
		message << "\n  " << PathToWx(file);
	}
	message << "\nOutputs:";
	for (const std::filesystem::path& file : report.outputFiles) {
		message << "\n  " << PathToWx(file);
	}
	message << "\n\nMonster areas: " << report.monsterAreas
			<< "\nMonsters: " << report.monsters
			<< "\nNPC areas: " << report.npcAreas
			<< "\nNPCs: " << report.npcs
			<< "\nAdditional attributes preserved: " << report.additionalAttributes
			<< "\nValidation: " << (report.validationPassed ? "passed" : "failed")
			<< "\nDuration: " << report.duration.count() << " ms";
	if (!report.warnings.empty()) {
		message << "\n\nWarnings (" << report.warnings.size() << "):";
		for (const std::string& warning : report.warnings) {
			message << "\n- " << wxString::FromUTF8(warning.c_str());
		}
	}
	wxMessageBox(message, "Spawn conversion complete", wxOK | wxICON_INFORMATION, this);
}

bool RunSpawnConverter(wxWindow* parent) {
	SpawnConverterWindow dialog(parent);
	return dialog.ShowModal() == wxID_OK;
}
