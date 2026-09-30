// SPDX-License-Identifier: GPL-3.0-or-later
#include "main.h"

#include "reference_style_window.h"

#include "editor.h"
#include "graphics.h"
#include "gui.h"
#include "item.h"
#include "items.h"
#include "map.h"
#include "reference_style.h"
#include "reference_style_store.h"
#include "tile.h"
#include "workspace_session.h"

#include <algorithm>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>

#include <wx/button.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>

namespace {

	ReferenceStyleWindow*& ReferenceStyleWindowInstance() {
		static ReferenceStyleWindow* instance = nullptr;
		return instance;
	}

	constexpr int ThumbnailSize = 160;

	wxBitmap CreateEmptyThumbnail() {
		wxBitmap bmp(ThumbnailSize, ThumbnailSize, 32);
		wxMemoryDC dc(bmp);
		dc.SetBackground(wxBrush(wxColour(18, 20, 26)));
		dc.Clear();
		dc.SetTextForeground(wxColour(120, 130, 145));
		dc.DrawLabel("No Reference", wxRect(0, 0, ThumbnailSize, ThumbnailSize), wxALIGN_CENTER);
		dc.SelectObject(wxNullBitmap);
		return bmp;
	}

	wxBitmap RenderThumbnail(const ReferenceStyleSnapshot& snapshot) {
		const int width = snapshot.sourceMax.x - snapshot.sourceMin.x + 1;
		const int height = snapshot.sourceMax.y - snapshot.sourceMin.y + 1;
		if (width <= 0 || height <= 0) {
			return CreateEmptyThumbnail();
		}

		const int maxDim = std::max(width, height);
		const int tileSize = std::max(1, std::min(32, ThumbnailSize / maxDim));
		const int renderW = width * tileSize;
		const int renderH = height * tileSize;

		wxBitmap contentBmp(renderW, renderH, 32);
		wxMemoryDC dc(contentBmp);
		dc.SetBackground(wxBrush(wxColour(12, 14, 18)));
		dc.Clear();

		const bool compatibleSprites = snapshot.workspaceGeneration == g_workspace.getGeneration() && !g_gui.gfx.isUnloaded();
		for (const auto& cell : snapshot.renderCells) {
			const int drawX = cell.localX * tileSize;
			const int drawY = cell.localY * tileSize;
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(wxColour(60, 80, 100)));
			dc.DrawRectangle(drawX, drawY, tileSize, tileSize);
			if (compatibleSprites) {
				for (uint16_t clientId : cell.clientIds) {
					if (clientId != 0) {
						if (Sprite* sprite = g_gui.gfx.getSprite(clientId)) {
							sprite->DrawTo(&dc, tileSize <= 16 ? SPRITE_SIZE_16x16 : SPRITE_SIZE_32x32, drawX, drawY, tileSize, tileSize);
						}
					}
				}
			}
		}
		dc.SelectObject(wxNullBitmap);

		// Center onto ThumbnailSize x ThumbnailSize bitmap.
		wxBitmap finalBmp(ThumbnailSize, ThumbnailSize, 32);
		wxMemoryDC finalDc(finalBmp);
		finalDc.SetBackground(wxBrush(wxColour(18, 20, 26)));
		finalDc.Clear();
		const double scale = std::min(1.0, static_cast<double>(ThumbnailSize) / std::max(renderW, renderH));
		const int scaledW = std::max(1, static_cast<int>(renderW * scale));
		const int scaledH = std::max(1, static_cast<int>(renderH * scale));
		const wxBitmap scaled = (scaledW == renderW && scaledH == renderH) ? contentBmp : wxBitmap(contentBmp.ConvertToImage().Scale(scaledW, scaledH));
		finalDc.DrawBitmap(scaled, (ThumbnailSize - scaledW) / 2, (ThumbnailSize - scaledH) / 2, false);
		finalDc.SelectObject(wxNullBitmap);
		return finalBmp;
	}

	constexpr const char* ExamplePromptTemplate = "Use reference_get and reference_render as SOURCE STYLE ONLY.\n\n"
												  "Call target_get and create the room only inside its exact target positions.\n"
												  "Never write in the reference source bounds or source mask.\n\n"
												  "Match the reference floors, walls, borders, and decoration density without copying protected metadata.\n"
												  "Use only loaded NexaMap resources.\n\n"
												  "Render and validate the target, then fix any border or path problems inside the target.";

} // namespace

