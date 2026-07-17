//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "border_workspace_window.h"

#include <algorithm>
#include <limits>
#include <utility>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/choicdlg.h>
#include <wx/filedlg.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/scrolwin.h>
#include <wx/srchctrl.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/splitter.h>
#include <wx/statbox.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/tglbtn.h>

#include "client_version.h"
#include "dcbutton.h"
#include "find_item_window.h"
#include "gui.h"
#include "items.h"

namespace {
	BorderWorkspaceWindow*& WindowInstance() {
		static BorderWorkspaceWindow* instance = nullptr;
		return instance;
	}

	wxString CurrentMaterialsPath() {
		if (!g_gui.IsVersionLoaded()) {
			return wxString();
		}
		FileName path = g_gui.GetCurrentVersion().getDataPath();
		path.SetFullName("materials.xml");
		path.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
		return path.GetFullPath();
	}

	wxString NormalizePath(const wxString& path) {
		wxFileName filename(path);
		filename.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
		return filename.GetFullPath();
	}

	constexpr std::array<const char*, 12> kEdges = {
		"cnw", "n", "cne", "dnw", "dne", "w", "e", "dsw", "dse", "csw", "s", "cse"
	};
	constexpr std::array<const char*, 12> kEdgeLabels = {
		"Corner SE", "South", "Corner SW", "Diag SW", "Diag SE", "East",
		"West", "Diag NW", "Diag NE", "Corner NE", "North", "Corner NW"
	};
	constexpr std::array<std::pair<int, int>, 12> kGridPositions = { { { 4, 4 }, { 4, 2 }, { 4, 0 }, { 3, 3 }, { 3, 1 }, { 2, 4 }, { 2, 0 }, { 1, 3 }, { 1, 1 }, { 0, 4 }, { 0, 2 }, { 0, 0 } } };

	wxString CleanDescription(pugi::xml_node border) {
		for (pugi::xml_node child = border.first_child(); child; child = child.next_sibling()) {
			if (child.type() != pugi::node_pcdata) {
				continue;
			}
			wxString text = wxString::FromUTF8(child.value());
			text.Trim(true).Trim(false);
			while (text.StartsWith("-")) {
				text.Remove(0, 1);
			}
			while (text.EndsWith("-")) {
				text.RemoveLast();
			}
			text.Trim(true).Trim(false);
			if (!text.IsEmpty()) {
				return text;
			}
		}
		return wxString();
	}

	pugi::xml_node MaterialsRoot(pugi::xml_document& document) {
		pugi::xml_node root = document.child("materials");
		if (!root) {
			root = document.child("materialsextension");
		}
		return root;
	}

	pugi::xml_node BorderAt(pugi::xml_node root, int index) {
		int current = 0;
		for (pugi::xml_node child = root.first_child(); child; child = child.next_sibling()) {
			if (as_lower_str(child.name()) != "border") {
				continue;
			}
			if (current++ == index) {
				return child;
			}
		}
		return pugi::xml_node();
	}
}

void BorderWorkspaceWindow::Open(wxWindow* parent) {
	if (WindowInstance()) {
		WindowInstance()->Raise();
		WindowInstance()->SetFocus();
		return;
	}
	WindowInstance() = newd BorderWorkspaceWindow(parent);
	WindowInstance()->Show();
}

void BorderWorkspaceWindow::OpenForItems(wxWindow* parent, const std::vector<ItemCount>& items) {
	if (!g_gui.IsVersionLoaded()) {
		wxMessageBox("Load a client version before opening Border Workspace.", "Border Workspace", wxOK | wxICON_INFORMATION, parent);
		return;
	}
	if (!IsAvailableForCurrentClient()) {
		wxMessageBox(
			wxString::Format("The current client's materials.xml was not found:\n%s", CurrentMaterialsPath()),
			"Border Workspace", wxOK | wxICON_ERROR, parent
		);
		return;
	}

	bool created = false;
	if (!WindowInstance()) {
		WindowInstance() = newd BorderWorkspaceWindow(parent);
		created = true;
	}
	BorderWorkspaceWindow* window = WindowInstance();
	if (!window->EnsureCurrentClientCatalog()) {
		if (created) {
			WindowInstance() = nullptr;
			window->Destroy();
		} else {
			window->Raise();
			window->SetFocus();
		}
		return;
	}

	window->Show();
	if (!window->OpenItems(items) && created) {
		WindowInstance() = nullptr;
		window->Destroy();
		return;
	}
	window->Show();
	window->Raise();
	window->SetFocus();
}

bool BorderWorkspaceWindow::IsAvailableForCurrentClient() {
	const wxString path = CurrentMaterialsPath();
	return !path.IsEmpty() && wxFileExists(path);
}

BorderWorkspaceWindow::BorderWorkspaceWindow(wxWindow* parent) :
	wxFrame(parent, wxID_ANY, "Border Workspace", wxDefaultPosition, wxSize(1180, 760), wxDEFAULT_FRAME_STYLE | wxCLIP_CHILDREN) {
	BuildLayout();
	CreateStatusBar();
	BindEvents();
	LoadDefaultMaterialsFile();
	CentreOnParent();
}

BorderWorkspaceWindow::~BorderWorkspaceWindow() {
	if (WindowInstance() == this) {
		WindowInstance() = nullptr;
	}
}

