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

#include "properties_window.h"

#include "gui_ids.h"
#include "gui.h"
#include "complexitem.h"
#include "container_properties_window.h"
#include "map.h"
#include "numbertextctrl.h"

BEGIN_EVENT_TABLE(PropertiesWindow, wxDialog)
EVT_BUTTON(wxID_OK, PropertiesWindow::OnClickOK)
EVT_BUTTON(wxID_CANCEL, PropertiesWindow::OnClickCancel)

EVT_BUTTON(ITEM_PROPERTIES_ADD_ATTRIBUTE, PropertiesWindow::OnClickAddAttribute)
EVT_BUTTON(ITEM_PROPERTIES_REMOVE_ATTRIBUTE, PropertiesWindow::OnClickRemoveAttribute)

EVT_GRID_CELL_CHANGED(PropertiesWindow::OnGridValueChanged)
END_EVENT_TABLE()

PropertiesWindow::PropertiesWindow(wxWindow* parent, const Map* map, const Tile* tile_parent, Item* item, wxPoint pos) :
	ObjectPropertiesWindowBase(parent, "Item Properties", map, tile_parent, item, pos) {
	ASSERT(edit_item);
	notebook = newd wxNotebook(this, wxID_ANY, wxDefaultPosition, wxSize(600, 300));

	notebook->AddPage(createGeneralPanel(notebook), "Simple", true);
	if (dynamic_cast<Container*>(item)) {
		notebook->AddPage(createContainerPanel(notebook), "Contents");
	}
	notebook->AddPage(createAttributesPanel(notebook), "Advanced");

	wxSizer* topSizer = newd wxBoxSizer(wxVERTICAL);
	topSizer->Add(notebook, wxSizerFlags(1).DoubleBorder());

	wxSizer* optSizer = newd wxBoxSizer(wxHORIZONTAL);
	optSizer->Add(newd wxButton(this, wxID_OK, "OK"), wxSizerFlags(0).Center());
	optSizer->Add(newd wxButton(this, wxID_CANCEL, "Cancel"), wxSizerFlags(0).Center());
	topSizer->Add(optSizer, wxSizerFlags(0).Center().DoubleBorder());

	SetSizerAndFit(topSizer);
	Centre(wxBOTH);
}

PropertiesWindow::~PropertiesWindow() = default;

void PropertiesWindow::Update() {
	const Container* container = dynamic_cast<Container*>(edit_item);
	if (container) {
		for (uint32_t i = 0; i < container->getVolume(); ++i) {
			container_items[i]->setItem(container->getItem(i));
		}
	}
	wxDialog::Update();
}

