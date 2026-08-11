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

#include "../main.h"
#include "replace_rule_builder_panel.h"

#include "../theme.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <wx/dnd.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/wrapsizer.h>

namespace {
	class ServerItemDropTarget final : public wxTextDropTarget {
	public:
		explicit ServerItemDropTarget(std::function<void(ServerItemId)> handler) :
			handler(std::move(handler)) { }

		bool OnDropText(wxCoord, wxCoord, const wxString& text) override {
			const std::optional<ServerItemId> serverId = ReplaceRuleEditor::ParseDragPayload(text.ToStdString());
			if (!serverId) {
				return false;
			}
			handler(*serverId);
			return true;
		}

	private:
		std::function<void(ServerItemId)> handler;
	};

	wxString ValidationMessage(ReplacementValidationError error) {
		switch (error) {
			case ReplacementValidationError::None:
				return "Rules are valid";
			case ReplacementValidationError::InvalidSourceServerId:
				return "A source ServerID is invalid";
			case ReplacementValidationError::MissingTargets:
				return "Every rule needs at least one target";
			case ReplacementValidationError::InvalidTargetServerId:
				return "A target ServerID is invalid";
			case ReplacementValidationError::SourceEqualsTarget:
				return "A source cannot replace itself";
			case ReplacementValidationError::InvalidProbability:
				return "Probabilities must be between 1 and 100";
			case ReplacementValidationError::ProbabilityTotalAbove100:
				return "Target probabilities cannot exceed 100%";
			case ReplacementValidationError::TrashMixedWithItemTargets:
				return "Trash must be the only target";
			case ReplacementValidationError::DuplicateSourceServerId:
				return "Source ServerIDs must be unique";
		}
		return "Rules are invalid";
	}
}

ReplaceRuleBuilderPanel::ReplaceRuleBuilderPanel(wxWindow* parent, Listener* listener) :
	wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL),
	listener(listener) {
	SetScrollRate(0, 12);
	rootSizer = new wxBoxSizer(wxVERTICAL);

	auto* header = new wxBoxSizer(wxHORIZONTAL);
	header->Add(new wxStaticText(this, wxID_ANY, "Rule Builder"), 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	saveButton = new wxButton(this, wxID_ANY, "Save rules");
	auto* clearButton = new wxButton(this, wxID_ANY, "Clear");
	header->Add(saveButton, 0, wxRIGHT, 4);
	header->Add(clearButton);
	rootSizer->Add(header, 0, wxEXPAND | wxALL, 6);

	validationLabel = new wxStaticText(this, wxID_ANY, "No rules");
	rootSizer->Add(validationLabel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
	rulesSizer = new wxBoxSizer(wxVERTICAL);
	rootSizer->Add(rulesSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
	SetSizer(rootSizer);

	saveButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		if (this->listener) {
			this->listener->OnReplaceRulesSaveRequested(editor.GetRules());
		}
	});
	clearButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		if (editor.GetRules().empty() || wxMessageBox("Clear all replacement rules?", "Advanced Replace", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
			return;
		}
		editor.Clear();
		NotifyChanged();
		ScheduleRebuild();
		if (this->listener) {
			this->listener->OnReplaceRulesCleared();
		}
	});
	Rebuild();
}

void ReplaceRuleBuilderPanel::SetRules(std::vector<ReplacementRule> rules) {
	editor.SetRules(std::move(rules));
	Rebuild();
}

bool ReplaceRuleBuilderPanel::AddSourceRule(ServerItemId serverId) {
	if (!editor.AddRule(serverId)) {
		return false;
	}
	NotifyChanged();
	Rebuild();
	return true;
}

size_t ReplaceRuleBuilderPanel::AddSourceRules(std::span<const ServerItemId> serverIds) {
	size_t added = 0;
	for (ServerItemId serverId : serverIds) {
		if (editor.AddRule(serverId)) {
			++added;
		}
	}
	if (added != 0) {
		NotifyChanged();
		Rebuild();
	}
	return added;
}

