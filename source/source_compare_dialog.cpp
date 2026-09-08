//////////////////////////////////////////////////////////////////////
// Side-by-side comparison for externally changed editor sources.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "source_compare_dialog.h"

#include "theme.h"

#include <wx/dialog.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace {
	wxString PathText(const std::filesystem::path& path) {
#ifdef __WINDOWS__
		return wxString(path.wstring());
#else
		return wxString::FromUTF8(path.string());
#endif
	}
}

SourceConflictChoice ShowSourceConflictDialog(wxWindow* parent, const std::vector<EditorSourceChange>& changes) {
	if (changes.empty()) {
		return SourceConflictChoice::KeepEditing;
	}
	const EditorSourceChange& change = changes.front();
	wxDialog dialog(parent, wxID_ANY, "Source changed outside NexaMap", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	dialog.SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	auto* root = newd wxBoxSizer(wxVERTICAL);
	wxString description = "The source changed after this editor opened. Autosave is paused to protect both versions.\n" + PathText(change.path);
	if (changes.size() > 1) {
		description += wxString::Format("\n%zu source files changed; the first is shown below.", changes.size());
	}
	root->Add(newd wxStaticText(&dialog, wxID_ANY, description), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
	auto* comparison = newd wxBoxSizer(wxHORIZONTAL);
	const auto addVersion = [&](const wxString& title, const std::string& text) {
		auto* column = newd wxBoxSizer(wxVERTICAL);
		column->Add(newd wxStaticText(&dialog, wxID_ANY, title), 0, wxBOTTOM, dialog.FromDIP(5));
		auto* source = newd wxTextCtrl(&dialog, wxID_ANY, wxString::FromUTF8(text), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
		source->SetFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE));
		column->Add(source, 1, wxEXPAND);
		comparison->Add(column, 1, wxEXPAND | wxRIGHT, dialog.FromDIP(8));
	};
	addVersion("Loaded in editor", change.loadedText);
	addVersion(change.deleted ? "Current disk version (deleted)" : "Current disk version", change.diskText);
	root->Add(comparison, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(10));
	auto* buttons = newd wxBoxSizer(wxHORIZONTAL);
	buttons->AddStretchSpacer();
	buttons->Add(newd wxButton(&dialog, wxID_CANCEL, "Keep Editing"), 0, wxRIGHT, dialog.FromDIP(8));
	buttons->Add(newd wxButton(&dialog, wxID_OK, "Close and Reopen Browser"), 0);
	root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(10));
	dialog.SetSizer(root);
	dialog.SetMinSize(dialog.FromDIP(wxSize(760, 480)));
	dialog.SetSize(dialog.FromDIP(wxSize(1040, 700)));
	dialog.CentreOnParent();
	return dialog.ShowModal() == wxID_OK ? SourceConflictChoice::Reopen : SourceConflictChoice::KeepEditing;
}
