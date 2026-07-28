//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_PALETTE_ZONES_H_
#define RME_PALETTE_ZONES_H_

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/listctrl.h>
#include <wx/stattext.h>
#include <wx/tglbtn.h>

#include "palette_common.h"

class ZonesPalettePanel : public NamedEntityPalettePanel {
public:
	ZonesPalettePanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	~ZonesPalettePanel() override = default;

	wxString GetName() const override;
	PaletteType GetType() const override;

	Brush* GetSelectedBrush() const override;
	bool SelectBrush(const Brush* whatbrush) override;

	void OnSwitchIn() override;
	void OnUpdate() override;

	void OnClickZone(wxListEvent& event);
	void OnRightClickZone(wxListEvent& event);
	void OnEditZoneLabel(wxListEvent& event);
	void OnClickNewZone(wxCommandEvent& event);
	void OnClickRenameZone(wxCommandEvent& event);
	void OnClickDeleteZone(wxCommandEvent& event);
	void OnClickPaintZone(wxCommandEvent& event);
	void OnClickEraseZone(wxCommandEvent& event);
	void OnClickApplyToSelection(wxCommandEvent& event);
	void OnClickRemoveFromSelection(wxCommandEvent& event);
	void OnClickShowOverlay(wxCommandEvent& event);
	void OnClickSelectZoneTiles(wxCommandEvent& event);
	void OnClickActivateZone(wxCommandEvent& event);
	void OnClickFindFirstTile(wxCommandEvent& event);
	void OnClickImportZone(wxCommandEvent& event);
	void OnClickExportZone(wxCommandEvent& event);

	void SetMap(Map* map);

protected:
	unsigned int getSelectedZoneId() const;
	long findZoneItem(unsigned int zoneId) const;
	bool activateZone(unsigned int zoneId, bool eraseMode, bool showStatus = true);
	void selectZoneItem(unsigned int zoneId, bool ensureVisible = true);
	void updateActiveZoneDisplay();
	void updateControlStates();
	void applyZoneToSelection(bool remove);

	wxListCtrl* zone_list;
	wxStaticText* active_zone_label;
	wxButton* new_zone_button;
	wxButton* rename_zone_button;
	wxButton* delete_zone_button;
	wxToggleButton* paint_zone_button;
	wxToggleButton* erase_zone_button;
	wxButton* apply_selection_button;
	wxButton* remove_selection_button;
	wxButton* select_tiles_button;
	wxCheckBox* show_overlay_checkbox;
	wxButton* import_zone_button;
	wxButton* export_zone_button;

	bool editing_new_zone;
	bool rebuilding_list;

	DECLARE_EVENT_TABLE()
};

#endif
