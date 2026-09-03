#include "main.h"

#include "quick_command_palette.h"
#include "theme.h"

#include <algorithm>
#include <limits>
#include <wx/display.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tokenzr.h>
#include <wx/wupdlock.h>

namespace {
	wxString PaletteLabel(wxString label) {
		// menubar.xml uses '$' for menu mnemonics.
		label.Replace("$", "&");
		return wxMenuItem::GetLabelText(label).Trim().Trim(false);
	}

	wxString NormalizePaletteQuery(wxString text) {
		text.MakeLower();
		text.Replace("_", " ");
		text.Replace(">", " ");
		text.Replace("...", "");
		text.Replace(wxString::FromUTF8("\xe2\x80\xa6"), "");
		wxString normalized;
		wxStringTokenizer words(text, " \t\r\n");
		while (words.HasMoreTokens()) {
			if (!normalized.empty()) {
				normalized += ' ';
			}
			normalized += words.GetNextToken();
		}
		return normalized;
	}

	std::optional<size_t> PaletteTokenCost(const wxString& field, const wxString& token) {
		const size_t substring = field.find(token);
		if (substring != wxString::npos) {
			return substring;
		}
		size_t position = 0;
		size_t gaps = 0;
		for (wxUniChar character : token) {
			const size_t found = field.find(character, position);
			if (found == wxString::npos) {
				return std::nullopt;
			}
			gaps += found - position;
			position = found + 1;
		}
		// Contiguous tokens beat scattered letters within the fuzzy tier.
		return 1000 + gaps;
	}

	std::optional<std::pair<int, size_t>> RankPaletteCommand(const std::array<wxString, 5>& fields, const wxString& query, const wxArrayString& tokens) {
		if (query.empty()) {
			return std::pair<int, size_t>(0, 0);
		}
		std::optional<std::pair<int, size_t>> best;
		for (size_t i = 0; i < fields.size(); ++i) {
			const size_t position = fields[i].find(query);
			if (position != wxString::npos) {
				const int rank = fields[i] == query ? 0 : (position == 0 ? 1 : 2);
				const std::pair<int, size_t> score(rank, i * 100 + position);
				if (!best || score < *best) {
					best = score;
				}
			}
		}
		if (best) {
			return best;
		}

		// Every word must match a field, in any word order. This supports
		// "pro gen", "multi host", and abbreviations such as "prcd gen".
		size_t cost = 0;
		for (const wxString& token : tokens) {
			std::optional<size_t> tokenCost;
			for (size_t i = 0; i < fields.size(); ++i) {
				if (const auto match = PaletteTokenCost(fields[i], token)) {
					const size_t candidate = *match + i * 100;
					if (!tokenCost || candidate < *tokenCost) {
						tokenCost = candidate;
					}
				}
			}
			if (!tokenCost) {
				return std::nullopt;
			}
			cost += *tokenCost;
		}
		return std::pair<int, size_t>(3, cost);
	}
}

class QuickCommandPalette::ResultsList : public wxListCtrl {
public:
	ResultsList(QuickCommandPalette* owner) :
		wxListCtrl(owner, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_VIRTUAL | wxLC_SINGLE_SEL | wxLC_NO_HEADER | wxBORDER_NONE),
		owner_(*owner) {
		SetName("Commands");
		AppendColumn("Command");
		AppendColumn("Category");
		AppendColumn("Shortcut", wxLIST_FORMAT_RIGHT);
		Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
			ResizeColumns();
			event.Skip();
		});
	}

	void ResizeColumns() {
		const int width = std::max(0, GetClientSize().GetWidth() - FromDIP(4));
		const int shortcut = std::min(FromDIP(125), width / 4);
		const int category = width / 3;
		SetColumnWidth(0, width - category - shortcut);
		SetColumnWidth(1, category);
		SetColumnWidth(2, shortcut);
	}

	void ApplyColours() {
		SetBackgroundColour(Theme::Get(Theme::Role::Surface));
		SetForegroundColour(Theme::Get(Theme::Role::Text));
		disabled_.SetTextColour(Theme::Get(Theme::Role::TextSubtle));
		disabled_.SetBackgroundColour(Theme::Get(Theme::Role::Surface));
		Refresh();
	}

private:
	wxString OnGetItemText(long item, long column) const override {
		if (item < 0 || static_cast<size_t>(item) >= owner_.matches_.size()) {
			return {};
		}
		const QuickCommandPalette::PaletteCommand& command = owner_.commands_[owner_.matches_[item].commandIndex];
		switch (column) {
			case 0:
				return command.label;
			case 1:
				return owner_.showRecent_ && command.recentOrder != std::numeric_limits<size_t>::max() ? "Recent > " + command.category : command.category;
			case 2:
				return command.hotkey;
			default:
				return {};
		}
	}

	wxListItemAttr* OnGetItemAttr(long item) const override {
		if (item < 0 || static_cast<size_t>(item) >= owner_.matches_.size()) {
			return nullptr;
		}
		return owner_.commands_[owner_.matches_[item].commandIndex].enabled ? nullptr : &disabled_;
	}

	QuickCommandPalette& owner_;
	mutable wxListItemAttr disabled_;
};

