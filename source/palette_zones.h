//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_PALETTE_ZONES_H_
#define RME_PALETTE_ZONES_H_

#include <wx/listctrl.h>

#include "zones.h"
#include "palette_common.h"

class ZonesPalettePanel : public NamedEntityPalettePanel {
public:
	ZonesPalettePanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	~ZonesPalettePanel() override;

	wxString GetName() const override;
	PaletteType GetType() const override;

	Brush* GetSelectedBrush() const override;
	bool SelectBrush(const Brush* whatbrush) override;

	void OnUpdate() override;

	void OnClickZone(wxListEvent& event);
	void OnRightClickZone(wxListEvent& event);
	void OnBeginEditZoneLabel(wxListEvent& event);
	void OnEditZoneLabel(wxListEvent& event);
	void OnClickAddZone(wxCommandEvent& event);
	void OnClickRemoveZone(wxCommandEvent& event);
	void OnClickImportZone(wxCommandEvent& event);
	void OnClickExportZone(wxCommandEvent& event);

	void SetMap(Map* map);

protected:
	wxListCtrl* zone_list;
	wxButton* add_zone_button;
	wxButton* remove_zone_button;
	wxButton* import_zone_button;
	wxButton* export_zone_button;

	DECLARE_EVENT_TABLE()
};

#endif
