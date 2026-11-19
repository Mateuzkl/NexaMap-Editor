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

#include "main.h"
#include "find_item_window.h"
#include "common_windows.h"
#include "graphics.h"
#include "gui.h"
#include "items.h"
#include "brush.h"
#include "raw_brush.h"

BEGIN_EVENT_TABLE(FindItemDialog, wxDialog)
EVT_TIMER(wxID_ANY, FindItemDialog::OnInputTimer)
EVT_BUTTON(wxID_OK, FindItemDialog::OnClickOK)
EVT_BUTTON(wxID_CANCEL, FindItemDialog::OnClickCancel)
END_EVENT_TABLE()

FindItemDialog::FindItemDialog(wxWindow* parent, const wxString& title, bool onlyPickupables /* = false*/) :
	wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxSize(840, 640), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	input_timer(this),
	result_brush(nullptr),
	result_id(0),
	map_search_kind(ItemIdSearchKind::ServerId),
	map_search_target_id(0),
	parsed_id_valid(false),
	only_pickupables(onlyPickupables) {
	this->SetMinSize(wxSize(820, 620));

	auto* box_sizer = newd wxBoxSizer(wxHORIZONTAL);

	auto* options_box_sizer = newd wxBoxSizer(wxVERTICAL);

	wxString radio_boxChoices[] = { "Find by Server ID",
									"Find by Client ID",
									"Find by Sprite ID",
									"Find by Name",
									"Find by Types",
									"Find by Properties" };

	int radio_boxNChoices = sizeof(radio_boxChoices) / sizeof(wxString);
	options_radio_box = newd wxRadioBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, radio_boxNChoices, radio_boxChoices, 1, wxRA_SPECIFY_COLS);
	options_radio_box->SetSelection(SearchMode::ServerIDs);
	options_box_sizer->Add(options_radio_box, 0, wxALL | wxEXPAND, 5);

	auto* server_id_box_sizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Server ID (0 - 4294967295)"), wxVERTICAL);
	server_id_input = newd wxTextCtrl(server_id_box_sizer->GetStaticBox(), wxID_ANY, "100", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	server_id_input->SetMaxLength(10);
	server_id_box_sizer->Add(server_id_input, 0, wxALL | wxEXPAND, 5);

	invalid_item = newd wxCheckBox(server_id_box_sizer->GetStaticBox(), wxID_ANY, "Force select", wxDefaultPosition, wxDefaultSize, 0);
	invalid_item->SetToolTip("Allow selecting or searching an ID even if it does not appear in the loaded item list.");
	server_id_box_sizer->Add(invalid_item, 0, wxALL | wxEXPAND, 5);

	options_box_sizer->Add(server_id_box_sizer, 0, wxALL | wxEXPAND, 5);

	auto* client_id_box_sizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Client ID (0 - 4294967295)"), wxVERTICAL);
	client_id_input = newd wxTextCtrl(client_id_box_sizer->GetStaticBox(), wxID_ANY, "100", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	client_id_input->SetMaxLength(10);
	client_id_input->Enable(false);
	client_id_box_sizer->Add(client_id_input, 0, wxALL | wxEXPAND, 5);
	options_box_sizer->Add(client_id_box_sizer, 0, wxALL | wxEXPAND, 5);

	auto* sprite_id_box_sizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Sprite ID (0 - 4294967295)"), wxVERTICAL);
	sprite_id_input = newd wxTextCtrl(sprite_id_box_sizer->GetStaticBox(), wxID_ANY, "1", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	sprite_id_input->SetMaxLength(10);
	sprite_id_input->Enable(false);
	sprite_id_box_sizer->Add(sprite_id_input, 0, wxALL | wxEXPAND, 5);
	options_box_sizer->Add(sprite_id_box_sizer, 0, wxALL | wxEXPAND, 5);

	auto* name_box_sizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Name"), wxVERTICAL);
	name_text_input = newd wxTextCtrl(name_box_sizer->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	name_text_input->Enable(false);
	name_box_sizer->Add(name_text_input, 0, wxALL | wxEXPAND, 5);
	options_box_sizer->Add(name_box_sizer, 0, wxALL | wxEXPAND, 5);

	id_status_label = newd wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(210, 48), wxST_NO_AUTORESIZE);
	options_box_sizer->Add(id_status_label, 0, wxALL | wxEXPAND, 5);

	// spacer
	options_box_sizer->Add(0, 0, 1, wxALL | wxEXPAND, 5);

	buttons_box_sizer = newd wxStdDialogButtonSizer();
	ok_button = newd wxButton(this, wxID_OK);
	buttons_box_sizer->AddButton(ok_button);
	cancel_button = newd wxButton(this, wxID_CANCEL);
	buttons_box_sizer->AddButton(cancel_button);
	buttons_box_sizer->Realize();
	options_box_sizer->Add(buttons_box_sizer, 0, wxALIGN_CENTER | wxALL, 5);

	box_sizer->Add(options_box_sizer, 0, wxALL | wxEXPAND, 5);

	// --------------- Types ---------------

	auto* type_box_sizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Types"), wxVERTICAL);

	wxString types_choices[] = { "Depot",
								 "Mailbox",
								 "Trash Holder",
								 "Container",
								 "Door",
								 "Magic Field",
								 "Teleport",
								 "Bed",
								 "Key",
								 "Podium" };

	int types_choices_count = sizeof(types_choices) / sizeof(wxString);
	types_radio_box = newd wxRadioBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, types_choices_count, types_choices, 1, wxRA_SPECIFY_COLS);
	types_radio_box->SetSelection(0);
	types_radio_box->Enable(false);
	type_box_sizer->Add(types_radio_box, 0, wxALL | wxEXPAND, 5);

	box_sizer->Add(type_box_sizer, 0, wxALL | wxEXPAND, 5);

	// --------------- Properties ---------------

	auto* properties_box_sizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Properties"), wxVERTICAL);

	unpassable = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Unpassable", wxDefaultPosition, wxDefaultSize, 0);
	properties_box_sizer->Add(unpassable, 0, wxALL, 5);

	unmovable = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Unmovable", wxDefaultPosition, wxDefaultSize, 0);
	properties_box_sizer->Add(unmovable, 0, wxALL, 5);

	block_missiles = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Block Missiles", wxDefaultPosition, wxDefaultSize, 0);
	properties_box_sizer->Add(block_missiles, 0, wxALL, 5);

	block_pathfinder = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Block Pathfinder", wxDefaultPosition, wxDefaultSize, 0);
	properties_box_sizer->Add(block_pathfinder, 0, wxALL, 5);

	readable = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Readable", wxDefaultPosition, wxDefaultSize, 0);
	properties_box_sizer->Add(readable, 0, wxALL, 5);

	writeable = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Writeable", wxDefaultPosition, wxDefaultSize, 0);
	properties_box_sizer->Add(writeable, 0, wxALL, 5);

	pickupable = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Pickupable", wxDefaultPosition, wxDefaultSize, 0);
	pickupable->SetValue(only_pickupables);
	pickupable->Enable(!only_pickupables);
	properties_box_sizer->Add(pickupable, 0, wxALL, 5);

	stackable = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Stackable", wxDefaultPosition, wxDefaultSize, 0);
	properties_box_sizer->Add(stackable, 0, wxALL, 5);

	rotatable = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Rotatable", wxDefaultPosition, wxDefaultSize, 0);
	properties_box_sizer->Add(rotatable, 0, wxALL, 5);

	hangable = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Hangable", wxDefaultPosition, wxDefaultSize, 0);
	properties_box_sizer->Add(hangable, 0, wxALL, 5);

	hook_east = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Hook East", wxDefaultPosition, wxDefaultSize, 0);
	properties_box_sizer->Add(hook_east, 0, wxALL, 5);

	hook_south = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Hook South", wxDefaultPosition, wxDefaultSize, 0);
	properties_box_sizer->Add(hook_south, 0, wxALL, 5);

	has_elevation = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Has Elevation", wxDefaultPosition, wxDefaultSize, 0);
	properties_box_sizer->Add(has_elevation, 0, wxALL, 5);

	ignore_look = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Ignore Look", wxDefaultPosition, wxDefaultSize, 0);
	properties_box_sizer->Add(ignore_look, 0, wxALL, 5);

	floor_change = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Floor Change", wxDefaultPosition, wxDefaultSize, 0);
	properties_box_sizer->Add(floor_change, 0, wxALL, 5);

	box_sizer->Add(properties_box_sizer, 0, wxALL | wxEXPAND, 5);

	// --------------- Items list ---------------

	auto* result_box_sizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Result"), wxVERTICAL);
	items_list = newd FindDialogListBox(result_box_sizer->GetStaticBox(), wxID_ANY);
	items_list->Bind(wxEVT_LISTBOX_DCLICK, &FindItemDialog::OnClickOK, this);
	items_list->SetMinSize(wxSize(240, 520));
	result_box_sizer->Add(items_list, 1, wxALL | wxEXPAND, 5);
	box_sizer->Add(result_box_sizer, 1, wxALL | wxEXPAND, 5);

	this->SetSizer(box_sizer);
	this->Layout();
	this->Centre(wxBOTH);
	this->EnableProperties(false);
	this->RefreshContentsInternal();

	// Connect Events
	options_radio_box->Connect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(FindItemDialog::OnOptionChange), nullptr, this);
	server_id_input->Connect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(FindItemDialog::OnServerIdChange), nullptr, this);
	server_id_input->Connect(wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler(FindItemDialog::OnClickOK), nullptr, this);
	client_id_input->Connect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(FindItemDialog::OnClientIdChange), nullptr, this);
	client_id_input->Connect(wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler(FindItemDialog::OnClickOK), nullptr, this);
	sprite_id_input->Connect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(FindItemDialog::OnSpriteIdChange), nullptr, this);
	sprite_id_input->Connect(wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler(FindItemDialog::OnClickOK), nullptr, this);
	name_text_input->Connect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(FindItemDialog::OnText), nullptr, this);
	name_text_input->Connect(wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler(FindItemDialog::OnClickOK), nullptr, this);

	types_radio_box->Connect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(FindItemDialog::OnTypeChange), nullptr, this);

	unpassable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	unmovable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	block_missiles->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	block_pathfinder->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	readable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	writeable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	pickupable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	stackable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	rotatable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	hangable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	hook_east->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	hook_south->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	has_elevation->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	ignore_look->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	floor_change->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	invalid_item->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
}