wxWindow* PropertiesWindow::createGeneralPanel(wxWindow* parent) {
	auto* panel = newd wxPanel(parent, ITEM_PROPERTIES_GENERAL_TAB);
	auto* topSizer = newd wxBoxSizer(wxVERTICAL);
	auto* gridsizer = newd wxFlexGridSizer(2, 10, 10);
	gridsizer->AddGrowableCol(1);

	gridsizer->Add(newd wxStaticText(panel, wxID_ANY, "ID " + i2ws(edit_item->getID())));
	gridsizer->Add(newd wxStaticText(panel, wxID_ANY, "\"" + wxstr(edit_item->getName()) + "\""));

	gridsizer->Add(newd wxStaticText(panel, wxID_ANY, "Action ID"));
	action_id_field = newd wxSpinCtrl(panel, ITEM_PROPERTIES_ACTION_ID, i2ws(edit_item->getActionID()), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 0xFFFF, edit_item->getActionID());
	gridsizer->Add(action_id_field, wxSizerFlags(1).Expand());

	gridsizer->Add(newd wxStaticText(panel, wxID_ANY, "Unique ID"));
	unique_id_field = newd wxSpinCtrl(panel, ITEM_PROPERTIES_UNIQUE_ID, i2ws(edit_item->getUniqueID()), wxDefaultPosition, wxSize(-1, 20), wxSP_ARROW_KEYS, 0, 0xFFFF, edit_item->getUniqueID());
	gridsizer->Add(unique_id_field, wxSizerFlags(1).Expand());

	if (auto* teleport = dynamic_cast<Teleport*>(edit_item)) {
		gridsizer->Add(newd wxStaticText(panel, wxID_ANY, "Destination"));

		auto* destinationSizer = newd wxBoxSizer(wxHORIZONTAL);
		const int maxX = edit_map ? edit_map->getWidth() : 0xFFFF;
		const int maxY = edit_map ? edit_map->getHeight() : 0xFFFF;
		teleport_x_field = newd NumberTextCtrl(panel, ITEM_PROPERTIES_TELEPORT_X, teleport->getX(), 0, maxX, wxTE_PROCESS_ENTER);
		teleport_y_field = newd NumberTextCtrl(panel, ITEM_PROPERTIES_TELEPORT_Y, teleport->getY(), 0, maxY, wxTE_PROCESS_ENTER);
		teleport_z_field = newd NumberTextCtrl(panel, ITEM_PROPERTIES_TELEPORT_Z, teleport->getZ(), 0, MAP_MAX_LAYER, wxTE_PROCESS_ENTER);
		teleport_x_field->Bind(wxEVT_TEXT_PASTE, &PropertiesWindow::OnTeleportPositionPaste, this);
		teleport_y_field->Bind(wxEVT_TEXT_PASTE, &PropertiesWindow::OnTeleportPositionPaste, this);
		teleport_z_field->Bind(wxEVT_TEXT_PASTE, &PropertiesWindow::OnTeleportPositionPaste, this);
		teleport_x_field->SetToolTip("Destination X");
		teleport_y_field->SetToolTip("Destination Y");
		teleport_z_field->SetToolTip("Destination Z");
		destinationSizer->Add(teleport_x_field, wxSizerFlags(3).Expand());
		destinationSizer->Add(teleport_y_field, wxSizerFlags(3).Expand().Border(wxLEFT, 5));
		destinationSizer->Add(teleport_z_field, wxSizerFlags(2).Expand().Border(wxLEFT, 5));
		gridsizer->Add(destinationSizer, wxSizerFlags(1).Expand());
	}

	topSizer->Add(gridsizer, wxSizerFlags(0).Expand().Border(wxALL, 10));
	if (edit_item->canHoldText()) {
		topSizer->Add(newd wxStaticText(panel, wxID_ANY, "Text"), wxSizerFlags(0).Expand().Border(wxLEFT | wxRIGHT, 10));
		text_field = newd wxTextCtrl(panel, ITEM_PROPERTIES_TEXT, wxstr(edit_item->getText()), wxDefaultPosition, wxSize(-1, 90), wxTE_MULTILINE);
		text_field->SetEditable(edit_item->canWriteText());
		topSizer->Add(text_field, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 10));
	}
	if (edit_item->canHoldDescription()) {
		topSizer->Add(newd wxStaticText(panel, wxID_ANY, "Description"), wxSizerFlags(0).Expand().Border(wxLEFT | wxRIGHT, 10));
		description_field = newd wxTextCtrl(panel, ITEM_PROPERTIES_DESCRIPTION, wxstr(edit_item->getDescription()), wxDefaultPosition, wxSize(-1, 70), wxTE_MULTILINE);
		topSizer->Add(description_field, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 10));
	}

	panel->SetSizer(topSizer);

	return panel;
}

void PropertiesWindow::saveGeneralPanel() {
	edit_item->setActionID(static_cast<uint16_t>(action_id_field->GetValue()));
	edit_item->setUniqueID(static_cast<uint16_t>(unique_id_field->GetValue()));
	if (text_field) {
		edit_item->setText(nstr(text_field->GetValue()));
	}
	if (description_field) {
		edit_item->setDescription(nstr(description_field->GetValue()));
	}
	if (auto* teleport = dynamic_cast<Teleport*>(edit_item); teleport && teleport_x_field && teleport_y_field && teleport_z_field) {
		teleport->setDestination(Position(teleport_x_field->GetIntValue(), teleport_y_field->GetIntValue(), teleport_z_field->GetIntValue()));
	}
}

bool PropertiesWindow::validateGeneralPanel() const {
	const int actionId = action_id_field->GetValue();
	if (actionId != 0 && actionId < 100) {
		g_gui.PopupDialog(const_cast<PropertiesWindow*>(this), "Invalid Action ID", "Action ID must be 0 or between 100 and 65535.", wxOK);
		return false;
	}
	const int uniqueId = unique_id_field->GetValue();
	if (uniqueId != 0 && uniqueId < 1000) {
		g_gui.PopupDialog(const_cast<PropertiesWindow*>(this), "Invalid Unique ID", "Unique ID must be 0 or between 1000 and 65535.", wxOK);
		return false;
	}
	if (text_field && edit_item->canWriteText()) {
		if (!IsTextLengthValid(text_field->GetValue(), 0)) {
			g_gui.PopupDialog(const_cast<PropertiesWindow*>(this), "Text too long", "Text must be shorter than 65535 UTF-8 bytes.", wxOK);
			return false;
		}
		const uint32_t maximum = edit_item->getMaxWriteLength();
		if (!IsTextLengthValid(text_field->GetValue(), maximum)) {
			g_gui.PopupDialog(const_cast<PropertiesWindow*>(this), "Text too long", wxString::Format("This item accepts at most %u UTF-8 bytes.", maximum), wxOK);
			return false;
		}
	}
	if (description_field && !IsTextLengthValid(description_field->GetValue(), 0)) {
		g_gui.PopupDialog(const_cast<PropertiesWindow*>(this), "Description too long", "Description must be shorter than 65535 UTF-8 bytes.", wxOK);
		return false;
	}
	return true;
}

