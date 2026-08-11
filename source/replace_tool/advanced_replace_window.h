//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_REPLACE_TOOL_ADVANCED_REPLACE_WINDOW_H_
#define RME_REPLACE_TOOL_ADVANCED_REPLACE_WINDOW_H_

#include "replace_library_panel.h"
#include "replace_rule_builder_panel.h"
#include "replace_rule_manager.h"
#include "replace_scope.h"

#include <wx/frame.h>

class Editor;
class MapCanvas;
class wxButton;
class wxCheckBox;
class wxChoice;
class wxRadioBox;
class wxStaticText;

class AdvancedReplaceWindow final : public wxFrame,
									private ReplaceLibraryPanel::Listener,
									private ReplaceRuleBuilderPanel::Listener {
public:
	AdvancedReplaceWindow(wxWindow* parent, Editor& editor, MapCanvas& canvas);

	[[nodiscard]] ReplaceScope GetScope() const;
	[[nodiscard]] const std::vector<ReplacementRule>& GetRules() const;

private:
	void OnReplaceLibraryItemSelected(ServerItemId serverId) override;
	void OnReplaceRulesChanged(const std::vector<ReplacementRule>& rules) override;
	void OnReplaceRulesSaveRequested(const std::vector<ReplacementRule>& rules) override;
	void OnReplaceRulesCleared() override;

	void RefreshSavedRuleSets(const std::string& preferredName = {});
	void LoadSelectedRuleSet();
	void RenameSelectedRuleSet();
	void DeleteSelectedRuleSet();
	void AddSelectedSource();
	void AddVisibleSources();
	void ExecuteReplace();
	void UpdateScopeStatus();
	void UpdateExecuteState();
	void SetStatus(const wxString& message, bool error = false);
	[[nodiscard]] ReplaceViewportBounds GetViewportBounds() const;

	Editor& editor;
	MapCanvas& canvas;
	ReplaceRuleManager ruleManager;
	ReplaceLibraryPanel* libraryPanel = nullptr;
	ReplaceRuleBuilderPanel* builderPanel = nullptr;
	wxChoice* savedRuleChoice = nullptr;
	wxRadioBox* scopeChoice = nullptr;
	wxButton* addSelectedButton = nullptr;
	wxButton* executeButton = nullptr;
	wxCheckBox* dryRunCheck = nullptr;
	wxStaticText* scopeStatus = nullptr;
	wxStaticText* statusLabel = nullptr;
	ServerItemId selectedLibraryItem;
	std::string activeRuleSetName;
	uint32_t pendingExecutionSeed = 0;
};

#endif
