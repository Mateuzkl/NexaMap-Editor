//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "../main.h"

#include "ingame_preview_window.h"

#include "../editor.h"
#include "../complexitem.h"
#include "../gui.h"
#include "../map_display.h"
#include "../map_tab.h"
#include "../map_window.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace {
	constexpr int PreviewUpdateIntervalMs = 16;
	constexpr int PreviewPlayerSpeed = 220;
}

IngamePreviewWindow::IngamePreviewWindow(wxWindow* parent) :
	wxPanel(parent, wxID_ANY),
	mainSizer(newd wxBoxSizer(wxVERTICAL)),
	followSelection(newd wxCheckBox(this, wxID_ANY, "Follow selection")),
	lightingEnabled(newd wxCheckBox(this, wxID_ANY, "Lighting")),
	statusText(newd wxStaticText(this, wxID_ANY, "Open a map to preview.")),
	movementStatus(newd wxStaticText(this, wxID_ANY, "Click the preview, then use arrows to walk; Ctrl + arrow turns; Alt ignores collision.")),
	previewView(nullptr),
	editor(nullptr),
	timer(this) {
	followSelection->SetValue(true);
	followSelection->SetToolTip("Center the 15 x 11 preview on the current selection, or on the editor camera when nothing is selected.");

	auto* toolbar = newd wxBoxSizer(wxHORIZONTAL);
	toolbar->Add(followSelection, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 12));
	toolbar->Add(lightingEnabled, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FROM_DIP(this, 12));
	toolbar->Add(newd wxStaticText(this, wxID_ANY, "Viewport: 15 x 11 | Current floor | In-game rendering"), 0, wxALIGN_CENTER_VERTICAL);

	mainSizer->Add(toolbar, 0, wxEXPAND | wxALL, FROM_DIP(this, 6));
	mainSizer->Add(movementStatus, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 6));
	mainSizer->Add(statusText, 0, wxALIGN_CENTER | wxALL, FROM_DIP(this, 12));
	SetSizer(mainSizer);

	Bind(wxEVT_TIMER, &IngamePreviewWindow::OnTimer, this);
	Bind(wxEVT_CHAR_HOOK, &IngamePreviewWindow::OnKeyDown, this);
	Bind(wxEVT_CLOSE_WINDOW, &IngamePreviewWindow::OnClose, this);
	Bind(wxEVT_DESTROY, &IngamePreviewWindow::OnDestroy, this);
	lightingEnabled->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
		if (previewView) {
			previewView->GetCanvas()->SetIngamePreviewLighting(lightingEnabled->GetValue());
		}
	});
	timer.Start(PreviewUpdateIntervalMs);
	UpdateState();
}

IngamePreviewWindow::~IngamePreviewWindow() {
	timer.Stop();
}

void IngamePreviewWindow::UpdateState() {
	if (!IsShownOnScreen()) {
		return;
	}

	MapTab* mapTab = g_gui.GetCurrentMapTab();
	Editor* activeEditor = mapTab ? mapTab->GetEditor() : nullptr;
	SyncEditor(activeEditor);
	if (!previewView || !mapTab) {
		return;
	}

	MapCanvas* sourceCanvas = mapTab->GetCanvas();
	const int currentFloor = sourceCanvas->GetFloor();
	if (!walking && followSelection->GetValue()) {
		Position target;
		if (activeEditor->selection.size() != 0) {
			const Position min = activeEditor->selection.minPosition();
			const Position max = activeEditor->selection.maxPosition();
			target.x = min.x + (max.x - min.x) / 2;
			target.y = min.y + (max.y - min.y) / 2;
			target.z = currentFloor;
		} else {
			target = mapTab->GetScreenCenterPosition();
			target.z = currentFloor;
		}
		if (target.isValid() && target != lastTarget) {
			playerPosition = target;
			lastTarget = target;
			walkQueue.clear();
		}
	}

	if (!playerPosition.isValid()) {
		playerPosition = mapTab->GetScreenCenterPosition();
		playerPosition.z = currentFloor;
	}

	UpdateWalk();
	ApplyPreviewState();
}

void IngamePreviewWindow::ResetMovement() {
	walking = false;
	walkQueue.clear();
	playerPosition = Position(-1, -1, -1);
	walkOffsetX = 0;
	walkOffsetY = 0;
	animationFrame = 0;
	playerDirection = SOUTH;
	lastTarget = Position(-1, -1, -1);
	movementStatus->SetLabel("Click the preview, then use arrows to walk; Ctrl + arrow turns; Alt ignores collision.");
}

