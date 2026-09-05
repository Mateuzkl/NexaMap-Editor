#include "main.h"

#include "map_diagnostics_window.h"

#include "gui.h"
#include "map.h"
#include "map_diagnostics_scanner.h"
#include "map_tab.h"
#include "theme.h"

#include <algorithm>
#include <array>

#include <wx/checkbox.h>
#include <wx/control.h>
#include <wx/dc.h>
#include <wx/display.h>
#include <wx/gauge.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/stopwatch.h>
#include <wx/vlbox.h>
#include <wx/wrapsizer.h>

namespace {
	constexpr std::array<MapDiagnosticCategory, 8> kCategoryOrder = {
		MapDiagnosticCategory::UniqueIds,
		MapDiagnosticCategory::ActionIds,
		MapDiagnosticCategory::Items,
		MapDiagnosticCategory::Teleports,
		MapDiagnosticCategory::Houses,
		MapDiagnosticCategory::Spawns,
		MapDiagnosticCategory::DoorsAndKeys,
		MapDiagnosticCategory::Waypoints,
	};

	wxString PositionLabel(const Position& position) {
		return wxString::Format("%d, %d, %d", position.x, position.y, position.z);
	}

	wxString Utf8(std::string_view value) {
		return wxString::FromUTF8(value.data(), value.size());
	}
}

class MapDiagnosticsWindow::ResultsList final : public wxVListBox {
public:
	explicit ResultsList(MapDiagnosticsWindow* owner) :
		wxVListBox(owner, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE | wxBORDER_SIMPLE),
		owner_(*owner) {
		SetMargins(FromDIP(8), FromDIP(2));
	}

	long HitTestRow(const wxPoint& point) const {
		return VirtualHitTest(point.y);
	}

	void SetRows(size_t count) {
		// Use the generic virtual list so replacing the model does not leave
		// stale native report subitem indices during Windows dark-mode painting.
		SetSelection(wxNOT_FOUND);
		SetItemCount(count);
		Refresh();
	}

	void ApplyTheme() {
		SetBackgroundColour(Theme::Get(Theme::Role::Surface));
		SetForegroundColour(Theme::Get(Theme::Role::Text));
		SetSelectionBackground(Theme::Get(Theme::Role::SelectionFill));
		Refresh();
	}

private:
	wxCoord OnMeasureItem(size_t rowIndex) const override {
		const VisibleRow* row = owner_.GetVisibleRow(static_cast<long>(rowIndex));
		return row && row->categoryHeader ? FromDIP(30) : FromDIP(46);
	}

	void OnDrawBackground(wxDC& dc, const wxRect& rect, size_t rowIndex) const override {
		const VisibleRow* row = owner_.GetVisibleRow(static_cast<long>(rowIndex));
		const wxColour background = row && row->categoryHeader ? Theme::Get(Theme::Role::RaisedSurface) : (IsSelected(rowIndex) ? Theme::Get(Theme::Role::SelectionFill) : Theme::Get(Theme::Role::Surface));
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(background);
		dc.DrawRectangle(rect);
		dc.SetPen(Theme::Get(Theme::Role::Border));
		dc.DrawLine(rect.GetLeft(), rect.GetBottom(), rect.GetRight(), rect.GetBottom());
	}

