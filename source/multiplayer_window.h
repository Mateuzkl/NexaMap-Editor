// NexaMap multiplayer. SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MULTIPLAYER_WINDOW_H
#define NEXAMAP_MULTIPLAYER_WINDOW_H
#include <wx/frame.h>
#include <wx/listctrl.h>
#include <wx/textctrl.h>
#include "multiplayer_session.h"
class MultiplayerWindow final : public wxFrame {
public:
    MultiplayerWindow(wxWindow* parent, MultiplayerSession& session);
    ~MultiplayerWindow() override;
    void update();
    void detach() { session = nullptr; }
    static bool configure(wxWindow* parent, bool host, MultiplayerSession::Options& options);
private:
    MultiplayerSession* session;
    wxListCtrl* players;
    wxListCtrl* approvals;
    wxTextCtrl* chat;
    wxTextCtrl* input;
    wxStaticText* diagnostics;
    std::string displayedLog;
    uint64_t selectedApproval() const;
    uint32_t selectedPlayer() const;
};
#endif