void BorderWorkspaceWindow::BuildLayout() {
	auto* rootSizer = newd wxBoxSizer(wxVERTICAL);
	auto* toolbar = newd wxBoxSizer(wxHORIZONTAL);
	auto* openButton = newd wxButton(this, wxID_OPEN, "Open materials.xml...");
	auto* reloadButton = newd wxButton(this, wxID_REFRESH, "Reload");
	fileLabel_ = newd wxStaticText(this, wxID_ANY, "No materials file loaded");
	toolbar->Add(openButton, 0, wxALL, FromDIP(5));
	toolbar->Add(reloadButton, 0, wxTOP | wxBOTTOM | wxRIGHT, FromDIP(5));
	toolbar->Add(fileLabel_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	rootSizer->Add(toolbar, 0, wxEXPAND);
	rootSizer->Add(newd wxStaticLine(this), 0, wxEXPAND);

	auto* splitter = newd wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
	auto* catalogPanel = newd wxPanel(splitter);
	auto* catalogSizer = newd wxBoxSizer(wxVERTICAL);
	auto* catalogTitle = newd wxStaticText(catalogPanel, wxID_ANY, "Border Catalog");
	wxFont titleFont = catalogTitle->GetFont();
	titleFont.MakeBold();
	catalogTitle->SetFont(titleFont);
	filterCtrl_ = newd wxSearchCtrl(catalogPanel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	filterCtrl_->SetDescriptiveText("Filter by ID, name or file");
	filterCtrl_->ShowCancelButton(true);
	borderList_ = newd wxListBox(catalogPanel, wxID_ANY);
	catalogSizer->Add(catalogTitle, 0, wxALL, FromDIP(8));
	catalogSizer->Add(filterCtrl_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	catalogSizer->Add(borderList_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	catalogPanel->SetSizer(catalogSizer);

	auto* workspace = newd wxScrolledWindow(splitter, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	workspace->SetScrollRate(0, FromDIP(12));
	auto* workspaceSizer = newd wxBoxSizer(wxVERTICAL);
	headingLabel_ = newd wxStaticText(workspace, wxID_ANY, "Border Workspace");
	wxFont headingFont = headingLabel_->GetFont();
	headingFont.SetPointSize(headingFont.GetPointSize() + 3);
	headingFont.MakeBold();
	headingLabel_->SetFont(headingFont);
	sourceLabel_ = newd wxStaticText(workspace, wxID_ANY, "Select a border to begin.");
	workspaceSizer->Add(headingLabel_, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(10));
	workspaceSizer->Add(sourceLabel_, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));

	auto* contentSizer = newd wxBoxSizer(wxHORIZONTAL);
	auto* leftColumn = newd wxBoxSizer(wxVERTICAL);
	auto* metaBox = newd wxStaticBoxSizer(wxVERTICAL, workspace, "Border Set");
	auto* metaGrid = newd wxFlexGridSizer(2, FromDIP(5), FromDIP(8));
	metaGrid->AddGrowableCol(1, 1);
	metaGrid->Add(newd wxStaticText(metaBox->GetStaticBox(), wxID_ANY, "Border ID"), 0, wxALIGN_CENTER_VERTICAL);
	idCtrl_ = newd wxSpinCtrl(metaBox->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 100000, 1);
	metaGrid->Add(idCtrl_, 1, wxEXPAND);
	metaGrid->Add(newd wxStaticText(metaBox->GetStaticBox(), wxID_ANY, "Autoborder Group"), 0, wxALIGN_CENTER_VERTICAL);
	groupCtrl_ = newd wxSpinCtrl(metaBox->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 65535, 0);
	metaGrid->Add(groupCtrl_, 1, wxEXPAND);
	metaGrid->Add(newd wxStaticText(metaBox->GetStaticBox(), wxID_ANY, "Border Style"), 0, wxALIGN_CENTER_VERTICAL);
	typeChoice_ = newd wxChoice(metaBox->GetStaticBox(), wxID_ANY);
	typeChoice_->Append("normal");
	typeChoice_->Append("optional");
	metaGrid->Add(typeChoice_, 1, wxEXPAND);
	metaBox->Add(metaGrid, 0, wxEXPAND | wxALL, FromDIP(7));
	auto* metaActions = newd wxBoxSizer(wxHORIZONTAL);
	auto* newButton = newd wxButton(metaBox->GetStaticBox(), wxID_NEW, "New Border Set");
	deleteButton_ = newd wxButton(metaBox->GetStaticBox(), wxID_DELETE, "Delete Border Set");
	metaActions->Add(newButton, 1, wxRIGHT, FromDIP(4));
	metaActions->Add(deleteButton_, 1);
	metaBox->Add(metaActions, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(7));
	leftColumn->Add(metaBox, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

	auto* selectedBox = newd wxStaticBoxSizer(wxVERTICAL, workspace, "Selected Slot");
	selectedEdgeLabel_ = newd wxStaticText(selectedBox->GetStaticBox(), wxID_ANY, "Edge: -");
	selectedBox->Add(selectedEdgeLabel_, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(7));
	auto* selectedRow = newd wxBoxSizer(wxHORIZONTAL);
	selectedItemPreview_ = newd DCButton(selectedBox->GetStaticBox(), wxID_ANY, wxDefaultPosition, DC_BTN_NORMAL, RENDER_SIZE_32x32, 0);
	selectedItemCtrl_ = newd wxSpinCtrl(selectedBox->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 65535, 0);
	selectedRow->Add(selectedItemPreview_, 0, wxRIGHT, FromDIP(6));
	selectedRow->Add(selectedItemCtrl_, 1, wxALIGN_CENTER_VERTICAL);
	selectedBox->Add(selectedRow, 0, wxEXPAND | wxALL, FromDIP(7));
	auto* selectedActions = newd wxBoxSizer(wxHORIZONTAL);
	auto* pickButton = newd wxButton(selectedBox->GetStaticBox(), wxID_FIND, "Pick Item...");
	auto* applyButton = newd wxButton(selectedBox->GetStaticBox(), wxID_APPLY, "Apply to Slot");
	auto* clearButton = newd wxButton(selectedBox->GetStaticBox(), wxID_CLEAR, "Clear Slot");
	selectedActions->Add(pickButton, 1, wxRIGHT, FromDIP(4));
	selectedActions->Add(applyButton, 1, wxRIGHT, FromDIP(4));
	selectedActions->Add(clearButton, 1);
	selectedBox->Add(selectedActions, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(7));
	leftColumn->Add(selectedBox, 0, wxEXPAND);
	contentSizer->Add(leftColumn, 0, wxEXPAND | wxRIGHT, FromDIP(8));

	auto* slotBox = newd wxStaticBoxSizer(wxVERTICAL, workspace, "Slot Grid");
	auto* slotGrid = newd wxGridSizer(5, 5, FromDIP(3), FromDIP(3));
	for (int row = 0; row < 5; ++row) {
		for (int col = 0; col < 5; ++col) {
			int slot = -1;
			for (int i = 0; i < 12; ++i) {
				if (kGridPositions[i] == std::make_pair(row, col)) {
					slot = i;
				}
			}
			auto* cell = newd wxPanel(slotBox->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxSize(FromDIP(66), FromDIP(58)));
			auto* cellSizer = newd wxBoxSizer(wxVERTICAL);
			if (slot >= 0) {
				slotButtons_[slot] = newd DCButton(cell, wxID_ANY, wxDefaultPosition, DC_BTN_TOGGLE, RENDER_SIZE_32x32, 0);
				slotLabels_[slot] = newd wxStaticText(cell, wxID_ANY, "empty", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
				cellSizer->Add(slotButtons_[slot], 0, wxALIGN_CENTER);
				cellSizer->Add(slotLabels_[slot], 0, wxALIGN_CENTER | wxTOP, FromDIP(1));
			} else if (row == 2 && col == 2) {
				auto* center = newd wxStaticText(cell, wxID_ANY, "GROUND", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL | wxST_NO_AUTORESIZE);
				cellSizer->AddStretchSpacer();
				cellSizer->Add(center, 0, wxALIGN_CENTER);
				cellSizer->AddStretchSpacer();
			}
			cell->SetSizer(cellSizer);
			slotGrid->Add(cell, 1, wxEXPAND);
		}
	}
	slotBox->Add(slotGrid, 1, wxEXPAND | wxALL, FromDIP(7));
	contentSizer->Add(slotBox, 1, wxEXPAND | wxRIGHT, FromDIP(8));

	auto* previewBox = newd wxStaticBoxSizer(wxVERTICAL, workspace, "Preview Matrix");
	auto* previewGrid = newd wxGridSizer(5, 5, FromDIP(1), FromDIP(1));
	for (int row = 0; row < 5; ++row) {
		for (int col = 0; col < 5; ++col) {
			int slot = -1;
			for (int i = 0; i < 12; ++i) {
				if (kGridPositions[i] == std::make_pair(row, col)) {
					slot = i;
				}
			}
			if (slot >= 0) {
				previewButtons_[slot] = newd DCButton(previewBox->GetStaticBox(), wxID_ANY, wxDefaultPosition, DC_BTN_TOGGLE, RENDER_SIZE_32x32, 0);
				previewGrid->Add(previewButtons_[slot], 0, wxALIGN_CENTER);
			} else {
				previewGrid->Add(newd wxPanel(previewBox->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxSize(FromDIP(36), FromDIP(36))), 0, wxEXPAND);
			}
		}
	}
	previewBox->Add(previewGrid, 0, wxALL, FromDIP(7));
	contentSizer->Add(previewBox, 0, wxEXPAND);
	workspaceSizer->Add(contentSizer, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));

	auto* bottom = newd wxBoxSizer(wxHORIZONTAL);
	bottom->AddStretchSpacer();
	revertButton_ = newd wxButton(workspace, wxID_REVERT, "Revert");
	saveButton_ = newd wxButton(workspace, wxID_SAVE, "Save Border Set");
	bottom->Add(revertButton_, 0, wxRIGHT, FromDIP(5));
	bottom->Add(saveButton_, 0);
	workspaceSizer->Add(newd wxStaticLine(workspace), 0, wxEXPAND | wxALL, FromDIP(10));
	workspaceSizer->Add(bottom, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
	workspace->SetSizer(workspaceSizer);

	splitter->SplitVertically(catalogPanel, workspace, FromDIP(285));
	splitter->SetMinimumPaneSize(FromDIP(220));
	rootSizer->Add(splitter, 1, wxEXPAND);
	SetSizer(rootSizer);
	RefreshButtons();
}

void BorderWorkspaceWindow::BindEvents() {
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OpenMaterialsFile(); }, wxID_OPEN);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { if (!rootMaterialsPath_.IsEmpty() && ResolvePendingChanges("reload")){ LoadCatalog(rootMaterialsPath_);
} }, wxID_REFRESH);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CreateBorder(); }, wxID_NEW);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { DeleteCurrentBorder(); }, wxID_DELETE);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { PickSelectedItem(); }, wxID_FIND);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ApplySelectedItem(); }, wxID_APPLY);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { selectedItemCtrl_->SetValue(0); ApplySelectedItem(); }, wxID_CLEAR);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SaveCurrentBorder(); }, wxID_SAVE);
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RevertCurrentBorder(); }, wxID_REVERT);
	Bind(wxEVT_CLOSE_WINDOW, &BorderWorkspaceWindow::OnClose, this);
	filterCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { PopulateBorderList(); });
	borderList_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) {
		const int selection = borderList_->GetSelection();
		if (selection == wxNOT_FOUND || selection >= static_cast<int>(visibleRecords_.size())) {
			return;
		}
		const int record = visibleRecords_[selection];
		if (record == currentRecord_) {
			return;
		}
		if (!ResolvePendingChanges("switch borders")) {
			PopulateBorderList();
			return;
		}
		LoadSelection(record);
	});
	for (int i = 0; i < 12; ++i) {
		slotButtons_[i]->Bind(wxEVT_TOGGLEBUTTON, [this, i](wxCommandEvent&) { SelectSlot(i); });
		previewButtons_[i]->Bind(wxEVT_TOGGLEBUTTON, [this, i](wxCommandEvent&) { SelectSlot(i); });
	}
	idCtrl_->Bind(wxEVT_SPINCTRL, [this](wxCommandEvent&) { MarkDirty(); });
	idCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { MarkDirty(); });
	groupCtrl_->Bind(wxEVT_SPINCTRL, [this](wxCommandEvent&) { MarkDirty(); });
	groupCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { MarkDirty(); });
	typeChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { MarkDirty(); });
	selectedItemCtrl_->Bind(wxEVT_SPINCTRL, [this](wxCommandEvent&) { selectedItemPreview_->SetSprite(ItemSpriteId(selectedItemCtrl_->GetValue())); });
	selectedItemCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { selectedItemPreview_->SetSprite(ItemSpriteId(selectedItemCtrl_->GetValue())); });
}

