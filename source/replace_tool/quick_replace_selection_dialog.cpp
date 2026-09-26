//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "../main.h"
#include "quick_replace_selection_dialog.h"

#include "replace_engine.h"
#include "replace_item_grid_panel.h"
#include "replace_library_catalog.h"

#include "../brush.h"
#include "../dcbutton.h"
#include "../doodad_brush.h"
#include "../editor.h"
#include "../graphics.h"
#include "../gui.h"
#include "../items.h"
#include "../map.h"
#include "../map_display.h"
#include "../theme.h"
#include "../tile.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/vlbox.h>

class QuickReplaceCandidateList final : public wxVListBox {
public:
	QuickReplaceCandidateList(wxWindow* parent, const std::vector<QuickReplaceCandidate>& candidates) :
		wxVListBox(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE),
		candidates_(candidates) {
		SetBackgroundColour(Theme::Get(Theme::Role::Background));
		SetForegroundColour(Theme::Get(Theme::Role::Text));
		SetSelectionBackground(Theme::Get(Theme::Role::SelectionFill));
	}

	void ResetItems() {
		SetItemCount(candidates_.size());
		RefreshAll();
	}

protected:
	void OnDrawItem(wxDC& dc, const wxRect& rect, size_t index) const override {
		const QuickReplaceCandidate& candidate = candidates_.at(index);
		if (Sprite* sprite = g_gui.gfx.getSprite(candidate.spriteClientId)) {
			sprite->DrawTo(&dc, SPRITE_SIZE_32x32, rect.GetX() + FromDIP(7), rect.GetY() + FromDIP(6), FromDIP(32), FromDIP(32));
		}
		dc.SetTextForeground(IsSelected(index) ? Theme::Get(Theme::Role::TextOnAccent) : Theme::Get(Theme::Role::Text));
		const int textX = rect.GetX() + FromDIP(48);
		dc.DrawText(
			wxString::Format("%s  |  ID %u  |  x%zu", QuickReplaceCategoryName(candidate.category), candidate.mapItemId, candidate.count),
			textX,
			rect.GetY() + FromDIP(4)
		);
		dc.SetTextForeground(IsSelected(index) ? Theme::Get(Theme::Role::TextOnAccent) : Theme::Get(Theme::Role::TextSubtle));
		dc.DrawText(wxString::FromUTF8(candidate.name), textX, rect.GetY() + FromDIP(23));
	}

	wxCoord OnMeasureItem(size_t WXUNUSED(index)) const override {
		return FromDIP(45);
	}

private:
	const std::vector<QuickReplaceCandidate>& candidates_;
};

class QuickReplacementPickerDialog final : public wxDialog {
public:
	QuickReplacementPickerDialog(wxWindow* parent, QuickReplaceCategory sourceCategory, bool categoryOnly) :
		wxDialog(
			parent,
			wxID_ANY,
			categoryOnly ? wxString::Format("Choose %s Replacement", QuickReplaceCategoryName(sourceCategory)) : wxString("Choose Replacement Item"),
			wxDefaultPosition,
			wxDefaultSize,
			wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER
		),
		sourceCategory_(sourceCategory),
		categoryOnly_(categoryOnly) {
		auto* root = newd wxBoxSizer(wxVERTICAL);
		const wxString categoryName = wxString::FromUTF8(QuickReplaceCategoryName(sourceCategory_));
		auto* hint = newd wxStaticText(
			this,
			wxID_ANY,
			categoryOnly_ ? wxString("Smart filter: showing only ") + categoryName + " items." : wxString("Category filter disabled: showing every item.")
		);
		root->Add(hint, 0, wxEXPAND | wxALL, FromDIP(10));
		search_ = newd wxSearchCtrl(this, wxID_ANY);
		search_->SetDescriptiveText("Search by item name or ID");
		search_->ShowCancelButton(true);
		root->Add(search_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
		showTechnical_ = newd wxCheckBox(this, wxID_ANY, "Show unnamed and reserved items");
		showTechnical_->SetToolTip("Show low-level asset entries that are hidden from the normal item picker.");
		root->Add(showTechnical_, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));

		grid_ = newd ReplaceItemGridPanel(this, [this](const ReplaceLibraryItem& item) {
			resultId_ = item.serverId.value;
			resultDoodadBrush_ = nullptr;
			if (const auto found = doodadBrushes_.find(item.key); found != doodadBrushes_.end()) {
				resultDoodadBrush_ = found->second;
			} else if (resultId_ != 0) {
				Brush* brush = g_items.getItemType(resultId_).doodad_brush;
				resultDoodadBrush_ = brush && brush->isDoodad() ? brush->asDoodad() : nullptr;
			}
			chooseButton_->Enable(resultId_ != 0);
		});
		grid_->SetRuntimeIdLabels(true);
		grid_->SetMinSize(FromDIP(wxSize(620, 400)));
		root->Add(grid_, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));