bool PropertiesWindow::IsTextLengthValid(const wxString& value, uint32_t maximumLength) {
	const size_t utf8Bytes = nstr(value).size();
	return utf8Bytes < std::numeric_limits<uint16_t>::max() && (maximumLength == 0 || utf8Bytes <= maximumLength);
}

void PropertiesWindow::OnTeleportPositionPaste(wxClipboardTextEvent&) {
	Position position;
	const int maxX = edit_map ? edit_map->getWidth() : MAP_MAX_WIDTH;
	const int maxY = edit_map ? edit_map->getHeight() : MAP_MAX_HEIGHT;
	if (posFromClipboard(position, maxX, maxY)) {
		teleport_x_field->SetIntValue(position.x);
		teleport_y_field->SetIntValue(position.y);
		teleport_z_field->SetIntValue(position.z);
	}
}

wxWindow* PropertiesWindow::createContainerPanel(wxWindow* parent) {
	const auto* container = static_cast<const Container*>(edit_item);
	auto* panel = newd wxPanel(parent, ITEM_PROPERTIES_CONTAINER_TAB);
	wxSizer* topSizer = newd wxBoxSizer(wxVERTICAL);

	wxSizer* gridSizer = newd wxGridSizer(6, 5, 5);

	const bool use_large_sprites = g_settings.getBoolean(Config::USE_LARGE_CONTAINER_ICONS);
	for (uint32_t i = 0; i < container->getVolume(); ++i) {
		Item* item = container->getItem(i);
		auto* containerItemButton = newd ContainerItemButton(panel, use_large_sprites, i, edit_map, item);

		container_items.push_back(containerItemButton);
		gridSizer->Add(containerItemButton, wxSizerFlags(0));
	}

	topSizer->Add(gridSizer, wxSizerFlags(1).Expand());

	panel->SetSizer(topSizer);
	return panel;
}

wxWindow* PropertiesWindow::createAttributesPanel(wxWindow* parent) {
	auto* panel = newd wxPanel(parent, wxID_ANY);
	wxSizer* topSizer = newd wxBoxSizer(wxVERTICAL);

	attributesGrid = newd wxGrid(panel, ITEM_PROPERTIES_ADVANCED_TAB, wxDefaultPosition, wxSize(-1, 160));
	topSizer->Add(attributesGrid, wxSizerFlags(1).Expand());

	const wxFont time_font(*wxSWISS_FONT);
	attributesGrid->SetDefaultCellFont(time_font);
	attributesGrid->CreateGrid(0, 3);
	attributesGrid->DisableDragRowSize();
	attributesGrid->DisableDragColSize();
	attributesGrid->SetSelectionMode(wxGrid::wxGridSelectRows);
	attributesGrid->SetRowLabelSize(0);
	// log->SetColLabelSize(0);
	// log->EnableGridLines(false);
	attributesGrid->EnableEditing(true);

	attributesGrid->SetColLabelValue(0, "Key");
	attributesGrid->SetColSize(0, 100);
	attributesGrid->SetColLabelValue(1, "Type");
	attributesGrid->SetColSize(1, 80);
	attributesGrid->SetColLabelValue(2, "Value");
	attributesGrid->SetColSize(2, 410);

	// contents
	ItemAttributeMap attrs = edit_item->getAttributes();
	attrs.erase("aid");
	attrs.erase("uid");
	if (text_field) {
		attrs.erase("text");
	}
	if (description_field) {
		attrs.erase("desc");
	}
	attributesGrid->AppendRows(static_cast<int>(attrs.size()));
	int i = 0;
	for (auto aiter = attrs.begin(); aiter != attrs.end(); ++aiter, ++i) {
		SetGridValue(attributesGrid, i, aiter->first, aiter->second);
	}

	wxSizer* optSizer = newd wxBoxSizer(wxHORIZONTAL);
	optSizer->Add(newd wxButton(panel, ITEM_PROPERTIES_ADD_ATTRIBUTE, "Add Attribute"), wxSizerFlags(0).Center());
	optSizer->Add(newd wxButton(panel, ITEM_PROPERTIES_REMOVE_ATTRIBUTE, "Remove Attribute"), wxSizerFlags(0).Center());
	topSizer->Add(optSizer, wxSizerFlags(0).Center().DoubleBorder());

	panel->SetSizer(topSizer);

	return panel;
}