void BorderWorkspaceWindow::LoadDefaultMaterialsFile() {
	if (!g_gui.IsVersionLoaded()) {
		sourceLabel_->SetLabel("Load a client version, then open Border Workspace again; or choose a materials.xml manually.");
		return;
	}
	LoadCatalog(CurrentMaterialsPath());
}

void BorderWorkspaceWindow::OpenMaterialsFile() {
	if (!ResolvePendingChanges("open another materials file")) {
		return;
	}
	wxFileDialog dialog(this, "Open materials.xml", wxEmptyString, "materials.xml", "XML files (*.xml)|*.xml", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dialog.ShowModal() == wxID_OK) {
		LoadCatalog(dialog.GetPath());
	}
}

bool BorderWorkspaceWindow::LoadCatalog(const wxString& path) {
	std::vector<BorderRecord> oldRecords = std::move(records_);
	records_.clear();
	std::set<wxString> visited;
	wxString error;
	if (!ScanMaterialsFile(path, visited, error)) {
		records_ = std::move(oldRecords);
		wxMessageBox(error, "Border Workspace", wxOK | wxICON_ERROR, this);
		return false;
	}
	rootMaterialsPath_ = NormalizePath(path);
	currentRecord_ = -1;
	hasOriginalRecord_ = false;
	dirty_ = false;
	RebuildItemIndex();
	fileLabel_->SetLabel(wxString::Format("%s  |  %zu border sets in %zu files", path, records_.size(), visited.size()));
	fileLabel_->SetToolTip(path);
	PopulateBorderList();
	if (!records_.empty()) {
		LoadSelection(0);
	} else {
		RefreshWorkspace();
	}
	return true;
}

