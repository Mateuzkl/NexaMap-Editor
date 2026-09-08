//////////////////////////////////////////////////////////////////////
// Native source-preserving editor for an indexed monster definition.
//////////////////////////////////////////////////////////////////////

#ifndef NEXAMAP_MONSTER_EDITOR_DIALOG_H_
#define NEXAMAP_MONSTER_EDITOR_DIALOG_H_

#include "monster_definition.h"
#include "editor_autosave_state.h"
#include "editor_source_monitor.h"

#include <wx/dialog.h>

#include <array>
#include <functional>
#include <memory>
#include <string>

class wxFlexGridSizer;
class wxChoice;
class wxCloseEvent;
class wxListCtrl;
class wxNotebook;
class wxSpinCtrl;
class wxStaticBitmap;
class wxStaticText;
class wxTextCtrl;
class wxTreeCtrl;
class wxWindow;
class wxTimer;
class wxTimerEvent;
class MonsterSpellPreview;
class OutfitColorPicker;

class MonsterEditorDialog final : public wxDialog {
public:
	MonsterEditorDialog(wxWindow* parent, std::unique_ptr<MonsterDefinitionDocument> document);

	[[nodiscard]] bool wasSaved() const;
	[[nodiscard]] const MonsterDefinition& savedDefinition() const;
	[[nodiscard]] bool wantsBrowse() const;

private:
	wxTextCtrl* addTextField(wxWindow* parent, wxFlexGridSizer* grid, MonsterField field, const std::string& value);
	wxSpinCtrl* addNumberField(wxWindow* parent, wxFlexGridSizer* grid, MonsterField field, int value, int minimum = 0, int maximum = 2000000000);
	wxWindow* addBooleanField(wxWindow* parent, wxFlexGridSizer* grid, MonsterField field, bool value);
	void applyCapability(wxWindow* control, MonsterField field);
	void addAdvancedPages(wxNotebook* notebook);
	void addAttackPage(wxNotebook* notebook);
	void applySectionCapability(wxWindow* page, MonsterSection section);
	void readControls();
	void refreshPreview();
	void refreshDefenseList();
	void refreshAttackList();
	void refreshAttackPreview();
	void refreshResistanceLists();
	void refreshLootTree();
	void refreshSummonList();
	void refreshVoiceList();
	bool editDefense(MonsterDefenseAction& action);
	bool editAttack(MonsterAttackDefinition& attack);
	bool editResistance(MonsterResistance& resistance);
	bool editImmunity(MonsterImmunity& immunity);
	bool editLoot(MonsterLootEntry& entry);
	bool editSummon(MonsterSummon& summon);
	bool editVoice(MonsterVoice& voice);
	bool confirmDiscard();
	void moveSelected(wxListCtrl* list, std::size_t size, bool up, const std::function<void(std::size_t, std::size_t)>& move);
	void onCancel(wxCommandEvent& event);
	void onClose(wxCloseEvent& event);
	void onSave(wxCommandEvent& event);
	void onBrowse(wxCommandEvent& event);
	void onFieldChanged(wxCommandEvent& event);
	void onAutosave(wxTimerEvent& event);
	void onLookChanged(wxCommandEvent& event);
	void onSourceWatch(wxTimerEvent& event);
	void onCompareExternal(wxCommandEvent& event);
	void onRotate(wxCommandEvent& event);
	void resetSourceMonitor();
	void scheduleAutosave();
	bool saveDocument(bool showErrors);
	void updateSaveState(const wxString& label, bool error = false);

	std::unique_ptr<MonsterDefinitionDocument> document;
	MonsterDefinition edited;
	std::array<wxWindow*, static_cast<std::size_t>(MonsterField::Count)> controls {};
	wxStaticBitmap* preview = nullptr;
	OutfitColorPicker* outfitColors = nullptr;
	wxStaticText* saveStateLabel = nullptr;
	wxTextCtrl* sourceView = nullptr;
	wxWindow* compareButton = nullptr;
	wxChoice* directionChoice = nullptr;
	wxSpinCtrl* frame = nullptr;
	wxListCtrl* defenseList = nullptr;
	wxListCtrl* attackList = nullptr;
	wxChoice* attackDirection = nullptr;
	MonsterSpellPreview* attackPreview = nullptr;
	wxListCtrl* resistanceList = nullptr;
	wxListCtrl* immunityList = nullptr;
	wxTreeCtrl* lootTree = nullptr;
	wxSpinCtrl* maxSummons = nullptr;
	wxListCtrl* summonList = nullptr;
	wxSpinCtrl* voiceInterval = nullptr;
	wxSpinCtrl* voiceChance = nullptr;
	wxListCtrl* voiceList = nullptr;
	int direction = 2;
	std::unique_ptr<wxTimer> autosaveTimer;
	std::unique_ptr<wxTimer> sourceWatchTimer;
	EditorAutosaveState autosaveState;
	EditorSourceMonitor sourceMonitor;
	std::vector<EditorSourceChange> externalChanges;
	bool constructing = true;
	bool saved = false;
	bool browseRequested = false;
};

#endif // NEXAMAP_MONSTER_EDITOR_DIALOG_H_
