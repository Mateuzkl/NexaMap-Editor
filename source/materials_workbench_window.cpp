//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "materials_workbench_window.h"

#include <algorithm>
#include <array>
#include <map>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/srchctrl.h>
#include <wx/statbox.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/treectrl.h>

#include "border_workspace_window.h"
#include "client_version.h"
#include "find_item_window.h"
#include "graphics.h"
#include "gui.h"
#include "items.h"
#include "theme.h"

namespace {
	MaterialsWorkbenchWindow*& WindowInstance() {
		static MaterialsWorkbenchWindow* instance = nullptr;
		return instance;
	}

	enum WorkbenchId {
		ID_RELOAD = wxID_HIGHEST + 5100,
		ID_BORDERS,
		ID_ADD_CONTEXT,
		ID_REMOVE_CONTEXT,
		ID_PICK_ITEM,
		ID_ADD_ITEM,
		ID_UPDATE_ITEM,
		ID_REMOVE_ITEM,
		ID_ADD_DOOR,
		ID_UPDATE_DOOR,
		ID_REMOVE_DOOR,
		ID_ADD_COMPOSITE,
		ID_REMOVE_COMPOSITE,
		ID_ADD_TILE,
		ID_UPDATE_TILE,
		ID_REMOVE_TILE,
		ID_USED_BY,
	};

	const std::array<wxString, 5> kTypes = { "ground", "wall", "doodad", "carpet", "table" };

	wxSize PreviewPanelSize(wxWindow* parent) {
		return parent ? parent->FromDIP(wxSize(420, 260)) : wxSize(420, 260);
	}

	pugi::xml_node MaterialsRoot(pugi::xml_document& document) {
		pugi::xml_node root = document.child("materials");
		if (!root) {
			root = document.child("materialsextension");
		}
		return root;
	}

	pugi::xml_node BrushAt(pugi::xml_node root, int index) {
		int current = 0;
		for (pugi::xml_node child = root.first_child(); child; child = child.next_sibling()) {
			if (as_lower_str(child.name()) != "brush") {
				continue;
			}
			if (current++ == index) {
				return child;
			}
		}
		return pugi::xml_node();
	}

	void SetAttribute(pugi::xml_node node, const char* name, const wxString& value) {
		pugi::xml_attribute attribute = node.attribute(name);
		if (!attribute) {
			attribute = node.append_attribute(name);
		}
		attribute.set_value(value.utf8_str());
	}

	void SetIntAttribute(pugi::xml_node node, const char* name, int value, bool omitZero = false) {
		if (omitZero && value == 0) {
			node.remove_attribute(name);
			return;
		}
		pugi::xml_attribute attribute = node.attribute(name);
		if (!attribute) {
			attribute = node.append_attribute(name);
		}
		attribute.set_value(value);
	}

	void SetBoolAttribute(pugi::xml_node node, const char* name, bool value, bool omitFalse = false) {
		if (omitFalse && !value) {
			node.remove_attribute(name);
			return;
		}
		pugi::xml_attribute attribute = node.attribute(name);
		if (!attribute) {
			attribute = node.append_attribute(name);
		}
		attribute.set_value(value ? "true" : "false");
	}

	void ShowInvalidItemId(wxWindow* parent) {
		wxMessageBox("Select a valid item ID.", "Materials Workbench", wxOK | wxICON_WARNING, parent);
	}

	class BrushTreeData final : public wxTreeItemData {
	public:
		explicit BrushTreeData(int index) : index(index) { }
		int index;
	};
}

class MaterialsBrushPreviewPanel final : public wxPanel {
public:
	struct Entry {
		int x = 0;
		int y = 0;
		int id = 0;
		bool selected = false;
	};

	explicit MaterialsBrushPreviewPanel(wxWindow* parent) :
		wxPanel(parent, wxID_ANY, wxDefaultPosition, PreviewPanelSize(parent), wxBORDER_THEME) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		Bind(wxEVT_PAINT, &MaterialsBrushPreviewPanel::OnPaint, this);
	}

	void SetEntries(std::vector<Entry> entries, const wxString& caption) {
		entries_ = std::move(entries);
		caption_ = caption;
		Refresh();
	}

private:
	void OnPaint(wxPaintEvent&) {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
		dc.Clear();
		dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
		dc.DrawText(caption_, FromDIP(8), FromDIP(6));
		if (entries_.empty()) {
			dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
			dc.DrawText("No visual variants in this context.", FromDIP(16), FromDIP(42));
			return;
		}

		int minX = entries_[0].x;
		int maxX = entries_[0].x;
		int minY = entries_[0].y;
		int maxY = entries_[0].y;
		for (const Entry& entry : entries_) {
			minX = std::min(minX, entry.x);
			maxX = std::max(maxX, entry.x);
			minY = std::min(minY, entry.y);
			maxY = std::max(maxY, entry.y);
		}
		const int cell = FromDIP(38);
		const int width = (maxX - minX + 1) * cell;
		const int height = (maxY - minY + 1) * cell;
		const int startX = std::max(FromDIP(12), (GetClientSize().GetWidth() - width) / 2);
		const int startY = std::max(FromDIP(38), (GetClientSize().GetHeight() - height) / 2);
		for (int y = minY; y <= maxY; ++y) {
			for (int x = minX; x <= maxX; ++x) {
				wxRect rect(startX + (x - minX) * cell, startY + (y - minY) * cell, cell, cell);
				dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
				dc.SetBrush(wxBrush(Theme::Get(Theme::Role::RaisedSurface)));
				dc.DrawRectangle(rect);
			}
		}
		for (const Entry& entry : entries_) {
			wxRect rect(startX + (entry.x - minX) * cell, startY + (entry.y - minY) * cell, cell, cell);
			if (entry.selected) {
				dc.SetPen(wxPen(Theme::Get(Theme::Role::AccentHover), FromDIP(2)));
				dc.SetBrush(*wxTRANSPARENT_BRUSH);
				dc.DrawRectangle(rect);
			}
			if (entry.id <= 0 || !g_items.typeExists(entry.id)) {
				continue;
			}
			Sprite* sprite = g_gui.gfx.getSprite(g_items[entry.id].clientID);
			if (sprite) {
				sprite->DrawTo(&dc, SPRITE_SIZE_32x32, rect.x + FromDIP(3), rect.y + FromDIP(3), FromDIP(32), FromDIP(32));
			}
		}
	}

	std::vector<Entry> entries_;
	wxString caption_;
};

void MaterialsWorkbenchWindow::Open(wxWindow* parent) {
	if (WindowInstance()) {
		WindowInstance()->Raise();
		WindowInstance()->SetFocus();
		return;
	}
	std::cerr << "[materials] Opening XML materials workbench" << std::endl;
	WindowInstance() = newd MaterialsWorkbenchWindow(parent);
	WindowInstance()->Show();
	std::cerr << "[materials] Materials workbench opened" << std::endl;
}

MaterialsWorkbenchWindow::MaterialsWorkbenchWindow(wxWindow* parent) :
	wxFrame(parent, wxID_ANY, "Materials Workbench", wxDefaultPosition, wxSize(1280, 820), wxDEFAULT_FRAME_STYLE | wxCLIP_CHILDREN) {
	std::cerr << "[materials] Building workbench layout" << std::endl;
	BuildLayout();
	CreateStatusBar();
	BindEvents();
	LoadDefaultMaterialsFile();
	CentreOnParent();
}

MaterialsWorkbenchWindow::~MaterialsWorkbenchWindow() {
	if (WindowInstance() == this) {
		WindowInstance() = nullptr;
	}
}

