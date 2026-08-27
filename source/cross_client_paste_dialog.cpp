#include "main.h"

#include "cross_client_paste_dialog.h"

#include "theme.h"

#include <algorithm>
#include <cstring>
#include <wx/filename.h>
#include <wx/imaglist.h>
#include <wx/listctrl.h>
#include <wx/settings.h>
#include <wx/statline.h>

namespace {
	void StyleText(wxStaticText* label, const wxColour& foreground, const wxColour& background) {
		label->SetForegroundColour(foreground);
		label->SetBackgroundColour(background);
	}

	wxBitmap PreviewBitmap(const CrossClientItemSnapshot& item, int size) {
		if (item.previewWidth <= 0 || item.previewHeight <= 0 || item.previewRgba.size() != static_cast<size_t>(item.previewWidth) * item.previewHeight * 4) {
			wxImage empty(size, size, true);
			empty.InitAlpha();
			std::memset(empty.GetData(), 0, static_cast<size_t>(size) * size * 3);
			std::memset(empty.GetAlpha(), 0, static_cast<size_t>(size) * size);
			return wxBitmap(empty);
		}
		wxImage image(item.previewWidth, item.previewHeight, false);
		image.InitAlpha();
		unsigned char* rgb = image.GetData();
		unsigned char* alpha = image.GetAlpha();
		for (size_t pixel = 0; pixel < static_cast<size_t>(item.previewWidth) * item.previewHeight; ++pixel) {
			rgb[pixel * 3 + 0] = item.previewRgba[pixel * 4 + 0];
			rgb[pixel * 3 + 1] = item.previewRgba[pixel * 4 + 1];
			rgb[pixel * 3 + 2] = item.previewRgba[pixel * 4 + 2];
			alpha[pixel] = item.previewRgba[pixel * 4 + 3];
		}
		image.Rescale(size, size, wxIMAGE_QUALITY_HIGH);
		return wxBitmap(image);
	}
}

CrossClientPasteDialog::CrossClientPasteDialog(wxWindow* parent, const CrossClientPasteAnalysis& analysis) :
	wxDialog(parent, wxID_ANY, "Cross-Client Paste", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	analysis(analysis) {
	const wxColour surface = Theme::GetDark(Theme::Role::Surface);
	const wxColour background = Theme::GetDark(Theme::Role::Background);
	const wxColour raised = Theme::GetDark(Theme::Role::RaisedSurface);
	const wxColour text = Theme::GetDark(Theme::Role::Text);
	const wxColour subtle = Theme::GetDark(Theme::Role::TextSubtle);
	const wxColour accent(116, 76, 238);
	SetBackgroundColour(surface);

	auto* root = newd wxBoxSizer(wxVERTICAL);
	auto* title = newd wxStaticText(this, wxID_ANY, "Verify resources before pasting");
	wxFont titleFont = title->GetFont();
	titleFont.SetWeight(wxFONTWEIGHT_BOLD);
	titleFont.SetPointSize(titleFont.GetPointSize() + 1);
	title->SetFont(titleFont);
	StyleText(title, text, surface);
	root->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 14));

	auto* explanation = newd wxStaticText(this, wxID_ANY, "Review exact sprite matches and ID remaps. Nothing has been changed yet.");
	StyleText(explanation, subtle, surface);
	root->Add(explanation, 0, wxLEFT | wxRIGHT | wxTOP, FROM_DIP(this, 5));

	auto* resourceGrid = newd wxFlexGridSizer(2, FROM_DIP(this, 4), FROM_DIP(this, 12));
	resourceGrid->AddGrowableCol(1, 1);
	auto addResource = [&](const wxString& labelText, const wxString& value, const wxString& tooltip) {
		auto* label = newd wxStaticText(this, wxID_ANY, labelText);
		auto* resource = newd wxStaticText(this, wxID_ANY, CompactPath(value));
		StyleText(label, subtle, surface);
		StyleText(resource, text, surface);
		resource->SetToolTip(tooltip);
		resourceGrid->Add(label, 0, wxALIGN_CENTER_VERTICAL);
		resourceGrid->Add(resource, 1, wxEXPAND);
	};
	addResource("Source", analysis.sourceClient, analysis.sourceClient + "\n" + analysis.sourceServer);
	addResource("Destination", analysis.destinationClient, analysis.destinationClient + "\n" + analysis.destinationServer);
	root->Add(resourceGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FROM_DIP(this, 14));

	auto* summaryPanel = newd wxPanel(this, wxID_ANY);
	summaryPanel->SetBackgroundColour(raised);
	auto* summary = newd wxBoxSizer(wxHORIZONTAL);
	auto addSummary = [&](const wxString& label, uint32_t value, const wxColour& colour) {
		auto* valueLabel = newd wxStaticText(summaryPanel, wxID_ANY, wxString::Format("%s %u", label.c_str(), value));
		wxFont font = valueLabel->GetFont();
		font.SetWeight(wxFONTWEIGHT_BOLD);
		valueLabel->SetFont(font);
		StyleText(valueLabel, colour, raised);
		summary->Add(valueLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 20));
	};
	summary->AddSpacer(FROM_DIP(this, 10));
	addSummary("Matched", analysis.matched, wxColour(51, 201, 111));
	addSummary("Remapped", analysis.remapped, wxColour(65, 205, 230));
	addSummary("Missing", analysis.missing, wxColour(238, 90, 105));
	auto* occurrences = newd wxStaticText(summaryPanel, wxID_ANY, wxString::Format("%u item instances", analysis.totalOccurrences));
	StyleText(occurrences, subtle, raised);
	summary->AddStretchSpacer();
	summary->Add(occurrences, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 10));
	summaryPanel->SetSizer(summary);
	summaryPanel->SetMinSize(wxSize(-1, FROM_DIP(this, 30)));
	root->Add(summaryPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 14));

	itemList = newd wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_VRULES | wxBORDER_SIMPLE);
	itemList->SetBackgroundColour(background);
	itemList->SetForegroundColour(text);
	itemList->InsertColumn(0, "Source item");
	itemList->InsertColumn(1, "Destination item");
	itemList->InsertColumn(2, "Result");
	itemList->InsertColumn(3, "Uses", wxLIST_FORMAT_RIGHT);
	root->Add(itemList, 1, wxEXPAND | wxLEFT | wxRIGHT, FROM_DIP(this, 16));
	PopulateRows();
	itemList->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
		UpdateColumnWidths();
		event.Skip();
	});

	auto* footer = newd wxBoxSizer(wxHORIZONTAL);
	if (analysis.missing != 0) {
		auto* warning = newd wxStaticText(this, wxID_ANY, "Missing binary resources cannot be pasted safely. Nothing will be modified.");
		StyleText(warning, wxColour(238, 150, 70), surface);
		footer->Add(warning, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 12));
	} else {
		auto* ready = newd wxStaticText(this, wxID_ANY, "All item IDs are verified. The source tab will remain unchanged.");
		StyleText(ready, wxColour(51, 201, 111), surface);
		footer->Add(ready, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 12));
	}
	auto* cancel = newd wxButton(this, wxID_CANCEL, "Cancel");
	auto* apply = newd wxButton(this, wxID_OK, "Apply && Paste");
	const bool canApply = analysis.canApply();
	apply->SetBackgroundColour(canApply ? accent : raised);
	apply->SetForegroundColour(canApply ? Theme::GetDark(Theme::Role::TextOnAccent) : subtle);
	apply->Enable(canApply);
	if (!canApply) {
		apply->SetToolTip("Resolve every missing destination resource before applying this paste.");
	}
	footer->Add(cancel, 0, wxRIGHT, FROM_DIP(this, 8));
	footer->Add(apply, 0);
	root->Add(footer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FROM_DIP(this, 16));

	SetSizer(root);
	SetMinClientSize(FROM_DIP(this, wxSize(650, 410)));
	SetClientSize(FROM_DIP(this, wxSize(740, 480)));
	CentreOnParent();
	apply->SetDefault();
}

