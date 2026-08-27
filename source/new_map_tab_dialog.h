#ifndef NEXAMAP_NEW_MAP_TAB_DIALOG_H_
#define NEXAMAP_NEW_MAP_TAB_DIALOG_H_

#include <wx/dialog.h>
#include <wx/filepicker.h>
#include <wx/tglbtn.h>

struct NewMapTabSelection {
	bool useCurrentClient = true;
	wxString clientDirectory;
	wxString serverDirectory;
	wxString mapFile;
};

class NewMapTabDialog final : public wxDialog {
public:
	explicit NewMapTabDialog(wxWindow* parent);
	NewMapTabSelection GetSelection() const;

private:
	void SelectMode(bool useCurrent);
	void OnCreate(wxCommandEvent& event);

	wxToggleButton* currentButton = nullptr;
	wxToggleButton* newButton = nullptr;
	wxDirPickerCtrl* clientPicker = nullptr;
	wxDirPickerCtrl* serverPicker = nullptr;
	wxFilePickerCtrl* mapPicker = nullptr;
	bool useCurrent = true;
};

#endif // NEXAMAP_NEW_MAP_TAB_DIALOG_H_
