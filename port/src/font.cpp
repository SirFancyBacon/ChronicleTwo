#include "font.hpp"
#include "mw_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

#include "font_outline.hpp"
#include "font_port.hpp"
#include "log/log.hpp"
#include "mainloop.hpp"
#include "mg_drawprim_port.hpp"
#include "mglib_port.hpp"
#include "platform/config.hpp"
#include "platform/paths.hpp"
#include "platform/ttffont.hpp"

// Retail's outline and shadow draw, kept by the unit copy's retail bridge (scripts/port/unit_copy.py).
void PortRetailSet2DSpriteFuchi(mgCDrawPrim *prim, RECT destination, RECT texture, int style, int alpha) asm("Font_Set2DSpriteFuchi");

namespace {

// Font numbers FONT_NO_ALPHABETICAL_TOP on of the European page: the Latin-1 codes of
// alphabetical_chara_tbl, in its order, and Œ œ.
constexpr std::array<char32_t, ALPHABETICAL_CHARA_NUM> kAccented = {
    0xA1, 0xAA, 0xB0, 0xBA, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA,
    0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD,
    0xDF, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE,
    0xEF, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFF, 0x152, 0x153};

// Font numbers FONT_NO_FONT_GAIJI_TOP on: FontGaijiConvTbl's characters, ○ ● ↑ ↓ ← → ０-９ ： ＋ － ・ “ ” ‘ ’.
// The page draws the last five half-width, and the font keeps the half-width middle dot at U+FF65.
constexpr std::array<char32_t, FONT_GAIJI_CONV_NUM> kFontGaiji = {
    0x25CB, 0x25CF, 0x2191, 0x2193, 0x2190, 0x2192, 0xFF10, 0xFF11, 0xFF12, 0xFF13, 0xFF14, 0xFF15,
    0xFF16, 0xFF17, 0xFF18, 0xFF19, 0xFF1A, 0xFF0B, 0xFF0D, 0xFF65, 0x201C, 0x201D, 0x2018, 0x2019};

// The English page's full-width symbols the font draws: Shift-JIS code, then code point. The European
// page draws “ ” ’ ° half-width at the standard code points, so the English forms take 0xE001 on.
struct EnglishSymbol {
    std::uint16_t code;
    char32_t      ch;
};

// clang-format off
constexpr EnglishSymbol kEnglishSymbols[] = {
    {0x8140, 0x3000}, {0x8141, 0x3001}, {0x8142, 0x3002}, {0x8143, 0xFF0C}, {0x8144, 0xFF0E}, {0x8145, 0x30FB},
    {0x8146, 0xFF1A}, {0x8147, 0xFF1B}, {0x8148, 0xFF1F}, {0x8149, 0xFF01}, {0x8151, 0xFF3F}, {0x8158, 0x3005},
    {0x815C, 0x2015}, {0x815E, 0xFF0F}, {0x8160, 0x301C}, {0x8162, 0xFF5C}, {0x8163, 0x2026}, {0x8164, 0x2025},
    {0x8166, 0xE004}, {0x8167, 0xE001}, {0x8168, 0xE002}, {0x8169, 0xFF08}, {0x816A, 0xFF09}, {0x816D, 0xFF3B},
    {0x816E, 0xFF3D}, {0x816F, 0xFF5B}, {0x8170, 0xFF5D}, {0x8175, 0x300C}, {0x8176, 0x300D}, {0x8177, 0x300E},
    {0x8178, 0x300F}, {0x8179, 0x3010}, {0x817A, 0x3011}, {0x817B, 0xFF0B}, {0x817C, 0x2212}, {0x817D, 0x00B1},
    {0x817E, 0x00D7}, {0x8181, 0xFF1D}, {0x8183, 0xFF1C}, {0x8184, 0xFF1E}, {0x818B, 0xE005}, {0x818F, 0xFFE5},
    {0x8190, 0xFF04}, {0x8193, 0xFF05}, {0x8194, 0xFF03}, {0x8195, 0xFF06}, {0x8196, 0xFF0A}, {0x8197, 0xFF20},
    {0x819B, 0x25CB}, {0x819C, 0x25CF}, {0x81A0, 0x25A1}, {0x81A1, 0x25A0}, {0x81A2, 0x25B3}, {0x81A8, 0x2192},
    {0x81A9, 0x2190}, {0x81AA, 0x2191}, {0x81AB, 0x2193}, {0x824F, 0xFF10}, {0x8250, 0xFF11}, {0x8251, 0xFF12},
    {0x8252, 0xFF13}, {0x8253, 0xFF14}, {0x8254, 0xFF15}, {0x8255, 0xFF16}, {0x8256, 0xFF17}, {0x8257, 0xFF18},
    {0x8258, 0xFF19}, {0x8260, 0xFF21}, {0x8261, 0xFF22}, {0x8262, 0xFF23}, {0x8263, 0xFF24}, {0x8264, 0xFF25},
    {0x8265, 0xFF26}, {0x8266, 0xFF27}, {0x8267, 0xFF28}, {0x8268, 0xFF29}, {0x8269, 0xFF2A}, {0x826A, 0xFF2B},
    {0x826B, 0xFF2C}, {0x826C, 0xFF2D}, {0x826D, 0xFF2E}, {0x826E, 0xFF2F}, {0x826F, 0xFF30}, {0x8270, 0xFF31},
    {0x8271, 0xFF32}, {0x8272, 0xFF33}, {0x8273, 0xFF34}, {0x8274, 0xFF35}, {0x8275, 0xFF36}, {0x8276, 0xFF37},
    {0x8277, 0xFF38}, {0x8278, 0xFF39}, {0x8279, 0xFF3A}, {0x8281, 0xFF41}, {0x8282, 0xFF42}, {0x8283, 0xFF43},
    {0x8284, 0xFF44}, {0x8285, 0xFF45}, {0x8286, 0xFF46}, {0x8287, 0xFF47}, {0x8288, 0xFF48}, {0x8289, 0xFF49},
    {0x828A, 0xFF4A}, {0x828B, 0xFF4B}, {0x828C, 0xFF4C}, {0x828D, 0xFF4D}, {0x828E, 0xFF4E}, {0x828F, 0xFF4F},
    {0x8290, 0xFF50}, {0x8291, 0xFF51}, {0x8292, 0xFF52}, {0x8293, 0xFF53}, {0x8294, 0xFF54}, {0x8295, 0xFF55},
    {0x8296, 0xFF56}, {0x8297, 0xFF57}, {0x8298, 0xFF58}, {0x8299, 0xFF59}, {0x829A, 0xFF5A}};
// clang-format on

} // namespace

