#pragma once

#include <string_view>

#include "font_port.hpp"

class mgCDrawPrim;

// How painted lettering looks: its fill over the dark of its rim and of its drop shadow, measured in texels of the
// art it stands in for.
struct LetteringStyle {
    FontPortTypeface typeface;
    float            rim;                // texels; 0 for none
    float            shadow_x, shadow_y; // texels
    float            shadow_floor;       // the shadow's opacity where it is furthest from the letters
    float            fill;
    float            dark[3];
};

// Where the x LetteringDraw is given sits on the text: its pen, or the middle of its ink.
enum class LetteringAlign {
    Pen,
    InkCentre,
};

// Draws UTF-8 `text` in its style through `prim`, in the prim's colour: `x` (as `align` says) and the baseline
// `base_y` in the logical pixels the prim's vertices take, an em `em_x` across and `em_y` down in texels of `texel_x`
// by `texel_y` logical pixels. The text is rendered at the target's resolution and laid on whole target pixels, so
// nothing is resampled. False, drawing nothing, where the typeface is missing or the place is degenerate.
bool LetteringDraw(mgCDrawPrim &prim, const LetteringStyle &style, std::string_view text, float x, float base_y,
                   float em_x, float em_y, float texel_x, float texel_y, LetteringAlign align = LetteringAlign::Pen);

// How far `text` moves the pen, in ems; 0 where the typeface is missing. Characters the font lacks move it nowhere.
float LetteringAdvance(FontPortTypeface typeface, std::string_view text);

// The next character of UTF-8 `text` from `at`, which moves past it; U+FFFD for a malformed sequence, which is
// skipped a byte at a time.
char32_t LetteringNextCharacter(std::string_view text, std::size_t &at);
