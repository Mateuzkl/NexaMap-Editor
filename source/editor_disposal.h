// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_EDITOR_DISPOSAL_H
#define NEXAMAP_EDITOR_DISPOSAL_H

#include "editor.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#ifdef __linux__
#include <malloc.h>
#endif

// GUI-owned service. Submit only after every view and the multiplayer session
// have been destroyed on the GUI thread. Remaining destructors free pure data.
class EditorDisposalQueue final {
public:
	EditorDisposalQueue() :
		worker([this] { run(); }) { }
	~EditorDisposalQueue() {
		{
			std::lock_guard lock(mutex);
			stopping = true;
		}
		ready.notify_one();
		worker.join(); // Drain all maps before GUI/application globals disappear.
	}

	void submit(std::unique_ptr<Editor> editor) {
		{
			std::lock_guard lock(mutex);
			pending.push_back(std::move(editor));
		}
		ready.notify_one();
	}

private:
	void run() {
		for (;;) {
			std::unique_ptr<Editor> editor;
			{
				std::unique_lock lock(mutex);
				ready.wait(lock, [this] { return stopping || !pending.empty(); });
				if (pending.empty()) {
					return;
				}
				editor = std::move(pending.front());
				pending.pop_front();
			}
			editor.reset();
#ifdef __linux__
			malloc_trim(0);
#endif
		}
	}
	std::mutex mutex;
	std::condition_variable ready;
	std::deque<std::unique_ptr<Editor>> pending;
	bool stopping = false;
	std::jthread worker;
};

#endif
