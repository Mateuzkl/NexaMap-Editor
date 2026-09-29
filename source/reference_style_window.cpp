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

#include <algorithm>
#include <iomanip>
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
	const int tileSize = std::max(2, std::min(32, ThumbnailSize / maxDim));
	const int renderW = width * tileSize;
	const int renderH = height * tileSize;

	wxBitmap contentBmp(renderW, renderH, 32);
	wxMemoryDC dc(contentBmp);
	dc.SetBackground(wxBrush(wxColour(12, 14, 18)));
	dc.Clear();

	// Check if current editor has the map available.
	Editor* editor = g_gui.GetCurrentEditor();
	const Map* map = editor ? &editor->map : nullptr;

	std::set<std::pair<int16_t, int16_t>> mask;
	for (const auto& cell : snapshot.cells) {
		mask.insert({ cell.localX, cell.localY });
	}

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const int drawX = x * tileSize;
			const int drawY = y * tileSize;

			if (!mask.contains({ static_cast<int16_t>(x), static_cast<int16_t>(y) })) {
				continue;
			}

			if (map && !g_gui.gfx.isUnloaded()) {
				const Tile* tile = map->getTile(snapshot.sourceMin.x + x, snapshot.sourceMin.y + y, snapshot.floor);
				if (tile) {
					if (tile->ground) {
						Sprite* sprite = g_gui.gfx.getSprite(tile->ground->getClientID());
						if (sprite) {
							sprite->DrawTo(&dc, tileSize <= 16 ? SPRITE_SIZE_16x16 : SPRITE_SIZE_32x32, drawX, drawY, tileSize, tileSize);
						}
					}
					for (const Item* item : tile->items) {
						if (item && !item->isMetaItem()) {
							Sprite* sprite = g_gui.gfx.getSprite(item->getClientID());
							if (sprite) {
								sprite->DrawTo(&dc, tileSize <= 16 ? SPRITE_SIZE_16x16 : SPRITE_SIZE_32x32, drawX, drawY, tileSize, tileSize);
							}
						}
					}
					continue;
				}
			}

			// Fallback: draw topology color.
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(wxColour(60, 80, 100)));
			dc.DrawRectangle(drawX, drawY, tileSize, tileSize);
		}
	}
	dc.SelectObject(wxNullBitmap);

	// Center onto ThumbnailSize x ThumbnailSize bitmap.
	wxBitmap finalBmp(ThumbnailSize, ThumbnailSize, 32);
	wxMemoryDC finalDc(finalBmp);
	finalDc.SetBackground(wxBrush(wxColour(18, 20, 26)));
	finalDc.Clear();
	const int offsetX = (ThumbnailSize - renderW) / 2;
	const int offsetY = (ThumbnailSize - renderH) / 2;
	finalDc.DrawBitmap(contentBmp, std::max(0, offsetX), std::max(0, offsetY), false);
	finalDc.SelectObject(wxNullBitmap);
	return finalBmp;
}