bool BorderWorkspaceWindow::EnsureCurrentClientCatalog() {
	const wxString currentPath = CurrentMaterialsPath();
	if (currentPath.IsEmpty()) {
		return false;
	}
	if (!rootMaterialsPath_.IsEmpty() && NormalizePath(rootMaterialsPath_).CmpNoCase(NormalizePath(currentPath)) == 0) {
		return true;
	}
	if (!ResolvePendingChanges("open the current client's border catalog")) {
		return false;
	}
	return LoadCatalog(currentPath);
}

bool BorderWorkspaceWindow::OpenItems(const std::vector<ItemCount>& items) {
	struct Choice {
		int recordIndex = -1;
		int slot = -1;
		size_t count = 0;
	};

	size_t totalItems = 0;
	for (const ItemCount& item : items) {
		totalItems += item.count;
	}

	std::vector<Choice> choices;
	if (totalItems == 1 && items.size() == 1) {
		const auto found = itemIndex_.find(items.front().itemId);
		if (found != itemIndex_.end()) {
			for (const ItemLocation& location : found->second) {
				choices.push_back({ location.recordIndex, location.slot, 1 });
			}
		}
	} else {
		std::map<int, size_t> recordCounts;
		std::map<std::pair<int, int>, size_t> slotCounts;
		for (const ItemCount& item : items) {
			const auto found = itemIndex_.find(item.itemId);
			if (found == itemIndex_.end()) {
				continue;
			}
			std::set<int> matchedRecords;
			for (const ItemLocation& location : found->second) {
				matchedRecords.insert(location.recordIndex);
				slotCounts[{ location.recordIndex, location.slot }] += item.count;
			}
			for (int recordIndex : matchedRecords) {
				recordCounts[recordIndex] += item.count;
			}
		}
		for (const auto& [recordIndex, count] : recordCounts) {
			int bestSlot = 0;
			size_t bestSlotCount = 0;
			for (int slot = 0; slot < 12; ++slot) {
				const size_t slotCount = slotCounts[{ recordIndex, slot }];
				if (slotCount > bestSlotCount) {
					bestSlot = slot;
					bestSlotCount = slotCount;
				}
			}
			choices.push_back({ recordIndex, bestSlot, count });
		}
	}

	if (choices.empty()) {
		wxString message;
		if (totalItems == 1 && items.size() == 1) {
			message = wxString::Format("Item ID %d was not found in any loaded border set.", items.front().itemId);
		} else {
			message = "None of the selected item IDs were found in the loaded border sets.";
		}
		message << "\n\nYou can open Border Workspace for a manual search.";
		wxMessageDialog dialog(this, message, "Border item not found", wxYES_NO | wxICON_INFORMATION);
		dialog.SetYesNoLabels("Open Border Workspace", "Cancel");
		return dialog.ShowModal() == wxID_YES;
	}

	std::sort(choices.begin(), choices.end(), [this](const Choice& left, const Choice& right) {
		if (left.count != right.count) {
			return left.count > right.count;
		}
		const BorderRecord& leftRecord = records_[left.recordIndex];
		const BorderRecord& rightRecord = records_[right.recordIndex];
		if (leftRecord.id != rightRecord.id) {
			return leftRecord.id < rightRecord.id;
		}
		return left.slot < right.slot;
	});

	Choice selected = choices.front();
	if (choices.size() > 1) {
		wxArrayString labels;
		for (const Choice& choice : choices) {
			const BorderRecord& record = records_[choice.recordIndex];
			wxString label = wxString::Format("Border %d", record.id);
			if (!record.description.IsEmpty()) {
				label << " - " << record.description;
			}
			label << wxString::Format(" | %s | %s | %llu match%s", EdgeLabel(choice.slot), record.sourceName,
				static_cast<unsigned long long>(choice.count), choice.count == 1 ? "" : "es");
			labels.Add(label);
		}
		const wxString prompt = totalItems == 1
			? wxString::Format("Item ID %d is used by multiple border slots. Select which border to edit.", items.front().itemId)
			: wxString("The selection matches multiple border sets. The strongest match is listed first.");
		wxSingleChoiceDialog dialog(this, prompt, "Select Border Set", labels);
		dialog.SetSelection(0);
		if (dialog.ShowModal() != wxID_OK) {
			return false;
		}
		selected = choices[dialog.GetSelection()];
	}
	return FocusBorder(selected.recordIndex, selected.slot);
}

