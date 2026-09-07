//////////////////////////////////////////////////////////////////////
// Native source-preserving editor for indexed global spells.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_SPELL_EDITOR_DIALOG_H_
#define NEXAMAP_SPELL_EDITOR_DIALOG_H_

#include "spell_definition.h"

#include <wx/dialog.h>

#include <array>
#include <memory>

class MonsterSpellPreview;
class wxCloseEvent;
class wxFlexGridSizer;
class wxStaticText;
class wxTextCtrl;
class wxWindow;

class SpellEditorDialog final : public wxDialog {
public:
	SpellEditorDialog(wxWindow* parent, std::unique_ptr<SpellDefinitionDocument> document);

	[[nodiscard]] bool wasSaved() const;
	[[nodiscard]] const SpellDefinition& savedDefinition() const;

private:
	wxWindow* addText(wxWindow* parent, wxFlexGridSizer* grid, SpellField field, const std::string& value, const wxArrayString& choices = {});
	wxWindow* addNumber(wxWindow* parent, wxFlexGridSizer* grid, SpellField field, int value, int maximum = 2000000000);
	wxWindow* addBoolean(wxWindow* parent, wxFlexGridSizer* grid, SpellField field, bool value);
	void applyCapability(wxWindow* control, SpellField field);
	void readControls();
	void refreshPreview();
	void updateDirtyState();
	bool confirmDiscard();
	void onFieldChanged(wxCommandEvent& event);
	void onSave(wxCommandEvent& event);
	void onCloseButton(wxCommandEvent& event);
	void onClose(wxCloseEvent& event);

	std::unique_ptr<SpellDefinitionDocument> document;
	SpellDefinition edited;
	std::array<wxWindow*, static_cast<std::size_t>(SpellField::Count)> controls {};
	MonsterSpellPreview* preview = nullptr;
	wxStaticText* areaStatus = nullptr;
	wxStaticText* saveState = nullptr;
	wxTextCtrl* declarationSource = nullptr;
	wxTextCtrl* implementationSource = nullptr;
	int direction = 0;
	bool constructing = true;
	bool saved = false;
};

#endif // NEXAMAP_SPELL_EDITOR_DIALOG_H_