	void OnDrawItem(wxDC& dc, const wxRect& rect, size_t rowIndex) const override {
		const VisibleRow* row = owner_.GetVisibleRow(static_cast<long>(rowIndex));
		if (!row) {
			return;
		}

		wxDCClipper clip(dc, rect);
		const int padding = FromDIP(8);
		const int left = rect.GetLeft() + padding;
		if (row->categoryHeader) {
			const bool collapsed = owner_.collapsed_.find(row->category) != owner_.collapsed_.end() && owner_.collapsed_.at(row->category);
			dc.SetFont(GetFont().Bold());
			dc.SetTextForeground(Theme::Get(Theme::Role::Accent));
			dc.DrawText(wxString::FromUTF8(collapsed ? "\xe2\x96\xb6 " : "\xe2\x96\xbc ") + Utf8(MapDiagnosticCategoryName(row->category)) + wxString::Format(" (%zu)", row->categoryCount), left, rect.GetTop() + FromDIP(6));
			return;
		}

		const MapDiagnosticIssue* issue = owner_.GetIssue(row->issueIndex);
		if (!issue) {
			return;
		}

		dc.SetFont(GetFont());
		dc.SetTextForeground(issue->severity == MapDiagnosticSeverity::Error ? (Theme::IsDark() ? wxColour(255, 116, 116) : wxColour(177, 32, 32)) : (Theme::IsDark() ? wxColour(255, 196, 86) : wxColour(139, 91, 0)));
		const wxString position = issue->position == Position() ? wxString("-") : PositionLabel(issue->position);
		wxCoord positionWidth = 0;
		dc.GetTextExtent(position, &positionWidth, nullptr);
		const int positionX = rect.GetRight() - padding - positionWidth;
		const int summaryWidth = std::max(0, positionX - left - FromDIP(12));
		const wxString summary = wxString(issue->severity == MapDiagnosticSeverity::Error ? "[E] " : "[!] ") + wxString::FromUTF8(issue->summary);
		dc.DrawText(wxControl::Ellipsize(summary, dc, wxELLIPSIZE_END, summaryWidth), left, rect.GetTop() + FromDIP(4));
		dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
		dc.DrawText(position, positionX, rect.GetTop() + FromDIP(4));
		const int detailWidth = std::max(0, rect.GetWidth() - padding * 2);
		dc.DrawText(wxControl::Ellipsize(wxString::FromUTF8(issue->detail), dc, wxELLIPSIZE_END, detailWidth), left, rect.GetTop() + FromDIP(23));
	}

	MapDiagnosticsWindow& owner_;
};

