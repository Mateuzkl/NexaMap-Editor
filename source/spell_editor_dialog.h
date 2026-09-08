//////////////////////////////////////////////////////////////////////
// Native source-preserving editor for indexed global spells.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_SPELL_EDITOR_DIALOG_H_
#define NEXAMAP_SPELL_EDITOR_DIALOG_H_

#include "editor_autosave_state.h"
#include "editor_source_monitor.h"
#include "spell_definition.h"
#include "server_vocation_catalog.h"
#include "server_visual_catalog.h"

#include <wx/dialog.h>

#include <array>
#include <memory>

class MonsterSpellPreview;
class SpellAreaResolver;
class wxCloseEvent;
class wxCheckBox;
class wxCheckListBox;
class wxFlexGridSizer;
class wxStaticText;
class wxTextCtrl;
class wxTimer;
class wxTimerEvent;
class wxWindow;

class SpellEditorDialog final : public wxDialog {
public:
	SpellEditorDialog(wxWindow* parent, std::unique_ptr<SpellDefinitionDocument> document);

	[[nodiscard]] bool wasSaved() const;
	[[nodiscard]] const SpellDefinition& savedDefinition() const;
	[[nodiscard]] bool wantsBrowse() const;

private:
	wxWindow* addText(wxWindow* parent, wxFlexGridSizer* grid, SpellField field, const std::string& value, const wxArrayString& choices = {});
	wxWindow* addNumber(wxWindow* parent, wxFlexGridSizer* grid, SpellField field, int value, int maximum = 2000000000);
	wxWindow* addBoolean(wxWindow* parent, wxFlexGridSizer* grid, SpellField field, bool value);
	void applyCapability(wxWindow* control, SpellField field);
	void createReusableArea();
	void syncField(SpellField field);
	void readControls();
	void refreshPreview();
	void schedulePreview();
	void syncVocations();
	void refreshVocationControls();
	void chooseVisual(ServerVisualKind kind);
	bool confirmDiscard();
	bool saveDocument(bool showErrors);
	void scheduleAutosave();
	void updateSaveState(const wxString& label, bool error = false);
	void onFieldChanged(SpellField field, wxCommandEvent& event);
	void onSave(wxCommandEvent& event);
	void onBrowse(wxCommandEvent& event);
	void onAutosave(wxTimerEvent& event);
	void onPreviewTimer(wxTimerEvent& event);
	void onSourceWatch(wxTimerEvent& event);
	void onCompareExternal(wxCommandEvent& event);
	void resetSourceMonitor();
	void onCloseButton(wxCommandEvent& event);
	void onClose(wxCloseEvent& event);

	std::unique_ptr<SpellDefinitionDocument> document;
	std::shared_ptr<const SpellAreaResolver> areaResolver;
	std::shared_ptr<const ServerVisualCatalog> visualCatalog;
	SpellDefinition edited;
	std::array<wxWindow*, static_cast<std::size_t>(SpellField::Count)> controls {};
	MonsterSpellPreview* preview = nullptr;
	wxStaticText* areaStatus = nullptr;
	wxStaticText* saveState = nullptr;
	wxTextCtrl* declarationSource = nullptr;
	wxTextCtrl* implementationSource = nullptr;
	wxWindow* compareButton = nullptr;
	wxCheckBox* allVocationsCheck = nullptr;
	wxCheckBox* showInDescriptionCheck = nullptr;
	wxCheckListBox* vocationList = nullptr;
	wxTextCtrl* customVocation = nullptr;
	std::shared_ptr<const std::vector<ServerVocation>> vocationCatalog;
	std::vector<std::string> vocationValues;
	std::unique_ptr<wxTimer> autosaveTimer;
	std::unique_ptr<wxTimer> previewTimer;
	std::unique_ptr<wxTimer> sourceWatchTimer;
	EditorAutosaveState autosaveState;
	EditorChangeCoalescer pendingChanges;
	EditorSourceMonitor sourceMonitor;
	std::vector<EditorSourceChange> externalChanges;
	int direction = 0;
	bool constructing = true;
	bool saved = false;
	bool browseRequested = false;
};

#endif // NEXAMAP_SPELL_EDITOR_DIALOG_H_
