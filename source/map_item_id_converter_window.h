//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_MAP_ITEM_ID_CONVERTER_WINDOW_H_
#define RME_MAP_ITEM_ID_CONVERTER_WINDOW_H_

#include "map_item_id_converter.h"

#include <wx/dialog.h>

#include <vector>

class wxFilePickerCtrl;
class wxFileDirPickerEvent;
class wxChoice;
class wxRadioBox;
class wxSpinCtrl;
class wxStaticText;

class MapItemIdConverterWindow final : public wxDialog {
public:
	explicit MapItemIdConverterWindow(wxWindow* parent);

	[[nodiscard]] MapItemIdConversionOptions getOptions() const;

private:
	void updateDestination();
	void updatePerformanceControls();
	void onSourceChanged(wxFileDirPickerEvent& event);
	void onDirectionChanged(wxCommandEvent& event);
	void onPerformanceModeChanged(wxCommandEvent& event);
	void onConfirm(wxCommandEvent& event);

	wxFilePickerCtrl* sourcePicker = nullptr;
	wxFilePickerCtrl* destinationPicker = nullptr;
	wxRadioBox* directionChoice = nullptr;
	wxChoice* targetVersionChoice = nullptr;
	wxRadioBox* processingModeChoice = nullptr;
	wxSpinCtrl* cpuThreadsSpin = nullptr;
	wxSpinCtrl* memoryLimitSpin = nullptr;
	wxStaticText* automaticPerformanceHelp = nullptr;
	wxStaticText* customPerformanceHelp = nullptr;
	int maximumMemoryLimitGiB = 1;
	std::vector<MapVersion> targetVersions;
	wxString generatedDestination;
};

enum class MapItemIdConverterLaunchContext {
	Welcome,
	Editor,
};

[[nodiscard]] bool RunMapItemIdConverter(wxWindow* parent, MapItemIdConverterLaunchContext context);

#endif // RME_MAP_ITEM_ID_CONVERTER_WINDOW_H_
