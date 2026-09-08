//////////////////////////////////////////////////////////////////////
// Searchable browser shared by Server Workspace content editors.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_SERVER_CONTENT_BROWSER_DIALOG_H_
#define NEXAMAP_SERVER_CONTENT_BROWSER_DIALOG_H_

#include "server_content_index.h"

#include <wx/dialog.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class ServerContentBrowserListCtrl;
class wxSearchCtrl;
class wxStaticText;
class wxTimer;
class wxTimerEvent;

class ServerContentBrowserDialog final : public wxDialog {
public:
	ServerContentBrowserDialog(
		wxWindow* parent,
		const wxString& title,
		const wxString& noun,
		std::filesystem::path contentRoot,
		std::vector<ServerContentSource> sources,
		bool allowCreate = true
	);
	ServerContentBrowserDialog(
		wxWindow* parent,
		const wxString& title,
		const wxString& noun,
		std::filesystem::path contentRoot,
		std::shared_ptr<const std::vector<ServerContentSource>> sources,
		std::vector<std::size_t> sourceIndices,
		bool allowCreate = true
	);

	[[nodiscard]] bool wantsCreate() const;
	[[nodiscard]] const ServerContentSource* selectedSource() const;

private:
	void rebuildList();
	void onFilterTimer(wxTimerEvent& event);
	void updateSelection();
	void openSelection();
	[[nodiscard]] wxString rowText(long row, long column) const;
	void createControls(const wxString& noun, bool allowCreate);

	std::filesystem::path contentRoot;
	std::shared_ptr<const std::vector<ServerContentSource>> sources;
	std::vector<std::size_t> sourceIndices;
	std::vector<std::string> searchKeys;
	std::vector<std::filesystem::path> relativePaths;
	std::vector<std::size_t> visible;
	wxSearchCtrl* search = nullptr;
	ServerContentBrowserListCtrl* list = nullptr;
	wxStaticText* details = nullptr;
	wxWindow* openButton = nullptr;
	std::unique_ptr<wxTimer> filterTimer;
	bool create = false;

	friend class ServerContentBrowserListCtrl;
};

#endif // NEXAMAP_SERVER_CONTENT_BROWSER_DIALOG_H_