		auto* buttons = newd wxStdDialogButtonSizer();
		chooseButton_ = newd wxButton(this, wxID_OK, "Choose");
		chooseButton_->Enable(false);
		buttons->AddButton(chooseButton_);
		buttons->AddButton(newd wxButton(this, wxID_CANCEL));
		buttons->Realize();
		root->Add(buttons, 0, wxALIGN_RIGHT | wxALL, FromDIP(10));
		SetSizer(root);
		SetMinSize(FromDIP(wxSize(660, 500)));
		SetSize(FromDIP(wxSize(760, 580)));
		CentreOnParent();

		const size_t storedMaximum = g_items.items.size() > 0 ? g_items.items.size() - 1 : 0;
		const size_t maximumId = std::min<size_t>(std::max<size_t>(g_items.getMaxID(), storedMaximum), std::numeric_limits<uint16_t>::max());
		allItems_.reserve(maximumId);
		std::unordered_map<uint16_t, ReplaceLibraryItem> itemsByClientId;
		std::unordered_map<DoodadBrush*, ReplaceLibraryItem> firstItemByDoodad;
		for (size_t id = 1; id <= maximumId; ++id) {
			if (!g_items.typeExists(static_cast<int>(id))) {
				continue;
			}
			const ItemType& type = g_items.getItemType(static_cast<int>(id));
			if (type.id == 0 || type.clientID == 0 || type.isMetaItem()) {
				continue;
			}
			ReplaceLibraryItem item;
			item.key = type.id;
			item.serverId = ServerItemId(type.id);
			item.clientId = type.clientID;
			item.name = type.name.empty() ? "Unnamed item" : type.name;
			itemsByClientId.try_emplace(item.clientId, item);
			if (type.doodad_brush && type.doodad_brush->isDoodad()) {
				firstItemByDoodad.try_emplace(type.doodad_brush->asDoodad(), item);
			}
			if (type.raw_brush != nullptr && (!categoryOnly_ || ClassifyItemType(type) == sourceCategory_)) {
				allItems_.push_back(item);
			}
		}

		if (categoryOnly_ && sourceCategory_ == QuickReplaceCategory::Doodad) {
			allItems_.clear();
			std::unordered_set<DoodadBrush*> visited;
			uint32_t choiceKey = 1;
			auto addDoodad = [&](DoodadBrush* brush) {
				if (!brush || !visited.insert(brush).second) {
					return;
				}
				auto representative = firstItemByDoodad.find(brush);
				if (representative == firstItemByDoodad.end()) {
					return;
				}
				ReplaceLibraryItem choice = representative->second;
				if (const auto look = itemsByClientId.find(static_cast<uint16_t>(brush->getLookID())); look != itemsByClientId.end()) {
					choice = look->second;
				}
				choice.key = choiceKey++;
				choice.clientId = static_cast<uint16_t>(brush->getLookID());
				if (!brush->getName().empty()) {
					choice.name = brush->getName();
				}
				doodadBrushes_.emplace(choice.key, brush);
				allItems_.push_back(std::move(choice));
			};
			for (const auto& [name, brush] : g_brushes.getMap()) {
				if (brush && brush->isDoodad()) {
					addDoodad(brush->asDoodad());
				}
			}
			for (const auto& [brush, item] : firstItemByDoodad) {
				addDoodad(brush);
			}
		}
		std::sort(allItems_.begin(), allItems_.end(), [](const ReplaceLibraryItem& left, const ReplaceLibraryItem& right) {
			if (left.name != right.name) {
				return left.name < right.name;
			}
			return left.serverId.value < right.serverId.value;
		});
		ApplyFilter();
		search_->Bind(wxEVT_TEXT, &QuickReplacementPickerDialog::OnSearch, this);
		showTechnical_->Bind(wxEVT_CHECKBOX, &QuickReplacementPickerDialog::OnTechnicalFilterChanged, this);
	}

	~QuickReplacementPickerDialog() override {
		search_->Unbind(wxEVT_TEXT, &QuickReplacementPickerDialog::OnSearch, this);
		showTechnical_->Unbind(wxEVT_CHECKBOX, &QuickReplacementPickerDialog::OnTechnicalFilterChanged, this);
	}

	uint16_t GetResultId() const {
		return resultId_;
	}

	DoodadBrush* GetResultDoodadBrush() const {
		return resultDoodadBrush_;
	}