bool BorderWorkspaceWindow::FocusBorder(int recordIndex, int slot) {
	if (recordIndex < 0 || recordIndex >= static_cast<int>(records_.size()) || slot < 0 || slot >= 12) {
		return false;
	}
	if (recordIndex != currentRecord_ && !ResolvePendingChanges(wxString::Format("open Border %d", records_[recordIndex].id))) {
		return false;
	}
	filterCtrl_->ChangeValue(wxEmptyString);
	if (recordIndex != currentRecord_) {
		LoadSelection(recordIndex);
	} else {
		PopulateBorderList();
	}
	SelectSlot(slot);
	return true;
}

void BorderWorkspaceWindow::RebuildItemIndex() {
	itemIndex_.clear();
	for (int recordIndex = 0; recordIndex < static_cast<int>(records_.size()); ++recordIndex) {
		for (int slot = 0; slot < 12; ++slot) {
			const int itemId = records_[recordIndex].items[slot];
			if (itemId > 0) {
				itemIndex_[itemId].push_back({ recordIndex, slot });
			}
		}
	}
}

bool BorderWorkspaceWindow::ScanMaterialsFile(const wxString& path, std::set<wxString>& visited, wxString& error) {
	wxFileName filename(path);
	filename.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
	const wxString normalized = filename.GetFullPath();
	if (visited.count(normalized)) {
		return true;
	}
	visited.insert(normalized);

	pugi::xml_document document;
	const pugi::xml_parse_result result = document.load_file(normalized.mb_str());
	if (!result) {
		error = wxString::Format("Could not parse %s: %s", normalized, wxString::FromUTF8(result.description()));
		return false;
	}
	pugi::xml_node root = MaterialsRoot(document);
	if (!root) {
		error = wxString::Format("%s does not contain <materials> or <materialsextension>.", normalized);
		return false;
	}

	int borderIndex = 0;
	for (pugi::xml_node child = root.first_child(); child; child = child.next_sibling()) {
		const std::string childName = as_lower_str(child.name());
		if (childName == "include") {
			const wxString includeName = wxString::FromUTF8(child.attribute("file").as_string());
			if (includeName.IsEmpty()) {
				continue;
			}
			wxFileName includePath(filename.GetPath(), includeName);
			if (!ScanMaterialsFile(includePath.GetFullPath(), visited, error)) {
				return false;
			}
		} else if (childName == "border") {
			BorderRecord record;
			record.sourcePath = normalized;
			record.sourceName = filename.GetFullName();
			record.sourceIndex = borderIndex++;
			record.id = child.attribute("id").as_int();
			record.group = child.attribute("group").as_int();
			record.optional = wxString::FromUTF8(child.attribute("type").as_string()).IsSameAs("optional", false);
			record.description = CleanDescription(child);
			for (pugi::xml_node item = child.child("borderitem"); item; item = item.next_sibling("borderitem")) {
				const int edge = EdgeIndex(wxString::FromUTF8(item.attribute("edge").as_string()));
				if (edge >= 0) {
					record.items[edge] = item.attribute("item").as_int();
				}
			}
			records_.push_back(record);
		}
	}
	return true;
}