void ReplaceRuleBuilderPanel::Rebuild() {
	Freeze();
	rulesSizer->Clear(true);
	const auto& rules = editor.GetRules();
	for (size_t ruleIndex = 0; ruleIndex < rules.size(); ++ruleIndex) {
		const ReplacementRule& rule = rules[ruleIndex];
		auto* card = new wxStaticBoxSizer(wxVERTICAL, this, wxString::Format("Rule %zu", ruleIndex + 1));
		wxWindow* cardParent = card->GetStaticBox();

		auto* sourceRow = new wxBoxSizer(wxHORIZONTAL);
		sourceRow->Add(CreateDropSlot(cardParent, wxString::Format("IF FOUND\nSID %u", rule.sourceServerId.value), [this, ruleIndex](ServerItemId serverId) {
						   if (editor.SetSource(ruleIndex, serverId)) {
							   NotifyChanged();
							   ScheduleRebuild();
						   }
					   }),
					   1, wxEXPAND | wxRIGHT, 6);
		auto* deleteRuleButton = new wxButton(cardParent, wxID_ANY, "Delete rule");
		deleteRuleButton->Bind(wxEVT_BUTTON, [this, ruleIndex](wxCommandEvent&) {
			editor.RemoveRule(ruleIndex);
			NotifyChanged();
			ScheduleRebuild();
		});
		sourceRow->Add(deleteRuleButton, 0, wxALIGN_CENTER_VERTICAL);
		card->Add(sourceRow, 0, wxEXPAND | wxALL, 6);

		auto* targets = new wxWrapSizer(wxHORIZONTAL);
		for (size_t targetIndex = 0; targetIndex < rule.targets.size(); ++targetIndex) {
			const ReplacementTarget& target = rule.targets[targetIndex];
			auto* targetPanel = new wxPanel(cardParent, wxID_ANY, wxDefaultPosition, wxSize(170, 92), wxBORDER_SIMPLE);
			auto* targetSizer = new wxBoxSizer(wxVERTICAL);
			if (target.isTrash()) {
				targetSizer->Add(new wxStaticText(targetPanel, wxID_ANY, "DELETE / TRASH"), 0, wxALIGN_CENTER | wxALL, 6);
			} else {
				targetSizer->Add(CreateDropSlot(targetPanel, wxString::Format("REPLACE WITH SID %u", target.serverId.value), [this, ruleIndex, targetIndex](ServerItemId serverId) {
									 if (editor.ReplaceItemTarget(ruleIndex, targetIndex, serverId)) {
										 NotifyChanged();
										 ScheduleRebuild();
									 }
								 }),
								 0, wxEXPAND | wxALL, 3);
			}

			auto* controls = new wxBoxSizer(wxHORIZONTAL);
			auto* probability = new wxSpinCtrl(targetPanel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(74, -1), wxSP_ARROW_KEYS, 1, 100, target.probability);
			probability->Bind(wxEVT_SPINCTRL, [this, ruleIndex, targetIndex](wxCommandEvent& event) {
				editor.SetTargetProbability(ruleIndex, targetIndex, static_cast<uint16_t>(event.GetInt()));
				NotifyChanged();
				UpdateValidationStatus();
			});
			controls->Add(probability, 0, wxRIGHT, 4);
			controls->Add(new wxStaticText(targetPanel, wxID_ANY, "%"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
			auto* removeButton = new wxButton(targetPanel, wxID_ANY, "Remove", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
			removeButton->Bind(wxEVT_BUTTON, [this, ruleIndex, targetIndex](wxCommandEvent&) {
				editor.RemoveTarget(ruleIndex, targetIndex);
				NotifyChanged();
				ScheduleRebuild();
			});
			controls->Add(removeButton, 0);
			targetSizer->Add(controls, 0, wxALIGN_CENTER | wxALL, 3);
			targetPanel->SetSizer(targetSizer);
			targets->Add(targetPanel, 0, wxRIGHT | wxBOTTOM, 6);
		}

		const bool hasTrash = std::any_of(rule.targets.begin(), rule.targets.end(), [](const ReplacementTarget& target) {
			return target.isTrash();
		});
		if (!hasTrash) {
			targets->Add(CreateDropSlot(cardParent, "Drop target item here", [this, ruleIndex](ServerItemId serverId) {
							 if (editor.AddItemTarget(ruleIndex, serverId)) {
								 NotifyChanged();
								 ScheduleRebuild();
							 }
						 }),
						 0, wxRIGHT | wxBOTTOM, 6);
			auto* trashButton = new wxButton(cardParent, wxID_ANY, "Add trash target");
			trashButton->Enable(rule.targets.empty());
			trashButton->Bind(wxEVT_BUTTON, [this, ruleIndex](wxCommandEvent&) {
				if (editor.AddTrashTarget(ruleIndex)) {
					NotifyChanged();
					ScheduleRebuild();
				}
			});
			targets->Add(trashButton, 0, wxRIGHT | wxBOTTOM, 6);
		}
		card->Add(new wxStaticText(cardParent, wxID_ANY, "Targets"), 0, wxLEFT | wxRIGHT | wxTOP, 6);
		card->Add(targets, 0, wxEXPAND | wxALL, 6);
		rulesSizer->Add(card, 0, wxEXPAND | wxBOTTOM, 8);
	}

	rulesSizer->Add(CreateDropSlot(this, "Drop a source item here to create a rule", [this](ServerItemId serverId) {
						if (editor.AddRule(serverId)) {
							NotifyChanged();
							ScheduleRebuild();
						}
					}),
					0, wxEXPAND | wxTOP, 4);
	UpdateValidationStatus();
	Layout();
	FitInside();
	Thaw();
}

void ReplaceRuleBuilderPanel::ScheduleRebuild() {
	if (rebuildScheduled) {
		return;
	}
	rebuildScheduled = true;
	CallAfter([this]() {
		rebuildScheduled = false;
		Rebuild();
	});
}

void ReplaceRuleBuilderPanel::NotifyChanged() {
	if (listener) {
		listener->OnReplaceRulesChanged(editor.GetRules());
	}
}

void ReplaceRuleBuilderPanel::UpdateValidationStatus() {
	if (editor.GetRules().empty()) {
		validationLabel->SetLabel("No rules");
		validationLabel->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
		saveButton->Enable(false);
		return;
	}
	const RuleSet draft = { "Draft", editor.GetRules() };
	const ReplacementValidationResult validation = ValidateRuleSet(draft);
	validationLabel->SetLabel(ValidationMessage(validation.error));
	validationLabel->SetForegroundColour(validation.isValid() ? Theme::Get(Theme::Role::Text) : wxColour(220, 90, 90));
	saveButton->Enable(validation.isValid());
}

wxWindow* ReplaceRuleBuilderPanel::CreateDropSlot(wxWindow* parent, const wxString& label, std::function<void(ServerItemId)> handler) {
	auto* panel = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(170, 54), wxBORDER_SIMPLE);
	panel->SetBackgroundColour(Theme::Get(Theme::Role::RaisedSurface));
	auto* sizer = new wxBoxSizer(wxVERTICAL);
	auto* text = new wxStaticText(panel, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
	sizer->AddStretchSpacer();
	sizer->Add(text, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, 4);
	sizer->AddStretchSpacer();
	panel->SetSizer(sizer);
	panel->SetDropTarget(new ServerItemDropTarget(std::move(handler)));
	return panel;
}
