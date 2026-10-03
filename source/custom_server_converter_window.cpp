//////////////////////////////////////////////////////////////////////
// This file is part of NexaMap Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "custom_server_converter_window.h"

#include "map_item_id_converter_window.h"

#include <iostream>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choicdlg.h>
#include <wx/filepicker.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace {
	std::filesystem::path PickerPath(const wxString& path) {
#ifdef _WIN32
		return std::filesystem::path(path.ToStdWstring());
#else
		return std::filesystem::path(path.ToStdString());
#endif
	}

	wxString PickerString(const std::filesystem::path& path) {
#ifdef _WIN32
		return wxString(path.wstring());
#else
		return wxString::FromUTF8(path.string());
#endif
	}

	std::filesystem::path NormalizePath(const std::filesystem::path& path) {
		std::error_code error;
		const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
		if (!error) {
			return canonical;
		}
		const std::filesystem::path absolute = std::filesystem::absolute(path, error);
		return (error ? path : absolute).lexically_normal();
	}

	bool IsWithin(const std::filesystem::path& child, const std::filesystem::path& parent) {
		const std::filesystem::path relative = NormalizePath(child).lexically_relative(NormalizePath(parent));
		return relative == "." || (!relative.empty() && *relative.begin() != "..");
	}

	std::filesystem::path AvailableConvertedFolder(const std::filesystem::path& parent, const std::filesystem::path& source) {
		const std::string baseName = source.filename().string() + "-clientid-converted";
		for (unsigned int suffix = 1; suffix < 1000; ++suffix) {
			const std::filesystem::path candidate = parent / (suffix == 1 ? baseName : baseName + "-" + std::to_string(suffix));
			std::error_code error;
			if (!std::filesystem::exists(candidate, error) || (!error && std::filesystem::is_directory(candidate, error) && std::filesystem::is_empty(candidate, error))) {
				return candidate;
			}
		}
		return parent / (baseName + "-new");
	}

	std::filesystem::path ResolveSafeDestination(const std::filesystem::path& source, const std::filesystem::path& selected) {
		const std::filesystem::path normalizedSource = NormalizePath(source);
		const std::filesystem::path normalizedDestination = NormalizePath(selected);
		if (normalizedDestination == normalizedSource || IsWithin(normalizedDestination, normalizedSource)) {
			return AvailableConvertedFolder(normalizedSource.parent_path(), normalizedSource);
		}
		if (IsWithin(normalizedSource, normalizedDestination)) {
			return AvailableConvertedFolder(normalizedDestination, normalizedSource);
		}
		return normalizedDestination;
	}
}

