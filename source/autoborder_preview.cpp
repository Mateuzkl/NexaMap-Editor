//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"
#include "autoborder_preview.h"

#include "brush.h"
#include "editor.h"
#include "ground_brush.h"
#include "settings.h"
#include "tile.h"

#include <algorithm>

AutoborderPreviewManager g_autoborder_preview;

bool AutoborderPreviewManager::Update(Editor& editor, const Position& cursor, Brush& brush, int brushSize, BrushShape brushShape, bool altPressed, const PositionVector& tilesToDraw, const PositionVector& tilesToBorder) {
	if (!invalidated && &editor == lastEditor && cursor == lastCursor && &brush == lastBrush && brushSize == lastBrushSize && brushShape == lastBrushShape && altPressed == lastAltPressed) {
		return false;
	}

	lastCursor = cursor;
	lastEditor = &editor;
	lastBrush = &brush;
	lastBrushSize = brushSize;
	lastBrushShape = brushShape;
	lastAltPressed = altPressed;
	invalidated = false;
	++generation;

	CopyLocalArea(editor, cursor, tilesToDraw, tilesToBorder);
	SimulateBrush(editor, brush, altPressed, tilesToDraw, tilesToBorder);
	PruneUnchanged(editor);
	return true;
}

void AutoborderPreviewManager::Invalidate() {
	invalidated = true;
}

void AutoborderPreviewManager::Clear() {
	previewMap.clear();
	deletionPositions.clear();
	lastCursor = Position(-1, -1, -1);
	lastBrush = nullptr;
	lastEditor = nullptr;
	lastBrushSize = -1;
	invalidated = true;
}

void AutoborderPreviewManager::CopyLocalArea(Editor& editor, const Position& cursor, const PositionVector& tilesToDraw, const PositionVector& tilesToBorder) {
	previewMap.clear();
	deletionPositions.clear();

	int minX = cursor.x;
	int minY = cursor.y;
	int maxX = cursor.x;
	int maxY = cursor.y;
	const auto includePositions = [&](const PositionVector& positions) {
		for (const Position& position : positions) {
			minX = std::min(minX, position.x);
			minY = std::min(minY, position.y);
			maxX = std::max(maxX, position.x);
			maxY = std::max(maxY, position.y);
		}
	};
	includePositions(tilesToDraw);
	includePositions(tilesToBorder);

	constexpr int SafetyMargin = 3;
	copiedMinX = std::max(0, minX - SafetyMargin);
	copiedMinY = std::max(0, minY - SafetyMargin);
	copiedMaxX = std::min(MAP_MAX_WIDTH, maxX + SafetyMargin);
	copiedMaxY = std::min(MAP_MAX_HEIGHT, maxY + SafetyMargin);
	copiedFloor = cursor.z;

	for (int y = copiedMinY; y <= copiedMaxY; ++y) {
		for (int x = copiedMinX; x <= copiedMaxX; ++x) {
			if (Tile* sourceTile = editor.map.getTile(x, y, copiedFloor)) {
				Tile* copy = sourceTile->deepCopy(previewMap);
				copy->setLocation(previewMap.createTileL(x, y, copiedFloor));
				previewMap.setTile(copy);
			}
		}
	}
}

void AutoborderPreviewManager::SimulateBrush(Editor& editor, Brush& brush, bool altPressed, const PositionVector& tilesToDraw, const PositionVector& tilesToBorder) {
	const bool automagic = g_settings.getBoolean(Config::USE_AUTOMAGIC);
	for (const Position& position : tilesToDraw) {
		Tile* tile = previewMap.getOrCreateTile(position);
		if ((brush.isGround() || brush.isEraser()) && automagic) {
			tile->cleanBorders();
		}
		if (brush.isWall()) {
			tile->cleanWalls();
		}

		if (brush.isGround() && altPressed) {
			std::pair<bool, GroundBrush*> parameter(editor.replace_brush == nullptr, editor.replace_brush);
			brush.draw(&previewMap, tile, &parameter);
		} else if (brush.isDoor()) {
			brush.asDoor()->draw(&previewMap, tile, &altPressed);
		} else {
			brush.draw(&previewMap, tile, nullptr);
		}
	}

	if (!automagic && !brush.isWall()) {
		return;
	}
	for (const Position& position : tilesToBorder) {
		Tile* tile = previewMap.getTile(position);
		if (!tile) {
			if (brush.isGround() || brush.isEraser()) {
				tile = previewMap.getOrCreateTile(position);
			} else {
				continue;
			}
		}

		if (brush.isEraser()) {
			tile->wallize(&previewMap);
			tile->tableize(&previewMap);
			tile->carpetize(&previewMap);
			tile->borderize(&previewMap);
		} else if (brush.isWall() || brush.isDoor()) {
			tile->wallize(&previewMap);
		} else if (brush.isTable()) {
			if (tile->hasTable()) {
				tile->tableize(&previewMap);
			}
		} else if (brush.isCarpet()) {
			if (tile->hasCarpet()) {
				tile->carpetize(&previewMap);
			}
		} else {
			tile->borderize(&previewMap);
		}
	}
}

void AutoborderPreviewManager::PruneUnchanged(Editor& editor) {
	PositionVector removePositions;
	for (int y = copiedMinY; y <= copiedMaxY; ++y) {
		for (int x = copiedMinX; x <= copiedMaxX; ++x) {
			const Position position(x, y, copiedFloor);
			Tile* previewTile = previewMap.getTile(position);
			Tile* sourceTile = editor.map.getTile(position);
			if (HasSameVisualContent(previewTile, sourceTile)) {
				if (previewTile) {
					removePositions.push_back(position);
				}
				continue;
			}
			if (sourceTile && sourceTile->size() != 0 && (!previewTile || previewTile->size() == 0)) {
				deletionPositions.insert(position);
				if (previewTile) {
					removePositions.push_back(position);
				}
			}
		}
	}
	for (const Position& position : removePositions) {
		previewMap.setTile(position, nullptr, true);
	}
}

bool AutoborderPreviewManager::HasSameVisualContent(const Tile* left, const Tile* right) {
	if (left == right) {
		return true;
	}
	if (!left || !right) {
		return (!left || left->size() == 0) && (!right || right->size() == 0);
	}
	if ((left->ground == nullptr) != (right->ground == nullptr)) {
		return false;
	}
	if (left->ground && left->ground->getID() != right->ground->getID()) {
		return false;
	}
	if (left->items.size() != right->items.size()) {
		return false;
	}
	for (size_t index = 0; index < left->items.size(); ++index) {
		if (left->items[index]->getID() != right->items[index]->getID()) {
			return false;
		}
	}
	return true;
}