private:
	void ApplyFilter() {
		const std::string query = search_->GetValue().Lower().ToStdString();
		std::vector<ReplaceLibraryItem> matches;
		matches.reserve(allItems_.size());
		for (const ReplaceLibraryItem& item : allItems_) {
			std::string loweredName = item.name;
			std::transform(loweredName.begin(), loweredName.end(), loweredName.begin(), [](unsigned char value) {
				return static_cast<char>(std::tolower(value));
			});
			const bool technical = loweredName == "unnamed item" || loweredName.find("reserved sprite") != std::string::npos;
			if (technical && !showTechnical_->GetValue()) {
				continue;
			}
			if (query.empty() || loweredName.find(query) != std::string::npos || std::to_string(item.serverId.value).find(query) != std::string::npos) {
				matches.push_back(item);
			}
		}
		resultId_ = 0;
		resultDoodadBrush_ = nullptr;
		chooseButton_->Enable(false);
		grid_->SetItems(std::move(matches));
	}

	void OnSearch(wxCommandEvent& WXUNUSED(event)) {
		ApplyFilter();
	}

	void OnTechnicalFilterChanged(wxCommandEvent& WXUNUSED(event)) {
		ApplyFilter();
	}

	QuickReplaceCategory sourceCategory_;
	bool categoryOnly_ = true;
	wxSearchCtrl* search_ = nullptr;
	wxCheckBox* showTechnical_ = nullptr;
	ReplaceItemGridPanel* grid_ = nullptr;
	wxButton* chooseButton_ = nullptr;
	std::vector<ReplaceLibraryItem> allItems_;
	std::unordered_map<uint32_t, DoodadBrush*> doodadBrushes_;
	uint16_t resultId_ = 0;
	DoodadBrush* resultDoodadBrush_ = nullptr;
};