void ReferenceStyleWindow::Open(wxWindow* parent) {
	if (ReferenceStyleWindowInstance()) {
		ReferenceStyleWindowInstance()->Raise();
		ReferenceStyleWindowInstance()->SetFocus();
		ReferenceStyleWindowInstance()->RefreshView();
		return;
	}

	ReferenceStyleWindowInstance() = newd ReferenceStyleWindow(parent);
	ReferenceStyleWindowInstance()->Show();
}

void ReferenceStyleWindow::CaptureAndOpen(wxWindow* parent) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		wxMessageBox("No map is currently open.", "AI Style Reference", wxOK | wxICON_INFORMATION, parent);
		return;
	}
	if (!editor->hasSelection()) {
		wxMessageBox("No map tiles are selected. Select an area in your map and try again.", "AI Style Reference", wxOK | wxICON_INFORMATION, parent);
		return;
	}

	ReferenceStyleCaptureOptions options;
	options.floor = g_gui.GetCurrentFloor();
	options.maxTiles = 65536;

	ReferenceStyleSnapshot snapshot = ReferenceStyleAnalyzer::Capture(editor->selection, editor->map, options);
	if (snapshot.selectedTileCount == 0) {
		wxMessageBox(snapshot.captureError.empty() ? "The selection has no tiles on the current floor." : snapshot.captureError, "AI Style Reference", wxOK | wxICON_WARNING, parent);
		return;
	}

	ReferenceStyleStore::Instance().set(std::move(snapshot));
	// This selection was the source reference, never an implicit generation target.
	editor->selection.clear();
	g_gui.RefreshView();
	Open(parent);
}

ReferenceStyleWindow::ReferenceStyleWindow(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "AI Style Reference", wxDefaultPosition, wxSize(420, 560), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
	BuildLayout();
	BindEvents();
	RefreshView();
	CentreOnParent();
}

ReferenceStyleWindow::~ReferenceStyleWindow() {
	if (ReferenceStyleWindowInstance() == this) {
		ReferenceStyleWindowInstance() = nullptr;
	}
}

