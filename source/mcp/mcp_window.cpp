// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_window.h"

#include "mcp_server.h"

#include "../settings.h"

#include <algorithm>

#include <wx/app.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace mcp {
	Window* Window::current_ = nullptr;

	void StartConfiguredServer() {
		Server& server = Server::Instance();
		server.setWriteAllowed(g_settings.getBoolean(Config::MCP_ALLOW_WRITE));
		if (g_settings.getBoolean(Config::MCP_ENABLED)) {
			const int configuredPort = std::clamp(g_settings.getInteger(Config::MCP_PORT), 1, 65535);
			server.start(static_cast<uint16_t>(configuredPort));
		}
	}

	void StopServer() {
		Server::Instance().stop();
	}

	void Window::Open(wxWindow* parent) {
		if (current_) {
			current_->Show();
			current_->Raise();
			return;
		}
		current_ = new Window(parent);
		current_->Show();
	}

	Window::Window(wxWindow* parent) :
		wxDialog(parent, wxID_ANY, "NexaMap MCP Server", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
		alive_(std::make_shared<std::atomic<bool>>(true)) {
		auto* root = new wxBoxSizer(wxVERTICAL);
		enabled_ = new wxCheckBox(this, wxID_ANY, "Enable MCP Server");
		enabled_->SetValue(Server::Instance().running());
		root->Add(enabled_, 0, wxALL, FromDIP(10));

		auto* portRow = new wxBoxSizer(wxHORIZONTAL);
		portRow->Add(new wxStaticText(this, wxID_ANY, "Port:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
		port_ = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 65535, std::clamp(g_settings.getInteger(Config::MCP_PORT), 1, 65535));
		portRow->Add(port_, 1);
		restart_ = new wxButton(this, wxID_ANY, "Restart");
		portRow->Add(restart_, 0, wxLEFT, FromDIP(8));
		root->Add(portRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));

		write_ = new wxCheckBox(this, wxID_ANY, "Allow write operations");
		write_->SetValue(Server::Instance().writeAllowed());
		write_->SetToolTip("Disabled by default. Mutating MCP tools are refused until explicitly enabled.");
		root->Add(write_, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));

		auto* usage = new wxStaticText(this, wxID_ANY, "Where to write the prompt:\n"
													   "• Inside NexaMap now: Tools > Procedural Map Generator > Preset > Generation Brief.\n"
													   "• With AI: connect this endpoint to Codex/ChatGPT/Claude, then write the prompt in the AI chat.");
		usage->Wrap(FromDIP(560));
		root->Add(usage, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));

		status_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
		root->Add(status_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
		log_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(560, 240)), wxTE_MULTILINE | wxTE_READONLY);
		root->Add(log_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));

		auto* buttons = new wxBoxSizer(wxHORIZONTAL);
		copyCodex_ = new wxButton(this, wxID_ANY, "Copy Codex Command");
		buttons->Add(copyCodex_, 0, wxRIGHT, FromDIP(8));
		copy_ = new wxButton(this, wxID_ANY, "Copy JSON Config");
		buttons->Add(copy_, 0);
		buttons->AddStretchSpacer();
		close_ = new wxButton(this, wxID_CANCEL, "Close");
		buttons->Add(close_, 0);
		root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
		SetSizerAndFit(root);
		SetMinSize(FromDIP(wxSize(600, 430)));
		CentreOnParent();

		enabled_->Bind(wxEVT_CHECKBOX, &Window::onEnable, this);
		write_->Bind(wxEVT_CHECKBOX, &Window::onWrite, this);
		restart_->Bind(wxEVT_BUTTON, &Window::onRestart, this);
		copy_->Bind(wxEVT_BUTTON, &Window::onCopyConfig, this);
		copyCodex_->Bind(wxEVT_BUTTON, &Window::onCopyCodexCommand, this);
		close_->Bind(wxEVT_BUTTON, &Window::onClose, this);
		const std::weak_ptr<std::atomic<bool>> weakAlive = alive_;
		Server::Instance().setLogCallback([this, weakAlive](Server::LogLevel level, const std::string& message) {
			const auto alive = weakAlive.lock();
			if (!alive || !*alive || !wxTheApp) {
				return;
			}
			wxTheApp->CallAfter([this, weakAlive, level, message] {
				const auto stillAlive = weakAlive.lock();
				if (stillAlive && *stillAlive) {
					appendLog(level == Server::LogLevel::Error, message);
					refreshStatus();
				}
			});
		});
		refreshStatus();
	}

	Window::~Window() {
		*alive_ = false;
		Server::Instance().setLogCallback({});
		current_ = nullptr;
	}

	void Window::refreshStatus() {
		Server& server = Server::Instance();
		if (server.running()) {
			status_->SetLabel("Status: " + wxString::FromUTF8(server.endpoint()) + (server.writeAllowed() ? " (writes allowed)" : " (read-only)"));
		} else if (!server.lastError().empty()) {
			status_->SetLabel("Status: stopped — " + wxString::FromUTF8(server.lastError()));
		} else {
			status_->SetLabel("Status: stopped");
		}
		enabled_->SetValue(server.running());
	}

	void Window::appendLog(bool error, const std::string& message) {
		log_->AppendText((error ? "[error] " : "[info] ") + wxString::FromUTF8(message) + "\n");
	}

	void Window::onEnable(wxCommandEvent& WXUNUSED(event)) {
		Server& server = Server::Instance();
		if (enabled_->GetValue()) {
			g_settings.setInteger(Config::MCP_PORT, port_->GetValue());
			if (!server.start(static_cast<uint16_t>(port_->GetValue()))) {
				appendLog(true, server.lastError());
				enabled_->SetValue(false);
			}
		} else {
			server.stop();
		}
		g_settings.setInteger(Config::MCP_ENABLED, server.running());
		g_settings.save();
		refreshStatus();
	}

	void Window::onWrite(wxCommandEvent& WXUNUSED(event)) {
		Server::Instance().setWriteAllowed(write_->GetValue());
		g_settings.setInteger(Config::MCP_ALLOW_WRITE, write_->GetValue());
		g_settings.save();
		appendLog(false, write_->GetValue() ? "MCP write operations enabled by user" : "MCP returned to read-only mode");
		refreshStatus();
	}

	void Window::onRestart(wxCommandEvent& WXUNUSED(event)) {
		Server& server = Server::Instance();
		server.stop();
		g_settings.setInteger(Config::MCP_PORT, port_->GetValue());
		if (!server.start(static_cast<uint16_t>(port_->GetValue()))) {
			appendLog(true, server.lastError());
		}
		g_settings.setInteger(Config::MCP_ENABLED, server.running());
		g_settings.save();
		refreshStatus();
	}

	void Window::onCopyConfig(wxCommandEvent& WXUNUSED(event)) {
		const std::string endpoint = Server::Instance().running() ? Server::Instance().endpoint() : "http://127.0.0.1:" + std::to_string(port_->GetValue()) + "/mcp";
		const Json config { { "mcpServers", { { "nexamap", { { "type", "http" }, { "url", endpoint } } } } } };
		if (wxTheClipboard->Open()) {
			wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(config.dump(2))));
			wxTheClipboard->Close();
			appendLog(false, "MCP client configuration copied to clipboard");
		}
	}

	void Window::onCopyCodexCommand(wxCommandEvent& WXUNUSED(event)) {
		const std::string endpoint = Server::Instance().running() ? Server::Instance().endpoint() : "http://127.0.0.1:" + std::to_string(port_->GetValue()) + "/mcp";
		const std::string command = "codex mcp add nexamap --url " + endpoint;
		if (wxTheClipboard->Open()) {
			wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(command)));
			wxTheClipboard->Close();
			appendLog(false, "Codex MCP command copied to clipboard");
		}
	}

	void Window::onClose(wxCommandEvent& WXUNUSED(event)) {
		Destroy();
	}
}
