// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MCP_WINDOW_H_
#define NEXAMAP_MCP_WINDOW_H_

#include <wx/dialog.h>

#include <atomic>
#include <memory>

class wxButton;
class wxCheckBox;
class wxCommandEvent;
class wxSpinCtrl;
class wxStaticText;
class wxTextCtrl;
class wxWindow;

namespace mcp {
	void StartConfiguredServer();
	void StopServer();

	class Window final : public wxDialog {
	public:
		static void Open(wxWindow* parent);
		~Window() override;

	private:
		explicit Window(wxWindow* parent);
		void refreshStatus();
		void appendLog(bool error, const std::string& message);
		void onEnable(wxCommandEvent& event);
		void onWrite(wxCommandEvent& event);
		void onRestart(wxCommandEvent& event);
		void onCopyConfig(wxCommandEvent& event);
		void onCopyCodexCommand(wxCommandEvent& event);
		void onClose(wxCommandEvent& event);

		static Window* current_;
		std::shared_ptr<std::atomic<bool>> alive_;
		wxCheckBox* enabled_ = nullptr;
		wxSpinCtrl* port_ = nullptr;
		wxCheckBox* write_ = nullptr;
		wxStaticText* status_ = nullptr;
		wxButton* restart_ = nullptr;
		wxButton* copy_ = nullptr;
		wxButton* copyCodex_ = nullptr;
		wxButton* close_ = nullptr;
		wxTextCtrl* log_ = nullptr;
	};
}

#endif