FindItemDialog::~FindItemDialog() {
	input_timer.Stop();
	// Disconnect Events
	options_radio_box->Disconnect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(FindItemDialog::OnOptionChange), nullptr, this);
	server_id_input->Disconnect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(FindItemDialog::OnServerIdChange), nullptr, this);
	server_id_input->Disconnect(wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler(FindItemDialog::OnClickOK), nullptr, this);
	client_id_input->Disconnect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(FindItemDialog::OnClientIdChange), nullptr, this);
	client_id_input->Disconnect(wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler(FindItemDialog::OnClickOK), nullptr, this);
	sprite_id_input->Disconnect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(FindItemDialog::OnSpriteIdChange), nullptr, this);
	sprite_id_input->Disconnect(wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler(FindItemDialog::OnClickOK), nullptr, this);
	name_text_input->Disconnect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(FindItemDialog::OnText), nullptr, this);
	name_text_input->Disconnect(wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler(FindItemDialog::OnClickOK), nullptr, this);

	types_radio_box->Disconnect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(FindItemDialog::OnTypeChange), nullptr, this);

	unpassable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	unmovable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	block_missiles->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	block_pathfinder->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	readable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	writeable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	pickupable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	stackable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	rotatable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	hangable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	hook_east->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	hook_south->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	has_elevation->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	ignore_look->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	floor_change->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	invalid_item->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	items_list->Unbind(wxEVT_LISTBOX_DCLICK, &FindItemDialog::OnClickOK, this);
}

