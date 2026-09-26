// SPDX-License-Identifier: GPL-3.0-or-later
// Dedicated full-editor harness for Quick Replace validation.
#include "main.h"

#include "copybuffer.h"
#include "editor.h"

#include <iostream>
#include <wx/evtloop.h>

Editor::Editor(CopyBuffer& buffer, std::nullptr_t) :
	actionQueue(new ActionQueue(*this)), selection(*this), copybuffer(buffer), replace_brush(nullptr) {
	map.mapVersion = { MAP_OTBM_4, static_cast<ClientVersionID>(17) };
	map.setWidth(512);
	map.setHeight(512);
	map.setName("Quick Replace test fixture");
	map.unnamed = true;
}

void RunQuickReplaceSelectionTests();

int RunMultiplayerSessionTests(int argc, char** argv) {
	if (!wxEntryStart(argc, argv)) {
		return 1;
	}
	wxTheApp->SetExitOnFrameDelete(false);
	wxLog::SetActiveTarget(new wxLogStderr());
	wxSetAssertHandler([](const wxString& file, int line, const wxString&, const wxString& condition, const wxString& message) {
		std::cerr << "wx assertion: " << file.ToStdString() << ':' << line << ' ' << condition.ToStdString() << ' ' << message.ToStdString() << std::endl;
		std::abort();
	});

	int result = 0;
	{
		wxEventLoop loop;
		wxEventLoopActivator activate(&loop);
		try {
			RunQuickReplaceSelectionTests();
		} catch (const std::exception& error) {
			std::cerr << "FAIL " << error.what() << std::endl;
			result = 1;
		}
	}
	wxEntryCleanup();
	return result;
}