char32_t FontPortCharacter(int language, int font_no, std::span<const std::uint8_t> table) {
    if (language < LANG_ENGLISH || language > LANG_SPANISH || font_no < 0) {
        return 0;
    }
    if (language != LANG_ENGLISH && font_no >= FONT_NO_ALPHABETICAL_TOP) {
        if (font_no >= FONT_NO_FONT_GAIJI_END) {
            return 0;
        }
        return font_no >= FONT_NO_FONT_GAIJI_TOP ? kFontGaiji[font_no - FONT_NO_FONT_GAIJI_TOP]
                                                 : kAccented[font_no - FONT_NO_ALPHABETICAL_TOP];
    }
    if (static_cast<std::size_t>(font_no) >= table.size() / 2) {
        return 0;
    }
    const auto first = table[static_cast<std::size_t>(font_no) * 2];
    const auto second = table[static_cast<std::size_t>(font_no) * 2 + 1];
    if (second == 0x20 && first >= 0x20 && first <= 0x7E) {
        return first;
    }
    if (language != LANG_ENGLISH) {
        return 0;
    }
    const std::uint16_t code = static_cast<std::uint16_t>(first << 8 | second);
    const auto          found = std::ranges::lower_bound(kEnglishSymbols, code, {}, &EnglishSymbol::code);
    return found != std::end(kEnglishSymbols) && found->code == code ? found->ch : 0;
}

// From scripts/font/message_font.json, which fit_message_font.py measured: the European page draws the
// font's master space as it is (64 units a texel, baseline at row 16); the English page draws it taller
// and narrower, its "english_page" affine.
FontPortPage FontPortPageOf(int language) {
    if (language == LANG_ENGLISH) {
        return {.texels_per_unit_x = 0.955f / 64.0f, .texels_per_unit_y = 1.335f / 64.0f, .pen_x = -40.4f / 64.0f, .baseline = 17.0f + 20.0f / 64.0f};
    }
    return {.texels_per_unit_x = 1.0f / 64.0f, .texels_per_unit_y = 1.0f / 64.0f, .pen_x = 0.0f, .baseline = 16.0f};
}

