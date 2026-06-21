//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Self-contained bitmap fonts, replacing the freeglut dependency.
//////////////////////////////////////////////////////////////////////

#ifndef RME_BITMAP_FONT_H_
#define RME_BITMAP_FONT_H_

using GLubyte = unsigned char;

struct BitmapFont {
	const char* name;
	int quantity;
	int height;
	const GLubyte** characters;
	float xorig;
	float yorig;
};

extern BitmapFont rme_bitmap_fixed_9x15;
extern BitmapFont rme_bitmap_helvetica_12;
extern BitmapFont rme_bitmap_helvetica_18;

int bitmapCharWidth(const BitmapFont& font, int character);
void drawBitmapChar(const BitmapFont& font, int character);

#endif