FindItemDialog::SearchMode FindItemDialog::getSearchMode() const {
	const int sel = options_radio_box->GetSelection();
	if (sel < SearchMode::ServerIDs || sel > SearchMode::Properties) {
		return SearchMode::ServerIDs;
	}
	return static_cast<SearchMode>(sel);
}

void FindItemDialog::setSearchMode(FindItemDialog::SearchMode mode) {
	if (mode < SearchMode::ServerIDs || mode > SearchMode::Properties) {
		mode = SearchMode::ServerIDs;
	}
	if ((SearchMode)options_radio_box->GetSelection() != mode) {
		options_radio_box->SetSelection(mode);
	}

	const bool isIdMode = (mode == SearchMode::ServerIDs || mode == SearchMode::ClientIDs || mode == SearchMode::SpriteIDs);
	server_id_input->Enable(mode == SearchMode::ServerIDs);
	invalid_item->Enable(isIdMode);
	client_id_input->Enable(mode == SearchMode::ClientIDs);
	sprite_id_input->Enable(mode == SearchMode::SpriteIDs);
	name_text_input->Enable(mode == SearchMode::Names);
	types_radio_box->Enable(mode == SearchMode::Types);
	EnableProperties(mode == SearchMode::Properties);
	RefreshContentsInternal();

	if (mode == SearchMode::ServerIDs) {
		server_id_input->SetFocus();
		server_id_input->SelectAll();
	} else if (mode == SearchMode::ClientIDs) {
		client_id_input->SetFocus();
		client_id_input->SelectAll();
	} else if (mode == SearchMode::SpriteIDs) {
		sprite_id_input->SetFocus();
		sprite_id_input->SelectAll();
	} else if (mode == SearchMode::Names) {
		name_text_input->SetFocus();
	}
}

