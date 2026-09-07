//////////////////////////////////////////////////////////////////////
// Native source-preserving NPC editor.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_NPC_EDITOR_DIALOG_H_
#define NEXAMAP_NPC_EDITOR_DIALOG_H_

#include "npc_definition.h"

#include <wx/dialog.h>

#include <array>
#include <memory>

class wxFlexGridSizer;
class wxListCtrl;
class wxSpinCtrl;
class wxStaticBitmap;
class wxTextCtrl;
class wxWindow;
class OutfitColorPicker;

class NpcEditorDialog final : public wxDialog {
public:
	NpcEditorDialog(wxWindow* parent, std::unique_ptr<NpcDefinitionDocument> document);
	[[nodiscard]] bool wasSaved() const;

private:
	wxTextCtrl* addText(wxWindow* parent, wxFlexGridSizer* grid, NpcField field, const std::string& value);
	wxSpinCtrl* addNumber(wxWindow* parent, wxFlexGridSizer* grid, NpcField field, int value, int maximum = 2000000000);
	void applyCapability(wxWindow* control, NpcField field);
	void readControls();
	void refreshPreview();
	void refreshMessages();
	void refreshShop();
	void refreshTravel();
	void editMessage(std::size_t index);
	void editShop(std::size_t index);
	void editTravel(std::size_t index);
	bool confirmDiscard();
	void onSave(wxCommandEvent& event);
	void onCancel(wxCommandEvent& event);
	void onClose(wxCloseEvent& event);

	std::unique_ptr<NpcDefinitionDocument> document;
	NpcDefinition edited;
	std::array<wxWindow*, static_cast<std::size_t>(NpcField::Count)> controls {};
	wxStaticBitmap* preview = nullptr;
	OutfitColorPicker* outfitColors = nullptr;
	wxListCtrl* messageList = nullptr;
	wxListCtrl* shopList = nullptr;
	wxListCtrl* travelList = nullptr;
	wxTextCtrl* sourceView = nullptr;
	bool saved = false;
};

#endif // NEXAMAP_NPC_EDITOR_DIALOG_H_
