//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#ifndef RME_RESULT_WINDOW_H_
#define RME_RESULT_WINDOW_H_

#include "main.h"

class Tile;

class SearchResultWindow : public wxPanel {
public:
	SearchResultWindow(wxWindow* parent);
	~SearchResultWindow() override;

	void Clear();
	void SetDuplicateMode(bool duplicateMode);
	void AddPosition(wxString description, Position pos);
	void AddDuplicateItem(const Position& pos, uint16_t itemId, uint16_t count);

	void OnClickResult(wxCommandEvent&);
	void OnClickExport(wxCommandEvent&);
	void OnClickClear(wxCommandEvent&);
	void OnClickRemove(wxCommandEvent&);
	void OnClickRemoveAll(wxCommandEvent&);

protected:
	struct ResultData;

	void DeleteResultData(uint32_t index);
	ResultData* GetResultData(int32_t index) const;
	void UpdateButtons();
	void UpdateDuplicateLabel(int32_t index, ResultData* data);
	bool RemoveDuplicateItem(ResultData* data, uint16_t amount);
	uint16_t RemoveDuplicateItemsFromTile(Tile* tile, uint16_t itemId, uint16_t amount);

	wxListBox* result_list;
	wxButton* export_button;
	wxButton* clear_button;
	wxButton* remove_button;
	wxButton* remove_all_button;
	bool duplicate_mode;

	DECLARE_EVENT_TABLE()
};

#endif