void BorderWorkspaceWindow::PopulateBorderList() {
	const wxString query = filterCtrl_->GetValue().Lower();
	borderList_->Freeze();
	borderList_->Clear();
	visibleRecords_.clear();
	int selectedVisible = wxNOT_FOUND;
	for (int i = 0; i < static_cast<int>(records_.size()); ++i) {
		const BorderRecord& record = records_[i];
		wxString label = wxString::Format("Border %d", record.id);
		if (!record.description.IsEmpty()) {
			label << " - " << record.description;
		}
		label << "  [" << record.sourceName << "]";
		if (!query.IsEmpty() && !label.Lower().Contains(query)) {
			continue;
		}
		if (i == currentRecord_) {
			selectedVisible = static_cast<int>(visibleRecords_.size());
		}
		visibleRecords_.push_back(i);
		borderList_->Append(label);
	}
	if (selectedVisible != wxNOT_FOUND) {
		borderList_->SetSelection(selectedVisible);
	}
	borderList_->Thaw();
}

bool BorderWorkspaceWindow::ResolvePendingChanges(const wxString& action) {
	if (!dirty_) {
		return true;
	}
	wxMessageDialog dialog(
		this,
		wxString::Format("Save changes to %s before you %s?", headingLabel_->GetLabel(), action),
		"Unsaved border changes", wxYES_NO | wxCANCEL | wxICON_WARNING
	);
	if (action.StartsWith("open Border")) {
		dialog.SetYesNoCancelLabels("Save and Open", "Discard and Open", "Cancel");
	} else {
		dialog.SetYesNoCancelLabels("Save", "Discard", "Cancel");
	}
	const int answer = dialog.ShowModal();
	if (answer == wxCANCEL) {
		return false;
	}
	if (answer == wxYES) {
		return SaveCurrentBorder();
	}
	dirty_ = false;
	if (hasOriginalRecord_ && currentRecord_ >= 0 && currentRecord_ < static_cast<int>(records_.size())) {
		records_[currentRecord_] = originalRecord_;
		RebuildItemIndex();
		LoadSelection(currentRecord_);
	}
	return true;
}

void BorderWorkspaceWindow::LoadSelection(int recordIndex) {
	if (recordIndex < 0 || recordIndex >= static_cast<int>(records_.size())) {
		return;
	}
	currentRecord_ = recordIndex;
	dirty_ = false;
	originalRecord_ = records_[currentRecord_];
	hasOriginalRecord_ = true;
	loading_ = true;
	const BorderRecord& record = records_[currentRecord_];
	idCtrl_->SetValue(record.id);
	groupCtrl_->SetValue(record.group);
	typeChoice_->SetSelection(record.optional ? 1 : 0);
	loading_ = false;
	selectedSlot_ = 0;
	RefreshWorkspace();
	PopulateBorderList();
}

void BorderWorkspaceWindow::RefreshWorkspace() {
	const bool hasSelection = currentRecord_ >= 0 && currentRecord_ < static_cast<int>(records_.size());
	if (!hasSelection) {
		headingLabel_->SetLabel("Border Workspace");
		sourceLabel_->SetLabel("No border sets found in this materials catalog.");
		for (int i = 0; i < 12; ++i) {
			slotButtons_[i]->SetSprite(0);
			slotButtons_[i]->SetValue(false);
			previewButtons_[i]->SetSprite(0);
			previewButtons_[i]->SetValue(false);
			slotLabels_[i]->SetLabel("empty");
		}
		selectedEdgeLabel_->SetLabel("Edge: -");
		selectedItemCtrl_->SetValue(0);
		selectedItemPreview_->SetSprite(0);
		RefreshButtons();
		return;
	}
	const BorderRecord& record = records_[currentRecord_];
	headingLabel_->SetLabel(wxString::Format("Border Workspace — Border %d%s", record.id, dirty_ ? " *" : ""));
	sourceLabel_->SetLabel(wxString::Format("Editing XML directly: %s", record.sourcePath));
	sourceLabel_->SetToolTip(record.sourcePath);
	for (int i = 0; i < 12; ++i) {
		RefreshSlot(i);
	}
	SelectSlot(selectedSlot_);
	RefreshButtons();
}

