//////////////////////////////////////////////////////////////////////
// Searchable browser for active-client magic effects and missiles.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_SPELL_VISUAL_BROWSER_DIALOG_H_
#define NEXAMAP_SPELL_VISUAL_BROWSER_DIALOG_H_

#include "server_visual_catalog.h"

#include <wx/dialog.h>

#include <optional>
#include <string>
#include <vector>

class wxListCtrl;
class wxSearchCtrl;
class wxStaticText;

class SpellVisualBrowserDialog final : public wxDialog {
public:
	SpellVisualBrowserDialog(
		wxWindow* parent,
		ServerVisualKind kind,
		const ServerVisualCatalog& catalog,
		const std::string& currentValue
	);

	[[nodiscard]] std::optional<std::string> selectedValue() const;

private:
	void rebuildList();
	void updateSelection();
	void openSelection();

	ServerVisualKind kind;
	std::vector<ServerVisualConstant> values;
	std::vector<std::size_t> visible;
	wxSearchCtrl* search = nullptr;
	wxListCtrl* list = nullptr;
	wxStaticText* details = nullptr;
	wxWindow* preview = nullptr;
	wxWindow* openButton = nullptr;
};

#endif // NEXAMAP_SPELL_VISUAL_BROWSER_DIALOG_H_
