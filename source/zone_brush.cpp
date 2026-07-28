//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "zone_brush.h"
#include "basemap.h"

ZoneBrush::ZoneBrush() :
	FlagBrush(0),
	zoneId(0),
	eraseMode(false) {
	////
}

void ZoneBrush::setZone(unsigned int id) {
	zoneId = id;
}

bool ZoneBrush::canDraw(BaseMap* map, const Position& position) const {
	return map->getTile(position) != nullptr && zoneId != 0;
}

void ZoneBrush::undraw(BaseMap*, Tile* tile) {
	tile->removeZone(zoneId);
}

void ZoneBrush::draw(BaseMap*, Tile* tile, void*) {
	if (eraseMode) {
		tile->removeZone(zoneId);
	} else if (tile->hasGround()) {
		tile->addZone(zoneId);
	}
}