namespace {

constexpr float kUnitsPerEm = 1280.0f;
constexpr float kHalfWidth = 512.0f; // a half-width cell's advance, in font units
constexpr int   kShiftSteps = 4;     // a glyph is drawn at a quarter-pixel pen; its baseline at a whole pixel

// The live FontPortPen's fraction of a logical pixel.
float pen_fraction = 0.0f;

bool Ready() { return FontPortFace(FontPortTypeface::Message) != nullptr; }

// The loaded font table's codes, two bytes a font number.
std::span<const std::uint8_t> LoadedTable() {
    const int count = std::clamp(GetYoyakuTblNum(), 0, static_cast<int>(sizeof(FONT_TBL_BIN::yoyaku_tbl) / 2));
    return {GetYoyakuTblTop(), static_cast<std::size_t>(count) * 2};
}

// A glyph's fill, or one of the rims drawn for retail's outline styles.
enum class Layer {
    Fill,
    Thin,
    Thick,
    Glow,
};

struct Ink {
    gfx::TextureHandle texture = gfx::kNullTexture;
    int                width = 0, height = 0, x0 = 0, y0 = 0;
    std::size_t        bytes = 0;
    int                used = 0; // the frame that last drew it
};

// Character, em across and down in 1/16 px, pen shift, layer, and a rim's target pixels to a logical
// pixel across and down in 1/16.
using InkKey = std::tuple<char32_t, int, int, int, Layer, int, int>;

FontOutline OutlineOf(Layer layer) {
    return layer == Layer::Thick ? FontOutline::Thick : layer == Layer::Glow ? FontOutline::Glow
                                                                             : FontOutline::Thin;
}

// Glyph images are immutable, so a recorded frame never samples one that changed. Past the budget the
// longest unused are destroyed; a frame's list keeps any it still draws alive until it is done with it.
const Ink *GetInk(const InkKey &key) {
    static std::map<InkKey, Ink> cache;
    static std::size_t           bytes_used = 0;
    constexpr std::size_t budget = 32 * 1024 * 1024;
    const int             frame = mgPortFrameCount();
    if (const auto found = cache.find(key); found != cache.end()) {
        found->second.used = frame;
        return found->second.texture == gfx::kNullTexture ? nullptr : &found->second;
    }
    const auto [ch, em_x, em_y, shift, layer, px_x, px_y] = key;
    Ink                        ink;
    std::vector<unsigned char> coverage;
    if (!FontPortFace(FontPortTypeface::Message)
             ->RenderGlyph(ch, em_x / 16.0f, em_y / 16.0f, static_cast<float>(shift) / kShiftSteps, 0.0f, coverage,
                           ink.width, ink.height, ink.x0, ink.y0) ||
        ink.width <= 0 || ink.height <= 0) {
        // Remembered as missing: a character the font cannot draw, or a size it will not.
        cache.emplace(key, ink);
        return nullptr;
    }
    if (layer != Layer::Fill) {
        FontOutlineImage rim = FontOutlineRim(coverage, ink.width, ink.height, px_x / 16.0f, px_y / 16.0f, OutlineOf(layer));
        coverage = std::move(rim.alpha);
        ink.width += 2 * rim.margin_x;
        ink.height += 2 * rim.margin_y;
        ink.x0 -= rim.margin_x;
        ink.y0 -= rim.margin_y;
    }
    ink.bytes = static_cast<std::size_t>(ink.width + 2) * (ink.height + 2) * 4;
    while (bytes_used + ink.bytes > budget) {
        auto oldest = cache.end();
        for (auto it = cache.begin(); it != cache.end(); ++it) {
            if (it->second.texture != gfx::kNullTexture && it->second.used < frame &&
                (oldest == cache.end() || it->second.used < oldest->second.used)) {
                oldest = it;
            }
        }
        if (oldest == cache.end()) {
            return nullptr; // everything cached is on screen this frame
        }
        gfx::DestroyTexture(oldest->second.texture);
        bytes_used -= oldest->second.bytes;
        cache.erase(oldest);
    }
    // White with the coverage as alpha and a clear border, so the draw's colour tints it and linear
    // sampling at the quad's edge reads nothing.
    std::vector<unsigned char> rgba(ink.bytes, 0);
    for (int row = 0; row < ink.height; ++row) {
        for (int col = 0; col < ink.width; ++col) {
            const auto target = (static_cast<std::size_t>(row + 1) * (ink.width + 2) + col + 1) * 4;
            rgba[target] = rgba[target + 1] = rgba[target + 2] = 255;
            rgba[target + 3] = coverage[static_cast<std::size_t>(row) * ink.width + col];
        }
    }
    gfx::TextureDesc desc;
    desc.width = static_cast<std::uint32_t>(ink.width + 2);
    desc.height = static_cast<std::uint32_t>(ink.height + 2);
    ink.texture = gfx::CreateTexture(desc);
    if (ink.texture == gfx::kNullTexture) {
        return nullptr;
    }
    gfx::UpdateTextureNow(ink.texture, 0, 0, 0, desc.width, desc.height, rgba.data());
    bytes_used += ink.bytes;
    ink.used = frame;
    return &cache.insert_or_assign(key, ink).first->second;
}

// Where a font cell's glyph lands: its character, its em and pen in target pixels, and the mapping
// back to vertex units.
struct Placement {
    char32_t            ch = 0;
    float               em_x = 0, em_y = 0;
    float               pen_px = 0, base_px = 0;
    float               px_x = 0, px_y = 0; // target pixels to a logical pixel
    gfx::LogicalMapping mapping{};
    gfx::Vertex2D       origin{};
};

// The font cell `uv` of the retail font page drawn into `dst` (both position and size) moved by
// (dx, dy) logical pixels, or nothing for a cell the bitmap keeps. A space places nothing either.
std::optional<Placement> Place(const mgCDrawPrim &prim, int x, int y, int w, int h, int u, int v, int u_w, int v_h,
                               float dx, float dy) {
    if (!ConfigGet().font_sharp || u_w <= 0 || v_h <= 0 || w <= 0 || h <= 0 || u % FONT_TEX_CHAR_W != 0 ||
        v % FONT_TEX_CHAR_H != 0 || v_h != FONT_TEX_CHAR_H) {
        return std::nullopt;
    }
    Placement out;
    const int font_no = v / FONT_TEX_CHAR_H * FONT_TEX_COLUMNS + u / FONT_TEX_CHAR_W;
    out.ch = FontPortCharacter(LanguageCode, font_no, LoadedTable());
    if (out.ch == 0 || !Ready()) {
        return std::nullopt;
    }
    const FontPortPage page = FontPortPageOf(LanguageCode);
    out.mapping = gfx::GetUiMapping(mgPortCurrentTarget());
    // Logical pixels a texel: retail stretches the cell's texels over dst.
    const float        texel_x = static_cast<float>(w) / static_cast<float>(u_w);
    const float        texel_y = static_cast<float>(h) / static_cast<float>(v_h);
    const float        pen_x = static_cast<float>(x) + pen_fraction + dx + page.pen_x * texel_x;
    const float        base_y = static_cast<float>(y) + dy + page.baseline * texel_y;
    const std::uint8_t none[4] = {};
    // Vertex units a logical pixel, and the pen in target pixels.
    out.origin = mgPortPrimVertex(prim, pen_x, base_y, 0, 0, 0, none);
    const gfx::Vertex2D step = mgPortPrimVertex(prim, pen_x + 1, base_y + 1, 0, 0, 0, none);
    out.px_x = (step.x - out.origin.x) * out.mapping.scale_x;
    out.px_y = (step.y - out.origin.y) * out.mapping.scale_y;
    out.em_x = kUnitsPerEm * page.texels_per_unit_x * texel_x * out.px_x;
    out.em_y = kUnitsPerEm * page.texels_per_unit_y * texel_y * out.px_y;
    out.pen_px = out.origin.x * out.mapping.scale_x + out.mapping.offset_x;
    out.base_px = std::round(out.origin.y * out.mapping.scale_y + out.mapping.offset_y);
    if (!std::isfinite(out.em_x) || !std::isfinite(out.em_y) || !std::isfinite(out.pen_px) ||
        !std::isfinite(out.base_px) || !(out.px_x > 0) || !(out.px_y > 0)) {
        return std::nullopt;
    }
    return out;
}

// Draws one layer of a placed glyph in a colour (alpha 0x80 opaque); false where it cannot be drawn.
bool Draw(mgCDrawPrim &prim, const Placement &at, Layer layer, std::uint8_t r, std::uint8_t g, std::uint8_t b,
          std::uint8_t a) {
    if (at.ch == 0x20 || at.ch == 0x3000) {
        return true;
    }
    const float pen_whole = std::floor(at.pen_px);
    const int   shift = std::min(kShiftSteps - 1, static_cast<int>((at.pen_px - pen_whole) * kShiftSteps));
    const bool  rim = layer != Layer::Fill;
    const Ink  *ink = GetInk({at.ch, static_cast<int>(std::lround(at.em_x * 16)), static_cast<int>(std::lround(at.em_y * 16)),
                              shift, layer, rim ? static_cast<int>(std::lround(at.px_x * 16)) : 0,
                              rim ? static_cast<int>(std::lround(at.px_y * 16)) : 0});
    if (!ink) {
        return false;
    }
    // The image sits on whole target pixels, so each texel lands on one pixel and nothing is resampled.
    auto vertex = [&](float px, float py, float u, float v) {
        gfx::Vertex2D out = at.origin;
        out.x = (px - at.mapping.offset_x) / at.mapping.scale_x;
        out.y = (py - at.mapping.offset_y) / at.mapping.scale_y;
        out.u = u;
        out.v = v;
        out.color[0] = r;
        out.color[1] = g;
        out.color[2] = b;
        out.color[3] = a;
        return out;
    };
    const float                        left = pen_whole + static_cast<float>(ink->x0 - 1);
    const float                        top = at.base_px + static_cast<float>(ink->y0 - 1);
    const auto                         w = static_cast<float>(ink->width + 2);
    const auto                         h = static_cast<float>(ink->height + 2);
    const std::array<gfx::Vertex2D, 4> quad = {vertex(left, top, 0, 0), vertex(left + w, top, w, 0),
                                               vertex(left + w, top + h, w, h), vertex(left, top + h, 0, h)};
    gfx::TextureBinding                binding;
    binding.texture = ink->texture;
    binding.filter = gfx::Filter::Linear;
    mgPortPrimSubmitTexture(prim, quad, binding);
    return true;
}

std::uint8_t Alpha(int alpha, float share) {
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(alpha * share)), 0, 255));
}

