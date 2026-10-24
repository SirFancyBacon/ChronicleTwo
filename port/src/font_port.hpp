#pragma once

#include <cstdint>
#include <span>

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