void MaterialsWorkbenchWindow::BuildLayout() {
	auto* rootSizer = newd wxBoxSizer(wxVERTICAL);
	auto* toolbar = newd wxBoxSizer(wxHORIZONTAL);
	auto* openButton = newd wxButton(this, wxID_OPEN, "Open XML...");
	auto* reloadButton = newd wxButton(this, ID_RELOAD, "Reload");
	auto* bordersButton = newd wxButton(this, ID_BORDERS, "Borders...");
	fileLabel_ = newd wxStaticText(this, wxID_ANY, "No materials file loaded");
	toolbar->Add(openButton, 0, wxALL, FromDIP(5));
	toolbar->Add(reloadButton, 0, wxTOP | wxBOTTOM | wxRIGHT, FromDIP(5));
	toolbar->Add(bordersButton, 0, wxTOP | wxBOTTOM | wxRIGHT, FromDIP(5));
	toolbar->Add(fileLabel_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	rootSizer->Add(toolbar, 0, wxEXPAND);
	rootSizer->Add(newd wxStaticLine(this), 0, wxEXPAND);

	auto* body = newd wxBoxSizer(wxHORIZONTAL);
	auto* catalogPanel = newd wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(300), -1));
	auto* catalogSizer = newd wxBoxSizer(wxVERTICAL);
	filterCtrl_ = newd wxSearchCtrl(catalogPanel, wxID_ANY);
	filterCtrl_->SetDescriptiveText("Filter catalog");
	filterCtrl_->ShowCancelButton(true);
	typeFilterCtrl_ = newd wxChoice(catalogPanel, wxID_ANY);
	typeFilterCtrl_->Append("All brush types");
	for (const wxString& type : kTypes) {
		typeFilterCtrl_->Append(TypeLabel(type));
	}
	typeFilterCtrl_->SetSelection(0);
	catalogTree_ = newd wxTreeCtrl(catalogPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT);
	catalogSizer->Add(filterCtrl_, 0, wxEXPAND | wxALL, FromDIP(7));
	catalogSizer->Add(typeFilterCtrl_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(7));
	catalogSizer->Add(catalogTree_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(7));
	catalogPanel->SetSizer(catalogSizer);
	body->Add(catalogPanel, 0, wxEXPAND);
	body->Add(newd wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL), 0, wxEXPAND);

	auto* workspace = newd wxPanel(this);
	auto* workspaceSizer = newd wxBoxSizer(wxVERTICAL);
	titleLabel_ = newd wxStaticText(workspace, wxID_ANY, "Brush Workspace");
	wxFont titleFont = titleLabel_->GetFont();
	titleFont.SetPointSize(titleFont.GetPointSize() + 3);
	titleFont.MakeBold();
	titleLabel_->SetFont(titleFont);
	subtitleLabel_ = newd wxStaticText(workspace, wxID_ANY, "Select a brush from the catalog.");
	workspaceSizer->Add(titleLabel_, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(10));
	workspaceSizer->Add(subtitleLabel_, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));

	notebook_ = newd wxNotebook(workspace, wxID_ANY);
	auto* metadataPage = newd wxScrolledWindow(notebook_);
	metadataPage->SetScrollRate(0, FromDIP(10));
	auto* metadataSizer = newd wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
	metadataSizer->AddGrowableCol(1, 1);
	auto addMetadataRow = [&](const wxString& label, wxWindow* control) {
		metadataSizer->Add(newd wxStaticText(metadataPage, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
		metadataSizer->Add(control, 1, wxEXPAND);
	};
	nameCtrl_ = newd wxTextCtrl(metadataPage, wxID_ANY);
	typeCtrl_ = newd wxChoice(metadataPage, wxID_ANY);
	for (const wxString& type : kTypes) {
		typeCtrl_->Append(TypeLabel(type), newd wxStringClientData(type));
	}
	typeCtrl_->Enable(false);
	serverLookCtrl_ = newd wxSpinCtrl(metadataPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 65535);
	clientLookCtrl_ = newd wxSpinCtrl(metadataPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 65535);
	zOrderCtrl_ = newd wxSpinCtrl(metadataPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, -100000, 100000);
	thicknessCtrl_ = newd wxTextCtrl(metadataPage, wxID_ANY);
	addMetadataRow("Brush name", nameCtrl_);
	addMetadataRow("Brush type", typeCtrl_);
	addMetadataRow("Server look ID", serverLookCtrl_);
	addMetadataRow("Client look ID", clientLookCtrl_);
	addMetadataRow("Z order", zOrderCtrl_);
	addMetadataRow("Thickness", thicknessCtrl_);
	auto* flags = newd wxBoxSizer(wxHORIZONTAL);
	draggableCtrl_ = newd wxCheckBox(metadataPage, wxID_ANY, "Draggable");
	onBlockingCtrl_ = newd wxCheckBox(metadataPage, wxID_ANY, "On blocking");
	onDuplicateCtrl_ = newd wxCheckBox(metadataPage, wxID_ANY, "On duplicate");
	redoBordersCtrl_ = newd wxCheckBox(metadataPage, wxID_ANY, "Redo borders");
	oneSizeCtrl_ = newd wxCheckBox(metadataPage, wxID_ANY, "One size");
	flags->Add(draggableCtrl_, 0, wxRIGHT, FromDIP(12));
	flags->Add(onBlockingCtrl_, 0, wxRIGHT, FromDIP(12));
	flags->Add(onDuplicateCtrl_, 0, wxRIGHT, FromDIP(12));
	flags->Add(redoBordersCtrl_, 0, wxRIGHT, FromDIP(12));
	flags->Add(oneSizeCtrl_);
	metadataSizer->Add(newd wxStaticText(metadataPage, wxID_ANY, "Flags"), 0, wxALIGN_CENTER_VERTICAL);
	metadataSizer->Add(flags, 1, wxEXPAND);
	auto* metadataOuter = newd wxBoxSizer(wxVERTICAL);
	metadataOuter->Add(metadataSizer, 0, wxEXPAND | wxALL, FromDIP(14));
	metadataOuter->Add(newd wxStaticText(metadataPage, wxID_ANY, "Changes are written to the original materials XML. Unknown attributes, borders and friend/enemy links are preserved."), 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(14));
	metadataPage->SetSizer(metadataOuter);
	notebook_->AddPage(metadataPage, "Metadata");

	auto* editorPage = newd wxPanel(notebook_);
	auto* editorSizer = newd wxBoxSizer(wxVERTICAL);
	editorTitleLabel_ = newd wxStaticText(editorPage, wxID_ANY, "Variation Data");
	wxFont sectionFont = editorTitleLabel_->GetFont();
	sectionFont.MakeBold();
	editorTitleLabel_->SetFont(sectionFont);
	editorHintLabel_ = newd wxStaticText(editorPage, wxID_ANY, "Select a brush.");
	editorSizer->Add(editorTitleLabel_, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
	editorSizer->Add(editorHintLabel_, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	auto* contextRow = newd wxBoxSizer(wxHORIZONTAL);
	contextLabel_ = newd wxStaticText(editorPage, wxID_ANY, "Context");
	contextCtrl_ = newd wxChoice(editorPage, wxID_ANY);
	addContextButton_ = newd wxButton(editorPage, ID_ADD_CONTEXT, "+");
	removeContextButton_ = newd wxButton(editorPage, ID_REMOVE_CONTEXT, "-");
	contextRow->Add(contextLabel_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	contextRow->Add(contextCtrl_, 1, wxRIGHT, FromDIP(5));
	contextRow->Add(addContextButton_, 0, wxRIGHT, FromDIP(3));
	contextRow->Add(removeContextButton_, 0);
	editorSizer->Add(contextRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	auto* variationRow = newd wxBoxSizer(wxHORIZONTAL);
	auto* itemBox = newd wxStaticBoxSizer(wxVERTICAL, editorPage, "Items / Variants");
	itemList_ = newd wxListBox(itemBox->GetStaticBox(), wxID_ANY);
	itemBox->Add(itemList_, 1, wxEXPAND | wxALL, FromDIP(6));
	auto* itemFields = newd wxFlexGridSizer(2, FromDIP(4), FromDIP(5));
	itemFields->AddGrowableCol(1, 1);
	itemIdCtrl_ = newd wxSpinCtrl(itemBox->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 65535);
	itemChanceCtrl_ = newd wxSpinCtrl(itemBox->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 1000000, 10);
	itemFields->Add(newd wxStaticText(itemBox->GetStaticBox(), wxID_ANY, "Item ID"), 0, wxALIGN_CENTER_VERTICAL);
	itemFields->Add(itemIdCtrl_, 1, wxEXPAND);
	itemFields->Add(newd wxStaticText(itemBox->GetStaticBox(), wxID_ANY, "Chance"), 0, wxALIGN_CENTER_VERTICAL);
	itemFields->Add(itemChanceCtrl_, 1, wxEXPAND);
	itemBox->Add(itemFields, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(6));
	auto* itemActions = newd wxBoxSizer(wxHORIZONTAL);
	itemActions->Add(newd wxButton(itemBox->GetStaticBox(), ID_PICK_ITEM, "Pick..."), 0, wxRIGHT, FromDIP(3));
	itemActions->Add(newd wxButton(itemBox->GetStaticBox(), ID_ADD_ITEM, "Add"), 0, wxRIGHT, FromDIP(3));
	updateItemButton_ = newd wxButton(itemBox->GetStaticBox(), ID_UPDATE_ITEM, "Update");
	removeItemButton_ = newd wxButton(itemBox->GetStaticBox(), ID_REMOVE_ITEM, "Remove");
	itemActions->Add(updateItemButton_, 0, wxRIGHT, FromDIP(3));
	itemActions->Add(removeItemButton_);
	itemBox->Add(itemActions, 0, wxALL, FromDIP(6));
	variationRow->Add(itemBox, 0, wxEXPAND | wxRIGHT, FromDIP(8));
	previewPanel_ = newd MaterialsBrushPreviewPanel(editorPage);
	variationRow->Add(previewPanel_, 1, wxEXPAND);
	editorSizer->Add(variationRow, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	auto* extraRow = newd wxBoxSizer(wxHORIZONTAL);
	doorBox_ = newd wxStaticBoxSizer(wxVERTICAL, editorPage, "Doors");
	doorList_ = newd wxListBox(doorBox_->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(110)));
	doorBox_->Add(doorList_, 1, wxEXPAND | wxALL, FromDIP(5));
	auto* doorFields = newd wxBoxSizer(wxHORIZONTAL);
	doorIdCtrl_ = newd wxSpinCtrl(doorBox_->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(95), -1), wxSP_ARROW_KEYS, 0, 65535);
	doorTypeCtrl_ = newd wxChoice(doorBox_->GetStaticBox(), wxID_ANY);
	for (const char* type : { "normal", "normal_alt", "locked", "quest", "magic", "archway", "window", "hatch_window" }) {
		doorTypeCtrl_->Append(type);
	}
	doorTypeCtrl_->SetSelection(0);
	doorOpenCtrl_ = newd wxCheckBox(doorBox_->GetStaticBox(), wxID_ANY, "Open");
	doorLockedCtrl_ = newd wxCheckBox(doorBox_->GetStaticBox(), wxID_ANY, "Locked");
	doorFields->Add(doorIdCtrl_, 0, wxRIGHT, FromDIP(4));
	doorFields->Add(doorTypeCtrl_, 1, wxRIGHT, FromDIP(4));
	doorFields->Add(doorOpenCtrl_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
	doorFields->Add(doorLockedCtrl_, 0, wxALIGN_CENTER_VERTICAL);
	doorBox_->Add(doorFields, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(5));
	auto* doorActions = newd wxBoxSizer(wxHORIZONTAL);
	doorActions->Add(newd wxButton(doorBox_->GetStaticBox(), ID_ADD_DOOR, "Add"), 0, wxRIGHT, FromDIP(3));
	doorActions->Add(newd wxButton(doorBox_->GetStaticBox(), ID_UPDATE_DOOR, "Update"), 0, wxRIGHT, FromDIP(3));
	doorActions->Add(newd wxButton(doorBox_->GetStaticBox(), ID_REMOVE_DOOR, "Remove"));
	doorBox_->Add(doorActions, 0, wxALL, FromDIP(5));
	extraRow->Add(doorBox_, 1, wxEXPAND | wxRIGHT, FromDIP(8));

	compositeBox_ = newd wxStaticBoxSizer(wxHORIZONTAL, editorPage, "Doodad Composites / Tile Layers");
	auto* compositeLeft = newd wxBoxSizer(wxVERTICAL);
	compositeList_ = newd wxListBox(compositeBox_->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxSize(FromDIP(190), FromDIP(105)));
	compositeChanceCtrl_ = newd wxSpinCtrl(compositeBox_->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 1000000, 10);
	compositeLeft->Add(compositeList_, 1, wxEXPAND | wxBOTTOM, FromDIP(4));
	compositeLeft->Add(compositeChanceCtrl_, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
	auto* compositeActions = newd wxBoxSizer(wxHORIZONTAL);
	compositeActions->Add(newd wxButton(compositeBox_->GetStaticBox(), ID_ADD_COMPOSITE, "Add composite"), 0, wxRIGHT, FromDIP(3));
	compositeActions->Add(newd wxButton(compositeBox_->GetStaticBox(), ID_REMOVE_COMPOSITE, "Remove"));
	compositeLeft->Add(compositeActions, 0, wxEXPAND);
	compositeBox_->Add(compositeLeft, 0, wxALL, FromDIP(5));
	auto* tileRight = newd wxBoxSizer(wxVERTICAL);
	tileList_ = newd wxListBox(compositeBox_->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxSize(FromDIP(260), FromDIP(105)));
	tileRight->Add(tileList_, 1, wxEXPAND | wxBOTTOM, FromDIP(4));
	auto* tileFields = newd wxBoxSizer(wxHORIZONTAL);
	tileIdCtrl_ = newd wxSpinCtrl(compositeBox_->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(90), -1), wxSP_ARROW_KEYS, 0, 65535);
	tileXCtrl_ = newd wxSpinCtrl(compositeBox_->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(60), -1), wxSP_ARROW_KEYS, -32767, 32767);
	tileYCtrl_ = newd wxSpinCtrl(compositeBox_->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(60), -1), wxSP_ARROW_KEYS, -32767, 32767);
	tileZCtrl_ = newd wxSpinCtrl(compositeBox_->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(60), -1), wxSP_ARROW_KEYS, -7, 7);
	tileFields->Add(tileIdCtrl_, 0, wxRIGHT, FromDIP(3));
	tileFields->Add(tileXCtrl_, 0, wxRIGHT, FromDIP(3));
	tileFields->Add(tileYCtrl_, 0, wxRIGHT, FromDIP(3));
	tileFields->Add(tileZCtrl_);
	tileRight->Add(tileFields, 0, wxBOTTOM, FromDIP(4));
	auto* tileActions = newd wxBoxSizer(wxHORIZONTAL);
	tileActions->Add(newd wxButton(compositeBox_->GetStaticBox(), ID_ADD_TILE, "Add tile"), 0, wxRIGHT, FromDIP(3));
	tileActions->Add(newd wxButton(compositeBox_->GetStaticBox(), ID_UPDATE_TILE, "Update"), 0, wxRIGHT, FromDIP(3));
	tileActions->Add(newd wxButton(compositeBox_->GetStaticBox(), ID_REMOVE_TILE, "Remove"));
	tileRight->Add(tileActions, 0);
	compositeBox_->Add(tileRight, 1, wxEXPAND | wxALL, FromDIP(5));
	extraRow->Add(compositeBox_, 1, wxEXPAND);
	editorSizer->Add(extraRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	editorPage->SetSizer(editorSizer);
	notebook_->AddPage(editorPage, "Editor");

	auto* linksPage = newd wxPanel(notebook_);
	auto* linksSizer = newd wxBoxSizer(wxVERTICAL);
	linksSizer->Add(newd wxStaticText(linksPage, wxID_ANY, "Friend and enemy links preserved from the XML"), 0, wxALL, FromDIP(8));
	linksList_ = newd wxListBox(linksPage, wxID_ANY);
	linksSizer->Add(linksList_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	linksPage->SetSizer(linksSizer);
	notebook_->AddPage(linksPage, "Links");
	workspaceSizer->Add(notebook_, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));

	auto* actions = newd wxBoxSizer(wxHORIZONTAL);
	newBrushButton_ = newd wxButton(workspace, wxID_NEW, "New Brush");
	deleteBrushButton_ = newd wxButton(workspace, wxID_DELETE, "Delete Brush");
	usedByButton_ = newd wxButton(workspace, ID_USED_BY, "Used By");
	revertButton_ = newd wxButton(workspace, wxID_REVERT, "Revert");
	saveButton_ = newd wxButton(workspace, wxID_SAVE, "Save Brush");
	actions->Add(newBrushButton_, 0, wxRIGHT, FromDIP(4));
	actions->Add(deleteBrushButton_, 0, wxRIGHT, FromDIP(4));
	actions->Add(usedByButton_, 0);
	actions->AddStretchSpacer();
	actions->Add(revertButton_, 0, wxRIGHT, FromDIP(4));
	actions->Add(saveButton_, 0);
	workspaceSizer->Add(newd wxStaticLine(workspace), 0, wxEXPAND | wxALL, FromDIP(8));
	workspaceSizer->Add(actions, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	workspace->SetSizer(workspaceSizer);
	body->Add(workspace, 1, wxEXPAND);
	rootSizer->Add(body, 1, wxEXPAND);
	SetSizer(rootSizer);
	RefreshActionState();
}

void MaterialsWorkbenchWindow::BindEvents() {
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OpenMaterialsFile(); }, wxID_OPEN);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (!rootMaterialsPath_.IsEmpty() && ResolvePendingChanges("reload")){ LoadCatalog(rootMaterialsPath_);
} }, ID_RELOAD);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { BorderWorkspaceWindow::Open(this); }, ID_BORDERS);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddContext(); }, ID_ADD_CONTEXT);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RemoveContext(); }, ID_REMOVE_CONTEXT);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { PickItem(); }, ID_PICK_ITEM);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ApplyItem(true); }, ID_ADD_ITEM);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ApplyItem(false); }, ID_UPDATE_ITEM);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RemoveItem(); }, ID_REMOVE_ITEM);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ApplyDoor(true); }, ID_ADD_DOOR);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ApplyDoor(false); }, ID_UPDATE_DOOR);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RemoveDoor(); }, ID_REMOVE_DOOR);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddComposite(); }, ID_ADD_COMPOSITE);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RemoveComposite(); }, ID_REMOVE_COMPOSITE);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ApplyTile(true); }, ID_ADD_TILE);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ApplyTile(false); }, ID_UPDATE_TILE);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RemoveTile(); }, ID_REMOVE_TILE);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CreateBrush(); }, wxID_NEW);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { DeleteBrush(); }, wxID_DELETE);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ShowUsedBy(); }, ID_USED_BY);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SaveCurrentBrush(); }, wxID_SAVE);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RevertCurrentBrush(); }, wxID_REVERT);
	Bind(wxEVT_CLOSE_WINDOW, &MaterialsWorkbenchWindow::OnClose, this);
	filterCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { PopulateCatalog(); });
	typeFilterCtrl_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { PopulateCatalog(); });
	catalogTree_->Bind(wxEVT_TREE_SEL_CHANGED, [this](wxTreeEvent& event) {
		auto* data = dynamic_cast<BrushTreeData*>(catalogTree_->GetItemData(event.GetItem()));
		if (!data || data->index == currentBrush_) {
			return;
		}
		if (!ResolvePendingChanges("switch brushes")) {
			return;
		}
		LoadBrush(data->index);
	});
	contextCtrl_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
		currentContext_ = std::max(0, contextCtrl_->GetSelection());
		currentItem_ = currentDoor_ = currentComposite_ = currentTile_ = -1;
		PopulateItems();
		PopulateDoors();
		PopulateComposites();
		RefreshPreview();
	});
	itemList_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { SelectItem(itemList_->GetSelection()); });
	doorList_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { SelectDoor(doorList_->GetSelection()); });
	compositeList_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { currentComposite_ = compositeList_->GetSelection(); currentTile_ = -1; PopulateTiles(); RefreshPreview(); });
	compositeChanceCtrl_->Bind(wxEVT_SPINCTRL, [this](wxCommandEvent&) {
		if (CompositeEntry* composite = CurrentComposite()) {
			composite->chance = compositeChanceCtrl_->GetValue();
			MarkDirty();
		}
	});
	tileList_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { SelectTile(tileList_->GetSelection()); });
	auto dirtyText = [this](wxCommandEvent&) { MarkDirty(); };
	nameCtrl_->Bind(wxEVT_TEXT, dirtyText);
	serverLookCtrl_->Bind(wxEVT_TEXT, dirtyText);
	clientLookCtrl_->Bind(wxEVT_TEXT, dirtyText);
	zOrderCtrl_->Bind(wxEVT_TEXT, dirtyText);
	thicknessCtrl_->Bind(wxEVT_TEXT, dirtyText);
	for (wxCheckBox* check : { draggableCtrl_, onBlockingCtrl_, onDuplicateCtrl_, redoBordersCtrl_, oneSizeCtrl_ }) {
		check->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { MarkDirty(); });
	}
}