CustomServerConverterWindow::CustomServerConverterWindow(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Custom Server -> ClientID", wxDefaultPosition, wxSize(820, 680), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
	auto* topSizer = newd wxBoxSizer(wxVERTICAL);
	auto* intro = newd wxStaticText(this, wxID_ANY,
		"Convert maps, items.xml, and a normalized items.otb with the selected server's own read-only mapping. "
		"The source server and its original items.otb are never modified.");
	intro->Wrap(760);
	topSizer->Add(intro, 0, wxEXPAND | wxALL, 14);

	auto* pathsBox = newd wxStaticBoxSizer(wxVERTICAL, this, "Source and destination");
	auto* grid = newd wxFlexGridSizer(2, 8, 12);
	grid->AddGrowableCol(1, 1);
	grid->Add(newd wxStaticText(pathsBox->GetStaticBox(), wxID_ANY, "Source server:"), 0, wxALIGN_CENTER_VERTICAL);
	sourcePicker = newd wxDirPickerCtrl(pathsBox->GetStaticBox(), wxID_ANY, wxEmptyString, "Select the custom OT server root", wxDefaultPosition, wxDefaultSize, wxDIRP_DIR_MUST_EXIST | wxDIRP_USE_TEXTCTRL);
	grid->Add(sourcePicker, 1, wxEXPAND);
	grid->Add(newd wxStaticText(pathsBox->GetStaticBox(), wxID_ANY, "Output folder:"), 0, wxALIGN_CENTER_VERTICAL);
	destinationPicker = newd wxDirPickerCtrl(pathsBox->GetStaticBox(), wxID_ANY, wxEmptyString, "Select a separate output folder", wxDefaultPosition, wxDefaultSize, wxDIRP_USE_TEXTCTRL);
	grid->Add(destinationPicker, 1, wxEXPAND);
	pathsBox->Add(grid, 1, wxEXPAND | wxALL, 10);
	topSizer->Add(pathsBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

	auto* scopeBox = newd wxStaticBoxSizer(wxHORIZONTAL, this, "Convert");
	mapsCheck = newd wxCheckBox(scopeBox->GetStaticBox(), wxID_ANY, "All detected maps");
	mapsCheck->SetValue(true);
	itemsXmlCheck = newd wxCheckBox(scopeBox->GetStaticBox(), wxID_ANY, "items.xml");
	itemsXmlCheck->SetValue(true);
	itemsOtbCheck = newd wxCheckBox(scopeBox->GetStaticBox(), wxID_ANY, "items.otb (ServerID = ClientID)");
	itemsOtbCheck->SetValue(true);
	scopeBox->Add(mapsCheck, 0, wxALL, 8);
	scopeBox->Add(itemsXmlCheck, 0, wxALL, 8);
	scopeBox->Add(itemsOtbCheck, 0, wxALL, 8);
	topSizer->Add(scopeBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

	auto* safetyBox = newd wxStaticBoxSizer(wxVERTICAL, this, "Compatibility override");
	allowDuplicateClientIdsCheck = newd wxCheckBox(safetyBox->GetStaticBox(), wxID_ANY, "Allow duplicate ClientID aliases (unsafe: distinct ServerIDs will merge)");
	allowDuplicateClientIdsCheck->SetValue(false);
	safetyBox->Add(allowDuplicateClientIdsCheck, 0, wxALL, 8);
	topSizer->Add(safetyBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

	auto* detectedBox = newd wxStaticBoxSizer(wxVERTICAL, this, "Detected resources and analysis");
	analysisText = newd wxTextCtrl(detectedBox->GetStaticBox(), wxID_ANY, "Select a server folder to analyze.", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
	detectedBox->Add(analysisText, 1, wxEXPAND | wxALL, 8);
	topSizer->Add(detectedBox, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

	auto* buttons = newd wxBoxSizer(wxHORIZONTAL);
	auto* analyzeButton = newd wxButton(this, wxID_ANY, "Analyze");
	convertButton = newd wxButton(this, wxID_ANY, "Convert and validate");
	convertButton->Enable(false);
	buttons->Add(analyzeButton, 0, wxRIGHT, 8);
	buttons->Add(convertButton, 0, wxRIGHT, 8);
	buttons->AddStretchSpacer();
	buttons->Add(newd wxButton(this, wxID_CANCEL, "Close"), 0);
	topSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

	SetSizer(topSizer);
	SetMinSize(wxSize(720, 560));
	CentreOnParent();

	sourcePicker->Bind(wxEVT_DIRPICKER_CHANGED, &CustomServerConverterWindow::onSourceChanged, this);
	destinationPicker->Bind(wxEVT_DIRPICKER_CHANGED, &CustomServerConverterWindow::onDestinationChanged, this);
	analyzeButton->Bind(wxEVT_BUTTON, &CustomServerConverterWindow::onAnalyze, this);
	convertButton->Bind(wxEVT_BUTTON, &CustomServerConverterWindow::onConvert, this);
	mapsCheck->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { updateConvertState(); });
	itemsXmlCheck->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { updateConvertState(); });
	itemsOtbCheck->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { updateConvertState(); });
	allowDuplicateClientIdsCheck->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { analyze(); });
}

void CustomServerConverterWindow::analyze() {
	const std::filesystem::path source = PickerPath(sourcePicker->GetPath());
	if (source.empty()) {
		currentAnalysis.reset();
		analysisText->SetValue("Select a server folder to analyze.");
		updateConvertState();
		return;
	}
	analysisText->SetValue("Analyzing items.otb, items.xml and maps...");
	Update();
	currentAnalysis = AnalyzeCustomServer(source, allowDuplicateClientIdsCheck->GetValue());
	analysisText->SetValue(wxString::FromUTF8(currentAnalysis->format()));
	updateConvertState();
}

void CustomServerConverterWindow::updateConvertState() {
	convertButton->Enable(currentAnalysis && currentAnalysis->ready && !destinationPicker->GetPath().empty() && (mapsCheck->GetValue() || itemsXmlCheck->GetValue() || itemsOtbCheck->GetValue()));
}

void CustomServerConverterWindow::onSourceChanged(wxFileDirPickerEvent& WXUNUSED(event)) {
	analyze();
}

void CustomServerConverterWindow::onDestinationChanged(wxFileDirPickerEvent& WXUNUSED(event)) {
	updateConvertState();
}

void CustomServerConverterWindow::onAnalyze(wxCommandEvent& WXUNUSED(event)) {
	analyze();
}

void CustomServerConverterWindow::onConvert(wxCommandEvent& WXUNUSED(event)) {
	if (!currentAnalysis || !currentAnalysis->ready) {
		analyze();
		if (!currentAnalysis || !currentAnalysis->ready) {
			return;
		}
	}
	CustomServerConversionOptions options;
	options.sourceRoot = PickerPath(sourcePicker->GetPath());
	options.destinationRoot = PickerPath(destinationPicker->GetPath());
	const std::filesystem::path safeDestination = ResolveSafeDestination(options.sourceRoot, options.destinationRoot);
	if (safeDestination != NormalizePath(options.destinationRoot)) {
		options.destinationRoot = safeDestination;
		destinationPicker->SetPath(PickerString(safeDestination));
		wxMessageBox(
			"The selected folder contains the source server. NexaMap selected a safe separate output folder instead:\n\n" + PickerString(safeDestination),
			"Output folder adjusted",
			wxOK | wxICON_INFORMATION,
			this
		);
	}
	options.convertMaps = mapsCheck->GetValue();
	options.convertItemsXml = itemsXmlCheck->GetValue();
	options.convertItemsOtb = itemsOtbCheck->GetValue();
	options.allowDuplicateClientIds = allowDuplicateClientIdsCheck->GetValue();
	if (wxMessageBox("Convert to the selected output folder? The source server will remain unchanged.", "Confirm custom conversion", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this) != wxYES) {
		return;
	}
	const CustomServerConversionReport report = ConvertCustomServer(options);
	if (report.success) {
		std::cout << "[CustomConverter] SUCCESS: output committed to " << options.destinationRoot.string() << std::endl;
	} else {
		std::cerr << "[CustomConverter] FAILED: " << report.error << std::endl;
	}
	const wxString text = wxString::FromUTF8(report.format(options));
	wxMessageBox(text, report.success ? "Custom conversion complete" : "Custom conversion failed", wxOK | (report.success ? wxICON_INFORMATION : wxICON_ERROR), this);
}

void RunCustomServerConverter(wxWindow* parent) {
	CustomServerConverterWindow dialog(parent);
	dialog.ShowModal();
}

void RunConvertersChooser(wxWindow* parent) {
	const wxArrayString choices {
		"Standard OTBM Item ID Converter",
		"Custom Server -> ClientID",
	};
	wxSingleChoiceDialog chooser(parent, "Choose a converter. The custom server converter uses the selected server's own items.otb.", "Converters", choices);
	if (chooser.ShowModal() != wxID_OK) {
		return;
	}
	if (chooser.GetSelection() == 0) {
		static_cast<void>(RunMapItemIdConverter(parent, MapItemIdConverterLaunchContext::Welcome));
	} else {
		RunCustomServerConverter(parent);
	}
}