namespace {
	wxStaticBoxSizer* CreatePreviewCard(wxWindow* parent, const wxString& title, DCButton*& sprite, wxStaticText*& details) {
		auto* card = newd wxStaticBoxSizer(wxVERTICAL, parent, title);
		card->SetMinSize(parent->FromDIP(wxSize(220, 320)));
		wxWindow* cardParent = card->GetStaticBox();
		sprite = newd DCButton(cardParent, wxID_ANY, wxDefaultPosition, DC_BTN_NORMAL, RENDER_SIZE_64x64, 0);
		details = newd wxStaticText(cardParent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
		details->SetMinSize(parent->FromDIP(wxSize(190, 100)));
		card->Add(sprite, 0, wxALIGN_CENTER | wxTOP | wxLEFT | wxRIGHT, parent->FromDIP(10));
		card->Add(details, 0, wxEXPAND | wxALL, parent->FromDIP(8));
		return card;
	}

	wxString CandidateDetails(const QuickReplaceCandidate& candidate) {
		wxString details = wxString::Format("ID %u\n", candidate.mapItemId);
		details << wxString::FromUTF8(candidate.name) << "\n";
		details << wxString::FromUTF8(QuickReplaceCategoryName(candidate.category)) << "\n";
		details << wxString::Format("%zu occurrence%s", candidate.count, candidate.count == 1 ? "" : "s");
		return details;
	}
}

QuickReplaceSelectionDialog::QuickReplaceSelectionDialog(MapCanvas* parent, Editor& editor, PositionVector selectedPositions) :
	wxDialog(parent, wxID_ANY, "Quick Replace - Selected Area", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	canvas_(*parent),
	editor_(editor),
	selectedPositions_(std::move(selectedPositions)) {
	std::sort(selectedPositions_.begin(), selectedPositions_.end());
	selectedPositions_.erase(std::unique(selectedPositions_.begin(), selectedPositions_.end()), selectedPositions_.end());

	auto* root = newd wxBoxSizer(wxVERTICAL);
	scopeLabel_ = newd wxStaticText(this, wxID_ANY, wxEmptyString);
	root->Add(scopeLabel_, 0, wxEXPAND | wxALL, FromDIP(10));

	auto* content = newd wxBoxSizer(wxHORIZONTAL);
	auto* foundBox = newd wxStaticBoxSizer(wxVERTICAL, this, "ITEMS FOUND IN SELECTION");
	sourceList_ = newd QuickReplaceCandidateList(foundBox->GetStaticBox(), candidates_);
	sourceList_->SetMinSize(FromDIP(wxSize(380, 380)));
	foundBox->Add(sourceList_, 1, wxEXPAND | wxALL, FromDIP(6));
	content->Add(foundBox, 1, wxEXPAND | wxRIGHT, FromDIP(8));

	auto* replacementBox = newd wxStaticBoxSizer(wxVERTICAL, this, "REPLACEMENT");
	wxWindow* replacementParent = replacementBox->GetStaticBox();
	auto* previews = newd wxBoxSizer(wxHORIZONTAL);
	previews->Add(CreatePreviewCard(replacementParent, "BEFORE", beforeSprite_, beforeDetails_), 1, wxEXPAND);
	auto* arrow = newd wxStaticText(replacementParent, wxID_ANY, wxString::FromUTF8("\xE2\x86\x92"));
	wxFont arrowFont = arrow->GetFont();
	arrowFont.SetPointSize(22);
	arrowFont.SetWeight(wxFONTWEIGHT_BOLD);
	arrow->SetFont(arrowFont);
	previews->Add(arrow, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(8));
	previews->Add(CreatePreviewCard(replacementParent, "AFTER", afterSprite_, afterDetails_), 1, wxEXPAND);
	replacementBox->Add(previews, 1, wxEXPAND | wxALL, FromDIP(6));

	auto* targetButtons = newd wxBoxSizer(wxHORIZONTAL);
	chooseButton_ = newd wxButton(replacementParent, wxID_ANY, "Choose Replacement...");
	removeButton_ = newd wxButton(replacementParent, wxID_ANY, "Remove Selected Item");
	removeButton_->SetToolTip("Remove every matching occurrence of the selected item and category from the captured area.");
	targetButtons->Add(chooseButton_, 0, wxRIGHT, FromDIP(6));
	targetButtons->Add(removeButton_, 0);
	replacementBox->Add(targetButtons, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	sameCategoryCheck_ = newd wxCheckBox(replacementParent, wxID_ANY, "Keep same item category");
	sameCategoryCheck_->SetValue(true);
	replacementBox->Add(sameCategoryCheck_, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	autoBorderCheck_ = newd wxCheckBox(replacementParent, wxID_ANY, "Rebuild surrounding floor borders");
	autoBorderCheck_->SetValue(false);
	autoBorderCheck_->SetToolTip("May modify adjacent tiles outside the selected area. All changes remain part of the same Undo/Redo operation.");
	replacementBox->Add(autoBorderCheck_, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	replaceButton_ = newd wxButton(replacementParent, wxID_ANY, "Replace");
	replaceButton_->SetDefault();
	replacementBox->Add(replaceButton_, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
	content->Add(replacementBox, 1, wxEXPAND);
	root->Add(content, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));

	statusLabel_ = newd wxStaticText(this, wxID_ANY, wxEmptyString);
	root->Add(statusLabel_, 0, wxEXPAND | wxALL, FromDIP(10));
	auto* buttons = newd wxBoxSizer(wxHORIZONTAL);
	buttons->AddStretchSpacer();
	closeButton_ = newd wxButton(this, wxID_CANCEL, "Close");
	buttons->Add(closeButton_, 0);
	root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));

	SetSizer(root);
	SetMinSize(FromDIP(wxSize(980, 600)));
	SetSize(FromDIP(wxSize(1040, 650)));
	CentreOnParent();

	sourceList_->Bind(wxEVT_LISTBOX, &QuickReplaceSelectionDialog::OnSourceSelected, this);
	chooseButton_->Bind(wxEVT_BUTTON, &QuickReplaceSelectionDialog::OnChooseReplacement, this);
	removeButton_->Bind(wxEVT_BUTTON, &QuickReplaceSelectionDialog::OnChooseRemoval, this);
	sameCategoryCheck_->Bind(wxEVT_CHECKBOX, &QuickReplaceSelectionDialog::OnSameCategoryChanged, this);
	replaceButton_->Bind(wxEVT_BUTTON, &QuickReplaceSelectionDialog::OnReplace, this);
	closeButton_->Bind(wxEVT_BUTTON, &QuickReplaceSelectionDialog::OnClose, this);

	RefreshCandidates();
}

QuickReplaceSelectionDialog::~QuickReplaceSelectionDialog() {
	sourceList_->Unbind(wxEVT_LISTBOX, &QuickReplaceSelectionDialog::OnSourceSelected, this);
	chooseButton_->Unbind(wxEVT_BUTTON, &QuickReplaceSelectionDialog::OnChooseReplacement, this);
	removeButton_->Unbind(wxEVT_BUTTON, &QuickReplaceSelectionDialog::OnChooseRemoval, this);
	sameCategoryCheck_->Unbind(wxEVT_CHECKBOX, &QuickReplaceSelectionDialog::OnSameCategoryChanged, this);
	replaceButton_->Unbind(wxEVT_BUTTON, &QuickReplaceSelectionDialog::OnReplace, this);
	closeButton_->Unbind(wxEVT_BUTTON, &QuickReplaceSelectionDialog::OnClose, this);
}

std::vector<Tile*> QuickReplaceSelectionDialog::ResolveSelectedTiles() const {
	std::vector<Tile*> tiles;
	tiles.reserve(selectedPositions_.size());
	for (const Position& position : selectedPositions_) {
		if (Tile* tile = editor_.map.getTile(position)) {
			tiles.push_back(tile);
		}
	}
	return tiles;
}

const QuickReplaceCandidate* QuickReplaceSelectionDialog::CurrentCandidate() const {
	const int selection = sourceList_->GetSelection();
	if (selection == wxNOT_FOUND || static_cast<size_t>(selection) >= candidates_.size()) {
		return nullptr;
	}
	return &candidates_[selection];
}

void QuickReplaceSelectionDialog::RefreshCandidates(std::optional<QuickReplaceCandidateKey> preferredSource) {
	candidates_ = CollectQuickReplaceCandidates(ResolveSelectedTiles());
	sourceList_->ResetItems();
	scopeLabel_->SetLabel(wxString::Format("Selected area: %zu tile%s   |   %zu unique visible item ID%s", selectedPositions_.size(), selectedPositions_.size() == 1 ? "" : "s", candidates_.size(), candidates_.size() == 1 ? "" : "s"));

	if (candidates_.empty()) {
		sourceKey_ = {};
		sourceList_->SetSelection(wxNOT_FOUND);
		UpdateSourcePreview();
		ClearTarget();
		SetStatus("Selected area has no remaining replaceable items.");
		return;
	}

	size_t selection = 0;
	if (preferredSource) {
		if (const std::optional<size_t> found = FindQuickReplaceCandidateIndex(candidates_, *preferredSource)) {
			selection = *found;
		}
	}
	SelectCandidate(selection);
}

void QuickReplaceSelectionDialog::SelectCandidate(size_t index) {
	if (index >= candidates_.size()) {
		return;
	}
	sourceList_->SetSelection(static_cast<int>(index));
	sourceKey_ = { candidates_[index].mapItemId, candidates_[index].category };
	UpdateSourcePreview();
	ClearTarget();
}

void QuickReplaceSelectionDialog::ClearTarget() {
	targetId_ = 0;
	targetDoodadBrush_ = nullptr;
	removeTarget_ = false;
	afterSprite_->SetSprite(0);
	afterDetails_->SetLabel("No replacement selected");
	sameCategoryCheck_->Enable(true);
	replaceButton_->SetLabel("Replace");
	UpdateReplaceState();
}

void QuickReplaceSelectionDialog::UpdateSourcePreview() {
	const QuickReplaceCandidate* candidate = CurrentCandidate();
	if (!candidate) {
		beforeSprite_->SetSprite(0);
		beforeDetails_->SetLabel("No source item selected");
		chooseButton_->Enable(false);
		removeButton_->Enable(false);
		autoBorderCheck_->Enable(false);
		UpdateReplaceState();
		return;
	}
	beforeSprite_->SetSprite(candidate->spriteClientId);
	beforeDetails_->SetLabel(CandidateDetails(*candidate));
	beforeDetails_->Wrap(FromDIP(190));
	chooseButton_->Enable(true);
	removeButton_->Enable(true);
	autoBorderCheck_->Enable(candidate->category == QuickReplaceCategory::Ground);
	Layout();
	UpdateReplaceState();
}

void QuickReplaceSelectionDialog::UpdateTargetPreview() {
	if (removeTarget_) {
		const QuickReplaceCandidate* source = CurrentCandidate();
		afterSprite_->SetSprite(0);
		afterDetails_->SetLabel(
			source ? wxString::Format(
				"Remove selected item\n%s\n%zu occurrence%s",
				QuickReplaceCategoryName(source->category),
				source->count,
				source->count == 1 ? "" : "s"
			)
				   : wxString("Remove selected item")
		);
		afterDetails_->Wrap(FromDIP(190));
		sameCategoryCheck_->Enable(false);
		replaceButton_->SetLabel("Remove");
		Layout();
		UpdateReplaceState();
		return;
	}
	if (targetId_ == 0) {
		ClearTarget();
		return;
	}
	const ItemType& type = g_items.getItemType(targetId_);
	afterSprite_->SetSprite(targetDoodadBrush_ ? static_cast<uint16_t>(targetDoodadBrush_->getLookID()) : type.clientID);
	wxString details = wxString::Format("ID %u\n", targetId_);
	details << wxString::FromUTF8(targetDoodadBrush_ && !targetDoodadBrush_->getName().empty() ? targetDoodadBrush_->getName() : (type.name.empty() ? "Unnamed item" : type.name)) << "\n";
	details << wxString::FromUTF8(QuickReplaceCategoryName(targetCategory_));
	if (targetDoodadBrush_) {
		details << "\nComplete palette brush";
	}
	afterDetails_->SetLabel(details);
	afterDetails_->Wrap(FromDIP(190));
	Layout();
	UpdateReplaceState();
}

void QuickReplaceSelectionDialog::UpdateReplaceState() {
	const QuickReplaceCandidate* source = CurrentCandidate();
	const bool categoryMatches = source && source->category == targetCategory_;
	Brush* sourceBrush = source ? g_items.getItemType(source->mapItemId).doodad_brush : nullptr;
	const bool replacementDiffers = removeTarget_ || (targetDoodadBrush_ ? sourceBrush != targetDoodadBrush_ : source && targetId_ != source->mapItemId);
	const bool hasTarget = removeTarget_ || targetId_ != 0;
	const bool enabled = source && hasTarget && replacementDiffers && (removeTarget_ || !sameCategoryCheck_->GetValue() || categoryMatches) && !ResolveSelectedTiles().empty();
	replaceButton_->Enable(enabled);
}

void QuickReplaceSelectionDialog::SetStatus(const wxString& message, bool error) {
	statusLabel_->SetLabel(message);
	statusLabel_->SetForegroundColour(error ? wxColour(220, 90, 90) : Theme::Get(Theme::Role::TextSubtle));
}

void QuickReplaceSelectionDialog::OnSourceSelected(wxCommandEvent& WXUNUSED(event)) {
	const QuickReplaceCandidate* candidate = CurrentCandidate();
	sourceKey_ = candidate ? QuickReplaceCandidateKey { candidate->mapItemId, candidate->category } : QuickReplaceCandidateKey {};
	UpdateSourcePreview();
	ClearTarget();
	SetStatus(wxEmptyString);
}

void QuickReplaceSelectionDialog::OnChooseReplacement(wxCommandEvent& WXUNUSED(event)) {
	const QuickReplaceCandidate* source = CurrentCandidate();
	if (!source) {
		return;
	}
	QuickReplacementPickerDialog dialog(this, source->category, sameCategoryCheck_->GetValue());
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}
	const uint16_t selectedId = dialog.GetResultId();
	const ItemType& selectedType = g_items.getItemType(selectedId);
	if (selectedId == 0 || selectedType.id == 0) {
		SetStatus("The selected replacement item is not available in the current item database.", true);
		return;
	}
	DoodadBrush* selectedDoodadBrush = dialog.GetResultDoodadBrush();
	const QuickReplaceCategory selectedCategory = selectedDoodadBrush ? QuickReplaceCategory::Doodad : ClassifyItemType(selectedType);
	if (sameCategoryCheck_->GetValue() && selectedCategory != source->category) {
		wxMessageBox(
			wxString::Format(
				"The selected item is %s, but the source is %s. Choose an item from the same category or turn off the category filter.",
				QuickReplaceCategoryName(selectedCategory),
				QuickReplaceCategoryName(source->category)
			),
			"Category Mismatch",
			wxOK | wxICON_WARNING,
			this
		);
		return;
	}
	targetId_ = selectedId;
	targetCategory_ = selectedCategory;
	targetDoodadBrush_ = selectedDoodadBrush;
	removeTarget_ = false;
	sameCategoryCheck_->Enable(true);
	replaceButton_->SetLabel("Replace");
	UpdateTargetPreview();
	SetStatus(wxEmptyString);
}

void QuickReplaceSelectionDialog::OnChooseRemoval(wxCommandEvent& WXUNUSED(event)) {
	if (!CurrentCandidate()) {
		return;
	}
	targetId_ = 0;
	targetDoodadBrush_ = nullptr;
	removeTarget_ = true;
	UpdateTargetPreview();
	SetStatus("Removal selected. Click Remove to apply it to the captured area.");
}

void QuickReplaceSelectionDialog::OnSameCategoryChanged(wxCommandEvent& WXUNUSED(event)) {
	UpdateReplaceState();
}

void QuickReplaceSelectionDialog::OnReplace(wxCommandEvent& WXUNUSED(event)) {
	const QuickReplaceCandidate* source = CurrentCandidate();
	Brush* sourceBrush = source ? g_items.getItemType(source->mapItemId).doodad_brush : nullptr;
	const bool replacementDiffers = removeTarget_ || (targetDoodadBrush_ ? sourceBrush != targetDoodadBrush_ : source && targetId_ != sourceKey_.mapItemId);
	const bool hasTarget = removeTarget_ || targetId_ != 0;
	if (!source || source->mapItemId != sourceKey_.mapItemId || source->category != sourceKey_.category || !hasTarget || !replacementDiffers) {
		return;
	}
	if (!removeTarget_ && sameCategoryCheck_->GetValue() && source->category != targetCategory_) {
		SetStatus("Choose a replacement item from the same category.", true);
		return;
	}
	if (!removeTarget_ && !sameCategoryCheck_->GetValue() && source->category != targetCategory_) {
		const int answer = wxMessageBox(
			wxString::Format(
				"Replace %s ID %u with %s ID %u?\n\nThis changes the item category and may alter specialized item state.",
				QuickReplaceCategoryName(source->category),
				sourceKey_.mapItemId,
				QuickReplaceCategoryName(targetCategory_),
				targetId_
			),
			"Confirm Cross-Category Replacement",
			wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
			this
		);
		if (answer != wxYES) {
			return;
		}
	}

	std::optional<QuickReplaceCandidateKey> nextSource;
	const int currentIndex = sourceList_->GetSelection();
	if (currentIndex != wxNOT_FOUND && candidates_.size() > 1) {
		for (size_t offset = 1; offset < candidates_.size(); ++offset) {
			const QuickReplaceCandidate& candidate = candidates_[(static_cast<size_t>(currentIndex) + offset) % candidates_.size()];
			if (removeTarget_ || candidate.mapItemId != targetId_) {
				nextSource = QuickReplaceCandidateKey { candidate.mapItemId, candidate.category };
				break;
			}
		}
	}

	ReplacementRule rule;
	rule.sourceServerId = ServerItemId(sourceKey_.mapItemId);
	rule.targets.push_back(removeTarget_ ? ReplacementTarget::ForTrash(100) : ReplacementTarget::ForItem(ServerItemId(targetId_), 100));
	ReplaceExecutionOptions options;
	options.dryRun = false;
	options.includeContainerContents = false;
	options.rebuildGroundBorders = autoBorderCheck_->GetValue() && source->category == QuickReplaceCategory::Ground && (removeTarget_ || targetCategory_ == QuickReplaceCategory::Ground);
	options.doodadReplacementBrush = removeTarget_ ? nullptr : targetDoodadBrush_;
	if (targetDoodadBrush_) {
		if (targetDoodadBrush_->hasCompositeObjects(options.doodadVariation)) {
			options.doodadComposite = &targetDoodadBrush_->getComposite(options.doodadVariation);
		}
		options.doodadAllowedPositions = selectedPositions_;
		options.enforceDoodadSelectionScope = true;
	}
	const QuickReplaceCategory sourceCategory = source->category;
	options.matchFilter = [sourceCategory](const Tile& tile, const Item& item) {
		return ClassifyPlacedItem(tile, item) == sourceCategory;
	};
	ReplaceExecutionResult result = ReplaceEngine::Run(editor_, ResolveSelectedTiles(), { rule }, options);
	if (!result.validation.isValid()) {
		SetStatus("The replacement rule is invalid.", true);
		return;
	}
	if (result.doodadOutsideScopeTiles != 0) {
		const int answer = wxMessageBox(
			wxString::Format(
				"This Doodad extends outside the selected area and will modify %zu neighboring tile%s.\n\nContinue?",
				result.doodadOutsideScopeTiles,
				result.doodadOutsideScopeTiles == 1 ? "" : "s"
			),
			"Doodad Extends Outside Selection",
			wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
			this
		);
		if (answer != wxYES) {
			SetStatus("Doodad replacement canceled; the selected area was not changed.");
			return;
		}
		options.allowDoodadOutsideScope = true;
		result = ReplaceEngine::Run(editor_, ResolveSelectedTiles(), { rule }, options);
	}
	if (!result.committed) {
		SetStatus("No matching visible items were found in the captured selection.", true);
		RefreshCandidates(sourceKey_);
		return;
	}

	g_gui.InvalidateAutoborderPreview();
	canvas_.RefreshViewport();
	wxString summary = removeTarget_ ? wxString::Format("Removed %zu item%s from %zu tile%s.", result.deletions, result.deletions == 1 ? "" : "s", result.changedTiles, result.changedTiles == 1 ? "" : "s") : wxString::Format("Replaced %zu item%s on %zu tile%s.", result.replacements, result.replacements == 1 ? "" : "s", result.changedTiles, result.changedTiles == 1 ? "" : "s");
	if (result.doodadPlacements != 0) {
		summary << wxString::Format(" Applied %zu complete Doodad brush%s across %zu tile%s.", result.doodadPlacements, result.doodadPlacements == 1 ? "" : "es", result.doodadTilesChanged, result.doodadTilesChanged == 1 ? "" : "s");
	}
	if (result.bordersRebuilt != 0) {
		summary << wxString::Format(" Rebuilt borders on %zu nearby tile%s.", result.bordersRebuilt, result.bordersRebuilt == 1 ? "" : "s");
	}
	RefreshCandidates(nextSource);
	SetStatus(summary);
}

void QuickReplaceSelectionDialog::OnClose(wxCommandEvent& WXUNUSED(event)) {
	EndModal(wxID_CANCEL);
}