void MaterialsWorkbenchWindow::LoadDefaultMaterialsFile() {
	if (!g_gui.IsVersionLoaded()) {
		subtitleLabel_->SetLabel("Load a client version or use Open XML...");
		return;
	}
	FileName path = g_gui.GetCurrentVersion().getDataPath();
	path.SetFullName("materials.xml");
	LoadCatalog(path.GetFullPath());
}

void MaterialsWorkbenchWindow::OpenMaterialsFile() {
	if (!ResolvePendingChanges("open another catalog")) {
		return;
	}
	wxFileDialog dialog(this, "Open materials.xml", wxEmptyString, "materials.xml", "XML files (*.xml)|*.xml", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dialog.ShowModal() == wxID_OK) {
		LoadCatalog(dialog.GetPath());
	}
}

bool MaterialsWorkbenchWindow::LoadCatalog(const wxString& path) {
	std::cerr << "[materials] Loading XML catalog: " << path.ToStdString() << std::endl;
	records_.clear();
	std::set<wxString> visited;
	wxString error;
	if (!ScanMaterialsFile(path, visited, error)) {
		wxMessageBox(error, "Materials Workbench", wxOK | wxICON_ERROR, this);
		return false;
	}
	std::cerr << "[materials] Parsed " << records_.size() << " brushes from "
	          << visited.size() << " XML files" << std::endl;
	rootMaterialsPath_ = path;
	currentBrush_ = -1;
	dirty_ = false;
	fileLabel_->SetLabel(wxString::Format("%zu brushes | %zu XML files | %s", records_.size(), visited.size(), path));
	fileLabel_->SetToolTip(path);
	PopulateCatalog();
	if (!records_.empty()) {
		std::cerr << "[materials] Loading first brush workspace" << std::endl;
		LoadBrush(0);
	}
	std::cerr << "[materials] XML catalog ready" << std::endl;
	return true;
}