QuickCommandPalette::QuickCommandPalette(wxWindow* parent, const HotkeyManager& hotkeys, MainMenuBar& menuBar, const std::vector<MenuBar::ActionID>& recentCommands) :
	wxDialog(parent, wxID_ANY, "Command Palette", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	menuBar_(menuBar) {
	// A display/search snapshot only. HotkeyManager remains the command registry.
	for (const auto& [id, info] : hotkeys.GetActionInfo()) {
		const wxString label = PaletteLabel(info.itemName);
		if (label.empty() || !menuBar_.HasItem(id)) {
			continue;
		}
		const wxString category = PaletteLabel(info.category);
		const auto recent = std::find(recentCommands.begin(), recentCommands.end(), id);
		commands_.push_back({ id, label, category, info.help, hotkeys.GetEffectiveKey(id), { NormalizePaletteQuery(label), NormalizePaletteQuery(info.name), NormalizePaletteQuery(category + " > " + label), NormalizePaletteQuery(category), NormalizePaletteQuery(info.help) }, recent == recentCommands.end() ? std::numeric_limits<size_t>::max() : static_cast<size_t>(recent - recentCommands.begin()), menuBar_.IsItemEnabled(id) });
	}

	const int margin = FromDIP(12);
	auto* sizer = new wxBoxSizer(wxVERTICAL);
	auto* searchSizer = new wxBoxSizer(wxHORIZONTAL);
	prompt_ = new wxStaticText(this, wxID_ANY, ">");
	prompt_->SetFont(GetFont().Bold());
	search_ = new wxTextCtrl(this, wxID_ANY);
	search_->SetHint("Search commands...");
	search_->SetName("Search commands");
	searchSizer->Add(prompt_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	searchSizer->Add(search_, 1, wxEXPAND);
	sizer->Add(searchSizer, 0, wxEXPAND | wxALL, margin);

	summary_ = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	summary_->SetMinSize(wxSize(1, GetCharHeight()));
	sizer->Add(summary_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, margin);
	results_ = new ResultsList(this);
	results_->SetMinSize(wxSize(1, FromDIP(80)));
	sizer->Add(results_, 1, wxEXPAND | wxLEFT | wxRIGHT, margin);
	description_ = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	description_->SetMinSize(wxSize(1, 2 * GetCharHeight() + FromDIP(4)));
	sizer->Add(description_, 0, wxEXPAND | wxALL, margin);
	hint_ = new wxStaticText(this, wxID_ANY, "Up/Down: select    Enter: run    Esc: close", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	hint_->SetMinSize(wxSize(1, GetCharHeight()));
	sizer->Add(hint_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, margin);
	SetSizer(sizer);
	SetEscapeId(wxID_CANCEL);
	ApplyColours();

	search_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { FilterCommands(); });
	results_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { UpdateDescription(); });
	results_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { ExecuteSelection(); });
	Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
		switch (event.GetKeyCode()) {
			case WXK_UP:
				MoveSelection(-1);
				break;
			case WXK_DOWN:
				MoveSelection(1);
				break;
			case WXK_RETURN:
			case WXK_NUMPAD_ENTER:
				ExecuteSelection();
				break;
			case WXK_ESCAPE:
				EndModal(wxID_CANCEL);
				break;
			default:
				event.Skip();
				break;
		}
	});
	Bind(wxEVT_SHOW, [this](wxShowEvent& event) {
		// INIT_DIALOG runs before the native dialog is visible. Windows can
		// reject SetFocus there with ERROR_INVALID_PARAMETER.
		if (event.IsShown()) {
			CallAfter([this] {
				if (!IsBeingDeleted() && IsShownOnScreen() && search_->CanBeFocused()) {
					search_->SetFocus();
				}
			});
		}
		event.Skip();
	});
	Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& event) {
		ApplyColours();
		event.Skip();
	});

	const int displayIndex = wxDisplay::GetFromWindow(parent);
	wxRect area = wxDisplay(displayIndex == wxNOT_FOUND ? 0 : displayIndex).GetClientArea();
	area.Deflate(margin);
	const wxSize desired = FromDIP(wxSize(600, 400));
	const wxSize size(std::min(desired.x, std::max(1, area.width)), std::min(desired.y, std::max(1, area.height)));
	SetMinSize(wxSize(std::min(FromDIP(360), size.x), std::min(FromDIP(240), size.y)));
	SetSize(size);
	CentreOnParent();
	SetPosition(wxPoint(std::clamp(GetPosition().x, area.x, area.GetRight() + 1 - size.x), std::clamp(GetPosition().y, area.y, area.GetBottom() + 1 - size.y)));
	FilterCommands();
}

