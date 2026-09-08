//////////////////////////////////////////////////////////////////////
// Visual editor for reusable server Lua combat-area matrices.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_SPELL_AREA_EDITOR_DIALOG_H_
#define NEXAMAP_SPELL_AREA_EDITOR_DIALOG_H_

#include "spell_area_library.h"

#include <memory>
#include <wx/dialog.h>

class SpellAreaGridPanel;
class SpellAreaResolver;
class wxChoice;
class wxSpinCtrl;
class wxStaticText;
class wxTextCtrl;

class SpellAreaEditorDialog final : public wxDialog {
public:
	SpellAreaEditorDialog(
		wxWindow* parent,
		const ServerWorkspace& workspace,
		std::shared_ptr<const SpellAreaResolver> resolver
	);

	[[nodiscard]] const SpellAreaLibrarySaveResult& saveResult() const;

private:
	void resizeGrid();
	void saveArea();

	const ServerWorkspace& workspace;
	std::shared_ptr<const SpellAreaResolver> resolver;
	std::vector<SpellAreaLibraryTarget> targets;
	SpellAreaLibrarySaveResult result;
	wxTextCtrl* name = nullptr;
	wxChoice* target = nullptr;
	wxSpinCtrl* width = nullptr;
	wxSpinCtrl* height = nullptr;
	SpellAreaGridPanel* grid = nullptr;
	wxStaticText* status = nullptr;
};

#endif // NEXAMAP_SPELL_AREA_EDITOR_DIALOG_H_