// Draws the outline styles as rims, and the white-and-blue style's blue as one extrusion where retail
// stacks two copies; false leaves the style, or a cell the bitmap keeps, to retail, whose remaining
// shadows are copies of the glyph and so already reach the painter through set2DSpriteEasyFont.
bool DrawSharpFuchi(mgCDrawPrim &prim, const RECT &dst, const RECT &uv, int style, int alpha) {
    auto place = [&](float dx, float dy) {
        return Place(prim, dst.x, dst.y, dst.width, dst.height, uv.x, uv.y, uv.width, uv.height, dx, dy);
    };
    auto rim = [&](Layer layer) {
        const auto at = place(0, 0);
        return at && Draw(prim, *at, layer, 0, 0, 0, Alpha(alpha, FontOutlineAlpha(OutlineOf(layer))));
    };
    switch (style) {
        case FUCHI_OUTLINE:
            return rim(Layer::Thin);
        case FUCHI_OUTLINE_THICK:
            return rim(Layer::Thick);
        case FUCHI_OUTLINE_WIDE:
            return rim(Layer::Glow);
        case FUCHI_SHADOW_WHITE_BLUE: {
            const auto highlight = place(1, 1);
            if (!highlight) {
                return false;
            }
            Draw(prim, *highlight, Layer::Fill, 255, 255, 255, Alpha(alpha, 255.0f / 128.0f));
            // Retail's copies a pixel and two up-left, and the quarter pixels between: one solid block.
            for (int step = 4; step <= 8; ++step) {
                const float offset = -step / 4.0f;
                if (const auto at = place(offset, offset)) {
                    Draw(prim, *at, Layer::Fill, 42, 43, 49, Alpha(alpha, 1.0f));
                }
            }
            return true;
        }
        default:
            return false;
    }
}

} // namespace