QuickCommandPalette::~QuickCommandPalette() {
	// Virtual list callbacks reference the snapshot, which must outlive controls.
	DestroyChildren();
}

void QuickCommandPalette::ApplyColours() {
	SetBackgroundColour(Theme::Get(Theme::Role::Background));
	SetForegroundColour(Theme::Get(Theme::Role::Text));
	for (wxStaticText* label : { summary_, description_, hint_ }) {
		label->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
		label->SetBackgroundColour(Theme::Get(Theme::Role::Background));
	}
	prompt_->SetForegroundColour(Theme::Get(Theme::Role::Accent));
	prompt_->SetBackgroundColour(Theme::Get(Theme::Role::Background));
	search_->SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	search_->SetForegroundColour(Theme::Get(Theme::Role::Text));
	results_->ApplyColours();
	Refresh();
}

void QuickCommandPalette::FilterCommands() {
	const wxString query = NormalizePaletteQuery(search_->GetValue());
	const wxArrayString tokens = wxSplit(query, ' ');
	showRecent_ = query.empty();
	menuBar_.Update();
	wxWindowUpdateLocker lock(results_);
	results_->SetItemCount(0);
	matches_.clear();
	for (size_t i = 0; i < commands_.size(); ++i) {
		commands_[i].enabled = menuBar_.IsItemEnabled(commands_[i].id);
		if (const auto score = RankPaletteCommand(commands_[i].searchFields, query, tokens)) {
			matches_.push_back({ i, score->first, score->second });
		}
	}
	std::sort(matches_.begin(), matches_.end(), [this](const Match& left, const Match& right) {
		const PaletteCommand& a = commands_[left.commandIndex];
		const PaletteCommand& b = commands_[right.commandIndex];
		if (showRecent_ && a.recentOrder != b.recentOrder) {
			return a.recentOrder < b.recentOrder;
		}
		if (left.rank != right.rank) {
			return left.rank < right.rank;
		}
		if (left.cost != right.cost) {
			return left.cost < right.cost;
		}
		const int labelOrder = a.label.CmpNoCase(b.label);
		if (labelOrder != 0) {
			return labelOrder < 0;
		}
		const int categoryOrder = a.category.CmpNoCase(b.category);
		return categoryOrder != 0 ? categoryOrder < 0 : a.id < b.id;
	});
	results_->SetItemCount(static_cast<long>(matches_.size()));
	if (!matches_.empty()) {
		results_->SetItemState(0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
		results_->EnsureVisible(0);
	}
	const bool hasRecent = showRecent_ && !matches_.empty() && commands_[matches_.front().commandIndex].recentOrder != std::numeric_limits<size_t>::max();
	summary_->SetLabel(matches_.empty() ? wxString("No matching commands") : wxString::Format("%zu commands", matches_.size()) + (hasRecent ? "  /  Recent first" : ""));
	results_->Refresh();
	UpdateDescription();
}

QuickCommandPalette::PaletteCommand* QuickCommandPalette::GetCurrentCommand() {
	const long selection = results_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (selection < 0 || static_cast<size_t>(selection) >= matches_.size()) {
		return nullptr;
	}
	return &commands_[matches_[selection].commandIndex];
}

void QuickCommandPalette::MoveSelection(int direction) {
	if (matches_.empty()) {
		return;
	}
	const long current = results_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	const long next = current < 0 ? 0 : std::clamp(current + direction, 0L, static_cast<long>(matches_.size()) - 1);
	if (current >= 0) {
		results_->SetItemState(current, 0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
	}
	results_->SetItemState(next, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
	results_->EnsureVisible(next);
	UpdateDescription();
}

void QuickCommandPalette::UpdateDescription() {
	if (PaletteCommand* command = GetCurrentCommand()) {
		const wxString context = command->category.empty() ? command->label : command->category + " > " + command->label;
		const wxString help = command->enabled ? command->help : "Unavailable in the current editor context. " + command->help;
		description_->SetLabel(context + "\n" + help);
		description_->SetToolTip(context + "\n" + help + (command->hotkey.empty() ? wxString() : "\n" + command->hotkey));
	} else {
		description_->SetLabel("Try a command name, category, or description.");
		description_->UnsetToolTip();
	}
}

void QuickCommandPalette::ExecuteSelection() {
	PaletteCommand* command = GetCurrentCommand();
	if (!command) {
		return;
	}
	menuBar_.Update();
	command->enabled = menuBar_.IsItemEnabled(command->id);
	if (!command->enabled) {
		results_->Refresh();
		UpdateDescription();
		return;
	}
	selectedAction_ = command->id;
	EndModal(wxID_OK);
}
