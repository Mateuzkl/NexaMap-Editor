#ifndef RME_QUICK_COMMAND_PALETTE_H_
#define RME_QUICK_COMMAND_PALETTE_H_

#include "hotkey_manager.h"

#include <array>
#include <optional>
#include <vector>
#include <wx/dialog.h>

class wxStaticText;
class wxTextCtrl;

class QuickCommandPalette : public wxDialog {
public:
	QuickCommandPalette(wxWindow* parent, const HotkeyManager& hotkeys, MainMenuBar& menuBar, const std::vector<MenuBar::ActionID>& recentCommands);
	~QuickCommandPalette() override;

	std::optional<MenuBar::ActionID> GetSelectedAction() const {
		return selectedAction_;
	}

private:
	struct PaletteCommand {
		MenuBar::ActionID id;
		wxString label;
		wxString category;
		wxString help;
		wxString hotkey;
		std::array<wxString, 5> searchFields;
		size_t recentOrder;
		bool enabled;
	};

	struct Match {
		size_t commandIndex;
		int rank;
		size_t cost;
	};

	class ResultsList;
	void FilterCommands();
	void MoveSelection(int direction);
	void UpdateDescription();
	void ExecuteSelection();
	void ApplyColours();
	PaletteCommand* GetCurrentCommand();

	MainMenuBar& menuBar_;
	std::vector<PaletteCommand> commands_;
	std::vector<Match> matches_;
	std::optional<MenuBar::ActionID> selectedAction_;
	bool showRecent_ = false;
	wxTextCtrl* search_;
	ResultsList* results_;
	wxStaticText* summary_;
	wxStaticText* description_;
	wxStaticText* hint_;
	wxStaticText* prompt_;
};

#endif