void IngamePreviewWindow::BufferWalk(Direction direction, bool ignoreCollision) {
	if (walkQueue.size() >= 2) {
		return;
	}
	if (!walkQueue.empty() && walkQueue.back().direction == direction) {
		return;
	}
	walkQueue.push_back({ direction, ignoreCollision });
	if (!walking) {
		UpdateWalk();
		ApplyPreviewState();
	}
}

void IngamePreviewWindow::StartWalk(Direction direction, bool ignoreCollision) {
	if (walking || !playerPosition.isValid()) {
		return;
	}

	Position target = playerPosition;
	switch (direction) {
		case NORTH:
			--target.y;
			break;
		case EAST:
			++target.x;
			break;
		case SOUTH:
			++target.y;
			break;
		case WEST:
			--target.x;
			break;
		default:
			return;
	}
	if (!CanWalk(target, ignoreCollision)) {
		walkQueue.clear();
		movementStatus->SetLabel(wxString::Format("Blocked at %d, %d, %d%s", target.x, target.y, target.z, ignoreCollision ? "" : " (hold Alt to ignore collision)"));
		return;
	}

	playerPosition = target;
	playerDirection = direction;
	walkDirection = direction;
	walking = true;
	walkStartedAt = wxGetLocalTimeMillis().GetValue();
	const Tile* targetTile = editor ? editor->map.getTile(target) : nullptr;
	walkDuration = GetStepDuration(targetTile);
	walkOffsetX = 0;
	walkOffsetY = 0;
	if (direction == NORTH) {
		walkOffsetY = TileSize;
	} else if (direction == EAST) {
		walkOffsetX = -TileSize;
	} else if (direction == SOUTH) {
		walkOffsetY = -TileSize;
	} else if (direction == WEST) {
		walkOffsetX = TileSize;
	}
	const uint16_t groundSpeed = targetTile ? targetTile->getGroundSpeed() : 0;
	movementStatus->SetLabel(wxString::Format("Walking | ground speed %d | %d ms", groundSpeed == 0 ? 100 : groundSpeed, walkDuration));
}

void IngamePreviewWindow::UpdateWalk() {
	if (!walking && !walkQueue.empty()) {
		const MoveRequest request = walkQueue.front();
		walkQueue.pop_front();
		StartWalk(request.direction, request.ignoreCollision);
	}
	if (!walking) {
		return;
	}

	const long long now = wxGetLocalTimeMillis().GetValue();
	const float progress = walkDuration <= 0 ? 1.0f : static_cast<float>(now - walkStartedAt) / static_cast<float>(walkDuration);
	if (progress >= 1.0f) {
		walking = false;
		walkOffsetX = 0;
		walkOffsetY = 0;
		animationFrame = 0;
		const bool teleported = ResolveTeleports();
		if (!teleported) {
			movementStatus->SetLabel(wxString::Format("Position %d, %d, %d", playerPosition.x, playerPosition.y, playerPosition.z));
		}
		g_gui.SetScreenCenterPosition(playerPosition, false);
		g_gui.UpdateMinimap(true);
		if (!walkQueue.empty()) {
			UpdateWalk();
		}
		return;
	}

	const int remainingPixels = static_cast<int>(std::lround(TileSize * (1.0f - std::clamp(progress, 0.0f, 1.0f))));
	walkOffsetX = 0;
	walkOffsetY = 0;
	if (walkDirection == NORTH) {
		walkOffsetY = remainingPixels;
	} else if (walkDirection == EAST) {
		walkOffsetX = -remainingPixels;
	} else if (walkDirection == SOUTH) {
		walkOffsetY = -remainingPixels;
	} else if (walkDirection == WEST) {
		walkOffsetX = remainingPixels;
	}
	animationFrame = 1 + (static_cast<int>(progress * 4.0f) % 2);
}

void IngamePreviewWindow::ApplyPreviewState() {
	if (!previewView || !playerPosition.isValid()) {
		return;
	}
	previewView->SetScreenCenterPositionInterpolated(playerPosition, walkOffsetX, walkOffsetY);
	MapCanvas* canvas = previewView->GetCanvas();
	canvas->SetIngamePreviewLighting(lightingEnabled->GetValue());
	canvas->SetIngamePreviewPlayer(playerPosition, playerDirection, walkOffsetX, walkOffsetY, animationFrame);
}

