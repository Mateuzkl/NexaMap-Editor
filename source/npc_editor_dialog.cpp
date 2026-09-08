//////////////////////////////////////////////////////////////////////
// Native source-preserving NPC editor.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "npc_editor_dialog.h"

#include "find_item_window.h"
#include "graphics.h"
#include "gui.h"
#include "items.h"
#include "outfit.h"
#include "outfit_color_picker.h"
#include "source_compare_dialog.h"
#include "theme.h"
#include "workspace_session.h"

#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/statline.h>
#include <wx/timer.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace {
	constexpr int ID_NPC_BACK = wxID_HIGHEST + 911;
	constexpr int ID_NPC_BROWSE = wxID_HIGHEST + 912;

	wxString Utf8(const std::string& value) {
		return wxString::FromUTF8(value);
	}
	std::string Narrow(const wxString& value) {
		return value.ToStdString(wxConvUTF8);
	}
	std::size_t Index(NpcField field) {
		return static_cast<std::size_t>(field);
	}

	wxScrolledWindow* Page(wxNotebook* notebook) {
		auto* page = newd wxScrolledWindow(notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
		page->SetScrollRate(0, page->FromDIP(12));
		return page;
	}

	wxFlexGridSizer* Grid(int columns = 2) {
		auto* grid = newd wxFlexGridSizer(columns, 8, 12);
		for (int column = 1; column < columns; column += 2) {
			grid->AddGrowableCol(column, 1);
		}
		return grid;
	}

	long Selected(wxListCtrl* list) {
		return list ? list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) : -1;
	}

	wxString PathText(const std::filesystem::path& path) {
#ifdef __WINDOWS__
		return wxString(path.wstring());
#else
		return wxString::FromUTF8(path.string());
#endif
	}

	wxBitmap PreviewBitmap(const NpcDefinition& definition, int target) {
		Outfit outfit;
		outfit.lookType = definition.lookType;
		outfit.lookItem = definition.lookTypeEx;
		outfit.lookHead = definition.lookHead;
		outfit.lookBody = definition.lookBody;
		outfit.lookLegs = definition.lookLegs;
		outfit.lookFeet = definition.lookFeet;
		outfit.lookAddon = definition.lookAddons;
		outfit.lookMount = definition.lookMount;
		int mountClientId = outfit.lookMount > 0 ? g_workspace.resolveMountClientId(outfit.lookMount) : 0;
		GameSprite* sprite = nullptr;
		if (outfit.lookItem > 0) {
			sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(outfit.lookItem));
		} else if (outfit.lookType > 0) {
			sprite = g_gui.gfx.getCreatureSprite(outfit.lookType);
		} else if (outfit.lookMount > 0) {
			sprite = g_gui.gfx.getCreatureSprite(mountClientId);
			outfit.lookType = mountClientId;
			outfit.lookMount = 0;
			mountClientId = 0;
		}
		if (!sprite) {
			return {};
		}
		std::vector<uint8_t> rgba;
		int width = 0, height = 0;
		bool pending = false;
		if (!sprite->getVisualPreviewRGBA(rgba, width, height, pending, false, outfit.lookItem > 0 ? nullptr : &outfit, definition.direction, 0, 0, 0, 0, mountClientId)) {
			return {};
		}
		wxImage image(width, height);
		image.InitAlpha();
		for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(width) * height; ++pixel) {
			std::copy_n(rgba.data() + pixel * 4, 3, image.GetData() + pixel * 3);
			image.GetAlpha()[pixel] = rgba[pixel * 4 + 3];
		}
		const double scale = static_cast<double>(target) / std::max(width, height);
		return wxBitmap(image.Scale(std::max(1, static_cast<int>(width * scale)), std::max(1, static_cast<int>(height * scale)), wxIMAGE_QUALITY_NEAREST));
	}
}