const ttffont::Face *FontPortFace(FontPortTypeface typeface) {
    struct Slot {
        const char   *file;
        const char   *text;
        bool          tried = false;
        ttffont::Face face;
    };

    static Slot slots[] = {
        {.file = "ChronicleTwoMessage-Regular.ttf", .text = "message text"        },
        {.file = "ChronicleTwoMenu-Regular.ttf",    .text = "menu lettering"      },
        {.file = "ChronicleTwoBoard-Regular.ttf",   .text = "time board lettering"},
        {.file = "ChronicleTwoTitle-Regular.ttf",   .text = "title lettering"     },
    };
    Slot &slot = slots[static_cast<int>(typeface)];
    if (!slot.tried) {
        slot.tried = true;
        const auto      file = PathsExecutable().parent_path() / "fonts" / slot.file;
        std::error_code error;
        if (std::filesystem::is_regular_file(file, error) && slot.face.Load(file)) {
            CT_LOG_INFO(Gfx, "font: {} from {}", slot.text, PathsDisplay(file));
        } else {
            CT_LOG_INFO(Gfx, "font: {} is missing; keeping the original {}", PathsDisplay(file), slot.text);
        }
    }
    return slot.face.Loaded() ? &slot.face : nullptr;
}

bool FontPortProportional() { return ConfigGet().font_sharp && Ready(); }

