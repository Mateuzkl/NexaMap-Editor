//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_ZONE_BRUSH_H
#define RME_ZONE_BRUSH_H

#include "brush.h"

class ZoneBrush : public FlagBrush {
public:
	ZoneBrush();
	~ZoneBrush() override;

	bool isZone() const override {
		return true;
	}
	ZoneBrush* asZone() override {
		return static_cast<ZoneBrush*>(this);
	}

	bool load(pugi::xml_node node, wxArrayString& warnings) override {
		return true;
	}

	bool canDraw(BaseMap* map, const Position& position) const override;
	void draw(BaseMap* map, Tile* tile, void* parameter) override;
	void undraw(BaseMap* map, Tile* tile) override;

	void setZone(unsigned int id);
	int getLookID() const override {
		return 0;
	}
	std::string getName() const override {
		return "Zone Brush";
	}

protected:
	unsigned int zoneId;
};

#endif
