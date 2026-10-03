// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_gui_call.h"

#include <wx/app.h>
#include <wx/thread.h>

#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>

namespace mcp {
	Json CallJsonOnGui(std::function<Json()> function, std::chrono::milliseconds timeout) {
		if (wxIsMainThread()) {
			return function();
		}
		if (!wxTheApp || !wxTheApp->IsMainLoopRunning()) {
			throw Error("NexaMap GUI is not available");
		}

		struct SharedState {
			std::mutex mutex;
			std::condition_variable condition;
			Json result;
			std::exception_ptr error;
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
			}
			Json result;
			std::exception_ptr error;
			try {
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
		if (!state->condition.wait_for(lock, timeout, [&state] { return state->complete; })) {
			state->canceled = true;
			throw Error("NexaMap GUI did not answer the MCP request before the timeout");
		}
		if (state->error) {
			std::rethrow_exception(state->error);
		}
		return std::move(state->result);
	}
}
