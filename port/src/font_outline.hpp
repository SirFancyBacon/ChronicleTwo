#pragma once

#include <cstdint>
#include <span>
#include <vector>

// The rims the sharp message font draws for retail's outline styles. Retail draws the glyph again a
// pixel or two off on each side, which a CRT blurred into one soft rim; drawn sharp, those copies are
// steps. These rims are round instead, with the ink retail's copies laid down (docs/PC.md "Message
// font" has the measurement).
enum class FontOutline {
    Thin,  // FUCHI_OUTLINE: one copy each side, a pixel out
    Thick, // FUCHI_OUTLINE_THICK: eight copies a pixel out and four two out
    Glow,  // FUCHI_OUTLINE_WIDE: four faint copies two out
};

// The rim's opacity d logical pixels outside the glyph's edge, 1 at its strongest.
float FontOutlineOpacity(FontOutline outline, float d);

// How far past the glyph's edge the rim reaches, in logical pixels.
float FontOutlineReach(FontOutline outline);

// The share of the text's alpha the rim is drawn with: the glow is faint.
float FontOutlineAlpha(FontOutline outline);

// The rim around a glyph's coverage (width x height, 0 to 255), the glyph included, at px_x and px_y
// target pixels to a logical pixel across and down. The image is margin_x pixels wider than the glyph
// on each side and margin_y taller.
struct FontOutlineImage {
    std::vector<std::uint8_t> alpha;
    int                       margin_x = 0;
    int                       margin_y = 0;
};

FontOutlineImage FontOutlineRim(std::span<const std::uint8_t> coverage, int width, int height, float px_x, float px_y,
                                FontOutline outline);

// A rim `radius` logical pixels out with the same half-pixel soft edge, for lettering whose painting has a rim of its
// own width.
FontOutlineImage FontOutlineRimOf(std::span<const std::uint8_t> coverage, int width, int height, float px_x, float px_y,
                                  float radius);

// Each pixel's distance in logical pixels from the glyph's edge (0 inside it) across an image_width x image_height
// image whose glyph coverage (width x height) sits margin_x, margin_y in from its corner; empty for a bad size.
std::vector<float> FontOutlineDistance(std::span<const std::uint8_t> coverage, int width, int height, float px_x,
                                       float px_y, int margin_x, int margin_y, int image_width, int image_height);
