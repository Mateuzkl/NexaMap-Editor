#include "main.h"

#include "new_map_tab_dialog.h"

#include "gui.h"
#include "theme.h"
#include "workspace_session.h"

namespace {
	wxString PathText(const std::filesystem::path& path) {
#ifdef __WINDOWS__
		return wxString(path.wstring());
#else
		return wxString::FromUTF8(path.string());
#endif
	}

	void StyleLabel(wxStaticText* label, const wxColour& colour) {
		label->SetForegroundColour(colour);
		label->SetBackgroundColour(Theme::GetDark(Theme::Role::Surface));
	}
}

NewMapTabDialog::NewMapTabDialog(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "New Tab", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
	const wxColour surface = Theme::GetDark(Theme::Role::Surface);
	const wxColour raised = Theme::GetDark(Theme::Role::RaisedSurface);
	const wxColour text = Theme::GetDark(Theme::Role::Text);
	const wxColour subtle = Theme::GetDark(Theme::Role::TextSubtle);
	const wxColour accent(116, 76, 238);
	SetBackgroundColour(surface);

	auto* root = newd wxBoxSizer(wxVERTICAL);
	auto* heading = newd wxStaticText(this, wxID_ANY, "Create an independent map tab");
	wxFont headingFont = heading->GetFont();
	headingFont.SetWeight(wxFONTWEIGHT_BOLD);
	headingFont.SetPointSize(headingFont.GetPointSize() + 1);
	heading->SetFont(headingFont);
	StyleLabel(heading, text);
	root->Add(heading, 0, wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 16));
	auto* explanation = newd wxStaticText(this, wxID_ANY, "Reuse the active resources or load another client and server without changing existing tabs.");
	StyleLabel(explanation, subtle);
	root->Add(explanation, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FROM_DIP(this, 16));

	auto* modes = newd wxBoxSizer(wxHORIZONTAL);
	const WorkspaceClientSelection& client = g_workspace.getClient();
	const ServerWorkspace& server = g_workspace.getServer();
	const bool hasCurrentResources = g_gui.IsVersionLoaded();
	const wxString currentVersion = client.valid
		? client.versionName
		: (hasCurrentResources ? wxstr(g_gui.GetCurrentVersion().getName()) : wxString("No active workspace"));
	const wxString currentDetails = "Use Current Client\n" + currentVersion
		+ (server.serverProfile.empty() ? wxString {} : "  |  " + wxstr(server.serverProfile));
	currentButton = newd wxToggleButton(this, wxID_ANY, currentDetails, wxDefaultPosition, FROM_DIP(this, wxSize(230, 76)));
	newButton = newd wxToggleButton(this, wxID_ANY, "Load New\nAnother client, items and server", wxDefaultPosition, FROM_DIP(this, wxSize(230, 76)));
	for (wxToggleButton* button : { currentButton, newButton }) {
		button->SetForegroundColour(text);
		button->SetBackgroundColour(raised);
		button->SetMinSize(FROM_DIP(this, wxSize(230, 76)));
		modes->Add(button, 1, wxEXPAND | (button == currentButton ? wxRIGHT : 0), FROM_DIP(this, 8));
	}
	currentButton->SetToolTip(client.rootPath + "\n" + PathText(server.rootPath));
	newButton->SetToolTip("Load another DAT/SPR or appearances client with its own Server Workspace.");
	root->Add(modes, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 16));

	auto addPickerLabel = [&](const wxString& value) {
		auto* label = newd wxStaticText(this, wxID_ANY, value);
		StyleLabel(label, subtle);
		root->Add(label, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 16));
	};
	addPickerLabel("Client folder (Tibia.dat + Tibia.spr or appearances assets)");
	clientPicker = newd wxDirPickerCtrl(this, wxID_ANY, wxEmptyString, "Select the client folder", wxDefaultPosition, wxDefaultSize, wxDIRP_USE_TEXTCTRL | wxDIRP_DIR_MUST_EXIST);
	root->Add(clientPicker, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 6));
	addPickerLabel("Server root (items.otb/items.xml or appearances.dat, creatures and maps)");
	serverPicker = newd wxDirPickerCtrl(this, wxID_ANY, wxEmptyString, "Select the OT server root", wxDefaultPosition, wxDefaultSize, wxDIRP_USE_TEXTCTRL | wxDIRP_DIR_MUST_EXIST);
	root->Add(serverPicker, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 6));
	addPickerLabel("Map (.otbm/.otgz) — optional; the server's primary map is used when empty");
	mapPicker = newd wxFilePickerCtrl(this, wxID_ANY, wxEmptyString, "Select a map", "OTBM maps (*.otbm;*.otgz)|*.otbm;*.otgz", wxDefaultPosition, wxDefaultSize, wxFLP_USE_TEXTCTRL | wxFLP_FILE_MUST_EXIST | wxFLP_OPEN);
	root->Add(mapPicker, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FROM_DIP(this, 6));

	if (wxSizer* buttons = CreateSeparatedButtonSizer(wxOK | wxCANCEL)) {
		if (wxWindow* create = FindWindow(wxID_OK)) {
			create->SetLabel("Create Tab");
			create->SetForegroundColour(Theme::GetDark(Theme::Role::TextOnAccent));
			create->SetBackgroundColour(accent);
		}
		root->Add(buttons, 0, wxEXPAND | wxALL, FROM_DIP(this, 16));
	}
	SetSizerAndFit(root);
	SetMinClientSize(FROM_DIP(this, wxSize(500, 430)));
	SetClientSize(FROM_DIP(this, wxSize(540, 450)));
	CentreOnParent();

	currentButton->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) { SelectMode(true); });
	newButton->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) { SelectMode(false); });
	Bind(wxEVT_BUTTON, &NewMapTabDialog::OnCreate, this, wxID_OK);
	SelectMode(true);
}

void NewMapTabDialog::SelectMode(bool useCurrentClient) {
	useCurrent = useCurrentClient;
	currentButton->SetValue(useCurrent);
	newButton->SetValue(!useCurrent);
	clientPicker->Enable(!useCurrent);
	serverPicker->Enable(!useCurrent);
	if (wxWindow* create = FindWindow(wxID_OK)) {
		create->Enable(!useCurrent || g_gui.IsVersionLoaded());
	}
}

void NewMapTabDialog::OnCreate(wxCommandEvent&) {
	if (!useCurrent && (clientPicker->GetPath().empty() || serverPicker->GetPath().empty())) {
		wxMessageBox("Select both the client folder and the server root.", "New Tab", wxOK | wxICON_WARNING, this);
		return;
	}
	EndModal(wxID_OK);
}

NewMapTabSelection NewMapTabDialog::GetSelection() const {
	return {
		useCurrent,
		clientPicker->GetPath(),
		serverPicker->GetPath(),
		mapPicker->GetPath(),
	};
}