void PropertiesWindow::SetGridValue(wxGrid* grid, int rowIndex, const std::string& label, const ItemAttribute& attr) {
	wxArrayString types;
	types.Add("Number");
	types.Add("Float");
	types.Add("Boolean");
	types.Add("String");

	grid->SetCellValue(rowIndex, 0, label);
	switch (attr.type) {
		case ItemAttribute::STRING: {
			grid->SetCellValue(rowIndex, 1, "String");
			grid->SetCellValue(rowIndex, 2, wxstr(*attr.getString()));
			break;
		}
		case ItemAttribute::INTEGER: {
			grid->SetCellValue(rowIndex, 1, "Number");
			grid->SetCellValue(rowIndex, 2, i2ws(*attr.getInteger()));
			grid->SetCellEditor(rowIndex, 2, new wxGridCellNumberEditor);
			break;
		}
		case ItemAttribute::DOUBLE:
		case ItemAttribute::FLOAT: {
			grid->SetCellValue(rowIndex, 1, "Float");
			wxString f;
			f << *attr.getFloat();
			grid->SetCellValue(rowIndex, 2, f);
			grid->SetCellEditor(rowIndex, 2, new wxGridCellFloatEditor);
			break;
		}
		case ItemAttribute::BOOLEAN: {
			grid->SetCellValue(rowIndex, 1, "Boolean");
			grid->SetCellValue(rowIndex, 2, *attr.getBoolean() ? "1" : "");
			grid->SetCellRenderer(rowIndex, 2, new wxGridCellBoolRenderer);
			grid->SetCellEditor(rowIndex, 2, new wxGridCellBoolEditor);
			break;
		}
		default: {
			grid->SetCellValue(rowIndex, 1, "Unknown");
			grid->SetCellBackgroundColour(rowIndex, 1, *wxLIGHT_GREY);
			grid->SetCellBackgroundColour(rowIndex, 2, *wxLIGHT_GREY);
			grid->SetReadOnly(rowIndex, 1, true);
			grid->SetReadOnly(rowIndex, 2, true);
			break;
		}
	}
	grid->SetCellEditor(rowIndex, 1, new wxGridCellChoiceEditor(types));
}

void PropertiesWindow::saveAttributesPanel() {
	edit_item->clearAllAttributes();
	for (int32_t rowIndex = 0; rowIndex < attributesGrid->GetNumberRows(); ++rowIndex) {
		ItemAttribute attr;
		const wxString type = attributesGrid->GetCellValue(rowIndex, 1);
		if (type == "String") {
			attr.set(nstr(attributesGrid->GetCellValue(rowIndex, 2)));
		} else if (type == "Float") {
			double value;
			if (attributesGrid->GetCellValue(rowIndex, 2).ToDouble(&value)) {
				attr.set(value);
			}
		} else if (type == "Number") {
			long value;
			if (attributesGrid->GetCellValue(rowIndex, 2).ToLong(&value)) {
				attr.set(static_cast<int32_t>(value));
			}
		} else if (type == "Boolean") {
			attr.set(attributesGrid->GetCellValue(rowIndex, 2) == "1");
		} else {
			continue;
		}
		edit_item->setAttribute(nstr(attributesGrid->GetCellValue(rowIndex, 0)), attr);
	}
}

void PropertiesWindow::OnGridValueChanged(wxGridEvent& event) {
	if (event.GetCol() == 1) {
		const wxString newType = attributesGrid->GetCellValue(event.GetRow(), 1);
		if (newType == event.GetString()) {
			return;
		}

		ItemAttribute attr;
		if (newType == "String") {
			attr.set(std::string());
		} else if (newType == "Float") {
			attr.set(0.0f);
		} else if (newType == "Number") {
			attr.set(0);
		} else if (newType == "Boolean") {
			attr.set(false);
		}
		SetGridValue(attributesGrid, event.GetRow(), nstr(attributesGrid->GetCellValue(event.GetRow(), 0)), attr);
	}
}

void PropertiesWindow::OnClickOK(wxCommandEvent&) {
	if (!TransferDataFromWindow()) {
		return;
	}
	EndModal(1);
}

bool PropertiesWindow::TransferDataFromWindow() {
	if (!validateGeneralPanel()) {
		return false;
	}
	saveAttributesPanel();
	saveGeneralPanel();
	return true;
}

void PropertiesWindow::OnClickAddAttribute(wxCommandEvent&) {
	attributesGrid->AppendRows(1);
	const ItemAttribute attr(0);
	SetGridValue(attributesGrid, attributesGrid->GetNumberRows() - 1, "", attr);
}

void PropertiesWindow::OnClickRemoveAttribute(wxCommandEvent&) {
	wxArrayInt rowIndexes = attributesGrid->GetSelectedRows();
	if (rowIndexes.Count() != 1) {
		return;
	}

	const int rowIndex = rowIndexes[0];
	attributesGrid->DeleteRows(rowIndex, 1);
}

void PropertiesWindow::OnClickCancel(wxCommandEvent&) {
	EndModal(0);
}