bool IngamePreviewWindow::CanWalk(const Position& position, bool ignoreCollision) const {
	if (!position.isValid()) {
		return false;
	}
	if (ignoreCollision) {
		return true;
	}
	const Tile* tile = editor ? editor->map.getTile(position) : nullptr;
	return tile && tile->hasGround() && !tile->isBlocking();
}

int IngamePreviewWindow::GetStepDuration(const Tile* tile) const {
	const int groundSpeed = tile && tile->getGroundSpeed() != 0 ? tile->getGroundSpeed() : 100;
	return std::clamp(1000 * groundSpeed / PreviewPlayerSpeed, 50, 1000);
}

bool IngamePreviewWindow::ResolveTeleports() {
	if (!editor) {
		return false;
	}

	bool teleported = false;
	std::set<Position> visited;
	for (int jump = 0; jump < 10 && visited.insert(playerPosition).second; ++jump) {
		const Tile* tile = editor->map.getTile(playerPosition);
		if (!tile) {
			break;
		}

		// Position() is (0,0,0) and isValid(); only jump when a real teleport dest was found.
		bool foundTeleport = false;
		Position destination(-1, -1, -1);
		for (const Item* item : tile->items) {
			const auto* teleport = dynamic_cast<const Teleport*>(item);
			if (teleport && teleport->hasDestination()) {
				destination = teleport->getDestination();
				foundTeleport = true;
				break;
			}
		}
		if (!foundTeleport || !destination.isValid()) {
			break;
		}
		playerPosition = destination;
		teleported = true;
	}

	if (teleported) {
		movementStatus->SetLabel(wxString::Format("Teleported to %d, %d, %d", playerPosition.x, playerPosition.y, playerPosition.z));
	}
	return teleported;
}

void IngamePreviewWindow::ReleaseEditor(Editor* closingEditor) {
	if (editor != closingEditor) {
		return;
	}

	// The editor is deleted on a worker immediately after its last map tab
	// closes. Destroy this canvas synchronously so no queued paint can retain a
	// reference to that editor.
	if (previewView) {
		previewView->Hide();
		mainSizer->Detach(previewView);
		MapWindow* closingView = previewView;
		previewView = nullptr;
		delete closingView;
	}
	editor = nullptr;
	ResetMovement();
	statusText->Show();
	Layout();
}

void IngamePreviewWindow::SyncEditor(Editor* activeEditor) {
	if (editor == activeEditor) {
		return;
	}

	RemovePreviewView();
	editor = activeEditor;
	ResetMovement();
	if (!editor) {
		statusText->Show();
		Layout();
		return;
	}

	previewView = newd MapWindow(this, *editor, true);
	previewView->FitToMap();
	previewView->GetCanvas()->SetIngamePreviewLighting(lightingEnabled->GetValue());
	statusText->Hide();
	mainSizer->Add(previewView, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT | wxBOTTOM, FROM_DIP(this, 6));
	Layout();
}

void IngamePreviewWindow::RemovePreviewView() {
	if (!previewView) {
		return;
	}
	previewView->Hide();
	mainSizer->Detach(previewView);
	previewView->Destroy();
	previewView = nullptr;
}

void IngamePreviewWindow::OnKeyDown(wxKeyEvent& event) {
	Direction direction = SOUTH;
	switch (event.GetKeyCode()) {
		case WXK_UP:
			direction = NORTH;
			break;
		case WXK_RIGHT:
			direction = EAST;
			break;
		case WXK_DOWN:
			direction = SOUTH;
			break;
		case WXK_LEFT:
			direction = WEST;
			break;
		default:
			event.Skip();
			return;
	}

	if (!previewView || !playerPosition.isValid()) {
		return;
	}
	if (event.ControlDown()) {
		if (!walking) {
			playerDirection = direction;
			animationFrame = 0;
			movementStatus->SetLabel("Turned without moving");
			ApplyPreviewState();
		}
		return;
	}

	followSelection->SetValue(false);
	lastTarget = Position(-1, -1, -1);
	BufferWalk(direction, event.AltDown());
}

void IngamePreviewWindow::OnTimer(wxTimerEvent&) {
	UpdateState();
}

void IngamePreviewWindow::OnClose(wxCloseEvent&) {
	g_gui.DestroyIngamePreview();
}

void IngamePreviewWindow::OnDestroy(wxWindowDestroyEvent& event) {
	if (event.GetEventObject() == this && g_gui.ingame_preview == this) {
		g_gui.ingame_preview = nullptr;
	}
	event.Skip();
}