bool MaterialsWorkbenchWindow::ScanMaterialsFile(const wxString& path, std::set<wxString>& visited, wxString& error) {
	wxFileName filename(path);
	filename.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
	const wxString normalized = filename.GetFullPath();
	if (visited.count(normalized)) {
		return true;
	}
	visited.insert(normalized);
	pugi::xml_document document;
	pugi::xml_parse_result result = document.load_file(normalized.mb_str());
	if (!result) {
		error = wxString::Format("Could not parse %s: %s", normalized, wxString::FromUTF8(result.description()));
		return false;
	}
	pugi::xml_node root = MaterialsRoot(document);
	if (!root) {
		return true;
	}
	int brushIndex = 0;
	for (pugi::xml_node child = root.first_child(); child; child = child.next_sibling()) {
		const std::string childName = as_lower_str(child.name());
		if (childName == "include") {
			wxFileName include(filename.GetPath(), wxString::FromUTF8(child.attribute("file").as_string()));
			if (!ScanMaterialsFile(include.GetFullPath(), visited, error)) {
				return false;
			}
		} else if (childName == "brush") {
			const wxString type = wxString::FromUTF8(child.attribute("type").as_string()).Lower();
			if (std::find(kTypes.begin(), kTypes.end(), type) != kTypes.end()) {
				records_.push_back(ParseBrush(child, normalized, filename.GetFullName(), brushIndex));
			}
			++brushIndex;
		}
	}
	return true;
}

MaterialsWorkbenchWindow::BrushRecord MaterialsWorkbenchWindow::ParseBrush(
	pugi::xml_node brush, const wxString& sourcePath, const wxString& sourceName, int sourceIndex
) const {
	BrushRecord record;
	record.sourcePath = sourcePath;
	record.sourceName = sourceName;
	record.sourceIndex = sourceIndex;
	record.name = wxString::FromUTF8(brush.attribute("name").as_string());
	record.type = wxString::FromUTF8(brush.attribute("type").as_string()).Lower();
	record.serverLookId = brush.attribute("server_lookid").as_int();
	record.lookId = brush.attribute("lookid").as_int();
	record.zOrder = brush.attribute("z-order").as_int();
	record.thickness = wxString::FromUTF8(brush.attribute("thickness").as_string());
	record.draggable = brush.attribute("draggable").as_bool();
	record.onBlocking = brush.attribute("on_blocking").as_bool();
	record.onDuplicate = brush.attribute("on_duplicate").as_bool();
	record.redoBorders = brush.attribute("redo_borders").as_bool();
	record.oneSize = brush.attribute("one_size").as_bool();

	if (record.type == "ground") {
		for (pugi::xml_node item = brush.child("item"); item; item = item.next_sibling("item")) {
			record.groundItems.push_back({ item.attribute("id").as_int(), item.attribute("chance").as_int(10) });
		}
	} else if (record.type == "wall") {
		for (pugi::xml_node wall = brush.child("wall"); wall; wall = wall.next_sibling("wall")) {
			NodeEntry node;
			node.key = wxString::FromUTF8(wall.attribute("type").as_string());
			for (pugi::xml_node child = wall.first_child(); child; child = child.next_sibling()) {
				const std::string name = as_lower_str(child.name());
				if (name == "item") {
					node.items.push_back({ child.attribute("id").as_int(), child.attribute("chance").as_int(10) });
				} else if (name == "door") {
					node.doors.push_back({ child.attribute("id").as_int(), wxString::FromUTF8(child.attribute("type").as_string()), child.attribute("open").as_bool(), child.attribute("locked").as_bool(), child.attribute("hate").as_bool() });
				}
			}
			record.nodes.push_back(node);
		}
	} else if (record.type == "carpet" || record.type == "table") {
		const char* tag = record.type == "carpet" ? "carpet" : "table";
		for (pugi::xml_node aligned = brush.child(tag); aligned; aligned = aligned.next_sibling(tag)) {
			NodeEntry node;
			node.key = wxString::FromUTF8(aligned.attribute("align").as_string());
			if (aligned.attribute("id")) {
				node.items.push_back({ aligned.attribute("id").as_int(), aligned.attribute("chance").as_int(1) });
			}
			for (pugi::xml_node item = aligned.child("item"); item; item = item.next_sibling("item")) {
				node.items.push_back({ item.attribute("id").as_int(), item.attribute("chance").as_int(10) });
			}
			record.nodes.push_back(node);
		}
	} else if (record.type == "doodad") {
		AlternativeEntry direct = ParseAlternative(brush);
		if (!direct.singles.empty() || !direct.composites.empty()) {
			record.alternatives.push_back(direct);
		}
		for (pugi::xml_node alternate = brush.child("alternate"); alternate; alternate = alternate.next_sibling("alternate")) {
			record.wrappedAlternatives = true;
			record.alternatives.push_back(ParseAlternative(alternate));
		}
		if (record.alternatives.empty()) {
			record.alternatives.emplace_back();
		}
	}
	for (pugi::xml_node child = brush.first_child(); child; child = child.next_sibling()) {
		const wxString relation = wxString::FromUTF8(child.name()).Lower();
		if (relation == "friend" || relation == "enemy") {
			record.links.push_back({ relation, wxString::FromUTF8(child.attribute("name").as_string()) });
		}
	}
	return record;
}

