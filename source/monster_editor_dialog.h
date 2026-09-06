//////////////////////////////////////////////////////////////////////
// Native Main/Look/Source editor for an indexed monster definition.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_MONSTER_EDITOR_DIALOG_H_
#define NEXAMAP_MONSTER_EDITOR_DIALOG_H_

#include "monster_definition.h"

#include <wx/dialog.h>

#include <array>
#include <memory>
#include <string>

class wxFlexGridSizer;
class wxChoice;
class wxNotebook;
class wxSpinCtrl;
class wxStaticBitmap;
class wxTextCtrl;
class wxWindow;

class MonsterEditorDialog final : public wxDialog {
public:
	MonsterEditorDialog(wxWindow* parent, std::unique_ptr<MonsterDefinitionDocument> document);

	[[nodiscard]] bool wasSaved() const;
	[[nodiscard]] const MonsterDefinition& savedDefinition() const;

private:
	wxTextCtrl* addTextField(wxWindow* parent, wxFlexGridSizer* grid, MonsterField field, const std::string& value);
	wxSpinCtrl* addNumberField(wxWindow* parent, wxFlexGridSizer* grid, MonsterField field, int value, int minimum = 0, int maximum = 2000000000);
	wxWindow* addBooleanField(wxWindow* parent, wxFlexGridSizer* grid, MonsterField field, bool value);
	void applyCapability(wxWindow* control, MonsterField field);
	void readControls();
	void refreshPreview();
	void onSave(wxCommandEvent& event);
	void onLookChanged(wxCommandEvent& event);
	void onRotate(wxCommandEvent& event);

	std::unique_ptr<MonsterDefinitionDocument> document;
	MonsterDefinition edited;
	std::array<wxWindow*, static_cast<std::size_t>(MonsterField::Count)> controls {};
	wxStaticBitmap* preview = nullptr;
	wxChoice* directionChoice = nullptr;
	wxSpinCtrl* frame = nullptr;
	int direction = 2;
	bool saved = false;
};

#endif // NEXAMAP_MONSTER_EDITOR_DIALOG_H_
