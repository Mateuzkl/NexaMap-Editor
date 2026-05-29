//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_ZONE_BRUSH_H
#define RME_ZONE_BRUSH_H

#include "brush.h"

class ZoneBrush : public FlagBrush {
public:
	ZoneBrush();
	virtual ~ZoneBrush();

	bool isZone() const {
		return true;
	}
	ZoneBrush* asZone() {
		return static_cast<ZoneBrush*>(this);
	}

	virtual bool load(pugi::xml_node node, wxArrayString& warnings) {
		return true;
	}

	virtual bool canDraw(BaseMap* map, const Position& position) const;
	virtual void draw(BaseMap* map, Tile* tile, void* parameter);
	virtual void undraw(BaseMap* map, Tile* tile);

	unsigned int getZone() const;
	void setZone(unsigned int id);
	virtual int getLookID() const {
		return 0;
	}
	virtual std::string getName() const {
		return "Zone Brush";
	}

protected:
	unsigned int zoneId;
};

#endif
