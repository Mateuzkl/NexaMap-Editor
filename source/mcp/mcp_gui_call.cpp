// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_gui_call.h"

#include <wx/app.h>
#include <wx/evtloop.h>
#include <wx/modalhook.h>
#include <wx/msgdlg.h>
#include <wx/thread.h>

#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>

namespace mcp {
	namespace {
		constexpr std::chrono::minutes ConfirmationTimeout { 2 };

		// wxModalDialogHook also sees native dialogs, which run outside any wx event loop.
		class ModalDialogTracker final : public wxModalDialogHook {
		public:
			[[nodiscard]] bool active() const noexcept {
				return depth_ > 0;
			}

		protected:
			int Enter(wxDialog*) override {
				++depth_;
				return wxID_NONE;
			}

			void Exit(wxDialog*) override {
				// A dialog already open at registration exits without having entered.
				if (depth_ > 0) {
					--depth_;
				}
			}

		private:
			int depth_ = 0;
		};

		ModalDialogTracker& Tracker() {
			static ModalDialogTracker tracker;
			return tracker;
		}
	}

	void InstallGuiCallGuards() {
		static std::once_flag once;
		std::call_once(once, [] { Tracker().Register(); });
	}

	bool IsModalDialogOpen() {
		const wxEventLoopBase* active = wxEventLoopBase::GetActive();
		return Tracker().active() || (active && wxTheApp && active != wxTheApp->GetMainLoop());
	}

	Json CallJsonOnGui(std::function<Json()> function, std::chrono::milliseconds timeout) {
		if (wxIsMainThread()) {
			return function();
		}
		if (!wxTheApp || !wxTheApp->IsMainLoopRunning()) {
			throw Error("NexaMap GUI is not available");
		}

		struct SharedState {
			std::mutex mutex;
			std::condition_variable_any condition;
			Json result;
			std::exception_ptr error;
			bool started = false;
			bool complete = false;
			bool canceled = false;
		};
		auto state = std::make_shared<SharedState>();
		wxTheApp->CallAfter([state, function = std::move(function)]() mutable {
			{
				std::lock_guard lock(state->mutex);
				if (state->canceled) {
					return;
				}
				state->started = true;
			}
			Json result;
			std::exception_ptr error;
			try {
				// Checked here, not when queuing: this may run inside a dialog's nested
				// event loop, where the dialog's Editor or Tile can be replaced underneath it.
				if (IsModalDialogOpen()) {
					throw Error("NexaMap has a modal dialog open; close it and retry");
				}
				result = function();
			} catch (...) {
				error = std::current_exception();
			}
			{
				std::lock_guard lock(state->mutex);
				if (state->canceled) {
					return;
				}
				state->result = std::move(result);
				state->error = error;
				state->complete = true;
			}
			state->condition.notify_one();
		});

		std::unique_lock lock(state->mutex);
		std::stop_token stopToken = RequestStopToken();
		if (!state->condition.wait_for(lock, stopToken, timeout, [&state] { return state->complete; })) {
			state->canceled = true;
			if (state->started) {
				throw Error("outcome unknown: the NexaMap GUI is still running this request and it may yet take effect; inspect the map before retrying");
			}
			throw Error("not applied: NexaMap GUI did not start the MCP request before the timeout");
		}
		if (state->error) {
			std::rethrow_exception(state->error);
		}
		return std::move(state->result);
	}

	void RequireUserConfirmation(const std::string& description) {
		const wxString message = wxString::FromUTF8("An MCP client wants to do the following:\n\n") + wxString::FromUTF8(description) + wxString::FromUTF8("\n\nAllow this?");
		Json approved;
		try {
			approved = CallJsonOnGui([message] {
				wxMessageDialog dialog(wxTheApp->GetTopWindow(), message, "NexaMap MCP confirmation", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
				return Json(dialog.ShowModal() == wxID_YES);
			},
				ConfirmationTimeout);
		} catch (const Error& error) {
			// The dialog may still be open, but its answer no longer reaches anything.
			if (std::string(error.what()).rfind("outcome unknown", 0) == 0) {
				throw Error("not applied: the user did not answer the confirmation dialog in time");
			}
			throw Error(std::string("not applied: could not ask the user: ") + error.what());
		}
		if (!approved.is_boolean() || !approved.get<bool>()) {
			throw Error("not applied: the user declined this operation in NexaMap");
		}
	}
}
