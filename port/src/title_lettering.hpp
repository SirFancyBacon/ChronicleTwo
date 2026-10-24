#pragma once

#include <array>
#include <span>
#include <string_view>

#include "mg_tanime.hpp"

class mgCDrawPrim;

// The language menu's buttons and the title menu's (New Game, Continue... and Push START) drawn as a blank plate
// cut from the painted ones, lettered in ChronicleTwoTitle, while the text font is sharp: in place of the
// lang_select or moji cell `cell` the prim is about to draw at (x, y), in the prim's colour. False for any other
// cell, or where the font is missing, which keeps the painting. docs/PC.md "Title lettering".
bool TitleLetteringDraw(mgCDrawPrim &prim, float x, float y, mgRect<int> cell);

// A piece of a blank plate: the atlas texels it samples and where it lands, in texels from its cell's top-left.
struct TitlePlatePiece {
    float src_x, src_y, src_w, src_h;
    float dst_x, dst_y, dst_w, dst_h;
};

// The nine pieces a blank plate's cell of w by h texels is drawn from, cut from the painted cell at (cell_x, cell_y):
// its corners as they are, its edges stretched along, and its middle a lettering-free column stretched across.
std::array<TitlePlatePiece, 9> TitlePlatePieces(int cell_x, int cell_y, float w, float h);

// A lettered cell: its rectangle in the atlas, how far below the cell's top its plate starts, and the text each
// language's cell paints (index LanguageCode - 1); the language menu's buttons name each language in itself, so
// they have one text whatever the language.
struct TitleLabel {
    int              x, y, w, h;
    int              plate_dy;
    std::string_view text[5];
};

std::span<const TitleLabel> TitleLanguageLabels();
std::span<const TitleLabel> TitleMenuLabels();
std::span<const TitleLabel> TitlePushStartLabels();