MaterialsWorkbenchWindow::AlternativeEntry MaterialsWorkbenchWindow::ParseAlternative(pugi::xml_node node) const {
	AlternativeEntry alternative;
	for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling()) {
		const std::string name = as_lower_str(child.name());
		if (name == "item") {
			alternative.singles.push_back({ child.attribute("id").as_int(), child.attribute("chance").as_int(10) });
		} else if (name == "composite") {
			CompositeEntry composite;
			composite.chance = child.attribute("chance").as_int(10);
			for (pugi::xml_node tile = child.child("tile"); tile; tile = tile.next_sibling("tile")) {
				for (pugi::xml_node item = tile.child("item"); item; item = item.next_sibling("item")) {
					composite.tiles.push_back({ tile.attribute("x").as_int(), tile.attribute("y").as_int(), tile.attribute("z").as_int(), item.attribute("id").as_int() });
				}
			}
			alternative.composites.push_back(composite);
		}
	}
	return alternative;
}

void MaterialsWorkbenchWindow::PopulateCatalog() {
	const wxString query = filterCtrl_->GetValue().Lower();
	const int filter = typeFilterCtrl_->GetSelection();
	catalogTree_->Freeze();
	catalogTree_->DeleteAllItems();
	wxTreeItemId root = catalogTree_->AddRoot("Materials");
	std::map<wxString, wxTreeItemId> groups;
	for (const wxString& type : kTypes) {
		groups[type] = catalogTree_->AppendItem(root, TypeLabel(type));
	}
	for (int i = 0; i < static_cast<int>(records_.size()); ++i) {
		const BrushRecord& record = records_[i];
		if (filter > 0 && record.type != kTypes[filter - 1]) {
			continue;
		}
		wxString haystack = (record.name + " " + record.type + " " + record.sourceName).Lower();
		if (!query.IsEmpty() && !haystack.Contains(query)) {
			continue;
		}
		catalogTree_->AppendItem(groups[record.type], record.name, -1, -1, newd BrushTreeData(i));
	}
	for (const auto& [type, item] : groups) {
		catalogTree_->Expand(item);
	}
	catalogTree_->Thaw();
}

void MaterialsWorkbenchWindow::LoadBrush(int index) {
	if (index < 0 || index >= static_cast<int>(records_.size())) {
		return;
	}
	currentBrush_ = index;
	currentContext_ = 0;
	currentItem_ = currentDoor_ = currentComposite_ = currentTile_ = -1;
	dirty_ = false;
	loading_ = true;
	PopulateMetadata();
	PopulateEditor();
	PopulateLinks();
	loading_ = false;
	RefreshHeader();
	RefreshActionState();
}

bool MaterialsWorkbenchWindow::ResolvePendingChanges(const wxString& action) {
	if (!dirty_) {
		return true;
	}
	int answer = wxMessageBox(wxString::Format("Save changes to %s before you %s?", records_[currentBrush_].name, action), "Unsaved brush changes", wxYES_NO | wxCANCEL | wxICON_WARNING, this);
	if (answer == wxCANCEL) {
		return false;
	}
	if (answer == wxYES) {
		return SaveCurrentBrush();
	}
	dirty_ = false;
	return true;
}

void MaterialsWorkbenchWindow::PopulateMetadata() {
	const BrushRecord& record = records_[currentBrush_];
	nameCtrl_->ChangeValue(record.name);
	for (unsigned int i = 0; i < typeCtrl_->GetCount(); ++i) {
		auto* data = dynamic_cast<wxStringClientData*>(typeCtrl_->GetClientObject(i));
		if (data && data->GetData() == record.type) {
			typeCtrl_->SetSelection(i);
		}
	}
	serverLookCtrl_->SetValue(record.serverLookId);
	clientLookCtrl_->SetValue(record.lookId);
	zOrderCtrl_->SetValue(record.zOrder);
	thicknessCtrl_->ChangeValue(record.thickness);
	draggableCtrl_->SetValue(record.draggable);
	onBlockingCtrl_->SetValue(record.onBlocking);
	onDuplicateCtrl_->SetValue(record.onDuplicate);
	redoBordersCtrl_->SetValue(record.redoBorders);
	oneSizeCtrl_->SetValue(record.oneSize);
}

void MaterialsWorkbenchWindow::PopulateEditor() {
	const wxString type = records_[currentBrush_].type;
	editorTitleLabel_->SetLabel(TypeLabel(type) + " Editor");
	if (type == "ground") {
		editorHintLabel_->SetLabel("Edit weighted ground items and inspect their distribution preview.");
	} else if (type == "wall") {
		editorHintLabel_->SetLabel("Edit wall parts, alternatives and door definitions.");
	} else if (type == "doodad") {
		editorHintLabel_->SetLabel("Edit alternatives, single items and composite tile layers.");
	} else {
		editorHintLabel_->SetLabel("Edit layout contexts and weighted variants with a seamless preview.");
	}
	PopulateContexts();
	PopulateItems();
	PopulateDoors();
	PopulateComposites();
	RefreshPreview();
}

void MaterialsWorkbenchWindow::PopulateContexts() {
	const BrushRecord& record = records_[currentBrush_];
	contextCtrl_->Clear();
	if (record.type == "ground") {
		contextLabel_->SetLabel("Variants");
		contextCtrl_->Append("Weighted ground items");
	} else if (record.type == "doodad") {
		contextLabel_->SetLabel("Alternative");
		for (int i = 0; i < static_cast<int>(record.alternatives.size()); ++i) {
			contextCtrl_->Append(wxString::Format("Alternative %d", i + 1));
		}
	} else {
		contextLabel_->SetLabel(record.type == "wall" ? "Wall part" : "Layout context");
		for (const NodeEntry& node : record.nodes) {
			contextCtrl_->Append(node.key);
		}
	}
	if (contextCtrl_->GetCount() > 0) {
		currentContext_ = std::clamp(currentContext_, 0, static_cast<int>(contextCtrl_->GetCount()) - 1);
		contextCtrl_->SetSelection(currentContext_);
	}
	const bool canEditContexts = record.type != "ground";
	addContextButton_->Enable(canEditContexts);
	removeContextButton_->Enable(canEditContexts && contextCtrl_->GetCount() > 1);
}

std::vector<MaterialsWorkbenchWindow::ItemEntry>* MaterialsWorkbenchWindow::CurrentItems() {
	if (currentBrush_ < 0) {
		return nullptr;
	}
	BrushRecord& record = records_[currentBrush_];
	if (record.type == "ground") {
		return &record.groundItems;
	}
	if (record.type == "doodad") {
		AlternativeEntry* alternative = CurrentAlternative();
		return alternative ? &alternative->singles : nullptr;
	}
	if (currentContext_ >= 0 && currentContext_ < static_cast<int>(record.nodes.size())) {
		return &record.nodes[currentContext_].items;
	}
	return nullptr;
}

std::vector<MaterialsWorkbenchWindow::DoorEntry>* MaterialsWorkbenchWindow::CurrentDoors() {
	if (currentBrush_ < 0 || records_[currentBrush_].type != "wall") {
		return nullptr;
	}
	BrushRecord& record = records_[currentBrush_];
	if (currentContext_ < 0 || currentContext_ >= static_cast<int>(record.nodes.size())) {
		return nullptr;
	}
	return &record.nodes[currentContext_].doors;
}

MaterialsWorkbenchWindow::AlternativeEntry* MaterialsWorkbenchWindow::CurrentAlternative() {
	if (currentBrush_ < 0) {
		return nullptr;
	}
	BrushRecord& record = records_[currentBrush_];
	if (record.type != "doodad" || currentContext_ < 0 || currentContext_ >= static_cast<int>(record.alternatives.size())) {
		return nullptr;
	}
	return &record.alternatives[currentContext_];
}

MaterialsWorkbenchWindow::CompositeEntry* MaterialsWorkbenchWindow::CurrentComposite() {
	AlternativeEntry* alternative = CurrentAlternative();
	if (!alternative || currentComposite_ < 0 || currentComposite_ >= static_cast<int>(alternative->composites.size())) {
		return nullptr;
	}
	return &alternative->composites[currentComposite_];
}