void ReferenceStyleWindow::BuildLayout() {
	auto* rootSizer = newd wxBoxSizer(wxVERTICAL);

	// Preview box.
	auto* previewBox = newd wxBoxSizer(wxHORIZONTAL);
	thumbnailBitmap_ = newd wxStaticBitmap(this, wxID_ANY, CreateEmptyThumbnail(), wxDefaultPosition, wxSize(ThumbnailSize, ThumbnailSize));
	previewBox->Add(thumbnailBitmap_, 0, wxALIGN_CENTER | wxALL, FromDIP(8));
	rootSizer->Add(previewBox, 0, wxALIGN_CENTER | wxTOP, FromDIP(4));

	// Status & metrics.
	statusLabel_ = newd wxStaticText(this, wxID_ANY, "Reference Source: Not selected", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
	wxFont boldFont = statusLabel_->GetFont();
	boldFont.SetWeight(wxFONTWEIGHT_BOLD);
	statusLabel_->SetFont(boldFont);
	rootSizer->Add(statusLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

	targetStatusLabel_ = newd wxStaticText(this, wxID_ANY, "Target Area: Not selected", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
	rootSizer->Add(targetStatusLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));

	workflowLabel_ = newd wxStaticText(this, wxID_ANY, "Capture a reference source, then explicitly set a different target area.", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
	workflowLabel_->Wrap(FromDIP(380));
	rootSizer->Add(workflowLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

	metricsLabel_ = newd wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
	rootSizer->Add(metricsLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));

	brushesLabel_ = newd wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
	rootSizer->Add(brushesLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

	// Materials list.
	rootSizer->Add(newd wxStaticText(this, wxID_ANY, "Captured Vocabulary & Materials:"), 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
	materialsListBox_ = newd wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(380, 160));
	rootSizer->Add(materialsListBox_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	// Action buttons.
	auto* actionRow1 = newd wxBoxSizer(wxHORIZONTAL);
	refreshButton_ = newd wxButton(this, wxID_REFRESH, "Capture Reference");
	clearButton_ = newd wxButton(this, wxID_CLEAR, "Clear Reference");
	actionRow1->Add(refreshButton_, 1, wxRIGHT, FromDIP(6));
	actionRow1->Add(clearButton_, 1);
	rootSizer->Add(actionRow1, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

	auto* actionRow2 = newd wxBoxSizer(wxHORIZONTAL);
	setTargetButton_ = newd wxButton(this, wxID_ANY, "Set Current Selection as Target");
	clearTargetButton_ = newd wxButton(this, wxID_ANY, "Clear Target");
	actionRow2->Add(setTargetButton_, 1, wxRIGHT, FromDIP(6));
	actionRow2->Add(clearTargetButton_, 1);
	rootSizer->Add(actionRow2, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

	auto* actionRow3 = newd wxBoxSizer(wxHORIZONTAL);
	copyPromptButton_ = newd wxButton(this, wxID_COPY, "Copy Example Prompt");
	copyDataButton_ = newd wxButton(this, wxID_ANY, "Copy IDs & Materials");
	auto* closeButton = newd wxButton(this, wxID_CLOSE, "Close");
	actionRow3->Add(copyPromptButton_, 1, wxRIGHT, FromDIP(6));
	actionRow3->Add(copyDataButton_, 1, wxRIGHT, FromDIP(6));
	actionRow3->Add(closeButton, 1);
	rootSizer->Add(actionRow3, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	SetSizer(rootSizer);
}

void ReferenceStyleWindow::BindEvents() {
	Bind(wxEVT_CLOSE_WINDOW, &ReferenceStyleWindow::OnClose, this);
	refreshButton_->Bind(wxEVT_BUTTON, &ReferenceStyleWindow::OnRefreshFromSelection, this);
	clearButton_->Bind(wxEVT_BUTTON, &ReferenceStyleWindow::OnClearReference, this);
	setTargetButton_->Bind(wxEVT_BUTTON, &ReferenceStyleWindow::OnSetCurrentSelectionAsTarget, this);
	clearTargetButton_->Bind(wxEVT_BUTTON, &ReferenceStyleWindow::OnClearTarget, this);
	copyPromptButton_->Bind(wxEVT_BUTTON, &ReferenceStyleWindow::OnCopyExamplePrompt, this);
	copyDataButton_->Bind(wxEVT_BUTTON, &ReferenceStyleWindow::OnCopyReferenceData, this);
	Bind(wxEVT_BUTTON, &ReferenceStyleWindow::OnCloseButton, this, wxID_CLOSE);
}

void ReferenceStyleWindow::RefreshView() {
	const std::optional<ReferenceStyleSnapshot> snapshot = ReferenceStyleStore::Instance().getSnapshot();
	if (!snapshot) {
		thumbnailBitmap_->SetBitmap(CreateEmptyThumbnail());
		statusLabel_->SetLabel("Reference Source: Not selected");
		targetStatusLabel_->SetLabel("Target Area: Not selected");
		workflowLabel_->SetLabel("Select a source area and click 'Capture Reference'.");
		metricsLabel_->SetLabel("The reference source is read-only and is never an automatic target.");
		brushesLabel_->SetLabel("");
		materialsListBox_->Clear();
		materialsListBox_->Append("(No materials captured)");
		clearButton_->Enable(false);
		setTargetButton_->Enable(false);
		clearTargetButton_->Enable(false);
		copyPromptButton_->Enable(false);
		copyDataButton_->Enable(false);
		return;
	}

	clearButton_->Enable(true);
	setTargetButton_->Enable(true);
	copyPromptButton_->Enable(true);
	copyDataButton_->Enable(true);

	// Update thumbnail.
	thumbnailBitmap_->SetBitmap(RenderThumbnail(*snapshot));

	// Status text.
	std::ostringstream ssStatus;
	ssStatus << "Reference Source: " << snapshot->selectedTileCount << " tiles \u2713";
	statusLabel_->SetLabel(ssStatus.str());

	const std::optional<TargetAreaSnapshot> target = ReferenceStyleStore::Instance().getTargetSnapshot();
	if (snapshot->workspaceGeneration != g_workspace.getGeneration()) {
		targetStatusLabel_->SetLabel("Target Area: Unavailable (resources changed)");
		workflowLabel_->SetLabel("The captured reference belongs to an older resource session. Capture it again before generating.");
		clearTargetButton_->Enable(target.has_value());
	} else if (target) {
		targetStatusLabel_->SetLabel("Target Area: " + std::to_string(target->positions.size()) + " tiles \u2713");
		workflowLabel_->SetLabel("AI writes are restricted to the captured target area. The reference source remains read-only.");
		clearTargetButton_->Enable(true);
	} else {
		targetStatusLabel_->SetLabel("Target Area: Not selected");
		workflowLabel_->SetLabel("Reference captured. Now select a different target area.");
		clearTargetButton_->Enable(false);
	}

	// Metrics.
	std::ostringstream ssMetrics;
	ssMetrics << "Floor: " << snapshot->floor << " | Visible IDs: " << snapshot->items.size()
			  << " | Bounds: [" << snapshot->sourceMin.x << "," << snapshot->sourceMin.y << "]-["
			  << snapshot->sourceMax.x << "," << snapshot->sourceMax.y << "]";
	metricsLabel_->SetLabel(ssMetrics.str());

	// Brushes breakdown.
	std::ostringstream ssBrushes;
	ssBrushes << "Ground: " << snapshot->groundFamilies.size()
			  << " | Walls: " << snapshot->layout.uniqueWallBrushCount
			  << " | Doodads: " << snapshot->layout.uniqueDoodadBrushCount;
	brushesLabel_->SetLabel(ssBrushes.str());

	// Materials list.
	materialsListBox_->Clear();

	// Top ground families.
	for (const auto& gf : snapshot->groundFamilies) {
		std::ostringstream entry;
		entry << "[Ground] " << gf.name << " (" << std::fixed << std::setprecision(1) << (gf.ratio * 100.0) << "%, " << gf.tiles << " tiles)";
		materialsListBox_->Append(entry.str());
	}

	// Brushes.
	for (const auto& bu : snapshot->brushes) {
		if (bu.kind == "ground") {
			continue; // already shown above
		}
		std::ostringstream entry;
		entry << "[" << bu.kind << "] " << bu.name << " (" << bu.count << " placed)";
		materialsListBox_->Append(entry.str());
	}

	// Top decorative items.
	for (const auto& item : snapshot->items) {
		if (item.category == "Doodad" || item.category == "Item") {
			std::ostringstream entry;
			entry << "[Item] " << item.name << " (ID " << item.activeId << ", x" << item.count << ")";
			materialsListBox_->Append(entry.str());
		}
	}
}

void ReferenceStyleWindow::OnRefreshFromSelection(wxCommandEvent& WXUNUSED(event)) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor || !editor->hasSelection()) {
		wxMessageBox("No map tiles are selected. Select an area in your map first.", "AI Style Reference", wxOK | wxICON_INFORMATION, this);
		return;
	}

	ReferenceStyleCaptureOptions options;
	options.floor = g_gui.GetCurrentFloor();
	options.maxTiles = 65536;

	ReferenceStyleSnapshot snapshot = ReferenceStyleAnalyzer::Capture(editor->selection, editor->map, options);
	if (snapshot.selectedTileCount == 0) {
		wxMessageBox(snapshot.captureError.empty() ? "The selection contains no tiles on the current floor." : snapshot.captureError, "AI Style Reference", wxOK | wxICON_WARNING, this);
		return;
	}

	ReferenceStyleStore::Instance().set(std::move(snapshot));
	// Clear the former source selection so it cannot be mistaken for a target.
	editor->selection.clear();
	g_gui.RefreshView();
	RefreshView();
}

void ReferenceStyleWindow::OnClearReference(wxCommandEvent& WXUNUSED(event)) {
	ReferenceStyleStore::Instance().clear();
	RefreshView();
}

void ReferenceStyleWindow::OnSetCurrentSelectionAsTarget(wxCommandEvent& WXUNUSED(event)) {
	const std::optional<ReferenceStyleSnapshot> reference = ReferenceStyleStore::Instance().getSnapshot();
	Editor* editor = g_gui.GetCurrentEditor();
	if (!reference || !editor || !editor->hasSelection()) {
		wxMessageBox("Capture a reference, then select a different target area first.", "AI Style Reference", wxOK | wxICON_INFORMATION, this);
		return;
	}

	TargetAreaSnapshot target = CaptureTargetAreaSnapshot(
		editor->selection,
		g_gui.GetCurrentFloor(),
		editor->map.getSessionId(),
		g_workspace.getGeneration()
	);
	if (target.positions.empty()) {
		wxMessageBox("The selection contains no tiles on the current floor.", "AI Style Reference", wxOK | wxICON_WARNING, this);
		return;
	}
	if (ReferenceSourceOverlapsTarget(*reference, target)) {
		wxMessageBox("Target area overlaps the captured reference source. Select a different target area.", "AI Style Reference", wxOK | wxICON_WARNING, this);
		return;
	}

	ReferenceStyleStore::Instance().setTarget(std::move(target));
	RefreshView();
}

void ReferenceStyleWindow::OnClearTarget(wxCommandEvent& WXUNUSED(event)) {
	ReferenceStyleStore::Instance().clearTarget();
	RefreshView();
}

void ReferenceStyleWindow::OnCopyExamplePrompt(wxCommandEvent& WXUNUSED(event)) {
	if (wxTheClipboard->Open()) {
		wxTheClipboard->SetData(new wxTextDataObject(ExamplePromptTemplate));
		wxTheClipboard->Close();
		wxMessageBox("Example prompt copied to clipboard!\n\nYou can now paste it directly into Codex or Claude.", "AI Style Reference", wxOK | wxICON_INFORMATION, this);
	}
}

void ReferenceStyleWindow::OnCopyReferenceData(wxCommandEvent& WXUNUSED(event)) {
	const std::optional<ReferenceStyleSnapshot> snapshot = ReferenceStyleStore::Instance().getSnapshot();
	if (!snapshot) {
		return;
	}
	std::ostringstream output;
	output << "AI STYLE REFERENCE\nAsset mode: "
		   << (snapshot->assetMode == ReferenceAssetMode::Appearances ? "Appearances" : snapshot->assetMode == ReferenceAssetMode::ClassicDatSpr ? "ClassicDatSpr"
																																				 : "Unknown")
		   << "\nItem ID mode: "
		   << (snapshot->itemIdMode == ItemIdMode::ClientId ? "ClientId" : snapshot->itemIdMode == ItemIdMode::ServerId ? "ServerId"
																														: "Unknown")
		   << "\nFloor: " << snapshot->floor << "\nTiles: " << snapshot->selectedTileCount << "\n";
	const std::pair<const char*, const char*> sections[] = {
		{ "GROUNDS", "Floor" },
		{ "WALLS", "Wall" },
		{ "BORDERS", "Border" },
		{ "DOORS", "Door" },
		{ "CARPETS", "Carpet" },
		{ "TABLES", "Table" },
		{ "DOODADS", "Doodad" },
		{ "RAW / ITEMS", "Item" },
	};
	for (const auto& [heading, category] : sections) {
		output << "\n[" << heading << "]\n";
		for (const auto& item : snapshot->items) {
			if (item.category == category) {
				output << "ActiveID: " << item.activeId << " | ServerID: " << item.serverId << " | ClientID: " << item.clientId
					   << " | Name: " << item.name << " | Count: " << item.count << " | Ratio: " << item.ratio
					   << " | Brush: " << item.brush << " | SourceVerified: " << (item.sourceVerified ? "yes" : "no") << '\n';
			}
		}
	}
	output << "\n[GROUND FAMILIES]\n";
	for (const auto& family : snapshot->groundFamilies) {
		output << "Name: " << family.name << " | Representative ID: " << family.representativeItemId
			   << " | Tiles: " << family.tiles << " | Ratio: " << family.ratio << " | Observed IDs:";
		for (uint16_t id : family.itemIds) {
			output << ' ' << id;
		}
		output << '\n';
	}
	output << "\n[BORDER TRANSITIONS]\n";
	for (const auto& transition : snapshot->transitions) {
		output << "Ground A: " << transition.groundA << " | Ground B: " << transition.groundB << " | Border IDs:";
		for (uint16_t id : transition.borderItemIds) {
			output << ' ' << id;
		}
		output << " | Alignments:";
		for (const auto& alignment : transition.borderAlignments) {
			output << ' ' << alignment;
		}
		output << '\n';
	}
	output << "\n[BRUSHES]\n";
	for (const auto& brush : snapshot->brushes) {
		output << "Kind: " << brush.kind << " | Name: " << brush.name << " | Look ID: " << brush.lookId << " | Tilesets:";
		for (const auto& tileset : brush.tilesets) {
			output << ' ' << tileset;
		}
		output << '\n';
	}
	if (wxTheClipboard->Open()) {
		const std::string text = output.str();
		wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(text.c_str())));
		wxTheClipboard->Close();
	}
}

void ReferenceStyleWindow::OnCloseButton(wxCommandEvent& WXUNUSED(event)) {
	Close();
}

void ReferenceStyleWindow::OnClose(wxCloseEvent& WXUNUSED(event)) {
	Destroy();
}