constexpr const char* ExamplePromptTemplate =
	"Use the captured NexaMap reference style.\n\n"
	"Create a polished 15x15 teleport room in the current selected area.\n"
	"Keep the same visual language as the reference: floors, walls, borders, and decoration density.\n\n"
	"Keep a clear walkable center and a strong focal point for the teleport.\n"
	"Do not copy unique IDs, action IDs or teleport destinations from the source.\n\n"
	"Use only loaded NexaMap resources.\n"
	"Render and validate the final area and fix any border/path problems.";

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
		wxMessageBox("The selection has no tiles on the current floor.", "AI Style Reference", wxOK | wxICON_WARNING, parent);
		return;
	}

	ReferenceStyleStore::Instance().set(std::move(snapshot));
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
	statusLabel_ = newd wxStaticText(this, wxID_ANY, "No reference style captured.", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
	wxFont boldFont = statusLabel_->GetFont();
	boldFont.SetWeight(wxFONTWEIGHT_BOLD);
	statusLabel_->SetFont(boldFont);
	rootSizer->Add(statusLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

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
	refreshButton_ = newd wxButton(this, wxID_REFRESH, "Refresh from Selection");
	clearButton_ = newd wxButton(this, wxID_CLEAR, "Clear Reference");
	actionRow1->Add(refreshButton_, 1, wxRIGHT, FromDIP(6));
	actionRow1->Add(clearButton_, 1);
	rootSizer->Add(actionRow1, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

	auto* actionRow2 = newd wxBoxSizer(wxHORIZONTAL);
	copyPromptButton_ = newd wxButton(this, wxID_COPY, "Copy Example Prompt");
	auto* closeButton = newd wxButton(this, wxID_CLOSE, "Close");
	actionRow2->Add(copyPromptButton_, 1, wxRIGHT, FromDIP(6));
	actionRow2->Add(closeButton, 1);
	rootSizer->Add(actionRow2, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	SetSizer(rootSizer);
}

void ReferenceStyleWindow::BindEvents() {
	Bind(wxEVT_CLOSE_WINDOW, &ReferenceStyleWindow::OnClose, this);
	refreshButton_->Bind(wxEVT_BUTTON, &ReferenceStyleWindow::OnRefreshFromSelection, this);
	clearButton_->Bind(wxEVT_BUTTON, &ReferenceStyleWindow::OnClearReference, this);
	copyPromptButton_->Bind(wxEVT_BUTTON, &ReferenceStyleWindow::OnCopyExamplePrompt, this);
	Bind(wxEVT_BUTTON, &ReferenceStyleWindow::OnCloseButton, this, wxID_CLOSE);
}

void ReferenceStyleWindow::RefreshView() {
	const ReferenceStyleSnapshot* snapshot = ReferenceStyleStore::Instance().get();
	if (!snapshot) {
		thumbnailBitmap_->SetBitmap(CreateEmptyThumbnail());
		statusLabel_->SetLabel("No reference style captured.");
		metricsLabel_->SetLabel("Select an area in your map and click 'Refresh from Selection'.");
		brushesLabel_->SetLabel("");
		materialsListBox_->Clear();
		materialsListBox_->Append("(No materials captured)");
		clearButton_->Enable(false);
		copyPromptButton_->Enable(false);
		return;
	}

	clearButton_->Enable(true);
	copyPromptButton_->Enable(true);

	// Update thumbnail.
	thumbnailBitmap_->SetBitmap(RenderThumbnail(*snapshot));

	// Status text.
	std::ostringstream ssStatus;
	ssStatus << "Reference Captured: " << snapshot->selectedTileCount << " tiles (Floor " << snapshot->floor << ")";
	statusLabel_->SetLabel(ssStatus.str());

	// Metrics.
	std::ostringstream ssMetrics;
	ssMetrics << "Visible IDs: " << snapshot->items.size()
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
		if (bu.kind == "ground") continue; // already shown above
		std::ostringstream entry;
		entry << "[" << bu.kind << "] " << bu.name << " (" << bu.count << " placed)";
		materialsListBox_->Append(entry.str());
	}

	// Top decorative items.
	for (const auto& item : snapshot->items) {
		if (item.category == "doodad" || item.category == "item") {
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
		wxMessageBox("The selection contains no tiles on the current floor.", "AI Style Reference", wxOK | wxICON_WARNING, this);
		return;
	}

	ReferenceStyleStore::Instance().set(std::move(snapshot));
	RefreshView();
}

void ReferenceStyleWindow::OnClearReference(wxCommandEvent& WXUNUSED(event)) {
	ReferenceStyleStore::Instance().clear();
	RefreshView();
}

void ReferenceStyleWindow::OnCopyExamplePrompt(wxCommandEvent& WXUNUSED(event)) {
	if (wxTheClipboard->Open()) {
		wxTheClipboard->SetData(new wxTextDataObject(ExamplePromptTemplate));
		wxTheClipboard->Close();
		wxMessageBox("Example prompt copied to clipboard!\n\nYou can now paste it directly into Codex or Claude.", "AI Style Reference", wxOK | wxICON_INFORMATION, this);
	}
}

void ReferenceStyleWindow::OnCloseButton(wxCommandEvent& WXUNUSED(event)) {
	Close();
}

void ReferenceStyleWindow::OnClose(wxCloseEvent& WXUNUSED(event)) {
	Destroy();
}
