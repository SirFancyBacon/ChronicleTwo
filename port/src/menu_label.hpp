#pragma once

#include <span>
#include <string_view>

#include "mg_tanime.hpp"

class mgCDrawPrim;

// The atlas's two letterings: the pause menu's words, and the time board's words and figures.
enum class MenuLettering {
    Words,
    Board,
};

// The pause menu's painted words (Item, Character, Make...) and the time board's (Day, AM, PM...) drawn in
// ChronicleTwoMenu and ChronicleTwoBoard while the text font is sharp, in place of the menu atlas's cell `uv` the
// prim is about to draw into `put`, in the prim's colour. False for any other cell, or where the word keeps its
// painting: the original text font, a language not lettered yet, or the font missing. docs/PC.md "Menu lettering".
bool MenuLabelDraw(mgCDrawPrim &prim, mgRect<float> put, mgRect<int> uv);

// The time board's figure the menu atlas's digit strip cell `uv` holds, drawn the same way into `put`.
bool MenuLabelDrawFigure(mgCDrawPrim &prim, mgRect<int> put, mgRect<int> uv);

// Where a lettering's font sets a word in its cell, in texels: its em, where the pen starts and the baseline's row.
struct MenuLabelGeometry {
    float em;
    float start;
    float baseline;
};

MenuLabelGeometry MenuLabelGeometryOf(MenuLettering lettering);

// A word the English atlas paints: its cell's name (Shift-JIS, as the menu's layout holds it), and its own offset
// in texels from its lettering's pen and baseline, where its painting has it.
struct MenuLabelWord {
    const char      *cell;
    std::string_view text;
    float            dx;
    float            dy;
};

std::span<const MenuLabelWord> MenuLabelWords(MenuLettering lettering);