MapDiagnosticsWindow::MapDiagnosticsWindow(wxWindow* parent, Map& map) :
	wxDialog(parent, wxID_ANY, "Map Diagnostics", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	map_(map),
	mapSessionId_(map.getSessionId()),
	scanner_(std::make_unique<MapDiagnosticsScanner>(map)),
	timer_(this) {
	const int margin = FromDIP(10);
	auto* root = new wxBoxSizer(wxVERTICAL);
	auto* title = new wxStaticText(this, wxID_ANY, "Map Health Scanner");
	title->SetFont(GetFont().Bold().Larger());
	subtitle_ = new wxStaticText(this, wxID_ANY, "Read-only checks for the active map and resource session.");
	root->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, margin);
	root->Add(subtitle_, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, margin);
	root->Add(new wxStaticLine(this), 0, wxEXPAND);

	auto* filters = new wxWrapSizer(wxHORIZONTAL);
	filters->Add(new wxStaticText(this, wxID_ANY, "Show:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	auto addFilter = [&](wxCheckBox*& target, const wxString& label) {
		target = new wxCheckBox(this, wxID_ANY, label);
		target->SetValue(true);
		filters->Add(target, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
		target->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { RebuildRows(); });
	};
	addFilter(errors_, "Errors");
	addFilter(warnings_, "Warnings");
	addFilter(uniqueIds_, "UID");
	addFilter(actionIds_, "AID");
	addFilter(items_, "Items");
	addFilter(teleports_, "Teleports");
	addFilter(houses_, "Houses");
	addFilter(spawns_, "Spawns");
	addFilter(doorsAndKeys_, "Doors/Keys");
	addFilter(waypoints_, "Waypoints");
	root->Add(filters, 0, wxEXPAND | wxALL, margin);

	results_ = new ResultsList(this);
	results_->SetMinSize(wxSize(FromDIP(420), FromDIP(220)));
	root->Add(results_, 1, wxEXPAND | wxLEFT | wxRIGHT, margin);

	progress_ = new wxGauge(this, wxID_ANY, 100);
	status_ = new wxStaticText(this, wxID_ANY, "Ready. Click Scan Map to start.", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	root->Add(progress_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);
	root->Add(status_, 0, wxEXPAND | wxALL, margin);

	auto* buttons = new wxBoxSizer(wxHORIZONTAL);
	scan_ = new wxButton(this, wxID_ANY, "Scan Map");
	refresh_ = new wxButton(this, wxID_REFRESH, "Refresh");
	clear_ = new wxButton(this, wxID_CLEAR, "Clear");
	cancel_ = new wxButton(this, wxID_CANCEL, "Cancel Scan");
	auto* close = new wxButton(this, wxID_CLOSE, "Close");
	buttons->Add(scan_, 0, wxRIGHT, FromDIP(6));
	buttons->Add(refresh_, 0, wxRIGHT, FromDIP(6));
	buttons->Add(clear_, 0, wxRIGHT, FromDIP(6));
	buttons->Add(cancel_, 0, wxRIGHT, FromDIP(6));
	buttons->AddStretchSpacer();
	buttons->Add(close);
	root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, margin);

	SetSizer(root);
	SetEscapeId(wxID_CLOSE);
	scan_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { StartScan(); });
	refresh_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { StartScan(); });
	clear_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ClearResults(); });
	cancel_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CancelScan(); });
	close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); });
	Bind(wxEVT_TIMER, &MapDiagnosticsWindow::OnTimer, this);
	Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
		CancelScan();
		event.Skip();
	});
	Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& event) {
		ApplyTheme();
		event.Skip();
	});
	results_->Bind(wxEVT_LEFT_DCLICK, &MapDiagnosticsWindow::OnResultDoubleClick, this);
	results_->Bind(wxEVT_MOTION, &MapDiagnosticsWindow::OnResultMotion, this);
	results_->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
		const long row = results_->HitTestRow(event.GetPosition());
		const VisibleRow* visible = GetVisibleRow(row);
		if (visible && visible->categoryHeader) {
			ToggleCategory(static_cast<size_t>(row));
			return;
		}
		event.Skip();
	});
	results_->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
		if (event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_NUMPAD_ENTER) {
			const long rowIndex = results_->GetSelection();
			const VisibleRow* row = GetVisibleRow(rowIndex);
			if (row && row->categoryHeader) {
				ToggleCategory(static_cast<size_t>(rowIndex));
			} else if (row) {
				NavigateToIssue(row->issueIndex);
			}
			return;
		}
		event.Skip();
	});

	ApplyTheme();
	UpdateControls();
	const int displayIndex = wxDisplay::GetFromWindow(parent);
	wxRect area = wxDisplay(displayIndex == wxNOT_FOUND ? 0 : displayIndex).GetClientArea();
	area.Deflate(margin);
	const wxSize desired = FromDIP(wxSize(820, 560));
	const wxSize size(std::min(desired.x, std::max(1, area.width)), std::min(desired.y, std::max(1, area.height)));
	SetMinSize(wxSize(std::min(FromDIP(520), size.x), std::min(FromDIP(360), size.y)));
	SetSize(size);
	CentreOnParent();
}

MapDiagnosticsWindow::~MapDiagnosticsWindow() {
	timer_.Stop();
	if (scanner_) {
		scanner_->Cancel();
	}
	DestroyChildren();
}

void MapDiagnosticsWindow::StartScan() {
	if (scanner_->IsRunning()) {
		return;
	}
	showResults_ = false;
	rows_.clear();
	results_->SetRows(0);
	tooltipRow_ = -1;
	results_->UnsetToolTip();
	progress_->SetValue(0);
	scanner_->Start();
	status_->SetLabel(wxString::Format("Scanning 0 of %llu tiles...", static_cast<unsigned long long>(scanner_->GetTotalTileCount())));
	UpdateControls();
	timer_.Start(10);
}

void MapDiagnosticsWindow::CancelScan() {
	if (!scanner_->IsRunning()) {
		return;
	}
	scanner_->Cancel();
	timer_.Stop();
	showResults_ = true;
	RebuildRows();
	status_->SetLabel(wxString::Format("Scan cancelled after %llu of %llu tiles. %zu partial findings retained.", static_cast<unsigned long long>(scanner_->GetProcessedTileCount()), static_cast<unsigned long long>(scanner_->GetTotalTileCount()), scanner_->GetIssues().size()));
	UpdateControls();
}

