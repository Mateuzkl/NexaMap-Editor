#ifndef RME_MAP_DIAGNOSTICS_WINDOW_H_
#define RME_MAP_DIAGNOSTICS_WINDOW_H_

#include "map_diagnostics.h"
#include "session_id.h"

#include <map>
#include <memory>
#include <vector>

#include <wx/dialog.h>
#include <wx/timer.h>

class Map;
class MapDiagnosticsScanner;
class wxButton;
class wxCheckBox;
class wxGauge;
class wxMouseEvent;
class wxStaticText;
class wxSysColourChangedEvent;

class MapDiagnosticsWindow final : public wxDialog {
public:
	MapDiagnosticsWindow(wxWindow* parent, Map& map);
	~MapDiagnosticsWindow() override;

private:
	class ResultsList;
	struct VisibleRow {
		bool categoryHeader = false;
		MapDiagnosticCategory category = MapDiagnosticCategory::Items;
		size_t issueIndex = 0;
		size_t categoryCount = 0;
	};

	void StartScan();
	void CancelScan();
	void ClearResults();
	void OnTimer(wxTimerEvent& event);
	void OnResultDoubleClick(wxMouseEvent& event);
	void OnResultMotion(wxMouseEvent& event);
	void ToggleCategory(size_t row);
	void RebuildRows();
	void UpdateControls();
	void ApplyTheme();
	void NavigateToIssue(size_t issueIndex);
	MapDiagnosticFilter ReadFilter() const;
	wxString BuildTooltip(const MapDiagnosticIssue& issue) const;
	const VisibleRow* GetVisibleRow(long row) const;
	const MapDiagnosticIssue* GetIssue(size_t issueIndex) const;

	Map& map_;
	SessionId mapSessionId_;
	uint64_t mapContentChanges_ = 0;
	uint64_t multiplayerRevision_ = 0;
	std::unique_ptr<MapDiagnosticsScanner> scanner_;
	wxTimer timer_;
	bool showResults_ = false;
	std::vector<VisibleRow> rows_;
	std::map<MapDiagnosticCategory, bool> collapsed_;
	long tooltipRow_ = -1;

	wxStaticText* subtitle_ = nullptr;
	wxCheckBox* errors_ = nullptr;
	wxCheckBox* warnings_ = nullptr;
	wxCheckBox* uniqueIds_ = nullptr;
	wxCheckBox* actionIds_ = nullptr;
	wxCheckBox* items_ = nullptr;
	wxCheckBox* teleports_ = nullptr;
	wxCheckBox* houses_ = nullptr;
	wxCheckBox* spawns_ = nullptr;
	wxCheckBox* doorsAndKeys_ = nullptr;
	wxCheckBox* waypoints_ = nullptr;
	ResultsList* results_ = nullptr;
	wxGauge* progress_ = nullptr;
	wxStaticText* status_ = nullptr;
	wxButton* scan_ = nullptr;
	wxButton* refresh_ = nullptr;
	wxButton* clear_ = nullptr;
	wxButton* cancel_ = nullptr;
};

#endif