void MaterialsWorkbenchWindow::PopulateItems() {
	itemList_->Clear();
	std::vector<ItemEntry>* items = CurrentItems();
	if (items) {
		int total = 0;
		for (const ItemEntry& item : *items) {
			total += item.chance;
		}
		for (int i = 0; i < static_cast<int>(items->size()); ++i) {
			const ItemEntry& item = (*items)[i];
			double percent = total > 0 ? item.chance * 100.0 / total : 0.0;
			itemList_->Append(wxString::Format("%d. item %d | chance %d | %.1f%%", i + 1, item.id, item.chance, percent));
		}
	}
	currentItem_ = -1;
	itemIdCtrl_->SetValue(0);
	itemChanceCtrl_->SetValue(10);
	RefreshActionState();
}

void MaterialsWorkbenchWindow::SelectItem(int index) {
	std::vector<ItemEntry>* items = CurrentItems();
	if (!items || index < 0 || index >= static_cast<int>(items->size())) {
		return;
	}
	currentItem_ = index;
	itemIdCtrl_->SetValue((*items)[index].id);
	itemChanceCtrl_->SetValue((*items)[index].chance);
	RefreshPreview();
	RefreshActionState();
}

void MaterialsWorkbenchWindow::PickItem() {
	FindItemDialog dialog(this, "Pick material item");
	dialog.setSearchMode(FindItemDialog::ServerIDs);
	if (dialog.ShowModal() == wxID_OK) {
		itemIdCtrl_->SetValue(dialog.getResultID());
	}
}

void MaterialsWorkbenchWindow::ApplyItem(bool add) {
	std::vector<ItemEntry>* items = CurrentItems();
	if (!items) {
		return;
	}
	const int id = itemIdCtrl_->GetValue();
	if (id <= 0 || !g_items.typeExists(id)) {
		ShowInvalidItemId(this);
		return;
	}
	ItemEntry entry { id, itemChanceCtrl_->GetValue() };
	if (add) {
		items->push_back(entry);
		currentItem_ = static_cast<int>(items->size()) - 1;
	} else if (currentItem_ >= 0 && currentItem_ < static_cast<int>(items->size())) {
		(*items)[currentItem_] = entry;
	} else {
		return;
	}
	MarkDirty();
	int selection = currentItem_;
	PopulateItems();
	if (selection >= 0 && selection < static_cast<int>(itemList_->GetCount())) {
		itemList_->SetSelection(selection);
		SelectItem(selection);
	}
}

void MaterialsWorkbenchWindow::RemoveItem() {
	std::vector<ItemEntry>* items = CurrentItems();
	if (!items || currentItem_ < 0 || currentItem_ >= static_cast<int>(items->size())) {
		return;
	}
	items->erase(items->begin() + currentItem_);
	MarkDirty();
	PopulateItems();
	RefreshPreview();
}

void MaterialsWorkbenchWindow::AddContext() {
	BrushRecord& record = records_[currentBrush_];
	if (record.type == "doodad") {
		record.alternatives.emplace_back();
		currentContext_ = static_cast<int>(record.alternatives.size()) - 1;
	} else {
		wxTextEntryDialog dialog(this, record.type == "wall" ? "Wall part type" : "Alignment key", "Add context");
		if (dialog.ShowModal() != wxID_OK || dialog.GetValue().IsEmpty()) {
			return;
		}
		record.nodes.push_back({ dialog.GetValue() });
		currentContext_ = static_cast<int>(record.nodes.size()) - 1;
	}
	MarkDirty();
	PopulateEditor();
}

void MaterialsWorkbenchWindow::RemoveContext() {
	BrushRecord& record = records_[currentBrush_];
	if (record.type == "doodad" && record.alternatives.size() > 1) {
		record.alternatives.erase(record.alternatives.begin() + currentContext_);
	} else if (record.type != "ground" && record.nodes.size() > 1) {
		record.nodes.erase(record.nodes.begin() + currentContext_);
	} else {
		return;
	}
	currentContext_ = std::max(0, currentContext_ - 1);
	MarkDirty();
	PopulateEditor();
}

void MaterialsWorkbenchWindow::PopulateDoors() {
	const bool isWall = currentBrush_ >= 0 && records_[currentBrush_].type == "wall";
	doorBox_->GetStaticBox()->GetParent()->Layout();
	doorBox_->Show(isWall);
	doorList_->Clear();
	std::vector<DoorEntry>* doors = CurrentDoors();
	if (doors) {
		for (const DoorEntry& door : *doors) {
			doorList_->Append(wxString::Format("item %d | %s | %s%s", door.id, door.type, door.open ? "open" : "closed", door.locked ? " | locked" : ""));
		}
	}
	currentDoor_ = -1;
	RefreshActionState();
	Layout();
}

void MaterialsWorkbenchWindow::SelectDoor(int index) {
	std::vector<DoorEntry>* doors = CurrentDoors();
	if (!doors || index < 0 || index >= static_cast<int>(doors->size())) {
		return;
	}
	currentDoor_ = index;
	const DoorEntry& door = (*doors)[index];
	doorIdCtrl_->SetValue(door.id);
	if (!doorTypeCtrl_->SetStringSelection(door.type)) {
		doorTypeCtrl_->SetSelection(wxNOT_FOUND);
	}
	doorOpenCtrl_->SetValue(door.open);
	doorLockedCtrl_->SetValue(door.locked);
	RefreshPreview();
}

void MaterialsWorkbenchWindow::ApplyDoor(bool add) {
	std::vector<DoorEntry>* doors = CurrentDoors();
	if (!doors) {
		return;
	}
	const DoorEntry* original = !add && currentDoor_ >= 0 && currentDoor_ < static_cast<int>(doors->size()) ? &(*doors)[currentDoor_] : nullptr;
	wxString type = doorTypeCtrl_->GetStringSelection();
	if (original && doorTypeCtrl_->GetSelection() == wxNOT_FOUND) {
		type = original->type;
	} else if (type.IsEmpty()) {
		type = "normal";
	}
	DoorEntry door { doorIdCtrl_->GetValue(), type, doorOpenCtrl_->GetValue(), doorLockedCtrl_->GetValue(), original ? original->hate : false };
	if (door.id <= 0 || !g_items.typeExists(door.id)) {
		ShowInvalidItemId(this);
		return;
	}
	if (add) {
		doors->push_back(door);
	} else if (currentDoor_ >= 0 && currentDoor_ < static_cast<int>(doors->size())) {
		(*doors)[currentDoor_] = door;
	} else {
		return;
	}
	MarkDirty();
	PopulateDoors();
	RefreshPreview();
}

void MaterialsWorkbenchWindow::RemoveDoor() {
	std::vector<DoorEntry>* doors = CurrentDoors();
	if (!doors || currentDoor_ < 0 || currentDoor_ >= static_cast<int>(doors->size())) {
		return;
	}
	doors->erase(doors->begin() + currentDoor_);
	MarkDirty();
	PopulateDoors();
	RefreshPreview();
}

void MaterialsWorkbenchWindow::PopulateComposites() {
	const bool isDoodad = currentBrush_ >= 0 && records_[currentBrush_].type == "doodad";
	compositeBox_->Show(isDoodad);
	compositeList_->Clear();
	AlternativeEntry* alternative = CurrentAlternative();
	if (alternative) {
		for (int i = 0; i < static_cast<int>(alternative->composites.size()); ++i) {
			const CompositeEntry& composite = alternative->composites[i];
			compositeList_->Append(wxString::Format("%d. chance %d | %zu tiles", i + 1, composite.chance, composite.tiles.size()));
		}
	}
	if (!alternative || currentComposite_ >= static_cast<int>(alternative->composites.size())) {
		currentComposite_ = -1;
	}
	if (currentComposite_ >= 0) {
		compositeList_->SetSelection(currentComposite_);
	}
	PopulateTiles();
	Layout();
}

void MaterialsWorkbenchWindow::AddComposite() {
	AlternativeEntry* alternative = CurrentAlternative();
	if (!alternative) {
		return;
	}
	alternative->composites.push_back({ compositeChanceCtrl_->GetValue() });
	currentComposite_ = static_cast<int>(alternative->composites.size()) - 1;
	MarkDirty();
	PopulateComposites();
	compositeList_->SetSelection(currentComposite_);
	PopulateTiles();
	RefreshPreview();
}

void MaterialsWorkbenchWindow::RemoveComposite() {
	AlternativeEntry* alternative = CurrentAlternative();
	if (!alternative || currentComposite_ < 0 || currentComposite_ >= static_cast<int>(alternative->composites.size())) {
		return;
	}
	alternative->composites.erase(alternative->composites.begin() + currentComposite_);
	MarkDirty();
	PopulateComposites();
	RefreshPreview();
}

void MaterialsWorkbenchWindow::PopulateTiles() {
	tileList_->Clear();
	CompositeEntry* composite = CurrentComposite();
	if (composite) {
		compositeChanceCtrl_->SetValue(composite->chance);
		for (const TileEntry& tile : composite->tiles) {
			tileList_->Append(wxString::Format("item %d @ (%d, %d, %d)", tile.id, tile.x, tile.y, tile.z));
		}
	}
	if (!composite || currentTile_ >= static_cast<int>(composite->tiles.size())) {
		currentTile_ = -1;
	}
	if (currentTile_ >= 0) {
		tileList_->SetSelection(currentTile_);
	}
}