void MapDiagnosticsWindow::ClearResults() {
	if (scanner_->IsRunning()) {
		CancelScan();
	}
	showResults_ = false;
	rows_.clear();
	results_->SetRows(0);
	progress_->SetValue(0);
	status_->SetLabel("Results cleared. Click Scan Map to scan again.");
	results_->UnsetToolTip();
	tooltipRow_ = -1;
	UpdateControls();
}

void MapDiagnosticsWindow::OnTimer(wxTimerEvent&) {
	if (!scanner_->IsRunning()) {
		timer_.Stop();
		UpdateControls();
		return;
	}
	MapTab* currentTab = g_gui.GetCurrentMapTab();
	if (!currentTab || &g_gui.GetCurrentMap() != &map_ || map_.getSessionId() != mapSessionId_) {
		CancelScan();
		status_->SetLabel("Scan cancelled because the active map or resource session changed.");
		return;
	}

	wxStopWatch budget;
	do {
		scanner_->Step(256);
	} while (scanner_->IsRunning() && budget.Time() < 8);
	progress_->SetValue(scanner_->GetProgressPercent());
	if (scanner_->IsRunning()) {
		status_->SetLabel(wxString::Format("Scanning %llu of %llu tiles... %zu findings", static_cast<unsigned long long>(scanner_->GetProcessedTileCount()), static_cast<unsigned long long>(scanner_->GetTotalTileCount()), scanner_->GetIssues().size()));
		return;
	}

	timer_.Stop();
	showResults_ = true;
	RebuildRows();
	const auto& issues = scanner_->GetIssues();
	const size_t errors = std::count_if(issues.begin(), issues.end(), [](const MapDiagnosticIssue& issue) { return issue.severity == MapDiagnosticSeverity::Error; });
	status_->SetLabel(wxString::Format("Scan complete: %zu errors, %zu warnings across %llu tiles.", errors, issues.size() - errors, static_cast<unsigned long long>(scanner_->GetProcessedTileCount())));
	UpdateControls();
}

void MapDiagnosticsWindow::OnResultDoubleClick(wxMouseEvent& event) {
	const long rowIndex = results_->HitTestRow(event.GetPosition());
	const VisibleRow* row = GetVisibleRow(rowIndex);
	if (!row) {
		return;
	}
	if (row->categoryHeader) {
		return;
	}
	NavigateToIssue(row->issueIndex);
}

void MapDiagnosticsWindow::OnResultMotion(wxMouseEvent& event) {
	const long rowIndex = results_->HitTestRow(event.GetPosition());
	if (rowIndex == tooltipRow_) {
		event.Skip();
		return;
	}
	tooltipRow_ = rowIndex;
	const VisibleRow* row = GetVisibleRow(rowIndex);
	if (!row || row->categoryHeader) {
		results_->UnsetToolTip();
	} else if (const MapDiagnosticIssue* issue = GetIssue(row->issueIndex)) {
		results_->SetToolTip(BuildTooltip(*issue));
	}
	event.Skip();
}

void MapDiagnosticsWindow::ToggleCategory(size_t rowIndex) {
	const VisibleRow* row = GetVisibleRow(static_cast<long>(rowIndex));
	if (!row || !row->categoryHeader) {
		return;
	}
	collapsed_[row->category] = !collapsed_[row->category];
	RebuildRows();
}

