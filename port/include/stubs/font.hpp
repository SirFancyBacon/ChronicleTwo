#pragma once

// Retail's outline and shadow draw, kept for the styles and cells the sharp painter leaves to it.
class mgCDrawPrim;
struct RECT;
void PortRetailSet2DSpriteFuchi(mgCDrawPrim *prim, RECT destination, RECT texture, int style, int alpha) asm("Font_Set2DSpriteFuchi");
