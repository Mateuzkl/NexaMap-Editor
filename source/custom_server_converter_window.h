//////////////////////////////////////////////////////////////////////
// This file is part of NexaMap Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_CUSTOM_SERVER_CONVERTER_WINDOW_H_
#define RME_CUSTOM_SERVER_CONVERTER_WINDOW_H_

#include "custom_server_converter.h"

#include <wx/dialog.h>

#include <optional>

class wxButton;
class wxCheckBox;
class wxDirPickerCtrl;
class wxFileDirPickerEvent;
class wxTextCtrl;

class CustomServerConverterWindow final : public wxDialog {
public:
	explicit CustomServerConverterWindow(wxWindow* parent);

private:
	void analyze();
	void updateConvertState();
	void onSourceChanged(wxFileDirPickerEvent& event);
	void onDestinationChanged(wxFileDirPickerEvent& event);
	void onAnalyze(wxCommandEvent& event);
	void onConvert(wxCommandEvent& event);

	wxDirPickerCtrl* sourcePicker = nullptr;
	wxDirPickerCtrl* destinationPicker = nullptr;
	wxTextCtrl* analysisText = nullptr;
	wxCheckBox* mapsCheck = nullptr;
	wxCheckBox* itemsXmlCheck = nullptr;
	wxCheckBox* itemsOtbCheck = nullptr;
	wxCheckBox* allowDuplicateClientIdsCheck = nullptr;
	wxButton* convertButton = nullptr;
	std::optional<CustomServerAnalysis> currentAnalysis;
};

void RunCustomServerConverter(wxWindow* parent);
void RunConvertersChooser(wxWindow* parent);

#endif // RME_CUSTOM_SERVER_CONVERTER_WINDOW_H_
