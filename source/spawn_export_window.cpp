#include "main.h"

#include "spawn_export_window.h"

#include "file_transaction.h"
#include "map.h"

#include <wx/filepicker.h>

namespace {
	std::string EnsureXmlExtension(std::string filename) {
		if (filename.size() < 4 || as_lower_str(filename.substr(filename.size() - 4)) != ".xml") {
			filename += ".xml";
		}
		return filename;
	}
}

SpawnExportWindow::SpawnExportWindow(wxWindow* parent, Map& map, const wxString& directory, const wxString& mapBaseName, bool saveAsMode) :
	wxDialog(parent, wxID_ANY, saveAsMode ? "Spawn format for Save As" : "Export Spawns", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	map(map),
	document(SpawnMapAdapter::Capture(map)),
	saveAsMode(saveAsMode) {
	auto* topSizer = newd wxBoxSizer(wxVERTICAL);
	auto* form = newd wxFlexGridSizer(2, 8, 12);
	form->AddGrowableCol(1, 1);

	form->Add(newd wxStaticText(this, wxID_ANY, "Source format:"), 0, wxALIGN_CENTER_VERTICAL);
	sourceLabel = newd wxStaticText(this, wxID_ANY, wxString::FromUTF8(SpawnFormatIO::GetFormatName(map.getSpawnFormat())));
	form->Add(sourceLabel, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

	form->Add(newd wxStaticText(this, wxID_ANY, "Target format:"), 0, wxALIGN_CENTER_VERTICAL);
	formatChoice = newd wxChoice(this, wxID_ANY);
	formatChoice->Append("TFS 1.8 / protocol 8.60 (one *-spawn.xml)");
	formatChoice->Append("Canary / Crystal 11.x (separate *-monster.xml and *-npc.xml)");
	formatChoice->SetSelection(map.getSpawnFormat() == SpawnFormat::CanaryCrystal ? 1 : 0);
	form->Add(formatChoice, 1, wxEXPAND);

	form->Add(newd wxStaticText(this, wxID_ANY, "Destination:"), 0, wxALIGN_CENTER_VERTICAL);
	directoryPicker = newd wxDirPickerCtrl(this, wxID_ANY, directory, "Select the spawn XML destination", wxDefaultPosition, wxDefaultSize, wxDIRP_USE_TEXTCTRL);
	directoryPicker->Enable(!saveAsMode);
	form->Add(directoryPicker, 1, wxEXPAND);

	const std::string baseName = nstr(mapBaseName).empty() ? "world" : nstr(mapBaseName);
	const std::string currentPrimary = map.getSpawnFilename();
	const std::string currentNpc = map.getSpawnNpcFilename();
	form->Add(newd wxStaticText(this, wxID_ANY, "TFS filename:"), 0, wxALIGN_CENTER_VERTICAL);
	tfsFilename = newd wxTextCtrl(this, wxID_ANY, wxstr(map.getSpawnFormat() == SpawnFormat::Tfs && !currentPrimary.empty() ? currentPrimary : baseName + "-spawn.xml"));
	form->Add(tfsFilename, 1, wxEXPAND);

	form->Add(newd wxStaticText(this, wxID_ANY, "Monster filename:"), 0, wxALIGN_CENTER_VERTICAL);
	monsterFilename = newd wxTextCtrl(this, wxID_ANY, wxstr(map.getSpawnFormat() == SpawnFormat::CanaryCrystal && !currentPrimary.empty() ? currentPrimary : baseName + "-monster.xml"));
	form->Add(monsterFilename, 1, wxEXPAND);

	form->Add(newd wxStaticText(this, wxID_ANY, "NPC filename:"), 0, wxALIGN_CENTER_VERTICAL);
	npcFilename = newd wxTextCtrl(this, wxID_ANY, wxstr(map.getSpawnFormat() == SpawnFormat::CanaryCrystal && !currentNpc.empty() ? currentNpc : baseName + "-npc.xml"));
	form->Add(npcFilename, 1, wxEXPAND);

	topSizer->Add(form, 0, wxEXPAND | wxALL, 14);
	previewLabel = newd wxStaticText(this, wxID_ANY, wxEmptyString);
	previewLabel->Wrap(580);
	topSizer->Add(previewLabel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

	wxSizer* buttons = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
	if (buttons) {
		if (wxWindow* ok = FindWindow(wxID_OK)) {
			ok->SetLabel(saveAsMode ? "Continue" : "Export");
		}
		topSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);
	}
	SetSizerAndFit(topSizer);
	SetMinSize(wxSize(660, GetSize().GetHeight()));
	CentreOnParent();

	formatChoice->Bind(wxEVT_CHOICE, &SpawnExportWindow::OnFormatChanged, this);
	Bind(wxEVT_BUTTON, &SpawnExportWindow::OnConfirm, this, wxID_OK);
	UpdatePreview();
}

SpawnExportOptions SpawnExportWindow::GetOptions() const {
	SpawnExportOptions options;
	options.format = formatChoice->GetSelection() == 1 ? SpawnFormat::CanaryCrystal : SpawnFormat::Tfs;
	options.directory = std::filesystem::path(nstr(directoryPicker->GetPath()));
	if (options.format == SpawnFormat::CanaryCrystal) {
		options.primaryFilename = EnsureXmlExtension(nstr(monsterFilename->GetValue()));
		options.npcFilename = EnsureXmlExtension(nstr(npcFilename->GetValue()));
	} else {
		options.primaryFilename = EnsureXmlExtension(nstr(tfsFilename->GetValue()));
	}
	return options;
}

void SpawnExportWindow::UpdatePreview() {
	const bool modern = formatChoice->GetSelection() == 1;
	tfsFilename->Enable(!modern);
	monsterFilename->Enable(modern);
	npcFilename->Enable(modern);

	size_t shortRespawns = 0;
	size_t weightedSets = 0;
	for (const SpawnAreaData& area : document.areas) {
		for (const SpawnEntryData& entry : area.entries) {
			shortRespawns += entry.spawnTime < 10 ? 1 : 0;
			weightedSets += entry.alternatives.size() > 1 ? 1 : 0;
		}
	}
	wxString preview;
	preview << "Summary: " << document.areas.size() << " spawn areas, " << document.monsterCount() << " monsters and " << document.npcCount() << " NPCs.";
	if (!modern && shortRespawns > 0) {
		preview << "\nWarning: " << shortRespawns << " respawn value(s) below 10 seconds will be adjusted to the TFS minimum.";
	}
	if (weightedSets > 0) {
		preview << (modern ? "\nWeighted alternatives will be written as Crystal weights." : "\nWeighted alternatives will be converted to TFS chance sets.");
	}
	preview << "\nThe generated XML is reopened and compared before the operation is accepted.";
	previewLabel->SetLabel(preview);
	Layout();
}

void SpawnExportWindow::OnFormatChanged(wxCommandEvent& WXUNUSED(event)) {
	UpdatePreview();
}

bool SpawnExportWindow::ValidateFilename(const wxString& filename, const wxString& label) const {
	const wxString trimmed = filename.Strip(wxString::both);
	if (trimmed.empty() || wxFileName(trimmed).GetFullName() != trimmed || trimmed == "." || trimmed == "..") {
		wxMessageBox(label + " must be a filename without folders.", "Invalid spawn filename", wxOK | wxICON_ERROR, const_cast<SpawnExportWindow*>(this));
		return false;
	}
	return true;
}

void SpawnExportWindow::OnConfirm(wxCommandEvent& WXUNUSED(event)) {
	if (directoryPicker->GetPath().empty()) {
		wxMessageBox("Select a destination folder.", "Missing destination", wxOK | wxICON_ERROR, this);
		return;
	}
	const bool modern = formatChoice->GetSelection() == 1;
	if ((!modern && !ValidateFilename(tfsFilename->GetValue(), "TFS filename")) ||
		(modern && (!ValidateFilename(monsterFilename->GetValue(), "Monster filename") || !ValidateFilename(npcFilename->GetValue(), "NPC filename")))) {
		return;
	}

	const SpawnExportOptions options = GetOptions();
	std::vector<std::filesystem::path> existing;
	const std::filesystem::path primary = options.directory / options.primaryFilename;
	const std::filesystem::path npc = options.directory / options.npcFilename;
	if (modern && FileSaveTransaction::PathsReferToSameFile(primary, npc)) {
		wxMessageBox("Monster and NPC output files must be different.", "Invalid spawn filenames", wxOK | wxICON_ERROR, this);
		return;
	}
	if (std::filesystem::exists(primary)) {
		existing.push_back(primary.filename());
	}
	if (modern) {
		if (std::filesystem::exists(npc)) {
			existing.push_back(npc.filename());
		}
	}
	if (!existing.empty()) {
		wxString names;
		for (const auto& file : existing) {
			names << "\n  " << wxstr(file.string());
		}
		if (wxMessageBox("The following spawn file(s) already exist:" + names + "\n\nOverwrite them?", "Confirm overwrite", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
			return;
		}
	}
	EndModal(wxID_OK);
}