float FontPortHalfAdvance(int font_no) {
    if (!FontPortProportional()) {
        return 1.0f;
    }
    const char32_t ch = FontPortCharacter(LanguageCode, font_no, LoadedTable());
    float          em = 0.0f;
    if (ch == 0 || !FontPortFace(FontPortTypeface::Message)->GetAdvance(ch, em)) {
        return 1.0f;
    }
    return em * kUnitsPerEm / kHalfWidth;
}

FontPortPen::FontPortPen(float fraction) : previous_(pen_fraction) { pen_fraction = fraction; }

FontPortPen::~FontPortPen() { pen_fraction = previous_; }

// Every font cell the game draws passes through here: CFont::DrawChar, retail's shadows (one call
// per copy, in retail's colours) and the event stopwatch.
PORT_OVERRIDE void set2DSpriteEasyFont(mgCDrawPrim *prim, mgRect<int> dst, mgRect<int> uv, RGBAQ_TYPE *color) {
    if (const auto at = Place(*prim, dst.left, dst.top, dst.right, dst.bottom, uv.left, uv.top, uv.right, uv.bottom, 0, 0);
        at && Draw(*prim, *at, Layer::Fill, color->r, color->g, color->b, color->a)) {
        return;
    }
    uv.right += uv.left;
    uv.bottom += uv.top;
    uv.right += 1;
    uv.bottom += 1;
    dst.right += dst.left;
    dst.bottom += dst.top;
    prim->Color(color->r, color->g, color->b, color->a);
    prim->TextureCrd(uv.left, uv.top);
    prim->Vertex(dst.left, dst.top, 0);
    prim->TextureCrd(uv.right, uv.bottom);
    prim->Vertex(dst.right, dst.bottom, 0);
}

PORT_OVERRIDE void set2DSprite_Fuchi(mgCDrawPrim *prim, RECT destination, RECT texture, int style, int alpha) {
    if (!DrawSharpFuchi(*prim, destination, texture, style, alpha)) {
        PortRetailSet2DSpriteFuchi(prim, destination, texture, style, alpha);
    }
}

