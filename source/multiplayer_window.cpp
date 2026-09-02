// NexaMap multiplayer. SPDX-License-Identifier: GPL-3.0-or-later
#include "main.h"
#include "multiplayer_window.h"
#include "gui.h"
#include "application.h"
#include "editor.h"
#include "map_tab.h"
#include <wx/notebook.h>
#include <wx/numdlg.h>
#include <wx/choicdlg.h>

bool MultiplayerWindow::configure(wxWindow* parent, bool hosting, MultiplayerSession::Options& options) {
	wxDialog dialog(parent, wxID_ANY, hosting ? "Host multiplayer session" : "Join multiplayer session", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	auto* layout = new wxBoxSizer(wxVERTICAL);
	auto* form = new wxFlexGridSizer(2, 8, 12);
	form->AddGrowableCol(1);
	auto field = [&](const wxString& label, wxWindow* input) { form->Add(new wxStaticText(&dialog, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL); form->Add(input, 1, wxEXPAND); };
	auto* name = new wxTextCtrl(&dialog, wxID_ANY, wxstr(options.name));
	name->SetMaxLength(Multiplayer::MaxName);
	field("Your name", name);
	wxTextCtrl* address = nullptr;
	if (!hosting) {
		address = new wxTextCtrl(&dialog, wxID_ANY, wxstr(options.address));
		field("Host / IP", address);
	}
	auto* port = new wxSpinCtrl(&dialog, wxID_ANY);
	port->SetRange(1, 65535);
	port->SetValue(options.port);
	field("Port", port);
	auto* password = new wxTextCtrl(&dialog, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	password->SetMaxLength(256);
	field("Session password", password);
	wxSpinCtrl* maxPlayers = nullptr;
	wxChoice* role = nullptr;
	wxCheckBox* approvals = nullptr;
	wxSpinCtrl* autosave = nullptr;
	if (hosting) {
		maxPlayers = new wxSpinCtrl(&dialog, wxID_ANY);
		maxPlayers->SetRange(2, Multiplayer::MaxPlayers);
		maxPlayers->SetValue(options.maxPlayers);
		field("Maximum players (including host)", maxPlayers);
		role = new wxChoice(&dialog, wxID_ANY);
		role->Append("Editor");
		role->Append("Reviewer");
		role->Append("Viewer");
		role->SetSelection(0);
		field("New participant role", role);
		approvals = new wxCheckBox(&dialog, wxID_ANY, "Require host approval for IDs and metadata");
		approvals->SetValue(true);
		field("Approvals", approvals);
		autosave = new wxSpinCtrl(&dialog, wxID_ANY);
		autosave->SetRange(0, 120);
		autosave->SetValue(options.autosaveMinutes);
		field("Backup interval in minutes (0 = off)", autosave);
	}
	layout->Add(form, 1, wxEXPAND | wxALL, 16);
	auto* help = new wxStaticText(&dialog, wxID_ANY, "Use the same NexaMap build and client/item definitions on every PC.\nConnect over your LAN or VPN. Password authentication does not encrypt map/chat traffic.");
	help->Wrap(510);
	layout->Add(help, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16);
	layout->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);
	dialog.SetSizerAndFit(layout);
	dialog.SetMinSize(dialog.GetSize());
	dialog.CentreOnParent();
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}
	options.name = nstr(name->GetValue());
	options.password = nstr(password->GetValue());
	options.port = static_cast<uint16_t>(port->GetValue());
	if (address) {
		options.address = nstr(address->GetValue());
	}
	if (hosting) {
		options.maxPlayers = maxPlayers->GetValue();
		options.defaultRole = static_cast<Multiplayer::Role>(role->GetSelection() + 1);
		options.approvals = approvals->GetValue();
		options.autosaveMinutes = autosave->GetValue();
	}
	return true;
}
MultiplayerWindow::MultiplayerWindow(wxWindow* parent, MultiplayerSession& live) :
	wxFrame(parent, wxID_ANY, "NexaMap Multiplayer", wxDefaultPosition, wxSize(850, 600)), session(&live) {
	auto* panel = new wxPanel(this);
	auto* layout = new wxBoxSizer(wxVERTICAL);
	diagnostics = new wxStaticText(panel, wxID_ANY, "");
	layout->Add(diagnostics, 0, wxEXPAND | wxALL, 12);
	auto* book = new wxNotebook(panel, wxID_ANY);
	auto* playerPage = new wxPanel(book);
	auto* playerLayout = new wxBoxSizer(wxVERTICAL);
	players = new wxListCtrl(playerPage, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	players->AppendColumn("Name", wxLIST_FORMAT_LEFT, 180);
	players->AppendColumn("Role", wxLIST_FORMAT_LEFT, 100);
	players->AppendColumn("Client ID", wxLIST_FORMAT_LEFT, 80);
	players->AppendColumn("Latency", wxLIST_FORMAT_LEFT, 100);
	players->AppendColumn("Status", wxLIST_FORMAT_LEFT, 120);
	players->AppendColumn("Position", wxLIST_FORMAT_LEFT, 140);
	playerLayout->Add(players, 1, wxEXPAND | wxALL, 8);
	auto* buttons = new wxBoxSizer(wxHORIZONTAL);
	auto button = [](wxWindow* parent, wxSizer* layout, const wxString& title, auto callback) { auto* b = new wxButton(parent, wxID_ANY, title); b->Bind(wxEVT_BUTTON, callback); layout->Add(b, 0, wxRIGHT, 6); return b; };
	button(playerPage, buttons, "Jump to player", [this](wxCommandEvent&) { if (!session){ return;
} auto p = session->players().find(selectedPlayer()); if (p != session->players().end() && g_gui.GetCurrentEditor() == &session->getEditor()){ g_gui.SetScreenCenterPosition(p->second.cursor);
} });
	auto* role = button(playerPage, buttons, "Change role", [this](wxCommandEvent&) {
		if (!session) {
			return;
		}
		const auto id = selectedPlayer();
		wxArrayString roles;
		roles.Add("Editor");
		roles.Add("Reviewer");
		roles.Add("Viewer");
		wxSingleChoiceDialog dialog(this, "Choose participant permissions", "Change role", roles);
		if (dialog.ShowModal() == wxID_OK && session) {
			session->changeRole(id, static_cast<Multiplayer::Role>(dialog.GetSelection() + 1));
		}
	});
	role->Enable(live.isHost());
	auto* kick = button(playerPage, buttons, "Kick", [this](wxCommandEvent&) { if (session){ session->kick(selectedPlayer());
} });
	kick->Enable(live.isHost());
	button(playerPage, buttons, "Resync", [this](wxCommandEvent&) { if (session){ session->requestResync();
} });
	playerLayout->Add(buttons, 0, wxALL, 8);
	playerPage->SetSizer(playerLayout);
	book->AddPage(playerPage, "Players & diagnostics");
	auto* chatPage = new wxPanel(book);
	auto* chatLayout = new wxBoxSizer(wxVERTICAL);
	chat = new wxTextCtrl(chatPage, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
	input = new wxTextCtrl(chatPage, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	input->SetMaxLength(Multiplayer::MaxChat);
	auto send = [this](wxCommandEvent&) { if (session && !input->GetValue().empty()) { session->sendChat(nstr(input->GetValue())); input->Clear(); update(); } };
	input->Bind(wxEVT_TEXT_ENTER, send);
	chatLayout->Add(chat, 1, wxEXPAND | wxALL, 8);
	auto* sendLayout = new wxBoxSizer(wxHORIZONTAL);
	sendLayout->Add(input, 1, wxRIGHT, 8);
	button(chatPage, sendLayout, "Send", send);
	chatLayout->Add(sendLayout, 0, wxEXPAND | wxALL, 8);
	chatPage->SetSizer(chatLayout);
	book->AddPage(chatPage, "Chat & log");
	auto* approvalPage = new wxPanel(book);
	auto* approvalLayout = new wxBoxSizer(wxVERTICAL);
	approvals = new wxListCtrl(approvalPage, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	approvals->AppendColumn("Request", wxLIST_FORMAT_LEFT, 70);
	approvals->AppendColumn("Requester", wxLIST_FORMAT_LEFT, 150);
	approvals->AppendColumn("Type", wxLIST_FORMAT_LEFT, 270);
	approvals->AppendColumn("Position", wxLIST_FORMAT_LEFT, 140);
	approvals->AppendColumn("Status", wxLIST_FORMAT_LEFT, 100);
	approvalLayout->Add(approvals, 1, wxEXPAND | wxALL, 8);
	auto* approvalButtons = new wxBoxSizer(wxHORIZONTAL);
	button(approvalPage, approvalButtons, "Jump to location", [this](wxCommandEvent&) { if (!session){ return;
} auto found = session->approvalRequests().find(selectedApproval()); if (found != session->approvalRequests().end() && !found->second.transaction.tiles.empty()) { auto key = found->second.transaction.tiles.front().key; g_gui.SetScreenCenterPosition({Multiplayer::tileX(key), Multiplayer::tileY(key), Multiplayer::tileZ(key)}); } });
	button(approvalPage, approvalButtons, "Approve", [this](wxCommandEvent&) { if (session){ session->approve(selectedApproval(), true);
} });
	button(approvalPage, approvalButtons, "Reject", [this](wxCommandEvent&) { if (session){ session->approve(selectedApproval(), false);
} });
	button(approvalPage, approvalButtons, "Approve with new UID", [this](wxCommandEvent&) { if (!session){ return;
} const auto request = selectedApproval(); auto id = wxGetNumberFromUser("Reserve a new Unique ID for the requested item", "Unique ID", "Approve with new ID", 1000, 1, 65535, this); if (id > 0 && session){ session->approve(request, true, static_cast<uint16_t>(id));
} });
	approvalLayout->Add(approvalButtons, 0, wxALL, 8);
	approvalPage->SetSizer(approvalLayout);
	approvalPage->Enable(live.isHost());
	book->AddPage(approvalPage, "Approvals & reviews");
	layout->Add(book, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
	panel->SetSizer(layout);
	Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) { if (event.CanVeto()) { Hide(); event.Veto(); } else { if (session){ session->window = nullptr;
} session = nullptr; event.Skip(); } });
	SetMinSize(wxSize(720, 480));
	CentreOnParent();
}
MultiplayerWindow::~MultiplayerWindow() {
	if (session) {
		session->window = nullptr;
	}
}
uint32_t MultiplayerWindow::selectedPlayer() const {
	auto index = players->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	return index < 0 ? UINT32_MAX : static_cast<uint32_t>(players->GetItemData(index));
}
uint64_t MultiplayerWindow::selectedApproval() const {
	auto index = approvals->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (index < 0) {
		return 0;
	}
	unsigned long long id = 0;
	approvals->GetItemText(index).ToULongLong(&id);
	return id;
}
void MultiplayerWindow::update() {
	if (!session) {
		return;
	}
	auto selected = selectedPlayer();
	auto selectedRequest = selectedApproval();
	players->Freeze();
	players->DeleteAllItems();
	for (const auto& [id, p] : session->players()) {
		auto row = players->InsertItem(players->GetItemCount(), wxstr(p.name));
		players->SetItemData(row, id);
		players->SetItem(row, 1, Multiplayer::roleName(p.role));
		players->SetItem(row, 2, std::to_string(id));
		players->SetItem(row, 3, std::to_string(p.latency) + " ms");
		players->SetItem(row, 4, p.afk ? "AFK" : "Active");
		players->SetItem(row, 5, wxString::Format("%d, %d, %d", p.cursor.x, p.cursor.y, p.cursor.z));
		if (id == selected) {
			players->SetItemState(row, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
		}
	}
	players->Thaw();
	approvals->Freeze();
	approvals->DeleteAllItems();
	for (const auto& [id, a] : session->approvalRequests()) {
		auto row = approvals->InsertItem(approvals->GetItemCount(), std::to_string(id));
		approvals->SetItem(row, 1, wxstr(a.requester));
		approvals->SetItem(row, 2, wxstr(a.description));
		if (!a.transaction.tiles.empty()) {
			auto key = a.transaction.tiles.front().key;
			approvals->SetItem(row, 3, wxString::Format("%u, %u, %u", Multiplayer::tileX(key), Multiplayer::tileY(key), Multiplayer::tileZ(key)));
		}
		approvals->SetItem(row, 4, "Pending");
		if (id == selectedRequest) {
			approvals->SetItemState(row, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
		}
	}
	approvals->Thaw();
	std::string log;
	for (const auto& line : session->messages()) {
		log += line + '\n';
	}
	if (log != displayedLog) {
		displayedLog = log;
		chat->ChangeValue(wxstr(log));
		chat->ShowPosition(chat->GetLastPosition());
	}
	diagnostics->SetLabel(wxstr(session->status() + " | Revision " + std::to_string(session->revision()) + " | Queued " + std::to_string(session->pendingBytes() / 1024) + " KiB | Locks " + std::to_string(session->locks().size()) + "\nSession " + session->sessionId() + " | Port " + std::to_string(session->settings().port)));
}
