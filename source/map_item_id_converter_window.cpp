//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "map_item_id_converter_window.h"

#include "file_transaction.h"
#include "gui.h"
#include "iomap.h"
#include "item_id_mapping.h"
#include "settings.h"

#include <wx/choice.h>
#include <wx/filepicker.h>
#include <wx/radiobox.h>
#include <wx/spinctrl.h>

#include <algorithm>
#include <limits>
#include <system_error>

namespace {
	wxString ConverterWindowPathToWxString(const std::filesystem::path& path) {
#ifdef _WIN32
		return wxString(path.wstring());
#else
		return wxString::FromUTF8(path.string());
#endif
	}

	class ClientIdViewCodec final : public ItemIdCodec {
	public:
		bool Decode(uint16_t storedId, uint16_t& serverId) const override {
			serverId = ItemIdMapping::clientToServer(storedId).converted;
			return true;
		}

		bool Encode(uint16_t serverId, uint16_t& storedId) const override {
			storedId = ItemIdMapping::serverToClient(serverId).converted;
			return true;
		}
	};
}

MapItemIdConverterWindow::MapItemIdConverterWindow(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Native OTBM Item ID Converter", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
	auto* topSizer = newd wxBoxSizer(wxVERTICAL);

	auto* intro = newd wxStaticText(
		this,
		wxID_ANY,
		"Convert every ground, item and nested container ID using the embedded native C++ mapping. "
		"The source is never modified. Output is staged, reopened and structurally validated before commit."
	);
	intro->Wrap(680);
	topSizer->Add(intro, 0, wxEXPAND | wxALL, 14);

	auto* fileBox = newd wxStaticBoxSizer(wxVERTICAL, this, "Files");
	auto* fileGrid = newd wxFlexGridSizer(2, 8, 12);
	fileGrid->AddGrowableCol(1, 1);
	fileGrid->Add(newd wxStaticText(fileBox->GetStaticBox(), wxID_ANY, "Source OTBM:"), 0, wxALIGN_CENTER_VERTICAL);
	sourcePicker = newd wxFilePickerCtrl(
		fileBox->GetStaticBox(), wxID_ANY, wxEmptyString, "Select the source OTBM map", "OpenTibia Binary Map (*.otbm)|*.otbm",
		wxDefaultPosition, wxDefaultSize, wxFLP_OPEN | wxFLP_FILE_MUST_EXIST | wxFLP_USE_TEXTCTRL
	);
	fileGrid->Add(sourcePicker, 1, wxEXPAND);
	fileGrid->Add(newd wxStaticText(fileBox->GetStaticBox(), wxID_ANY, "Destination:"), 0, wxALIGN_CENTER_VERTICAL);
	destinationPicker = newd wxFilePickerCtrl(
		fileBox->GetStaticBox(), wxID_ANY, wxEmptyString, "Select the output OTBM map", "OpenTibia Binary Map (*.otbm)|*.otbm",
		wxDefaultPosition, wxDefaultSize, wxFLP_SAVE | wxFLP_OVERWRITE_PROMPT | wxFLP_USE_TEXTCTRL
	);
	fileGrid->Add(destinationPicker, 1, wxEXPAND);
	fileBox->Add(fileGrid, 1, wxEXPAND | wxALL, 10);
	topSizer->Add(fileBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

	auto* versionBox = newd wxStaticBoxSizer(wxHORIZONTAL, this, "Map version");
	versionBox->Add(newd wxStaticText(versionBox->GetStaticBox(), wxID_ANY, "Target map version:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
	targetVersionChoice = newd wxChoice(versionBox->GetStaticBox(), wxID_ANY);
	const ClientVersionID currentVersion = g_gui.GetCurrentVersionID();
	int defaultVersionIndex = wxNOT_FOUND;
	for (ClientVersion* client : ClientVersion::getAllVisible()) {
		targetVersions.emplace_back(client->getPrefferedMapVersionID(), client->getID());
		targetVersionChoice->Append(wxstr(client->getName()));
		if (client->getID() == currentVersion) {
			defaultVersionIndex = static_cast<int>(targetVersions.size() - 1);
		}
	}
	if (!targetVersions.empty()) {
		targetVersionChoice->SetSelection(defaultVersionIndex == wxNOT_FOUND ? static_cast<int>(targetVersions.size() - 1) : defaultVersionIndex);
	}
	versionBox->Add(targetVersionChoice, 1, wxALIGN_CENTER_VERTICAL);
	topSizer->Add(versionBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

	wxString directions[] = { "Server ID -> Client ID", "Client ID -> Server ID" };
	directionChoice = newd wxRadioBox(this, wxID_ANY, "Direction", wxDefaultPosition, wxDefaultSize, 2, directions, 1, wxRA_SPECIFY_COLS);
	directionChoice->SetSelection(0);
	topSizer->Add(directionChoice, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

	auto* performanceBox = newd wxStaticBoxSizer(wxVERTICAL, this, "Performance");
	wxString processingModes[] = { "Automatic", "Custom" };
	processingModeChoice = newd wxRadioBox(performanceBox->GetStaticBox(), wxID_ANY, "Processing mode", wxDefaultPosition, wxDefaultSize, 2, processingModes, 1, wxRA_SPECIFY_COLS);
	processingModeChoice->SetSelection(0);
	performanceBox->Add(processingModeChoice, 0, wxEXPAND | wxALL, 8);

	const MapItemIdPerformanceLimits limits = GetMapItemIdConverterPerformanceLimits();
	const int maximumThreads = static_cast<int>(std::min<uint32_t>(limits.hardwareThreads, static_cast<uint32_t>(std::numeric_limits<int>::max())));
	const uint64_t maximumCustomMemoryGiB = std::max<uint64_t>(1, limits.maximumCustomMemoryLimitBytes / (1024ull * 1024ull * 1024ull));
	maximumMemoryLimitGiB = static_cast<int>(std::min<uint64_t>(maximumCustomMemoryGiB, static_cast<uint64_t>(std::numeric_limits<int>::max())));
	auto* performanceGrid = newd wxFlexGridSizer(2, 8, 12);
	performanceGrid->AddGrowableCol(1, 1);
	performanceGrid->Add(newd wxStaticText(performanceBox->GetStaticBox(), wxID_ANY, "CPU threads:"), 0, wxALIGN_CENTER_VERTICAL);
	cpuThreadsSpin = newd wxSpinCtrl(performanceBox->GetStaticBox(), wxID_ANY);
	cpuThreadsSpin->SetRange(1, std::max(1, maximumThreads));
	cpuThreadsSpin->SetValue(std::min(4, std::max(1, maximumThreads)));
	performanceGrid->Add(cpuThreadsSpin, 1, wxEXPAND);
	performanceGrid->Add(newd wxStaticText(performanceBox->GetStaticBox(), wxID_ANY, "Memory limit (GB):"), 0, wxALIGN_CENTER_VERTICAL);
	const int storedMemoryGiB = std::clamp(g_settings.getInteger(Config::MAP_ITEM_ID_CONVERTER_MEMORY_LIMIT_GIB), 1, maximumMemoryLimitGiB);
	memoryLimitSpin = newd wxSpinCtrl(performanceBox->GetStaticBox(), wxID_ANY);
	memoryLimitSpin->SetRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
	memoryLimitSpin->SetValue(storedMemoryGiB);
	performanceGrid->Add(memoryLimitSpin, 1, wxEXPAND);
	performanceBox->Add(performanceGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
	automaticPerformanceHelp = newd wxStaticText(performanceBox->GetStaticBox(), wxID_ANY, "Automatic mode chooses safe values from available CPU, memory, process architecture and source file size. Memory is a limit, not a preallocation.");
	automaticPerformanceHelp->Wrap(680);
	performanceBox->Add(automaticPerformanceHelp, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
	wxString customHelp;
	customHelp << "Custom mode accepts whole values from 1 to " << maximumMemoryLimitGiB << " GB on this " << (sizeof(void*) >= 8 ? "64-bit" : "32-bit") << " build. Memory is a limit, not a preallocation.";
	if (sizeof(void*) < 8) {
		customHelp << " The smaller maximum is required by the 32-bit process address space.";
	}
	customPerformanceHelp = newd wxStaticText(performanceBox->GetStaticBox(), wxID_ANY, customHelp);
	customPerformanceHelp->Wrap(680);
	performanceBox->Add(customPerformanceHelp, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
	topSizer->Add(performanceBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

	const auto statistics = ItemIdMapping::statistics();
	wxString mappingSummary;
	mappingSummary << "Embedded mapping: " << statistics.mapping_count << " forward pairs, "
				   << statistics.unique_client_count << " unique client IDs, "
				   << statistics.collision_count << " ambiguous client IDs. No external runtime or scripts are used.";
	auto* mappingLabel = newd wxStaticText(this, wxID_ANY, mappingSummary);
	mappingLabel->Wrap(680);
	topSizer->Add(mappingLabel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

	wxSizer* buttons = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
	if (buttons) {
		if (wxWindow* confirm = FindWindow(wxID_OK)) {
			confirm->SetLabel("Convert and validate");
		}
		topSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);
	}

	SetSizerAndFit(topSizer);
	SetMinSize(wxSize(740, GetSize().GetHeight()));
	CentreOnParent();

	sourcePicker->Bind(wxEVT_FILEPICKER_CHANGED, &MapItemIdConverterWindow::onSourceChanged, this);
	directionChoice->Bind(wxEVT_RADIOBOX, &MapItemIdConverterWindow::onDirectionChanged, this);
	processingModeChoice->Bind(wxEVT_RADIOBOX, &MapItemIdConverterWindow::onPerformanceModeChanged, this);
	Bind(wxEVT_BUTTON, &MapItemIdConverterWindow::onConfirm, this, wxID_OK);
	updatePerformanceControls();
}

MapItemIdConversionOptions MapItemIdConverterWindow::getOptions() const {
	MapItemIdConversionOptions options;
	options.source = std::filesystem::u8path(nstr(sourcePicker->GetPath()));
	options.destination = std::filesystem::u8path(nstr(destinationPicker->GetPath()));
	options.direction = directionChoice->GetSelection() == 0 ? ItemIdMapping::Direction::ServerToClient : ItemIdMapping::Direction::ClientToServer;
	const int targetSelection = targetVersionChoice->GetSelection();
	if (targetSelection != wxNOT_FOUND && static_cast<std::size_t>(targetSelection) < targetVersions.size()) {
		options.targetVersion = targetVersions[static_cast<std::size_t>(targetSelection)];
	}
	options.performance.mode = processingModeChoice->GetSelection() == 0 ? MapItemIdProcessingMode::Automatic : MapItemIdProcessingMode::Custom;
	options.performance.cpuThreads = static_cast<uint32_t>(cpuThreadsSpin->GetValue());
	wxULongLong_t memoryGiB = 0;
	if (!memoryLimitSpin->GetTextValue().ToULongLong(&memoryGiB) || memoryGiB > std::numeric_limits<uint64_t>::max() / (1024ull * 1024ull * 1024ull)) {
		memoryGiB = 0;
	}
	options.performance.memoryLimitBytes = static_cast<uint64_t>(memoryGiB) * 1024ull * 1024ull * 1024ull;
	return options;
}

void MapItemIdConverterWindow::updateDestination() {
	const wxString source = sourcePicker->GetPath();
	if (source.empty()) {
		return;
	}
	const wxString currentDestination = destinationPicker->GetPath();
	if (!currentDestination.empty() && currentDestination != generatedDestination) {
		return;
	}

	wxFileName destination(source);
	destination.SetName(destination.GetName() + (directionChoice->GetSelection() == 0 ? "-client" : "-server"));
	destination.SetExt("otbm");
	generatedDestination = destination.GetFullPath();
	destinationPicker->SetPath(generatedDestination);
}

void MapItemIdConverterWindow::updatePerformanceControls() {
	const bool custom = processingModeChoice->GetSelection() == 1;
	cpuThreadsSpin->Enable(custom);
	memoryLimitSpin->Enable(custom);
	automaticPerformanceHelp->Show(!custom);
	customPerformanceHelp->Show(custom);
	Layout();
}

void MapItemIdConverterWindow::onSourceChanged(wxFileDirPickerEvent& WXUNUSED(event)) {
	updateDestination();
}

void MapItemIdConverterWindow::onDirectionChanged(wxCommandEvent& WXUNUSED(event)) {
	updateDestination();
}

void MapItemIdConverterWindow::onPerformanceModeChanged(wxCommandEvent& WXUNUSED(event)) {
	updatePerformanceControls();
}

void MapItemIdConverterWindow::onConfirm(wxCommandEvent& WXUNUSED(event)) {
	const bool customMode = processingModeChoice->GetSelection() == 1;
	int selectedMemoryGiB = 0;
	if (customMode) {
		wxString memoryText = memoryLimitSpin->GetTextValue();
		memoryText.Trim(true).Trim(false);
		wxULongLong_t memoryGiB = 0;
		if (memoryText.empty() || memoryText.StartsWith("-") || !memoryText.ToULongLong(&memoryGiB) || memoryGiB == 0) {
			memoryLimitSpin->SetFocus();
			memoryLimitSpin->SetSelection(-1, -1);
			wxMessageBox("Enter a positive whole number of GB for the memory limit.", "Invalid memory limit", wxOK | wxICON_ERROR, this);
			return;
		}
		if (memoryGiB > static_cast<wxULongLong_t>(maximumMemoryLimitGiB)) {
			memoryLimitSpin->SetFocus();
			memoryLimitSpin->SetSelection(-1, -1);
			wxMessageBox(wxString::Format("Enter a memory limit from 1 to %d GB for this machine and process architecture.", maximumMemoryLimitGiB), "Unsafe memory limit", wxOK | wxICON_ERROR, this);
			return;
		}
		selectedMemoryGiB = static_cast<int>(memoryGiB);
		memoryLimitSpin->SetValue(selectedMemoryGiB);
	}
	const MapItemIdConversionOptions options = getOptions();
	std::error_code filesystemError;
	if (options.source.empty() || !std::filesystem::is_regular_file(options.source, filesystemError) || filesystemError) {
		wxMessageBox("Select an existing source .otbm file.", "Invalid source", wxOK | wxICON_ERROR, this);
		return;
	}
	if (options.destination.empty()) {
		wxMessageBox("Select a destination .otbm file.", "Missing destination", wxOK | wxICON_ERROR, this);
		return;
	}
	if (ClientVersion::get(options.targetVersion.client) == nullptr) {
		wxMessageBox("Select a supported target map version.", "Missing target version", wxOK | wxICON_ERROR, this);
		return;
	}
	if (as_lower_str(options.source.extension().string()) != ".otbm" || as_lower_str(options.destination.extension().string()) != ".otbm") {
		wxMessageBox("Source and destination must use the .otbm extension.", "Invalid file type", wxOK | wxICON_ERROR, this);
		return;
	}
	if (FileSaveTransaction::PathsReferToSameFile(options.source, options.destination)) {
		wxMessageBox("Source and destination must be different files.", "Unsafe destination", wxOK | wxICON_ERROR, this);
		return;
	}
	filesystemError.clear();
	const bool destinationExists = std::filesystem::exists(options.destination, filesystemError);
	if (filesystemError) {
		wxMessageBox("The destination cannot be inspected.", "Invalid destination", wxOK | wxICON_ERROR, this);
		return;
	}
	if (destinationExists && wxMessageBox("The destination already exists. Replace it after successful validation?", "Confirm replacement", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
		return;
	}
	if (customMode) {
		g_settings.setInteger(Config::MAP_ITEM_ID_CONVERTER_MEMORY_LIMIT_GIB, selectedMemoryGiB);
		g_settings.save();
	}
	EndModal(wxID_OK);
}

bool RunMapItemIdConverter(wxWindow* parent, MapItemIdConverterLaunchContext context) {
	MapItemIdConverterWindow dialog(parent);
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}

	const MapItemIdConversionOptions options = dialog.getOptions();
	const MapItemIdConversionReport report = ConvertMapItemIds(options);
	const wxString title = report.success ? "OTBM Item ID Conversion Complete" : (report.cancelled ? "OTBM Item ID Conversion Cancelled" : "OTBM Item ID Conversion Failed");
	g_gui.ShowTextBox(parent, title, wxstr(report.format(options)));
	if (!report.success) {
		return false;
	}

	if (context == MapItemIdConverterLaunchContext::Editor && report.outputValidated) {
		const FileName destination(ConverterWindowPathToWxString(options.destination));
		if (options.direction == ItemIdMapping::Direction::ClientToServer) {
			return g_gui.LoadValidatedConvertedMap(destination);
		}
		ClientIdViewCodec readCodec;
		return g_gui.LoadValidatedConvertedMap(destination, &readCodec, true);
	}
	return true;
}