NpcEditorDialog::NpcEditorDialog(wxWindow* parent, std::unique_ptr<NpcDefinitionDocument> value) :
	wxDialog(parent, wxID_ANY, "NPC Editor", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	document(std::move(value)), edited(document->definition()) {
	SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	auto* root = newd wxBoxSizer(wxVERTICAL);
	auto* header = newd wxPanel(this);
	header->SetBackgroundColour(Theme::Get(Theme::Role::RaisedSurface));
	auto* headerSizer = newd wxBoxSizer(wxVERTICAL);
	auto* title = newd wxStaticText(header, wxID_ANY, Utf8(edited.name));
	wxFont font = title->GetFont();
	font.SetPointSize(font.GetPointSize() + 3);
	font.SetWeight(wxFONTWEIGHT_BOLD);
	title->SetFont(font);
	headerSizer->Add(title, 0, wxBOTTOM, FromDIP(3));
	headerSizer->Add(newd wxStaticText(header, wxID_ANY, Utf8(ServerContentFormatName(document->source().format)) + "  |  " + PathText(document->source().declarationPath)), 0, wxEXPAND);
	header->SetSizer(headerSizer);
	root->Add(header, 0, wxEXPAND | wxALL, FromDIP(12));

	auto* notebook = newd wxNotebook(this, wxID_ANY);
	auto* mainPage = Page(notebook);
	auto* mainSizer = newd wxBoxSizer(wxVERTICAL);
	auto* identity = newd wxStaticBoxSizer(wxVERTICAL, mainPage, "Identity and source");
	auto* identityGrid = Grid();
	addText(identity->GetStaticBox(), identityGrid, NpcField::Name, edited.name);
	addText(identity->GetStaticBox(), identityGrid, NpcField::Description, edited.description);
	addText(identity->GetStaticBox(), identityGrid, NpcField::Script, edited.script);
	identity->Add(identityGrid, 0, wxEXPAND | wxALL, FromDIP(8));
	mainSizer->Add(identity, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	auto* runtime = newd wxStaticBoxSizer(wxVERTICAL, mainPage, "Runtime and movement");
	auto* runtimeGrid = Grid(4);
	addNumber(runtime->GetStaticBox(), runtimeGrid, NpcField::Health, edited.health);
	addNumber(runtime->GetStaticBox(), runtimeGrid, NpcField::MaxHealth, edited.maxHealth);
	addNumber(runtime->GetStaticBox(), runtimeGrid, NpcField::WalkInterval, edited.walkInterval);
	addNumber(runtime->GetStaticBox(), runtimeGrid, NpcField::WalkRadius, edited.walkRadius);
	addNumber(runtime->GetStaticBox(), runtimeGrid, NpcField::Speed, edited.speed);
	addNumber(runtime->GetStaticBox(), runtimeGrid, NpcField::Direction, edited.direction, 3);
	runtimeGrid->Add(newd wxStaticText(runtime->GetStaticBox(), wxID_ANY, NpcFieldName(NpcField::FloorChange)), 0, wxALIGN_CENTER_VERTICAL);
	auto* floor = newd wxCheckBox(runtime->GetStaticBox(), wxID_ANY, "May change floors");
	floor->SetValue(edited.floorChange);
	controls[Index(NpcField::FloorChange)] = floor;
	applyCapability(floor, NpcField::FloorChange);
	runtimeGrid->Add(floor, 1, wxEXPAND);
	runtime->Add(runtimeGrid, 0, wxEXPAND | wxALL, FromDIP(8));
	mainSizer->Add(runtime, 0, wxEXPAND);
	mainPage->SetSizer(mainSizer);
	notebook->AddPage(mainPage, "Main");

	auto* lookPage = Page(notebook);
	auto* lookSizer = newd wxBoxSizer(wxHORIZONTAL);
	auto* previewBox = newd wxStaticBoxSizer(wxVERTICAL, lookPage, "Active client preview");
	preview = newd wxStaticBitmap(previewBox->GetStaticBox(), wxID_ANY, wxBitmap());
	preview->SetMinSize(FromDIP(wxSize(180, 180)));
	previewBox->Add(preview, 1, wxALIGN_CENTER | wxALL, FromDIP(12));
	lookSizer->Add(previewBox, 0, wxEXPAND | wxRIGHT, FromDIP(10));
	auto* outfit = newd wxStaticBoxSizer(wxVERTICAL, lookPage, "Outfit");
	auto* outfitGrid = Grid();
	for (const auto [field, current, maximum] : { std::tuple(NpcField::LookType, edited.lookType, 200000), std::tuple(NpcField::LookTypeEx, edited.lookTypeEx, 200000), std::tuple(NpcField::LookAddons, edited.lookAddons, 255), std::tuple(NpcField::LookMount, edited.lookMount, 200000) }) {
		auto* control = addNumber(outfit->GetStaticBox(), outfitGrid, field, current, maximum);
		control->Bind(wxEVT_SPINCTRL, [this](wxCommandEvent&) {
			readControls();
			refreshPreview();
			scheduleAutosave();
		});
	}
	outfit->Add(outfitGrid, 0, wxEXPAND | wxALL, FromDIP(8));
	outfitColors = newd OutfitColorPicker(outfit->GetStaticBox(), [this](int channel, int color) {
		switch (channel) {
			case 0:
				edited.lookHead = color;
				break;
			case 1:
				edited.lookBody = color;
				break;
			case 2:
				edited.lookLegs = color;
				break;
			case 3:
				edited.lookFeet = color;
				break;
		}
		refreshPreview();
		scheduleAutosave();
	});
	outfitColors->SetColors(edited.lookHead, edited.lookBody, edited.lookLegs, edited.lookFeet);
	for (int channel = 0; channel < 4; ++channel) {
		const NpcField field = static_cast<NpcField>(static_cast<int>(NpcField::LookHead) + channel);
		outfitColors->SetChannelEnabled(channel, edited.capability(field).editable);
	}
	outfit->Add(outfitColors, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	lookSizer->Add(outfit, 1, wxEXPAND);
	lookPage->SetSizer(lookSizer);
	notebook->AddPage(lookPage, "Look");

	const auto listPage = [&](const wxString& label, wxListCtrl*& list) { auto* page = Page(notebook); auto* sizer = newd wxBoxSizer(wxVERTICAL); list = newd wxListCtrl(page, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL); sizer->Add(list, 1, wxEXPAND | wxALL, FromDIP(10)); auto* edit = newd wxButton(page, wxID_ANY, "Edit selected literal..."); sizer->Add(edit, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10)); page->SetSizer(sizer); notebook->AddPage(page, label); return edit; };
	auto* editMessages = listPage("Messages", messageList);
	messageList->AppendColumn("Message", wxLIST_FORMAT_LEFT, FromDIP(190));
	messageList->AppendColumn("Text", wxLIST_FORMAT_LEFT, FromDIP(560));
	editMessages->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { const long row = Selected(messageList); if (row >= 0){ editMessage(static_cast<std::size_t>(row));
} });
	messageList->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) { editMessage(static_cast<std::size_t>(event.GetIndex())); });
	auto* editShopButton = listPage("Shop", shopList);
	shopList->AppendColumn("Item", wxLIST_FORMAT_LEFT, FromDIP(310));
	shopList->AppendColumn("ID", wxLIST_FORMAT_RIGHT, FromDIP(90));
	shopList->AppendColumn("Buy", wxLIST_FORMAT_RIGHT, FromDIP(100));
	shopList->AppendColumn("Sell", wxLIST_FORMAT_RIGHT, FromDIP(100));
	editShopButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { const long row = Selected(shopList); if (row >= 0){ editShop(static_cast<std::size_t>(row));
} });
	shopList->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) { editShop(static_cast<std::size_t>(event.GetIndex())); });
	auto* editTravelButton = listPage("Travel", travelList);
	travelList->AppendColumn("Keyword", wxLIST_FORMAT_LEFT, FromDIP(240));
	travelList->AppendColumn("Cost", wxLIST_FORMAT_RIGHT, FromDIP(90));
	travelList->AppendColumn("Destination", wxLIST_FORMAT_LEFT, FromDIP(260));
	travelList->AppendColumn("Premium", wxLIST_FORMAT_LEFT, FromDIP(90));
	editTravelButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { const long row = Selected(travelList); if (row >= 0){ editTravel(static_cast<std::size_t>(row));
} });
	travelList->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) { editTravel(static_cast<std::size_t>(event.GetIndex())); });

	auto* behaviorPage = Page(notebook);
	auto* behaviorSizer = newd wxBoxSizer(wxVERTICAL);
	behaviorSizer->Add(newd wxStaticText(behaviorPage, wxID_ANY, "Supported direct literals are available in Messages, Shop and Travel."), 0, wxALL, FromDIP(10));
	for (const std::string& note : edited.behaviorNotes) {
		behaviorSizer->Add(newd wxStaticText(behaviorPage, wxID_ANY, Utf8(note)), 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
	}
	behaviorSizer->Add(newd wxStaticText(behaviorPage, wxID_ANY, "Complex callbacks, conditions, storage logic and keyword graphs stay in Source and are never regenerated."), 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
	behaviorPage->SetSizer(behaviorSizer);
	notebook->AddPage(behaviorPage, "Behavior");
	auto* sourcePage = newd wxPanel(notebook);
	auto* sourceSizer = newd wxBoxSizer(wxVERTICAL);
	sourceView = newd wxTextCtrl(sourcePage, wxID_ANY, Utf8(document->sourceText()), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
	sourceView->SetFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE));
	sourceSizer->Add(sourceView, 1, wxEXPAND | wxALL, FromDIP(8));
	sourcePage->SetSizer(sourceSizer);
	notebook->AddPage(sourcePage, "Source");
	root->Add(notebook, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
	auto* footer = newd wxBoxSizer(wxHORIZONTAL);
	saveStateLabel = newd wxStaticText(this, wxID_ANY, "No unsaved changes");
	saveStateLabel->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	footer->Add(saveStateLabel, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
	compareButton = newd wxButton(this, wxID_ANY, "Compare External...");
	compareButton->Hide();
	compareButton->Bind(wxEVT_BUTTON, &NpcEditorDialog::onCompareExternal, this);
	footer->Add(compareButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	footer->Add(newd wxButton(this, ID_NPC_BACK, "< Back to NPCs"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	footer->Add(newd wxButton(this, ID_NPC_BROWSE, "Browse NPCs..."), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
	auto* buttons = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
	if (auto* save = FindWindow(wxID_OK)) {
		save->SetLabel("Save");
	}
	if (auto* close = FindWindow(wxID_CANCEL)) {
		close->SetLabel("Close");
	}
	footer->Add(buttons, 0, wxALIGN_CENTER_VERTICAL);
	root->Add(newd wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
	root->Add(footer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
	SetSizer(root);
	SetMinSize(FromDIP(wxSize(760, 590)));
	SetSize(FromDIP(wxSize(900, 700)));
	CentreOnParent();
	Bind(wxEVT_BUTTON, &NpcEditorDialog::onSave, this, wxID_OK);
	Bind(wxEVT_BUTTON, &NpcEditorDialog::onBrowse, this, ID_NPC_BACK);
	Bind(wxEVT_BUTTON, &NpcEditorDialog::onBrowse, this, ID_NPC_BROWSE);
	Bind(wxEVT_BUTTON, &NpcEditorDialog::onCancel, this, wxID_CANCEL);
	Bind(wxEVT_CLOSE_WINDOW, &NpcEditorDialog::onClose, this);
	Bind(wxEVT_TEXT, &NpcEditorDialog::onFieldChanged, this);
	Bind(wxEVT_SPINCTRL, &NpcEditorDialog::onFieldChanged, this);
	Bind(wxEVT_CHECKBOX, &NpcEditorDialog::onFieldChanged, this);
	autosaveTimer = std::make_unique<wxTimer>(this);
	Bind(wxEVT_TIMER, &NpcEditorDialog::onAutosave, this, autosaveTimer->GetId());
	sourceWatchTimer = std::make_unique<wxTimer>(this);
	Bind(wxEVT_TIMER, &NpcEditorDialog::onSourceWatch, this, sourceWatchTimer->GetId());
	resetSourceMonitor();
	sourceWatchTimer->Start(1000);
	constructing = false;
	refreshPreview();
	refreshMessages();
	refreshShop();
	refreshTravel();
}

bool NpcEditorDialog::wasSaved() const {
	return saved;
}

bool NpcEditorDialog::wantsBrowse() const {
	return browseRequested;
}
wxTextCtrl* NpcEditorDialog::addText(wxWindow* parent, wxFlexGridSizer* grid, NpcField field, const std::string& value) {
	grid->Add(newd wxStaticText(parent, wxID_ANY, NpcFieldName(field)), 0, wxALIGN_CENTER_VERTICAL);
	auto* control = newd wxTextCtrl(parent, wxID_ANY, Utf8(value));
	grid->Add(control, 1, wxEXPAND);
	controls[Index(field)] = control;
	applyCapability(control, field);
	return control;
}
wxSpinCtrl* NpcEditorDialog::addNumber(wxWindow* parent, wxFlexGridSizer* grid, NpcField field, int value, int maximum) {
	grid->Add(newd wxStaticText(parent, wxID_ANY, NpcFieldName(field)), 0, wxALIGN_CENTER_VERTICAL);
	auto* control = newd wxSpinCtrl(parent, wxID_ANY, wxString::Format("%d", value), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, maximum, value);
	grid->Add(control, 1, wxEXPAND);
	controls[Index(field)] = control;
	applyCapability(control, field);
	return control;
}
void NpcEditorDialog::applyCapability(wxWindow* control, NpcField field) {
	const auto& capability = edited.capability(field);
	control->Enable(capability.editable);
	if (!capability.editable) {
		control->SetToolTip(Utf8(capability.limitation));
	}
}

void NpcEditorDialog::readControls() {
	const auto text = [&](NpcField field, std::string& target) { if (edited.capability(field).editable){ target = Narrow(dynamic_cast<wxTextCtrl*>(controls[Index(field)])->GetValue());
} };
	const auto number = [&](NpcField field, int& target) { if (edited.capability(field).editable){ target = dynamic_cast<wxSpinCtrl*>(controls[Index(field)])->GetValue();
} };
	text(NpcField::Name, edited.name);
	text(NpcField::Description, edited.description);
	text(NpcField::Script, edited.script);
	number(NpcField::Health, edited.health);
	number(NpcField::MaxHealth, edited.maxHealth);
	number(NpcField::WalkInterval, edited.walkInterval);
	number(NpcField::WalkRadius, edited.walkRadius);
	number(NpcField::Speed, edited.speed);
	number(NpcField::Direction, edited.direction);
	number(NpcField::LookType, edited.lookType);
	number(NpcField::LookTypeEx, edited.lookTypeEx);
	if (outfitColors) {
		if (edited.capability(NpcField::LookHead).editable) {
			edited.lookHead = outfitColors->GetColor(0);
		}
		if (edited.capability(NpcField::LookBody).editable) {
			edited.lookBody = outfitColors->GetColor(1);
		}
		if (edited.capability(NpcField::LookLegs).editable) {
			edited.lookLegs = outfitColors->GetColor(2);
		}
		if (edited.capability(NpcField::LookFeet).editable) {
			edited.lookFeet = outfitColors->GetColor(3);
		}
	}
	number(NpcField::LookAddons, edited.lookAddons);
	number(NpcField::LookMount, edited.lookMount);
	if (edited.capability(NpcField::FloorChange).editable) {
		edited.floorChange = dynamic_cast<wxCheckBox*>(controls[Index(NpcField::FloorChange)])->GetValue();
	}
}

void NpcEditorDialog::refreshPreview() {
	preview->SetBitmap(PreviewBitmap(edited, FromDIP(170)));
	preview->SetToolTip(wxString::Format("lookType %d | lookTypeEx %d | mount %d | direction %d", edited.lookType, edited.lookTypeEx, edited.lookMount, edited.direction));
	preview->Refresh();
}
void NpcEditorDialog::refreshMessages() {
	messageList->DeleteAllItems();
	for (std::size_t i = 0; i < edited.messages.size(); ++i) {
		const long row = messageList->InsertItem(static_cast<long>(i), Utf8(edited.messages[i].key));
		messageList->SetItem(row, 1, Utf8(edited.messages[i].text));
	}
}
void NpcEditorDialog::refreshShop() {
	shopList->DeleteAllItems();
	for (std::size_t i = 0; i < edited.shop.size(); ++i) {
		const auto& item = edited.shop[i];
		wxString name = Utf8(item.itemName);
		if (item.itemId > 0 && g_items.typeExists(item.itemId)) {
			name = Utf8(g_items[item.itemId].name);
		}
		const long row = shopList->InsertItem(static_cast<long>(i), name);
		shopList->SetItem(row, 1, wxString::Format("%d", item.itemId));
		shopList->SetItem(row, 2, item.buy ? wxString::Format("%d", item.buy) : wxString("-"));
		shopList->SetItem(row, 3, item.sell ? wxString::Format("%d", item.sell) : wxString("-"));
	}
}
void NpcEditorDialog::refreshTravel() {
	travelList->DeleteAllItems();
	for (std::size_t i = 0; i < edited.travel.size(); ++i) {
		const auto& travel = edited.travel[i];
		const long row = travelList->InsertItem(static_cast<long>(i), Utf8(travel.keyword));
		travelList->SetItem(row, 1, wxString::Format("%d", travel.cost));
		travelList->SetItem(row, 2, wxString::Format("%d, %d, %d", travel.x, travel.y, travel.z));
		travelList->SetItem(row, 3, travel.premium ? "Yes" : "No");
	}
}

void NpcEditorDialog::editMessage(std::size_t index) {
	if (index >= edited.messages.size() || !edited.messages[index].editable) {
		return;
	}
	wxTextEntryDialog dialog(this, "Message text", Utf8(edited.messages[index].key), Utf8(edited.messages[index].text), wxOK | wxCANCEL | wxTE_MULTILINE);
	if (dialog.ShowModal() == wxID_OK) {
		edited.messages[index].text = Narrow(dialog.GetValue());
		refreshMessages();
		scheduleAutosave();
	}
}
void NpcEditorDialog::editShop(std::size_t index) {
	if (index >= edited.shop.size() || !edited.shop[index].editable) {
		return;
	}
	auto& item = edited.shop[index];
	wxDialog dialog(this, wxID_ANY, "Shop item");
	auto* root = newd wxBoxSizer(wxVERTICAL);
	auto* grid = Grid();
	grid->Add(newd wxStaticText(&dialog, wxID_ANY, "Item name"), 0, wxALIGN_CENTER_VERTICAL);
	auto* name = newd wxTextCtrl(&dialog, wxID_ANY, Utf8(item.itemName));
	grid->Add(name, 1, wxEXPAND);
	const auto spin = [&](const wxString& label, int value) { grid->Add(newd wxStaticText(&dialog, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL); auto* control = newd wxSpinCtrl(&dialog, wxID_ANY, wxString::Format("%d", value), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, std::numeric_limits<int>::max(), value); grid->Add(control, 1, wxEXPAND); return control; };
	auto* id = spin("Server item ID", item.itemId);
	auto* buy = spin("Buy price", item.buy);
	auto* sell = spin("Sell price", item.sell);
	root->Add(grid, 1, wxEXPAND | wxALL, FromDIP(12));
	auto* choose = newd wxButton(&dialog, wxID_ANY, "Choose from active items...");
	choose->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { FindItemDialog chooser(&dialog, "Choose shop item"); chooser.setSearchMode(FindItemDialog::ServerIDs); if (chooser.ShowModal() == wxID_OK) { id->SetValue(chooser.getResultID()); if (g_items.typeExists(chooser.getResultID())){ name->SetValue(Utf8(g_items[chooser.getResultID()].name));
} } });
	root->Add(choose, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
	root->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(12));
	dialog.SetSizerAndFit(root);
	dialog.SetMinSize(FromDIP(wxSize(480, 320)));
	if (dialog.ShowModal() == wxID_OK) {
		item.itemName = Narrow(name->GetValue());
		item.itemId = id->GetValue();
		item.buy = buy->GetValue();
		item.sell = sell->GetValue();
		refreshShop();
		scheduleAutosave();
	}
}
void NpcEditorDialog::editTravel(std::size_t index) {
	if (index >= edited.travel.size() || !edited.travel[index].editable) {
		return;
	}
	auto& travel = edited.travel[index];
	wxDialog dialog(this, wxID_ANY, "Travel destination");
	auto* root = newd wxBoxSizer(wxVERTICAL);
	auto* grid = Grid();
	grid->Add(newd wxStaticText(&dialog, wxID_ANY, "Keyword"), 0, wxALIGN_CENTER_VERTICAL);
	auto* keyword = newd wxTextCtrl(&dialog, wxID_ANY, Utf8(travel.keyword));
	grid->Add(keyword, 1, wxEXPAND);
	const auto spin = [&](const wxString& label, int value, int maximum) { grid->Add(newd wxStaticText(&dialog, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL); auto* control = newd wxSpinCtrl(&dialog, wxID_ANY, wxString::Format("%d", value), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, maximum, value); grid->Add(control, 1, wxEXPAND); return control; };
	auto* cost = spin("Cost", travel.cost, std::numeric_limits<int>::max());
	auto* x = spin("X", travel.x, 65535);
	auto* y = spin("Y", travel.y, 65535);
	auto* z = spin("Z", travel.z, 15);
	root->Add(grid, 1, wxEXPAND | wxALL, FromDIP(12));
	root->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(12));
	dialog.SetSizerAndFit(root);
	if (dialog.ShowModal() == wxID_OK) {
		travel.keyword = Narrow(keyword->GetValue());
		travel.cost = cost->GetValue();
		travel.x = x->GetValue();
		travel.y = y->GetValue();
		travel.z = z->GetValue();
		refreshTravel();
		scheduleAutosave();
	}
}

void NpcEditorDialog::onSave(wxCommandEvent&) {
	saveDocument(true);
}

bool NpcEditorDialog::saveDocument(bool showErrors) {
	readControls();
	if (!externalChanges.empty()) {
		if (showErrors) {
			wxCommandEvent event;
			onCompareExternal(event);
		}
		return false;
	}
	std::string error;
	if (!document->save(edited, error)) {
		autosaveState.failed(error);
		updateSaveState("Save error: " + Utf8(error), true);
		if (showErrors) {
			wxMessageBox(Utf8(error), "Could not save NPC", wxOK | wxICON_ERROR, this);
		}
		return false;
	}
	edited = document->definition();
	sourceView->ChangeValue(Utf8(document->sourceText()));
	autosaveState.saved();
	saved = true;
	resetSourceMonitor();
	updateSaveState(showErrors ? "Saved" : "Saved automatically");
	return true;
}

void NpcEditorDialog::scheduleAutosave() {
	if (constructing || !document || !externalChanges.empty()) {
		return;
	}
	if (!document->hasChanges(edited)) {
		if (!autosaveState.hasError()) {
			updateSaveState(saved ? "Saved" : "No unsaved changes");
		}
		return;
	}
	autosaveState.changed();
	updateSaveState("Unsaved changes - autosave pending");
	if (autosaveTimer) {
		autosaveTimer->StartOnce(650);
	}
}

void NpcEditorDialog::updateSaveState(const wxString& label, bool error) {
	if (!saveStateLabel) {
		return;
	}
	saveStateLabel->SetLabel(label);
	saveStateLabel->SetForegroundColour(error ? wxColour(232, 72, 85) : Theme::Get(Theme::Role::TextSubtle));
	saveStateLabel->SetToolTip(label);
	saveStateLabel->GetParent()->Layout();
}

void NpcEditorDialog::onFieldChanged(wxCommandEvent& event) {
	readControls();
	scheduleAutosave();
	event.Skip();
}

void NpcEditorDialog::onAutosave(wxTimerEvent&) {
	if (autosaveState.ready()) {
		saveDocument(false);
	}
}

void NpcEditorDialog::resetSourceMonitor() {
	std::vector<EditorSourceSnapshot> sources { { document->source().declarationPath, document->sourceText() } };
	const auto addRelated = [&](const std::optional<std::filesystem::path>& path) {
		if (!path || path->lexically_normal() == document->source().declarationPath.lexically_normal()) {
			return;
		}
		if (const auto text = SourceText::ReadBoundedFile(*path, 32 * 1024 * 1024)) {
			sources.push_back({ *path, *text });
		}
	};
	addRelated(document->source().registrationPath);
	addRelated(document->source().relatedScriptPath);
	sourceMonitor.reset(std::move(sources));
	externalChanges.clear();
	if (compareButton) {
		compareButton->Hide();
		Layout();
	}
}

void NpcEditorDialog::onSourceWatch(wxTimerEvent&) {
	if (!externalChanges.empty()) {
		return;
	}
	externalChanges = sourceMonitor.poll();
	if (externalChanges.empty()) {
		return;
	}
	if (autosaveTimer) {
		autosaveTimer->Stop();
	}
	updateSaveState("External source change detected - autosave paused", true);
	compareButton->Show();
	Layout();
}

void NpcEditorDialog::onCompareExternal(wxCommandEvent&) {
	if (ShowSourceConflictDialog(this, externalChanges) == SourceConflictChoice::Reopen) {
		browseRequested = true;
		EndModal(wxID_CANCEL);
	}
}

void NpcEditorDialog::onBrowse(wxCommandEvent&) {
	if (autosaveTimer) {
		autosaveTimer->Stop();
	}
	readControls();
	if (document->hasChanges(edited) && !saveDocument(true)) {
		return;
	}
	browseRequested = true;
	EndModal(wxID_CANCEL);
}
bool NpcEditorDialog::confirmDiscard() {
	readControls();
	return !document->hasChanges(edited) || wxMessageBox("Discard the unsaved NPC changes?", "NPC Editor", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) == wxYES;
}
void NpcEditorDialog::onCancel(wxCommandEvent&) {
	if (confirmDiscard()) {
		EndModal(wxID_CANCEL);
	}
}
void NpcEditorDialog::onClose(wxCloseEvent& event) {
	if (!IsModal() || confirmDiscard()) {
		event.Skip();
	} else {
		event.Veto();
	}
}