void FindItemDialog::EnableProperties(bool enable) {
	unpassable->Enable(enable);
	unmovable->Enable(enable);
	block_missiles->Enable(enable);
	block_pathfinder->Enable(enable);
	readable->Enable(enable);
	writeable->Enable(enable);
	pickupable->Enable(!only_pickupables && enable);
	stackable->Enable(enable);
	rotatable->Enable(enable);
	hangable->Enable(enable);
	hook_east->Enable(enable);
	hook_south->Enable(enable);
	has_elevation->Enable(enable);
	ignore_look->Enable(enable);
	floor_change->Enable(enable);
}

void FindItemDialog::SetStatusMessage(const wxString& message, bool isError) {
	if (!id_status_label) {
		return;
	}
	id_status_label->SetForegroundColour(isError ? wxColour(180, 30, 30) : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
	id_status_label->SetLabel(message);
	id_status_label->Wrap(210);
}

void FindItemDialog::RefreshContentsInternal() {
	input_timer.Stop();
	std::vector<Brush*> matches;
	ok_button->Enable(false);
	parsed_id_valid = false;
	result_id = 0;

	const SearchMode selection = getSearchMode();
	bool found_search_results = false;
	SetStatusMessage(wxEmptyString, false);

	if (selection == SearchMode::ServerIDs) {
		const std::string rawText = nstr(server_id_input->GetValue());
		const ParsedItemId parsed = ParseUint32ItemId(rawText);
		if (!parsed.valid) {
			SetStatusMessage(wxstr(parsed.error), !parsed.empty);
		} else {
			parsed_id_valid = true;
			result_id = parsed.value;
			if (g_items.typeExists(parsed.value)) {
				ItemType& item = g_items.getItemType(parsed.value);
				RAWBrush* raw_brush = item.raw_brush;
				if (raw_brush) {
					if (!only_pickupables || item.pickupable) {
						found_search_results = true;
						matches.push_back(raw_brush);
					}
				}
				wxString info = wxString::Format("Loaded: Server %u | Client %u", item.id, item.clientID);
				if (item.sprite) {
					const auto spriteIds = item.sprite->getUniqueSpriteIds();
					if (!spriteIds.empty()) {
						info << wxString::Format(" | Sprite %u", spriteIds.front());
						if (spriteIds.size() > 1) {
							info << wxString::Format(" (+%zu)", spriteIds.size() - 1);
						}
					}
				}
				if (!found_search_results && !invalid_item->GetValue()) {
					info << "\nNo RAW brush (enable Force select).";
				}
				SetStatusMessage(info, false);
			} else {
				wxString info = wxString::Format(
					"Server ID %u not in loaded data (max: %u).",
					parsed.value,
					g_items.getMaxID()
				);
				if (!invalid_item->GetValue()) {
					info << "\nEnable 'Force select' to search raw ID.";
				}
				SetStatusMessage(info, false);
			}

			if (invalid_item->GetValue()) {
				found_search_results = true;
			}
		}
	} else if (selection == SearchMode::ClientIDs) {
		const std::string rawText = nstr(client_id_input->GetValue());
		const ParsedItemId parsed = ParseUint32ItemId(rawText);
		if (!parsed.valid) {
			SetStatusMessage(wxstr(parsed.error), !parsed.empty);
		} else {
			parsed_id_valid = true;
			result_id = parsed.value;
			const uint32_t clientID = parsed.value;
			g_items.forEachItemType([&](ItemType& item) {
				if (item.id == 0 || item.clientID != clientID) {
					return;
				}
				RAWBrush* raw_brush = item.raw_brush;
				if (!raw_brush) {
					return;
				}
				if (only_pickupables && !item.pickupable) {
					return;
				}
				found_search_results = true;
				matches.push_back(raw_brush);
			});

			if (found_search_results) {
				SetStatusMessage(wxString::Format("Found %zu item(s) with Client ID %u.", matches.size(), clientID), false);
			} else {
				wxString info = wxString::Format(
					"Client ID %u not in loaded data (max: %u).",
					clientID,
					g_gui.gfx.getItemSpriteMaxID()
				);
				if (!invalid_item->GetValue()) {
					info << "\nEnable 'Force select' to search raw ID.";
				}
				SetStatusMessage(info, false);
			}

			if (invalid_item->GetValue()) {
				found_search_results = true;
			}
		}
	} else if (selection == SearchMode::SpriteIDs) {
		const std::string rawText = nstr(sprite_id_input->GetValue());
		const ParsedItemId parsed = ParseUint32ItemId(rawText);
		if (!parsed.valid) {
			SetStatusMessage(wxstr(parsed.error), !parsed.empty);
		} else {
			parsed_id_valid = true;
			result_id = parsed.value;
			const uint32_t spriteID = parsed.value;
			g_items.forEachItemType([&](ItemType& item) {
				if (item.id == 0 || !ItemHasSpriteId(item, spriteID)) {
					return;
				}
				RAWBrush* raw_brush = item.raw_brush;
				if (!raw_brush) {
					return;
				}
				if (only_pickupables && !item.pickupable) {
					return;
				}
				found_search_results = true;
				matches.push_back(raw_brush);
			});

			if (found_search_results) {
				SetStatusMessage(wxString::Format("Found %zu item(s) using Sprite ID %u.", matches.size(), spriteID), false);
			} else {
				wxString info = wxString::Format("No loaded item references Sprite ID %u.", spriteID);
				if (!invalid_item->GetValue()) {
					info << "\nEnable 'Force select' to search anyway.";
				}
				SetStatusMessage(info, false);
			}

			if (invalid_item->GetValue()) {
				found_search_results = true;
			}
		}
	} else if (selection == SearchMode::Names) {
		wxString search_string = name_text_input->GetValue().Lower();
		if (search_string.size() >= 2) {
			g_items.forEachItemType([&](ItemType& item) {
				if (item.id == 0) {
					return;
				}

				RAWBrush* raw_brush = item.raw_brush;
				if (!raw_brush) {
					return;
				}

				if (only_pickupables && !item.pickupable) {
					return;
				}

				wxString item_name = wxstr(raw_brush->getName()).Lower();
				if (item_name.Find(search_string) == wxNOT_FOUND) {
					return;
				}

				found_search_results = true;
				matches.push_back(raw_brush);
			});
		}
	} else if (selection == SearchMode::Types) {
		// NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) - out-of-range selection falls through to the no-filter branch below.
		const auto typeSelection = static_cast<SearchItemType>(types_radio_box->GetSelection());
		g_items.forEachItemType([&](ItemType& item) {
			if (item.id == 0) {
				return;
			}

			RAWBrush* raw_brush = item.raw_brush;
			if (!raw_brush) {
				return;
			}

			if (only_pickupables && !item.pickupable) {
				return;
			}

			if ((typeSelection == SearchItemType::Depot && !item.isDepot()) || (typeSelection == SearchItemType::Mailbox && !item.isMailbox()) || (typeSelection == SearchItemType::TrashHolder && !item.isTrashHolder()) || (typeSelection == SearchItemType::Container && !item.isContainer()) || (typeSelection == SearchItemType::Door && !item.isDoor()) || (typeSelection == SearchItemType::MagicField && !item.isMagicField()) || (typeSelection == SearchItemType::Teleport && !item.isTeleport()) || (typeSelection == SearchItemType::Bed && !item.isBed()) || (typeSelection == SearchItemType::Key && !item.isKey()) || (typeSelection == SearchItemType::Podium && !item.isPodium())) {
				return;
			}

			found_search_results = true;
			matches.push_back(raw_brush);
		});
	} else if (selection == SearchMode::Properties) {
		bool has_selected = (unpassable->GetValue() || unmovable->GetValue() || block_missiles->GetValue() || block_pathfinder->GetValue() || readable->GetValue() || writeable->GetValue() || pickupable->GetValue() || stackable->GetValue() || rotatable->GetValue() || hangable->GetValue() || hook_east->GetValue() || hook_south->GetValue() || has_elevation->GetValue() || ignore_look->GetValue() || floor_change->GetValue());

		if (has_selected) {
			g_items.forEachItemType([&](ItemType& item) {
				if (item.id == 0) {
					return;
				}

				RAWBrush* raw_brush = item.raw_brush;
				if (!raw_brush) {
					return;
				}

				if ((unpassable->GetValue() && !item.unpassable) || (unmovable->GetValue() && item.moveable) || (block_missiles->GetValue() && !item.blockMissiles) || (block_pathfinder->GetValue() && !item.blockPathfinder) || (readable->GetValue() && !item.canReadText) || (writeable->GetValue() && !item.canWriteText) || (pickupable->GetValue() && !item.pickupable) || (stackable->GetValue() && !item.stackable) || (rotatable->GetValue() && !item.rotable) || (hangable->GetValue() && !item.isHangable) || (hook_east->GetValue() && !item.hookEast) || (hook_south->GetValue() && !item.hookSouth) || (has_elevation->GetValue() && !item.hasElevation) || (ignore_look->GetValue() && !item.ignoreLook) || (floor_change->GetValue() && !item.isFloorChange())) {
					return;
				}

				found_search_results = true;
				matches.push_back(raw_brush);
			});
		}
	}

	const bool has_matches = !matches.empty();
	items_list->SetBrushes(std::move(matches));
	items_list->Enable(true);
	if (has_matches) {
		items_list->SetSelection(0);
	}
	ok_button->Enable(found_search_results);

	items_list->Refresh();
}

void FindItemDialog::OnOptionChange(wxCommandEvent& WXUNUSED(event)) {
	setSearchMode(getSearchMode());
}

void FindItemDialog::OnServerIdChange(wxCommandEvent& WXUNUSED(event)) {
	RefreshContentsInternal();
}

void FindItemDialog::OnClientIdChange(wxCommandEvent& WXUNUSED(event)) {
	RefreshContentsInternal();
}

void FindItemDialog::OnSpriteIdChange(wxCommandEvent& WXUNUSED(event)) {
	RefreshContentsInternal();
}

void FindItemDialog::OnText(wxCommandEvent& WXUNUSED(event)) {
	// Old results must not be accepted while the next query is pending.
	items_list->Enable(false);
	ok_button->Enable(false);
	input_timer.Start(300, true);
}

void FindItemDialog::OnTypeChange(wxCommandEvent& WXUNUSED(event)) {
	RefreshContentsInternal();
}

void FindItemDialog::OnPropertyChange(wxCommandEvent& WXUNUSED(event)) {
	RefreshContentsInternal();
}

void FindItemDialog::OnInputTimer(wxTimerEvent& WXUNUSED(event)) {
	RefreshContentsInternal();
}

void FindItemDialog::OnClickOK(wxCommandEvent& WXUNUSED(event)) {
	if (input_timer.IsRunning() || !ok_button->IsEnabled()) {
		return;
	}

	const SearchMode mode = getSearchMode();
	if (mode == SearchMode::ServerIDs) {
		map_search_kind = ItemIdSearchKind::ServerId;
		map_search_target_id = result_id;
		if (invalid_item->GetValue() && parsed_id_valid && items_list->GetItemCount() == 0) {
			result_brush = nullptr;
			EndModal(wxID_OK);
			return;
		}
	} else if (mode == SearchMode::ClientIDs) {
		map_search_kind = ItemIdSearchKind::ClientId;
		map_search_target_id = result_id;
		if (invalid_item->GetValue() && parsed_id_valid && items_list->GetItemCount() == 0) {
			result_brush = nullptr;
			EndModal(wxID_OK);
			return;
		}
	} else if (mode == SearchMode::SpriteIDs) {
		map_search_kind = ItemIdSearchKind::SpriteId;
		map_search_target_id = result_id;
		if (invalid_item->GetValue() && parsed_id_valid && items_list->GetItemCount() == 0) {
			result_brush = nullptr;
			EndModal(wxID_OK);
			return;
		}
	}

	if (items_list->GetItemCount() != 0) {
		Brush* brush = items_list->GetSelectedBrush();
		if (brush) {
			result_brush = brush;
			result_id = brush->asRaw()->getItemID();
			if (mode != SearchMode::ClientIDs && mode != SearchMode::SpriteIDs) {
				map_search_kind = ItemIdSearchKind::ServerId;
				map_search_target_id = result_id;
			}
			EndModal(wxID_OK);
			return;
		}
	}

	if (invalid_item->GetValue() && parsed_id_valid) {
		result_brush = nullptr;
		EndModal(wxID_OK);
	}
}

void FindItemDialog::OnClickCancel(wxCommandEvent& WXUNUSED(event)) {
	input_timer.Stop();
	EndModal(wxID_CANCEL);
}
