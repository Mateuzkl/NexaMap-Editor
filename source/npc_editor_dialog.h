//////////////////////////////////////////////////////////////////////
// Native source-preserving NPC editor.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_NPC_EDITOR_DIALOG_H_
#define NEXAMAP_NPC_EDITOR_DIALOG_H_

#include "editor_autosave_state.h"
#include "editor_source_monitor.h"
#include "npc_definition.h"

#include <wx/dialog.h>

#include <array>
#include <memory>

class wxFlexGridSizer;
class wxListCtrl;
class wxSpinCtrl;
class wxStaticBitmap;
class wxStaticText;
class wxTextCtrl;
class wxTimer;
class wxTimerEvent;
class wxWindow;
class OutfitColorPicker;

class NpcEditorDialog final : public wxDialog {
public:
	NpcEditorDialog(wxWindow* parent, std::unique_ptr<NpcDefinitionDocument> document);
	[[nodiscard]] bool wasSaved() const;
	[[nodiscard]] bool wantsBrowse() const;

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
	bool saveDocument(bool showErrors);
	void scheduleAutosave();
	void updateSaveState(const wxString& label, bool error = false);
	void onSave(wxCommandEvent& event);
	void onBrowse(wxCommandEvent& event);
	void onFieldChanged(wxCommandEvent& event);
	void onAutosave(wxTimerEvent& event);
	void onSourceWatch(wxTimerEvent& event);
	void onCompareExternal(wxCommandEvent& event);
	void resetSourceMonitor();
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
	wxWindow* compareButton = nullptr;
	wxStaticText* saveStateLabel = nullptr;
	std::unique_ptr<wxTimer> autosaveTimer;
	std::unique_ptr<wxTimer> sourceWatchTimer;
	EditorAutosaveState autosaveState;
	EditorSourceMonitor sourceMonitor;
	std::vector<EditorSourceChange> externalChanges;
	bool constructing = true;
	bool saved = false;
	bool browseRequested = false;
};

#endif // NEXAMAP_NPC_EDITOR_DIALOG_H_
