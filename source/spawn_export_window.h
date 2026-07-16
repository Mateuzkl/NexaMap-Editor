#ifndef RME_SPAWN_EXPORT_WINDOW_H_
#define RME_SPAWN_EXPORT_WINDOW_H_

#include "spawn_format.h"

#include <wx/dialog.h>

class Map;
class wxChoice;
class wxDirPickerCtrl;
class wxStaticText;
class wxTextCtrl;
class wxWindow;

struct SpawnExportOptions {
	SpawnFormat format = SpawnFormat::Tfs;
	std::filesystem::path directory;
	std::string primaryFilename;
	std::string npcFilename;
};

class SpawnExportWindow final : public wxDialog {
public:
	SpawnExportWindow(wxWindow* parent, Map& map, const wxString& directory, const wxString& mapBaseName, bool saveAsMode);

	SpawnExportOptions GetOptions() const;

private:
	void UpdatePreview();
	void OnFormatChanged(wxCommandEvent& event);
	void OnConfirm(wxCommandEvent& event);
	bool ValidateFilename(const wxString& filename, const wxString& label) const;

	Map& map;
	SpawnDocument document;
	bool saveAsMode;
	wxChoice* formatChoice = nullptr;
	wxDirPickerCtrl* directoryPicker = nullptr;
	wxTextCtrl* tfsFilename = nullptr;
	wxTextCtrl* monsterFilename = nullptr;
	wxTextCtrl* npcFilename = nullptr;
	wxStaticText* sourceLabel = nullptr;
	wxStaticText* previewLabel = nullptr;
};

#endif