void MapDiagnosticsWindow::RebuildRows() {
	tooltipRow_ = -1;
	results_->UnsetToolTip();
	rows_.clear();
	if (!showResults_) {
		results_->SetRows(0);
		return;
	}
	const MapDiagnosticFilter filter = ReadFilter();
	const auto& issues = scanner_->GetIssues();
	std::array<std::vector<size_t>, kCategoryOrder.size()> matchesByCategory;
	for (size_t index = 0; index < issues.size(); ++index) {
		if (MatchesMapDiagnosticFilter(issues[index], filter)) {
			matchesByCategory[static_cast<size_t>(issues[index].category)].push_back(index);
		}
	}
	for (MapDiagnosticCategory category : kCategoryOrder) {
		const auto& matches = matchesByCategory[static_cast<size_t>(category)];
		if (matches.empty()) {
			continue;
		}
		rows_.push_back({ true, category, 0, matches.size() });
		if (!collapsed_[category]) {
			for (size_t issueIndex : matches) {
				rows_.push_back({ false, category, issueIndex, 0 });
			}
		}
	}
	results_->SetRows(rows_.size());
}

void MapDiagnosticsWindow::UpdateControls() {
	const bool running = scanner_->IsRunning();
	scan_->Enable(!running);
	refresh_->Enable(!running);
	clear_->Enable(!running && showResults_);
	cancel_->Enable(running);
	for (wxCheckBox* filter : { errors_, warnings_, uniqueIds_, actionIds_, items_, teleports_, houses_, spawns_, doorsAndKeys_, waypoints_ }) {
		filter->Enable(!running);
	}
}

void MapDiagnosticsWindow::ApplyTheme() {
	const wxColour background = Theme::Get(Theme::Role::Background);
	const wxColour text = Theme::Get(Theme::Role::Text);
	SetBackgroundColour(background);
	SetForegroundColour(text);
	for (wxWindow* child : GetChildren()) {
		if (child != results_ && child != progress_) {
			child->SetBackgroundColour(background);
			child->SetForegroundColour(text);
		}
	}
	subtitle_->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	status_->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	results_->ApplyTheme();
	Refresh();
}

void MapDiagnosticsWindow::NavigateToIssue(size_t issueIndex) {
	const MapDiagnosticIssue* issue = GetIssue(issueIndex);
	if (!issue || issue->position == Position() || !issue->position.isValid()) {
		return;
	}
	if (!g_gui.GetCurrentMapTab() || &g_gui.GetCurrentMap() != &map_) {
		return;
	}
	g_gui.ChangeFloor(issue->position.z);
	g_gui.SetScreenCenterPosition(issue->position, true);
	g_gui.RefreshView();
}

MapDiagnosticFilter MapDiagnosticsWindow::ReadFilter() const {
	return { errors_->GetValue(), warnings_->GetValue(), uniqueIds_->GetValue(), actionIds_->GetValue(), items_->GetValue(), teleports_->GetValue(), houses_->GetValue(), spawns_->GetValue(), doorsAndKeys_->GetValue(), waypoints_->GetValue() };
}

wxString MapDiagnosticsWindow::BuildTooltip(const MapDiagnosticIssue& issue) const {
	wxString tooltip;
	tooltip << "Position: " << (issue.position == Position() ? wxString("Unavailable") : PositionLabel(issue.position));
	tooltip << "\nItem ID: " << (issue.itemId == 0 ? wxString("-") : wxString::Format("%u", issue.itemId));
	tooltip << "\nActionID: " << (issue.actionId == 0 ? wxString("-") : wxString::Format("%u", issue.actionId));
	tooltip << "\nUniqueID: " << (issue.uniqueId == 0 ? wxString("-") : wxString::Format("%u", issue.uniqueId));
	tooltip << "\nSeverity: " << Utf8(MapDiagnosticSeverityName(issue.severity));
	if (!issue.detail.empty()) {
		tooltip << "\n\n"
				<< wxString::FromUTF8(issue.detail);
	}
	return tooltip;
}

const MapDiagnosticsWindow::VisibleRow* MapDiagnosticsWindow::GetVisibleRow(long row) const {
	return row >= 0 && static_cast<size_t>(row) < rows_.size() ? &rows_[row] : nullptr;
}

const MapDiagnosticIssue* MapDiagnosticsWindow::GetIssue(size_t issueIndex) const {
	const auto& issues = scanner_->GetIssues();
	return issueIndex < issues.size() ? &issues[issueIndex] : nullptr;
}
