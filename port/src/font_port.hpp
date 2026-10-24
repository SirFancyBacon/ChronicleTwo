#pragma once

#include <cstdint>
#include <span>

namespace ttffont {
class Face;
}

// The ChronicleTwoMessage character the language's retail font page draws at font_no, or 0 where the font
// draws nothing for it (Japanese, kana, kanji), which keeps its bitmap. `table` is the loaded font table's
// list of codes, two bytes a font number. docs/PC.md "Message font".
char32_t FontPortCharacter(int language, int font_no, std::span<const std::uint8_t> table);

// How a language's retail font page draws ChronicleTwoMessage inside a 16x20-texel font cell.
struct FontPortPage {
    float texels_per_unit_x;
    float texels_per_unit_y;
    float pen_x;    // texels right of the cell's left edge
    float baseline; // texels below the cell's top
};

FontPortPage FontPortPageOf(int language);

// The port's typefaces: ChronicleTwoMessage for the game's text, ChronicleTwoMenu for the pause menu's painted
// words, ChronicleTwoBoard for its time board's and ChronicleTwoTitle for the title and language menus' buttons.
enum class FontPortTypeface {
    Message,
    Menu,
    Board,
    Title,
};

// The typeface's font, read from fonts/ beside the executable on first use; null where it is missing, which
// keeps the original text.
const ttffont::Face *FontPortFace(FontPortTypeface typeface);

// Whether text takes ChronicleTwoMessage's own letter widths: while the font is drawn sharp and has loaded. The
// original font keeps retail's layout, every half-width character in the same cell.
bool FontPortProportional();

// How many of retail's half-width advances the half-width character at font_no moves the pen: its advance in the
// font over the half-width cell's while FontPortProportional, and 1 otherwise or where the font does not draw it.
// Digits and symbols keep the cell's width, so figures still line up in columns.
float FontPortHalfAdvance(int font_no);

// While one lives, glyphs sit `fraction` of a logical pixel right of the whole pixel they are drawn at: a kerned pen
// falls between the pixels retail's text positions hold.
class FontPortPen {
public:
    explicit FontPortPen(float fraction);
    ~FontPortPen();
    FontPortPen(const FontPortPen &) = delete;
    FontPortPen &operator=(const FontPortPen &) = delete;

private:
    float previous_;
};
