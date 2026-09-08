//////////////////////////////////////////////////////////////////////
// Visual editor for reusable server Lua combat-area matrices.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "spell_area_editor_dialog.h"

#include "spell_area_resolver.h"
#include "theme.h"

#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/msgdlg.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <algorithm>

namespace {
	wxString PathText(const std::filesystem::path& path) {
#ifdef __WINDOWS__
		return wxString(path.wstring());
#else
		return wxString::FromUTF8(path.string());
#endif
	}
}

class SpellAreaGridPanel final : public wxPanel {
public:
	explicit SpellAreaGridPanel(wxWindow* parent) :
		wxPanel(parent, wxID_ANY) {
		SetInitialSize(FromDIP(wxSize(470, 470)));
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetDimensions(9, 9);
		Bind(wxEVT_PAINT, &SpellAreaGridPanel::onPaint, this);
		Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) { changeCell(event.GetPosition(), false); });
		Bind(wxEVT_RIGHT_DOWN, [this](wxMouseEvent& event) { changeCell(event.GetPosition(), true); });
	}

	void SetDimensions(std::size_t newWidth, std::size_t newHeight) {
		width = newWidth;
		height = newHeight;
		cells.assign(width * height, 0);
		cells[(height / 2) * width + width / 2] = 3;
		Refresh();
	}

	EditableSpellArea area(const std::string& name) const {
		return { name, width, height, cells };
	}

	void ClearAffected() {
		for (uint8_t& cell : cells) {
			if (cell == 1) {
				cell = 0;
			} else if (cell == 3) {
				cell = 2;
			}
		}
		Refresh();
	}

private:
	wxRect boardRect() const {
		const wxSize client = GetClientSize();
		const int cell = std::max(4, std::min(client.x / static_cast<int>(width), client.y / static_cast<int>(height)));
		const int boardWidth = cell * static_cast<int>(width);
		const int boardHeight = cell * static_cast<int>(height);
		return { (client.x - boardWidth) / 2, (client.y - boardHeight) / 2, boardWidth, boardHeight };
	}

	void changeCell(const wxPoint& point, bool setCenter) {
		const wxRect board = boardRect();
		if (!board.Contains(point)) {
			return;
		}
		const int cellSize = board.width / static_cast<int>(width);
		const std::size_t x = static_cast<std::size_t>((point.x - board.x) / cellSize);
		const std::size_t y = static_cast<std::size_t>((point.y - board.y) / cellSize);
		const std::size_t selected = y * width + x;
		if (setCenter) {
			for (uint8_t& cell : cells) {
				if (cell == 2) {
					cell = 0;
				} else if (cell == 3) {
					cell = 1;
				}
			}
			cells[selected] = cells[selected] == 1 ? 3 : 2;
		} else if (cells[selected] == 2) {
			cells[selected] = 3;
		} else if (cells[selected] == 3) {
			cells[selected] = 2;
		} else {
			cells[selected] = cells[selected] == 0 ? 1 : 0;
		}
		Refresh();
	}

	void onPaint(wxPaintEvent&) {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Surface)));
		dc.Clear();
		const wxRect board = boardRect();
		const int cellSize = board.width / static_cast<int>(width);
		for (std::size_t y = 0; y < height; ++y) {
			for (std::size_t x = 0; x < width; ++x) {
				const uint8_t value = cells[y * width + x];
				wxColour fill = Theme::Get(Theme::Role::RaisedSurface);
				if (value == 1) {
					fill = wxColour(216, 72, 72);
				} else if (value == 2) {
					fill = wxColour(55, 145, 225);
				} else if (value == 3) {
					fill = wxColour(165, 78, 215);
				}
				dc.SetPen(wxPen(wxColour(42, 86, 100)));
				dc.SetBrush(wxBrush(fill));
				dc.DrawRectangle(board.x + static_cast<int>(x) * cellSize, board.y + static_cast<int>(y) * cellSize, cellSize, cellSize);
			}
		}
	}

	std::size_t width = 9;
	std::size_t height = 9;
	std::vector<uint8_t> cells;
};