void BorderWorkspaceWindow::RefreshSlot(int slot) {
	if (currentRecord_ < 0 || slot < 0 || slot >= 12) {
		return;
	}
	const int itemId = records_[currentRecord_].items[slot];
	const int spriteId = ItemSpriteId(itemId);
	slotButtons_[slot]->SetSprite(spriteId);
	previewButtons_[slot]->SetSprite(spriteId);
	slotLabels_[slot]->SetLabel(itemId > 0 ? wxString::Format("%d", itemId) : wxString("empty"));
}

void BorderWorkspaceWindow::RefreshButtons() {
	const bool enabled = currentRecord_ >= 0;
	idCtrl_->Enable(enabled);
	groupCtrl_->Enable(enabled);
	typeChoice_->Enable(enabled);
	selectedItemCtrl_->Enable(enabled);
	deleteButton_->Enable(enabled);
	saveButton_->Enable(enabled && dirty_);
	revertButton_->Enable(enabled && dirty_);
}

void BorderWorkspaceWindow::SelectSlot(int slot) {
	if (currentRecord_ < 0 || slot < 0 || slot >= 12) {
		return;
	}
	selectedSlot_ = slot;
	for (int i = 0; i < 12; ++i) {
		slotButtons_[i]->SetValue(i == slot);
		previewButtons_[i]->SetValue(i == slot);
	}
	selectedEdgeLabel_->SetLabel(wxString::Format("Edge: %s (%s)", EdgeLabel(slot), EdgeName(slot)));
	const int itemId = records_[currentRecord_].items[slot];
	selectedItemCtrl_->SetValue(itemId);
	selectedItemPreview_->SetSprite(ItemSpriteId(itemId));
}

void BorderWorkspaceWindow::MarkDirty() {
	if (loading_ || currentRecord_ < 0) {
		return;
	}
	dirty_ = true;
	RefreshWorkspace();
}

void BorderWorkspaceWindow::ApplySelectedItem() {
	if (currentRecord_ < 0) {
		return;
	}
	const int itemId = selectedItemCtrl_->GetValue();
	if (itemId > 0 && !g_items.typeExists(itemId)) {
		wxMessageBox(wxString::Format("Item ID %d is not valid for the loaded client.", itemId), "Border Workspace", wxOK | wxICON_WARNING, this);
		return;
	}
	records_[currentRecord_].items[selectedSlot_] = itemId;
	RebuildItemIndex();
	MarkDirty();
}

void BorderWorkspaceWindow::PickSelectedItem() {
	if (currentRecord_ < 0) {
		return;
	}
	FindItemDialog dialog(this, "Pick border item");
	dialog.setSearchMode(FindItemDialog::ServerIDs);
	if (dialog.ShowModal() == wxID_OK && dialog.getResultID() > 0) {
		selectedItemCtrl_->SetValue(dialog.getResultID());
		ApplySelectedItem();
	}
}

bool BorderWorkspaceWindow::SaveCurrentBorder() {
	if (currentRecord_ < 0) {
		return false;
	}
	BorderRecord& record = records_[currentRecord_];
	const int newId = idCtrl_->GetValue();
	for (int i = 0; i < static_cast<int>(records_.size()); ++i) {
		if (i != currentRecord_ && records_[i].id == newId) {
			wxMessageBox(wxString::Format("Border ID %d already exists in this catalog.", newId), "Border Workspace", wxOK | wxICON_ERROR, this);
			return false;
		}
	}

	pugi::xml_document document;
	pugi::xml_parse_result result = document.load_file(record.sourcePath.mb_str());
	if (!result) {
		wxMessageBox("Could not reopen the source XML file.", "Border Workspace", wxOK | wxICON_ERROR, this);
		return false;
	}
	pugi::xml_node border = BorderAt(MaterialsRoot(document), record.sourceIndex);
	if (!border) {
		wxMessageBox("The border moved or was removed on disk. Reload the catalog and try again.", "Border Workspace", wxOK | wxICON_ERROR, this);
		return false;
	}
	border.attribute("id").set_value(newId);
	const int group = groupCtrl_->GetValue();
	if (group > 0) {
		pugi::xml_attribute attr = border.attribute("group");
		if (!attr) {
			attr = border.append_attribute("group");
		}
		attr.set_value(group);
	} else {
		border.remove_attribute("group");
	}
	const bool optional = typeChoice_->GetSelection() == 1;
	if (optional) {
		pugi::xml_attribute attr = border.attribute("type");
		if (!attr) {
			attr = border.append_attribute("type");
		}
		attr.set_value("optional");
	} else {
		border.remove_attribute("type");
	}
	for (pugi::xml_node item = border.child("borderitem"); item;) {
		pugi::xml_node next = item.next_sibling("borderitem");
		border.remove_child(item);
		item = next;
	}
	for (int i = 0; i < 12; ++i) {
		if (record.items[i] <= 0) {
			continue;
		}
		pugi::xml_node item = border.append_child("borderitem");
		item.append_attribute("edge").set_value(EdgeName(i));
		item.append_attribute("item").set_value(record.items[i]);
	}

	const wxString backupPath = record.sourcePath + ".bak";
	if (!wxCopyFile(record.sourcePath, backupPath, true)) {
		wxMessageBox("Could not create the XML backup. The border was not saved.", "Border Workspace", wxOK | wxICON_ERROR, this);
		return false;
	}
	if (!document.save_file(record.sourcePath.mb_str(), "\t", pugi::format_default, pugi::encoding_utf8)) {
		wxMessageBox("Could not save the source XML file.", "Border Workspace", wxOK | wxICON_ERROR, this);
		return false;
	}
	record.id = newId;
	record.group = group;
	record.optional = optional;
	originalRecord_ = record;
	hasOriginalRecord_ = true;
	dirty_ = false;
	RebuildItemIndex();
	RefreshWorkspace();
	PopulateBorderList();
	SetStatusText(wxString::Format("Saved Border %d. Backup: %s. Reload data files to apply it to the current session.", newId, backupPath));
	return true;
}

