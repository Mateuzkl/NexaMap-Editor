//////////////////////////////////////////////////////////////////////
// Searchable browser shared by Server Workspace content editors.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "server_content_browser_dialog.h"

#include "theme.h"

#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/srchctrl.h>
#include <wx/statline.h>
#include <wx/stattext.h>

#include <algorithm>
#include <cctype>
#include <tuple>

namespace {
	constexpr int ID_CREATE_CONTENT = wxID_HIGHEST + 841;

	wxString PathText(const std::filesystem::path& path) {
#ifdef __WINDOWS__
		return wxString(path.wstring());
#else
		return wxString::FromUTF8(path.string());
#endif
	}

	std::string LowerAscii(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
			return static_cast<char>(character >= 'A' && character <= 'Z' ? character + ('a' - 'A') : character);
		});
		return value;
	}

	std::string SearchText(const ServerContentSource& source) {
		return LowerAscii(source.name + " " + ServerContentFormatName(source.format) + " " + source.declarationPath.generic_string());
	}
}

ServerContentBrowserDialog::ServerContentBrowserDialog(
	wxWindow* parent,
	const wxString& title,
	const wxString& noun,
	std::filesystem::path root,
	std::vector<ServerContentSource> contentSources,
	bool allowCreate
) :
	wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	contentRoot(std::move(root)),
	sources(std::move(contentSources)) {
	SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	std::sort(sources.begin(), sources.end(), [](const ServerContentSource& left, const ServerContentSource& right) {
		return std::tie(left.name, left.declarationPath) < std::tie(right.name, right.declarationPath);
	});

	auto* rootSizer = newd wxBoxSizer(wxVERTICAL);
	wxButton* createButton = nullptr;
	if (allowCreate) {
		createButton = newd wxButton(this, ID_CREATE_CONTENT, "Create a new " + noun + "...");
		rootSizer->Add(createButton, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
		rootSizer->Add(newd wxStaticLine(this), 0, wxEXPAND | wxALL, FromDIP(12));
	}

	search = newd wxSearchCtrl(this, wxID_ANY);
	search->SetDescriptiveText("Search " + noun + "s by name, format or source path...");
	search->ShowCancelButton(true);
	rootSizer->Add(search, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

	list = newd wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	list->AppendColumn("Name", wxLIST_FORMAT_LEFT, FromDIP(250));
	list->AppendColumn("Format", wxLIST_FORMAT_LEFT, FromDIP(90));
	list->AppendColumn("Source", wxLIST_FORMAT_LEFT, FromDIP(470));
	rootSizer->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));

	details = newd wxStaticText(this, wxID_ANY, "Select a " + noun + " to see its complete source path.");
	details->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	details->SetMinSize(FromDIP(wxSize(-1, 34)));
	details->Wrap(FromDIP(800));
	rootSizer->Add(details, 0, wxEXPAND | wxALL, FromDIP(12));

	auto* buttons = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
	rootSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
	openButton = FindWindow(wxID_OK);
	if (openButton) {
		openButton->SetLabel("Open");
		openButton->Enable(false);
	}
	SetSizer(rootSizer);
	SetMinSize(FromDIP(wxSize(720, 480)));
	SetSize(FromDIP(wxSize(900, 620)));
	CentreOnParent();

	if (createButton) {
		createButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			create = true;
			EndModal(wxID_OK);
		});
	}
	search->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { rebuildList(); });
	search->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent&) {
		search->Clear();
		rebuildList();
	});
	list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { updateSelection(); });
	list->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent&) { updateSelection(); });
	list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { openSelection(); });
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { openSelection(); }, wxID_OK);
	rebuildList();
	search->SetFocus();
}

bool ServerContentBrowserDialog::wantsCreate() const {
	return create;
}

std::optional<ServerContentSource> ServerContentBrowserDialog::selectedSource() const {
	if (create || !list) {
		return std::nullopt;
	}
	const long row = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (row < 0 || static_cast<std::size_t>(row) >= visible.size()) {
		return std::nullopt;
	}
	return sources[visible[static_cast<std::size_t>(row)]];
}

void ServerContentBrowserDialog::rebuildList() {
	const std::string needle = LowerAscii(search->GetValue().ToStdString(wxConvUTF8));
	visible.clear();
	list->DeleteAllItems();
	for (std::size_t index = 0; index < sources.size(); ++index) {
		const ServerContentSource& source = sources[index];
		if (!needle.empty() && SearchText(source).find(needle) == std::string::npos) {
			continue;
		}
		visible.push_back(index);
		const long row = list->InsertItem(list->GetItemCount(), wxString::FromUTF8(source.name));
		list->SetItem(row, 1, wxString::FromUTF8(ServerContentFormatName(source.format)));
		std::filesystem::path relative = source.declarationPath.lexically_relative(contentRoot);
		if (relative.empty() || relative.native().starts_with(std::filesystem::path("..").native())) {
			relative = source.declarationPath.filename();
		}
		list->SetItem(row, 2, PathText(relative));
	}
	if (!visible.empty()) {
		list->SetItemState(0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
	}
	updateSelection();
}

void ServerContentBrowserDialog::updateSelection() {
	const auto selected = selectedSource();
	if (openButton) {
		openButton->Enable(selected.has_value());
	}
	details->SetLabel(selected ? PathText(selected->declarationPath) : wxString("No matching source selected."));
	details->SetToolTip(selected ? PathText(selected->declarationPath) : wxString());
}

void ServerContentBrowserDialog::openSelection() {
	if (selectedSource()) {
		EndModal(wxID_OK);
	}
}
