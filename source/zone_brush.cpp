//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "zone_brush.h"
#include "basemap.h"

ZoneBrush::ZoneBrush() :
	FlagBrush(0),
	zoneId(0) {
	////
}

ZoneBrush::~ZoneBrush() {
	////
}

void ZoneBrush::setZone(unsigned int id) {
	zoneId = id;
}

unsigned int ZoneBrush::getZone() const {
	return zoneId;
}

bool ZoneBrush::canDraw(BaseMap* map, const Position& position) const {
	return map->getTile(position) != nullptr && zoneId != 0;
}

void ZoneBrush::undraw(BaseMap* map, Tile* tile) {
	tile->removeZone(zoneId);
}

void ZoneBrush::draw(BaseMap* map, Tile* tile, void* parameter) {
	if (tile->hasGround()) {
		tile->addZone(zoneId);
	}
}