void MaterialsWorkbenchWindow::SelectTile(int index) {
	CompositeEntry* composite = CurrentComposite();
	if (!composite || index < 0 || index >= static_cast<int>(composite->tiles.size())) {
		return;
	}
	currentTile_ = index;
	const TileEntry& tile = composite->tiles[index];
	tileIdCtrl_->SetValue(tile.id);
	tileXCtrl_->SetValue(tile.x);
	tileYCtrl_->SetValue(tile.y);
	tileZCtrl_->SetValue(tile.z);
	RefreshPreview();
}

void MaterialsWorkbenchWindow::ApplyTile(bool add) {
	CompositeEntry* composite = CurrentComposite();
	if (!composite) {
		return;
	}
	TileEntry tile { tileXCtrl_->GetValue(), tileYCtrl_->GetValue(), tileZCtrl_->GetValue(), tileIdCtrl_->GetValue() };
	if (tile.id <= 0 || !g_items.typeExists(tile.id)) {
		ShowInvalidItemId(this);
		return;
	}
	composite->chance = compositeChanceCtrl_->GetValue();
	if (add) {
		composite->tiles.push_back(tile);
	} else if (currentTile_ >= 0 && currentTile_ < static_cast<int>(composite->tiles.size())) {
		composite->tiles[currentTile_] = tile;
	} else {
		return;
	}
	MarkDirty();
	PopulateComposites();
	RefreshPreview();
}

void MaterialsWorkbenchWindow::RemoveTile() {
	CompositeEntry* composite = CurrentComposite();
	if (!composite || currentTile_ < 0 || currentTile_ >= static_cast<int>(composite->tiles.size())) {
		return;
	}
	composite->tiles.erase(composite->tiles.begin() + currentTile_);
	MarkDirty();
	PopulateComposites();
	RefreshPreview();
}

void MaterialsWorkbenchWindow::PopulateLinks() {
	linksList_->Clear();
	if (currentBrush_ < 0) {
		return;
	}
	for (const LinkEntry& link : records_[currentBrush_].links) {
		linksList_->Append(link.relation.Upper() + " → " + link.target);
	}
}

void MaterialsWorkbenchWindow::RefreshPreview() {
	if (currentBrush_ < 0) {
		return;
	}
	const BrushRecord& record = records_[currentBrush_];
	std::vector<MaterialsBrushPreviewPanel::Entry> entries;
	if (record.type == "ground") {
		for (int i = 0; i < std::min(20, static_cast<int>(record.groundItems.size())); ++i) {
			entries.push_back({ i, 0, record.groundItems[i].id, i == currentItem_ });
		}
	} else if (record.type == "doodad") {
		CompositeEntry* composite = CurrentComposite();
		if (composite && !composite->tiles.empty()) {
			for (int i = 0; i < static_cast<int>(composite->tiles.size()); ++i) {
				entries.push_back({ composite->tiles[i].x, composite->tiles[i].y, composite->tiles[i].id, i == currentTile_ });
			}
		} else if (AlternativeEntry* alternative = CurrentAlternative()) {
			for (int i = 0; i < std::min(12, static_cast<int>(alternative->singles.size())); ++i) {
				entries.push_back({ i % 6, i / 6, alternative->singles[i].id, i == currentItem_ });
			}
		}
	} else if (!record.nodes.empty()) {
		static const std::map<wxString, std::pair<int, int>> positions = {
			{ "n", { 2, 0 } }, { "north", { 2, 0 } }, { "s", { 2, 4 } }, { "south", { 2, 4 } }, { "e", { 4, 2 } }, { "east", { 4, 2 } }, { "w", { 0, 2 } }, { "west", { 0, 2 } }, { "cnw", { 0, 0 } }, { "cne", { 4, 0 } }, { "csw", { 0, 4 } }, { "cse", { 4, 4 } }, { "center", { 2, 2 } }, { "alone", { 2, 2 } }, { "horizontal", { 2, 2 } }, { "vertical", { 2, 2 } }, { "corner", { 3, 3 } }, { "pole", { 2, 2 } }
		};
		for (int nodeIndex = 0; nodeIndex < static_cast<int>(record.nodes.size()); ++nodeIndex) {
			const NodeEntry& node = record.nodes[nodeIndex];
			auto position = positions.find(node.key.Lower());
			int baseX = position == positions.end() ? nodeIndex % 5 : position->second.first;
			int baseY = position == positions.end() ? nodeIndex / 5 : position->second.second;
			if (!node.items.empty()) {
				entries.push_back({ baseX, baseY, node.items[0].id, nodeIndex == currentContext_ && currentItem_ <= 0 });
			}
			if (nodeIndex == currentContext_) {
				for (int i = 1; i < static_cast<int>(node.items.size()); ++i) {
					entries.push_back({ baseX + i, baseY, node.items[i].id, i == currentItem_ });
				}
				for (int i = 0; i < static_cast<int>(node.doors.size()); ++i) {
					entries.push_back({ baseX + i, baseY + 1, node.doors[i].id, i == currentDoor_ });
				}
			}
		}
	}
	previewPanel_->SetEntries(entries, TypeLabel(record.type) + " preview — " + record.name);
}

void MaterialsWorkbenchWindow::RefreshHeader() {
	if (currentBrush_ < 0) {
		return;
	}
	const BrushRecord& record = records_[currentBrush_];
	titleLabel_->SetLabel("Brush Workspace" + wxString(dirty_ ? " *" : ""));
	subtitleLabel_->SetLabel(wxString::Format("Editing %s brush: %s | XML: %s", TypeLabel(record.type), record.name, record.sourceName));
}

void MaterialsWorkbenchWindow::RefreshActionState() {
	const bool enabled = currentBrush_ >= 0;
	deleteBrushButton_->Enable(enabled);
	usedByButton_->Enable(enabled);
	saveButton_->Enable(enabled && dirty_);
	revertButton_->Enable(enabled && dirty_);
	updateItemButton_->Enable(currentItem_ >= 0);
	removeItemButton_->Enable(currentItem_ >= 0);
}

void MaterialsWorkbenchWindow::MarkDirty() {
	if (loading_ || currentBrush_ < 0) {
		return;
	}
	dirty_ = true;
	RefreshHeader();
	RefreshActionState();
}

void MaterialsWorkbenchWindow::CreateBrush() {
	if (rootMaterialsPath_.IsEmpty() || !ResolvePendingChanges("create a brush")) {
		return;
	}
	wxTextEntryDialog nameDialog(this, "Brush name", "New Brush");
	if (nameDialog.ShowModal() != wxID_OK || nameDialog.GetValue().IsEmpty()) {
		return;
	}
	wxArrayString choices;
	for (const wxString& type : kTypes) {
		choices.Add(TypeLabel(type));
	}
	wxSingleChoiceDialog typeDialog(this, "Brush type", "New Brush", choices);
	if (typeDialog.ShowModal() != wxID_OK) {
		return;
	}
	pugi::xml_document document;
	if (!document.load_file(rootMaterialsPath_.mb_str())) {
		return;
	}
	pugi::xml_node root = MaterialsRoot(document);
	if (!root) {
		wxMessageBox("The XML file does not contain <materials> or <materialsextension>. The brush was not created.", "Materials Workbench", wxOK | wxICON_ERROR, this);
		return;
	}
	pugi::xml_node brush = root.append_child("brush");
	SetAttribute(brush, "name", nameDialog.GetValue());
	SetAttribute(brush, "type", kTypes[typeDialog.GetSelection()]);
	if (!wxCopyFile(rootMaterialsPath_, rootMaterialsPath_ + ".bak", true)) {
		wxMessageBox("Could not create the XML backup. The brush was not created.", "Materials Workbench", wxOK | wxICON_ERROR, this);
		return;
	}
	if (document.save_file(rootMaterialsPath_.mb_str(), "\t", pugi::format_default, pugi::encoding_utf8)) {
		LoadCatalog(rootMaterialsPath_);
	}
}