void CrossClientPasteDialog::PopulateRows() {
	const int imageSize = FROM_DIP(this, 32);
	auto* images = newd wxImageList(imageSize, imageSize, true, static_cast<int>(std::max<size_t>(1, analysis.rows.size())));
	for (size_t index = 0; index < analysis.rows.size(); ++index) {
		const CrossClientPasteRow& row = analysis.rows[index];
		const int imageIndex = images->Add(PreviewBitmap(row.source, imageSize));
		wxString sourceLabel = wxString::Format("%u", row.source.sourceId);
		if (!row.source.name.empty()) {
			sourceLabel += "  " + wxString::FromUTF8(row.source.name);
		}
		const long listIndex = itemList->InsertItem(static_cast<long>(index), sourceLabel, imageIndex);
		if (row.state == CrossClientMatchState::Missing) {
			itemList->SetItem(listIndex, 1, "Not found");
			itemList->SetItem(listIndex, 2, "!  Missing");
			itemList->SetItemTextColour(listIndex, wxColour(238, 90, 105));
		} else {
			wxString destination = wxString::Format("%u", row.destinationId);
			if (!row.destinationName.empty()) {
				destination += "  " + wxString::FromUTF8(row.destinationName);
			}
			itemList->SetItem(listIndex, 1, destination);
			const bool remapped = row.state == CrossClientMatchState::Remapped;
			itemList->SetItem(listIndex, 2, remapped ? "->  Remapped" : "OK  Matched");
			itemList->SetItemTextColour(listIndex, remapped ? wxColour(65, 205, 230) : Theme::GetDark(Theme::Role::Text));
		}
		itemList->SetItem(listIndex, 3, wxString::Format("%u", row.source.occurrences));
	}
	itemList->AssignImageList(images, wxIMAGE_LIST_SMALL);
	UpdateColumnWidths();
}

void CrossClientPasteDialog::UpdateColumnWidths() {
	if (!itemList) {
		return;
	}
	int scrollbarWidth = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, itemList);
	if (scrollbarWidth <= 0) {
		scrollbarWidth = FROM_DIP(this, 17);
	}
	// wxListCtrl reports the full client width even when the native vertical
	// scrollbar overlays its last report column on Windows. Keep an explicit
	// gutter so right-aligned occurrence counts never sit under the scrollbar.
	const int rightGutter = scrollbarWidth + FROM_DIP(this, 10);
	const int available = std::max(FROM_DIP(this, 580), itemList->GetClientSize().x - rightGutter);
	const int usesWidth = FROM_DIP(this, 68);
	const int resultWidth = FROM_DIP(this, 112);
	const int resourceWidth = std::max(FROM_DIP(this, 190), (available - usesWidth - resultWidth) / 2);
	itemList->SetColumnWidth(0, resourceWidth);
	itemList->SetColumnWidth(1, resourceWidth);
	itemList->SetColumnWidth(2, resultWidth);
	itemList->SetColumnWidth(3, std::max(usesWidth, available - resourceWidth * 2 - resultWidth));
}

wxString CrossClientPasteDialog::CompactPath(const wxString& path) const {
	if (path.length() <= 58) {
		return path;
	}
	return path.Left(18) + "..." + path.Right(36);
}
