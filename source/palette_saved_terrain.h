//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_PALETTE_SAVED_TERRAIN_H_
#define RME_PALETTE_SAVED_TERRAIN_H_

#include <wx/button.h>
#include <wx/listctrl.h>

#include "palette_common.h"

class SavedTerrainPalettePanel : public PalettePanel {
public:
	SavedTerrainPalettePanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	~SavedTerrainPalettePanel() override = default;

	wxString GetName() const override;
	PaletteType GetType() const override;

	Brush* GetSelectedBrush() const override;
	bool SelectBrush(const Brush* whatbrush) override;

	void OnSwitchIn() override;
	void OnUpdate() override;

	void OnClickStamp(wxListEvent& event);
	void OnClickSaveFromSelection(wxCommandEvent& event);
	void OnClickPlace(wxCommandEvent& event);
	void OnClickRename(wxCommandEvent& event);
	void OnClickDelete(wxCommandEvent& event);
	void OnClickImport(wxCommandEvent& event);
	void OnClickExport(wxCommandEvent& event);

	void ReloadList();

protected:
	wxString getSelectedName() const;
	void updateControlStates();
	bool placeSelectedStamp();

	wxListCtrl* stamp_list = nullptr;
	wxButton* save_button = nullptr;
	wxButton* place_button = nullptr;
	wxButton* rename_button = nullptr;
	wxButton* delete_button = nullptr;
	wxButton* import_button = nullptr;
	wxButton* export_button = nullptr;

	DECLARE_EVENT_TABLE()
};

// Shared entry point used by palette and map context menu.
bool PromptAndSaveTerrainFromSelection(wxWindow* parent);

#endif
