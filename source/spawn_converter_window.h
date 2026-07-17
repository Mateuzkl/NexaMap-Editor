#ifndef RME_SPAWN_CONVERTER_WINDOW_H_
#define RME_SPAWN_CONVERTER_WINDOW_H_

#include <wx/dialog.h>

#include <filesystem>
#include <vector>

class wxCheckBox;
class wxDirPickerCtrl;
class wxFileDirPickerEvent;
class wxFilePickerCtrl;
class wxNotebook;
struct SpawnXmlConversionResult;

class SpawnConverterWindow final : public wxDialog {
public:
	explicit SpawnConverterWindow(wxWindow* parent);

private:
	void OnCanaryMonsterChanged(wxFileDirPickerEvent& event);
	void OnCanaryNpcChanged(wxFileDirPickerEvent& event);
	void OnTfsSourceChanged(wxFileDirPickerEvent& event);
	void OnOutputDirectoryChanged(wxFileDirPickerEvent& event);
	void OnPluralMonsterChanged(wxCommandEvent& event);
	void OnConvert(wxCommandEvent& event);

	void UpdateCanarySuggestions(wxFilePickerCtrl* changedPicker);
	void UpdateTfsSuggestions(bool forceMonsterName = false);
	bool ConfirmOverwrite(const std::vector<std::filesystem::path>& files, bool& allowOverwrite);
	void ShowConversionResult(const SpawnXmlConversionResult& result);

	wxNotebook* notebook = nullptr;
	wxFilePickerCtrl* canaryMonsterPicker = nullptr;
	wxFilePickerCtrl* canaryNpcPicker = nullptr;
	wxFilePickerCtrl* canaryOutputPicker = nullptr;
	wxFilePickerCtrl* tfsSourcePicker = nullptr;
	wxDirPickerCtrl* outputDirectoryPicker = nullptr;
	wxFilePickerCtrl* monsterOutputPicker = nullptr;
	wxFilePickerCtrl* npcOutputPicker = nullptr;
	wxCheckBox* pluralMonsterCheck = nullptr;
	wxString generatedCanaryMonster;
	wxString generatedCanaryNpc;
	wxString generatedCanaryOutput;
	wxString generatedOutputDirectory;
	wxString generatedMonsterOutput;
	wxString generatedNpcOutput;
};

[[nodiscard]] bool RunSpawnConverter(wxWindow* parent);

#endif // RME_SPAWN_CONVERTER_WINDOW_H_