void BorderWorkspaceWindow::RevertCurrentBorder() {
	if (currentRecord_ < 0) {
		return;
	}
	if (!dirty_ || wxMessageBox("Discard the unsaved changes to this border?", "Border Workspace", wxYES_NO | wxICON_QUESTION, this) == wxYES) {
		const int wantedId = records_[currentRecord_].id;
		const wxString wantedSource = records_[currentRecord_].sourcePath;
		LoadCatalog(rootMaterialsPath_);
		for (int i = 0; i < static_cast<int>(records_.size()); ++i) {
			if (records_[i].id == wantedId && records_[i].sourcePath == wantedSource) {
				LoadSelection(i);
				break;
			}
		}
	}
}

void BorderWorkspaceWindow::CreateBorder() {
	if (rootMaterialsPath_.IsEmpty() || !ResolvePendingChanges("create a border")) {
		return;
	}
	wxString targetPath = currentRecord_ >= 0 ? records_[currentRecord_].sourcePath : rootMaterialsPath_;
	pugi::xml_document document;
	if (!document.load_file(targetPath.mb_str())) {
		wxMessageBox("Could not open the target XML file.", "Border Workspace", wxOK | wxICON_ERROR, this);
		return;
	}
	pugi::xml_node root = MaterialsRoot(document);
	if (!root) {
		return;
	}
	int nextId = 1;
	for (const BorderRecord& record : records_) {
		nextId = std::max(nextId, record.id + 1);
	}
	pugi::xml_node border = root.append_child("border");
	border.append_attribute("id").set_value(nextId);
	if (!wxCopyFile(targetPath, targetPath + ".bak", true)) {
		wxMessageBox("Could not create the XML backup. The border was not created.", "Border Workspace", wxOK | wxICON_ERROR, this);
		return;
	}
	if (!document.save_file(targetPath.mb_str(), "\t", pugi::format_default, pugi::encoding_utf8)) {
		wxMessageBox("Could not save the new border.", "Border Workspace", wxOK | wxICON_ERROR, this);
		return;
	}
	LoadCatalog(rootMaterialsPath_);
	for (int i = 0; i < static_cast<int>(records_.size()); ++i) {
		if (records_[i].id == nextId && records_[i].sourcePath == targetPath) {
			LoadSelection(i);
		}
	}
}

void BorderWorkspaceWindow::DeleteCurrentBorder() {
	if (currentRecord_ < 0 || !ResolvePendingChanges("delete this border")) {
		return;
	}
	const BorderRecord record = records_[currentRecord_];
	if (wxMessageBox(wxString::Format("Delete Border %d from %s?\n\nA .bak copy will be created first.", record.id, record.sourceName), "Delete Border Set", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
		return;
	}
	pugi::xml_document document;
	if (!document.load_file(record.sourcePath.mb_str())) {
		return;
	}
	pugi::xml_node root = MaterialsRoot(document);
	pugi::xml_node border = BorderAt(root, record.sourceIndex);
	if (!border) {
		return;
	}
	if (!wxCopyFile(record.sourcePath, record.sourcePath + ".bak", true)) {
		wxMessageBox("Could not create the XML backup. The border was not deleted.", "Border Workspace", wxOK | wxICON_ERROR, this);
		return;
	}
	root.remove_child(border);
	if (!document.save_file(record.sourcePath.mb_str(), "\t", pugi::format_default, pugi::encoding_utf8)) {
		wxMessageBox("Could not save the source XML file.", "Border Workspace", wxOK | wxICON_ERROR, this);
		return;
	}
	LoadCatalog(rootMaterialsPath_);
}

void BorderWorkspaceWindow::OnClose(wxCloseEvent& event) {
	if (event.CanVeto() && !ResolvePendingChanges("close the workspace")) {
		event.Veto();
		return;
	}
	WindowInstance() = nullptr;
	Destroy();
}

int BorderWorkspaceWindow::EdgeIndex(const wxString& edge) {
	for (int i = 0; i < 12; ++i) {
		if (edge.IsSameAs(wxString::FromUTF8(kEdges[i]), false)) {
			return i;
		}
	}
	return -1;
}

const char* BorderWorkspaceWindow::EdgeName(int index) {
	return kEdges[index];
}
const char* BorderWorkspaceWindow::EdgeLabel(int index) {
	return kEdgeLabels[index];
}

int BorderWorkspaceWindow::ItemSpriteId(int itemId) {
	if (itemId <= 0 || itemId > std::numeric_limits<uint16_t>::max() || !g_items.typeExists(itemId)) {
		return 0;
	}
	return g_items[static_cast<uint16_t>(itemId)].clientID;
}
