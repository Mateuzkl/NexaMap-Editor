//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#ifndef RME_REPLACE_TOOL_REPLACE_RULE_BUILDER_PANEL_H_
#define RME_REPLACE_TOOL_REPLACE_RULE_BUILDER_PANEL_H_

#include "replace_rule_editor.h"

#include <functional>
#include <span>
#include <wx/scrolwin.h>

class wxBoxSizer;
class wxButton;
class wxStaticText;

class ReplaceRuleBuilderPanel : public wxScrolledWindow {
public:
	class Listener {
	public:
		virtual ~Listener() = default;
		virtual void OnReplaceRulesChanged(const std::vector<ReplacementRule>& rules) = 0;
		virtual void OnReplaceRulesSaveRequested(const std::vector<ReplacementRule>& rules) = 0;
		virtual void OnReplaceRulesCleared() = 0;
	};

	ReplaceRuleBuilderPanel(wxWindow* parent, Listener* listener);

	void SetRules(std::vector<ReplacementRule> rules);
	bool AddSourceRule(ServerItemId serverId);
	size_t AddSourceRules(std::span<const ServerItemId> serverIds);
	[[nodiscard]] const std::vector<ReplacementRule>& GetRules() const {
		return editor.GetRules();
	}

private:
	void Rebuild();
	void ScheduleRebuild();
	void NotifyChanged();
	void UpdateValidationStatus();
	wxWindow* CreateDropSlot(wxWindow* parent, const wxString& label, std::function<void(ServerItemId)> handler);

	ReplaceRuleEditor editor;
	Listener* listener = nullptr;
	wxBoxSizer* rootSizer = nullptr;
	wxBoxSizer* rulesSizer = nullptr;
	wxStaticText* validationLabel = nullptr;
	wxButton* saveButton = nullptr;
	bool rebuildScheduled = false;
};

#endif
