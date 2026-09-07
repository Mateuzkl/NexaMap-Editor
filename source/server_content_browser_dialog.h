//////////////////////////////////////////////////////////////////////
// Searchable browser shared by Server Workspace content editors.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_SERVER_CONTENT_BROWSER_DIALOG_H_
#define NEXAMAP_SERVER_CONTENT_BROWSER_DIALOG_H_

#include "server_content_index.h"

#include <wx/dialog.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class wxListCtrl;
class wxSearchCtrl;
class wxStaticText;

class ServerContentBrowserDialog final : public wxDialog {
public:
	ServerContentBrowserDialog(
		wxWindow* parent,
		const wxString& title,
		const wxString& noun,
		std::filesystem::path contentRoot,
		std::vector<ServerContentSource> sources
	);

	[[nodiscard]] bool wantsCreate() const;
	[[nodiscard]] std::optional<ServerContentSource> selectedSource() const;

private:
	void rebuildList();
	void updateSelection();
	void openSelection();

	std::filesystem::path contentRoot;
	std::vector<ServerContentSource> sources;
	std::vector<std::size_t> visible;
	wxSearchCtrl* search = nullptr;
	wxListCtrl* list = nullptr;
	wxStaticText* details = nullptr;
	wxWindow* openButton = nullptr;
	bool create = false;
};

#endif // NEXAMAP_SERVER_CONTENT_BROWSER_DIALOG_H_