SpellAreaEditorDialog::SpellAreaEditorDialog(
	wxWindow* parent,
	const ServerWorkspace& selectedWorkspace,
	std::shared_ptr<const SpellAreaResolver> selectedResolver
) :
	wxDialog(parent, wxID_ANY, "Create Reusable Spell Area", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	workspace(selectedWorkspace), resolver(std::move(selectedResolver)), targets(SpellAreaLibrary::DiscoverTargets(workspace, *resolver)) {
	SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	auto* root = newd wxBoxSizer(wxVERTICAL);
	auto* intro = newd wxStaticText(this, wxID_ANY, "Draw the exact combat area used by player spells and registered monster spells. The existing Lua library is preserved byte-for-byte outside the new definition.");
	intro->Wrap(FromDIP(760));
	root->Add(intro, 0, wxEXPAND | wxALL, FromDIP(12));

	auto* form = newd wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
	form->AddGrowableCol(1, 1);
	form->Add(newd wxStaticText(this, wxID_ANY, "Area name"), 0, wxALIGN_CENTER_VERTICAL);
	name = newd wxTextCtrl(this, wxID_ANY, "AREA_CUSTOM");
	form->Add(name, 1, wxEXPAND);
	form->Add(newd wxStaticText(this, wxID_ANY, "Server library"), 0, wxALIGN_CENTER_VERTICAL);
	target = newd wxChoice(this, wxID_ANY);
	for (const SpellAreaLibraryTarget& entry : targets) {
		target->Append(PathText(entry.relativePath) + wxString::Format("  (%zu areas)", entry.existingDefinitions));
	}
	if (!targets.empty()) {
		target->SetSelection(0);
	}
	form->Add(target, 1, wxEXPAND);
	form->Add(newd wxStaticText(this, wxID_ANY, "Grid size"), 0, wxALIGN_CENTER_VERTICAL);
	auto* sizeRow = newd wxBoxSizer(wxHORIZONTAL);
	width = newd wxSpinCtrl(this, wxID_ANY, "9", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 31, 9);
	height = newd wxSpinCtrl(this, wxID_ANY, "9", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 31, 9);
	sizeRow->Add(width, 1);
	sizeRow->Add(newd wxStaticText(this, wxID_ANY, " x "), 0, wxALIGN_CENTER_VERTICAL);
	sizeRow->Add(height, 1);
	form->Add(sizeRow, 1, wxEXPAND);
	root->Add(form, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

	grid = newd SpellAreaGridPanel(this);
	root->Add(grid, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));
	auto* legend = newd wxStaticText(this, wxID_ANY, "Left click: affected tile   |   Right click: caster/target center   |   Purple: affected center");
	root->Add(legend, 0, wxEXPAND | wxALL, FromDIP(12));

	auto* actions = newd wxBoxSizer(wxHORIZONTAL);
	auto* clear = newd wxButton(this, wxID_ANY, "Clear affected tiles");
	actions->Add(clear, 0, wxRIGHT, FromDIP(8));
	status = newd wxStaticText(this, wxID_ANY, targets.empty() ? "No compatible spell-area library was detected in this Server Workspace." : "Ready to create an area.");
	status->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	actions->Add(status, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
	auto* save = newd wxButton(this, wxID_OK, "Save Area");
	save->Enable(!targets.empty());
	actions->Add(save, 0, wxRIGHT, FromDIP(8));
	actions->Add(newd wxButton(this, wxID_CANCEL, "Cancel"), 0);
	root->Add(actions, 0, wxEXPAND | wxALL, FromDIP(12));

	SetSizerAndFit(root);
	SetMinSize(FromDIP(wxSize(780, 690)));
	SetSize(FromDIP(wxSize(860, 760)));
	CentreOnParent();
	width->Bind(wxEVT_SPINCTRL, [this](wxCommandEvent&) { resizeGrid(); });
	height->Bind(wxEVT_SPINCTRL, [this](wxCommandEvent&) { resizeGrid(); });
	clear->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { grid->ClearAffected(); });
	save->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { saveArea(); });
}

const SpellAreaLibrarySaveResult& SpellAreaEditorDialog::saveResult() const {
	return result;
}

void SpellAreaEditorDialog::resizeGrid() {
	grid->SetDimensions(static_cast<std::size_t>(width->GetValue()), static_cast<std::size_t>(height->GetValue()));
}

void SpellAreaEditorDialog::saveArea() {
	const int selected = target->GetSelection();
	if (selected < 0 || static_cast<std::size_t>(selected) >= targets.size()) {
		return;
	}
	const EditableSpellArea area = grid->area(name->GetValue().ToStdString(wxConvUTF8));
	std::string error;
	if (!SpellAreaLibrary::Save(workspace, *resolver, targets[static_cast<std::size_t>(selected)], area, result, error)) {
		status->SetLabel(wxString::FromUTF8(error));
		status->SetForegroundColour(wxColour(235, 95, 95));
		wxMessageBox(wxString::FromUTF8(error), "Area was not saved", wxOK | wxICON_ERROR, this);
		return;
	}
	EndModal(wxID_OK);
}
