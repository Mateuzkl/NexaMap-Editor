//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_AUTOBORDER_PREVIEW_H_
#define RME_AUTOBORDER_PREVIEW_H_

#include "basemap.h"
#include "brush_enums.h"
#include "gui_ids.h"
#include "position.h"

#include <cstdint>
#include <set>

class Brush;
class Editor;

class AutoborderPreviewManager {
public:
	bool Update(Editor& editor, const Position& cursor, Brush& brush, int brushSize, BrushShape brushShape, bool altPressed, const PositionVector& tilesToDraw, const PositionVector& tilesToBorder);
	void Invalidate();
	void Clear();

	[[nodiscard]] BaseMap* GetBufferMap() {
		return &previewMap;
	}
	[[nodiscard]] bool Owns(const BaseMap* map) const {
		return map == &previewMap;
	}
	[[nodiscard]] bool IsDeletion(const Position& position) const {
		return deletionPositions.contains(position);
	}
	[[nodiscard]] bool HasPreview() const {
		return previewMap.size() != 0 || !deletionPositions.empty();
	}
	[[nodiscard]] uint64_t GetGeneration() const {
		return generation;
	}

private:
	void CopyLocalArea(Editor& editor, const Position& cursor, const PositionVector& tilesToDraw, const PositionVector& tilesToBorder);
	void SimulateBrush(Editor& editor, Brush& brush, bool altPressed, const PositionVector& tilesToDraw, const PositionVector& tilesToBorder);
	void PruneUnchanged(Editor& editor);
	[[nodiscard]] static bool HasSameVisualContent(const Tile* left, const Tile* right);

	BaseMap previewMap;
	std::set<Position> deletionPositions;
	Position lastCursor { -1, -1, -1 };
	Brush* lastBrush = nullptr;
	Editor* lastEditor = nullptr;
	int lastBrushSize = -1;
	BrushShape lastBrushShape = BRUSHSHAPE_SQUARE;
	bool lastAltPressed = false;
	bool invalidated = true;
	uint64_t generation = 0;
	int copiedMinX = 0;
	int copiedMinY = 0;
	int copiedMaxX = -1;
	int copiedMaxY = -1;
	int copiedFloor = 0;
};

extern AutoborderPreviewManager g_autoborder_preview;

#endif
