//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_INGAME_PREVIEW_WINDOW_H_
#define RME_INGAME_PREVIEW_WINDOW_H_

#include "../creature.h"
#include "../position.h"

#include <deque>

class Editor;
class MapWindow;
class Tile;

class IngamePreviewWindow final : public wxPanel {
public:
	explicit IngamePreviewWindow(wxWindow* parent);
	~IngamePreviewWindow() override;

	void UpdateState();
	void ReleaseEditor(Editor* closingEditor);

private:
	void SyncEditor(Editor* activeEditor);
	void RemovePreviewView();
	void ResetMovement();
	void BufferWalk(Direction direction, bool ignoreCollision);
	void StartWalk(Direction direction, bool ignoreCollision);
	void UpdateWalk();
	void ApplyPreviewState();
	bool CanWalk(const Position& position, bool ignoreCollision) const;
	int GetStepDuration(const Tile* tile) const;
	bool ResolveTeleports();
	void OnKeyDown(wxKeyEvent& event);
	void OnTimer(wxTimerEvent& event);
	void OnClose(wxCloseEvent& event);
	void OnDestroy(wxWindowDestroyEvent& event);

	wxBoxSizer* mainSizer;
	wxCheckBox* followSelection;
	wxCheckBox* lightingEnabled;
	wxStaticText* statusText;
	wxStaticText* movementStatus;
	MapWindow* previewView;
	Editor* editor;
	wxTimer timer;
	Position lastTarget { -1, -1, -1 };
	Position playerPosition { -1, -1, -1 };
	Direction playerDirection = SOUTH;
	Direction walkDirection = SOUTH;
	bool walking = false;
	long long walkStartedAt = 0;
	int walkDuration = 0;
	int walkOffsetX = 0;
	int walkOffsetY = 0;
	int animationFrame = 0;
	struct MoveRequest {
		Direction direction;
		bool ignoreCollision;
	};
	std::deque<MoveRequest> walkQueue;
};

#endif