void MaterialsWorkbenchWindow::DeleteBrush() {
	if (currentBrush_ < 0 || !ResolvePendingChanges("delete this brush")) {
		return;
	}
	const BrushRecord record = records_[currentBrush_];
	if (wxMessageBox(wxString::Format("Delete brush '%s' from %s?", record.name, record.sourceName), "Delete Brush", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
		return;
	}
	pugi::xml_document document;
	if (!document.load_file(record.sourcePath.mb_str())) {
		return;
	}
	pugi::xml_node root = MaterialsRoot(document);
	pugi::xml_node brush = BrushAt(root, record.sourceIndex);
	if (!brush) {
		return;
	}
	if (!wxCopyFile(record.sourcePath, record.sourcePath + ".bak", true)) {
		wxMessageBox("Could not create the XML backup. The brush was not deleted.", "Materials Workbench", wxOK | wxICON_ERROR, this);
		return;
	}
	root.remove_child(brush);
	if (document.save_file(record.sourcePath.mb_str(), "\t", pugi::format_default, pugi::encoding_utf8)) {
		LoadCatalog(rootMaterialsPath_);
	}
}

void MaterialsWorkbenchWindow::ShowUsedBy() {
	if (currentBrush_ < 0) {
		return;
	}
	const wxString name = records_[currentBrush_].name;
	wxArrayString usages;
	for (const BrushRecord& record : records_) {
		for (const LinkEntry& link : record.links) {
			if (link.target == name) {
				usages.Add(record.name + " — " + link.relation);
			}
		}
	}
	if (usages.empty()) {
		usages.Add("No friend/enemy references found in this catalog.");
	}
	wxSingleChoiceDialog dialog(this, wxString::Format("References to '%s'", name), "Used By", usages);
	dialog.ShowModal();
}

bool MaterialsWorkbenchWindow::SaveCurrentBrush() {
	if (currentBrush_ < 0) {
		return false;
	}
	BrushRecord& record = records_[currentBrush_];
	const wxString newName = nameCtrl_->GetValue();
	if (newName.IsEmpty()) {
		return false;
	}
	for (int i = 0; i < static_cast<int>(records_.size()); ++i) {
		if (i != currentBrush_ && records_[i].name == newName) {
			wxMessageBox("Another brush already uses this name.", "Materials Workbench", wxOK | wxICON_ERROR, this);
			return false;
		}
	}
	pugi::xml_document document;
	if (!document.load_file(record.sourcePath.mb_str())) {
		return false;
	}
	pugi::xml_node brush = BrushAt(MaterialsRoot(document), record.sourceIndex);
	if (!brush) {
		return false;
	}
	SetAttribute(brush, "name", newName);
	SetIntAttribute(brush, "server_lookid", serverLookCtrl_->GetValue(), true);
	SetIntAttribute(brush, "lookid", clientLookCtrl_->GetValue(), true);
	SetIntAttribute(brush, "z-order", zOrderCtrl_->GetValue(), true);
	if (thicknessCtrl_->GetValue().IsEmpty()) {
		brush.remove_attribute("thickness");
	} else {
		SetAttribute(brush, "thickness", thicknessCtrl_->GetValue());
	}
	SetBoolAttribute(brush, "draggable", draggableCtrl_->GetValue(), true);
	SetBoolAttribute(brush, "on_blocking", onBlockingCtrl_->GetValue(), true);
	SetBoolAttribute(brush, "on_duplicate", onDuplicateCtrl_->GetValue(), true);
	SetBoolAttribute(brush, "redo_borders", redoBordersCtrl_->GetValue(), true);
	SetBoolAttribute(brush, "one_size", oneSizeCtrl_->GetValue(), true);

	std::set<std::string> structural;
	if (record.type == "ground") {
		structural = { "item" };
	} else if (record.type == "wall") {
		structural = { "wall" };
	} else if (record.type == "carpet") {
		structural = { "carpet" };
	} else if (record.type == "table") {
		structural = { "table" };
	} else {
		structural = { "item", "composite", "alternate" };
	}
	for (pugi::xml_node child = brush.first_child(); child;) {
		pugi::xml_node next = child.next_sibling();
		if (structural.count(as_lower_str(child.name()))) {
			brush.remove_child(child);
		}
		child = next;
	}
	auto appendItem = [](pugi::xml_node parent, const ItemEntry& item) {
		pugi::xml_node node = parent.append_child("item");
		node.append_attribute("id").set_value(item.id);
		node.append_attribute("chance").set_value(item.chance);
	};
	if (record.type == "ground") {
		for (const ItemEntry& item : record.groundItems) {
			appendItem(brush, item);
		}
	} else if (record.type == "wall") {
		for (const NodeEntry& part : record.nodes) {
			pugi::xml_node node = brush.append_child("wall");
			node.append_attribute("type").set_value(part.key.utf8_str());
			for (const ItemEntry& item : part.items) {
				appendItem(node, item);
			}
			for (const DoorEntry& door : part.doors) {
				pugi::xml_node doorNode = node.append_child("door");
				doorNode.append_attribute("id").set_value(door.id);
				doorNode.append_attribute("type").set_value(door.type.utf8_str());
				doorNode.append_attribute("open").set_value(door.open ? "true" : "false");
				if (door.locked) {
					doorNode.append_attribute("locked").set_value("true");
				}
				if (door.hate) {
					doorNode.append_attribute("hate").set_value("true");
				}
			}
		}
	} else if (record.type == "carpet" || record.type == "table") {
		const char* tag = record.type == "carpet" ? "carpet" : "table";
		for (const NodeEntry& context : record.nodes) {
			pugi::xml_node node = brush.append_child(tag);
			node.append_attribute("align").set_value(context.key.utf8_str());
			if (record.type == "carpet" && context.items.size() == 1 && context.items[0].chance <= 1) {
				node.append_attribute("id").set_value(context.items[0].id);
			} else {
				for (const ItemEntry& item : context.items) {
					appendItem(node, item);
				}
			}
		}
	} else {
		auto appendAlternative = [&](pugi::xml_node parent, const AlternativeEntry& alternative) {
			for (const ItemEntry& item : alternative.singles) {
				appendItem(parent, item);
			}
			for (const CompositeEntry& composite : alternative.composites) {
				pugi::xml_node compositeNode = parent.append_child("composite");
				compositeNode.append_attribute("chance").set_value(composite.chance);
				for (const TileEntry& tile : composite.tiles) {
					pugi::xml_node tileNode = compositeNode.append_child("tile");
					tileNode.append_attribute("x").set_value(tile.x);
					tileNode.append_attribute("y").set_value(tile.y);
					if (tile.z != 0) {
						tileNode.append_attribute("z").set_value(tile.z);
					}
					pugi::xml_node itemNode = tileNode.append_child("item");
					itemNode.append_attribute("id").set_value(tile.id);
				}
			}
		};
		if (record.alternatives.size() == 1 && !record.wrappedAlternatives) {
			appendAlternative(brush, record.alternatives[0]);
		} else {
			for (const AlternativeEntry& alternative : record.alternatives) {
				appendAlternative(brush.append_child("alternate"), alternative);
			}
		}
	}
	if (!wxCopyFile(record.sourcePath, record.sourcePath + ".bak", true)) {
		wxMessageBox("Could not create the XML backup. The brush was not saved.", "Materials Workbench", wxOK | wxICON_ERROR, this);
		return false;
	}
	if (!document.save_file(record.sourcePath.mb_str(), "\t", pugi::format_default, pugi::encoding_utf8)) {
		wxMessageBox("Could not save the XML file. The brush was not saved.", "Materials Workbench", wxOK | wxICON_ERROR, this);
		return false;
	}
	record.name = newName;
	record.serverLookId = serverLookCtrl_->GetValue();
	record.lookId = clientLookCtrl_->GetValue();
	record.zOrder = zOrderCtrl_->GetValue();
	record.thickness = thicknessCtrl_->GetValue();
	record.draggable = draggableCtrl_->GetValue();
	record.onBlocking = onBlockingCtrl_->GetValue();
	record.onDuplicate = onDuplicateCtrl_->GetValue();
	record.redoBorders = redoBordersCtrl_->GetValue();
	record.oneSize = oneSizeCtrl_->GetValue();
	dirty_ = false;
	RefreshHeader();
	RefreshActionState();
	PopulateCatalog();
	SetStatusText("Brush saved to XML. Use File > Reload Data Files to apply it to the current runtime.");
	return true;
}

void MaterialsWorkbenchWindow::RevertCurrentBrush() {
	if (currentBrush_ < 0) {
		return;
	}
	const wxString name = records_[currentBrush_].name;
	if (dirty_ && wxMessageBox("Discard unsaved brush changes?", "Materials Workbench", wxYES_NO | wxICON_QUESTION, this) != wxYES) {
		return;
	}
	LoadCatalog(rootMaterialsPath_);
	for (int i = 0; i < static_cast<int>(records_.size()); ++i) {
		if (records_[i].name == name) {
			LoadBrush(i);
			break;
		}
	}
}

void MaterialsWorkbenchWindow::OnClose(wxCloseEvent& event) {
	if (event.CanVeto() && !ResolvePendingChanges("close the workbench")) {
		event.Veto();
		return;
	}
	WindowInstance() = nullptr;
	Destroy();
}

wxString MaterialsWorkbenchWindow::TypeLabel(const wxString& type) {
	if (type == "ground") {
		return "Ground";
	}
	if (type == "wall") {
		return "Wall";
	}
	if (type == "doodad") {
		return "Doodad";
	}
	if (type == "carpet") {
		return "Carpet";
	}
	if (type == "table") {
		return "Table";
	}
	return type;
}