// Retail's measure and draw of a line of menu text, with each half-width character advancing by its own width in
// the message font (FontPortHalfAdvance). The pen runs between pixels; each glyph is drawn at the whole pixel left of
// it and carries the rest in a FontPortPen. The width rounds up, so the text stays inside what was measured.
PORT_OVERRIDE void CFont::CalcDrawWH(char *text, int *width, int *height) {
    const int len = static_cast<int>(strlen(text));
    float     max_width = 0;
    int       max_height = 0;
    float     pen_x = 0;
    int       pen_y = 0;
    int       pos = 0;
    auto      reach = [&](int bottom) {
        max_width = std::max(max_width, pen_x);
        max_height = std::max(max_height, bottom);
    };

    while (pos < len) {
        char *cursor = &text[pos];

        if (const int font_no = GetAlphabeticalFontNo_cp(cursor); 0 < font_no) {
            pen_x += static_cast<float>(clearance_w / 2) * FontPortHalfAdvance(font_no);
            pos += 9;
            reach(pen_y + clearance_h);
        } else if (const u16 gaiji = GetFontGaijiFontNo(cursor); gaiji != 0) {
            pen_x += static_cast<float>(GetFontGaijiHankaku(gaiji) != 0 ? clearance_w / 2 : clearance_w);
            pos += 2;
            reach(pen_y + clearance_h);
        } else if (const u16 gaiji_no = GetGaijiFontNo(cursor); gaiji_no >= 0xFD00 && gaiji_no < 0xFD32) {
            pen_x += static_cast<float>(GetGaijiW(gaiji_no));
            pos += GetGaijiLen(gaiji_no);
            reach(pen_y + GetGaijiH(gaiji_no));
        } else if (const int half = GetHalfFontNo(*cursor); half == -2) {
            pen_x = 0;
            pos += 1;
            pen_y += clearance_h;
        } else if (CheckHalfFont(half) != 0) {
            pen_x += static_cast<float>(clearance_w / 2) * FontPortHalfAdvance(half);
            pos += 1;
            reach(pen_y + clearance_h);
        } else {
            pen_x += static_cast<float>(clearance_w);
            pos += 2;
            reach(pen_y + clearance_h);
        }
    }

    *width = static_cast<int>(std::ceil(max_width));
    *height = max_height;
}

PORT_OVERRIDE void CFont::DrawDirect(char *text, int x, int y) {
    SetPos(x, y);

    mgCDrawPrim prim;

    MySetPrim(&prim, 1, 0);

    int height = fptosi(offset_y);
    prim.offset_x = fptosi(offset_x) * 16;
    prim.offset_y = height * 16;
    prim.Begin(MG_PRIM_SPRITE);
    const int len = static_cast<int>(strlen(text));
    float     pen_x = 0;
    int       pen_y = 0;
    int       pos = 0;
    // Draws at the whole pixel left of the pen, carrying the rest to the painter.
    auto at_pen = [&](auto draw) {
        const float       exact = static_cast<float>(pos_x) + pen_x;
        const int         whole = static_cast<int>(std::floor(exact));
        const FontPortPen kerned(exact - static_cast<float>(whole));
        draw(whole);
    };

    while (pos < len) {
        char *cursor = &text[pos];

        if (const int font_no = GetAlphabeticalFontNo_cp(cursor); 0 < font_no) {
            at_pen([&](int at) { DrawChar(&prim, font_no, at, pos_y + pen_y, 1, color, (int) alpha); });
            pen_x += static_cast<float>(clearance_w / 2) * FontPortHalfAdvance(font_no);
            pos += 9;
        } else if (const u16 gaiji = GetFontGaijiFontNo(cursor); gaiji != 0) {
            at_pen([&](int at) { DrawChar(&prim, gaiji & 0xFFFF, at, pos_y + pen_y, 1, color, (int) alpha); });
            pen_x += static_cast<float>(GetFontGaijiHankaku(gaiji) != 0 ? clearance_w / 2 : clearance_w);
            pos += 2;
        } else if (const u16 gaiji_no = GetGaijiFontNo(cursor); gaiji_no >= 0xFD00 && gaiji_no < 0xFD32) {
            at_pen([&](int at) { DrawGaiji(&prim, gaiji_no, at, pos_y + pen_y); });
            pen_x += static_cast<float>(GetGaijiW(gaiji_no));
            pos += GetGaijiLen(gaiji_no);
        } else if (const int half = GetHalfFontNo(*cursor); half == -2) {
            pen_x = 0;
            pos += 1;
            pen_y += clearance_h;
        } else if (CheckHalfFont(half) != 0) {
            at_pen([&](int at) { DrawChar(&prim, half, at, pos_y + pen_y, 1, color, (int) alpha); });
            pen_x += static_cast<float>(clearance_w / 2) * FontPortHalfAdvance(half);
            pos += 1;
        } else {
            at_pen([&](int at) { DrawChar(&prim, cursor, at, pos_y + pen_y); });
            pen_x += static_cast<float>(clearance_w);
            pos += 2;
        }
    }

    prim.End();
}
