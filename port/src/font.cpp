#include "font.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <map>
#include <tuple>
#include <vector>

#include "font_port.hpp"
#include "log/log.hpp"
#include "mainloop.hpp"
#include "mg_drawprim_port.hpp"
#include "mglib_port.hpp"
#include "platform/config.hpp"
#include "platform/paths.hpp"
#include "platform/ttffont.hpp"

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
constexpr int   kShiftSteps = 4; // a glyph is drawn at a quarter-pixel pen; its baseline at a whole pixel

bool Ready() {
    static bool tried = false;
    static bool loaded = false;
    if (!tried) {
        tried = true;
        const auto      file = PathsExecutable().parent_path() / "fonts" / "ChronicleTwoMessage-Regular.ttf";
        std::error_code error;
        loaded = std::filesystem::is_regular_file(file, error) && ttffont::Load(file);
        if (loaded) {
            CT_LOG_INFO(Gfx, "font: message text from {}", PathsDisplay(file));
        } else {
            CT_LOG_INFO(Gfx, "font: {} is missing; keeping the original text", PathsDisplay(file));
        }
    }
    return loaded;
}

struct Ink {
    gfx::TextureHandle texture = gfx::kNullTexture;
    int                width = 0, height = 0, x0 = 0, y0 = 0;
    std::size_t        bytes = 0;
    int                used = 0; // the frame that last drew it
};

using InkKey = std::tuple<char32_t, int, int, int>; // character, em across and down in 1/16 px, pen shift

// Glyph images are immutable, so a recorded frame never samples one that changed. Past the budget the
// longest unused are destroyed; a frame's list keeps any it still draws alive until it is done with it.
const Ink *GetInk(char32_t ch, float em_x, float em_y, int shift) {
    static std::map<InkKey, Ink> cache;
    static std::size_t           bytes_used = 0;
    constexpr std::size_t budget = 32 * 1024 * 1024;
    const InkKey          key{ch, static_cast<int>(std::lround(em_x * 16)), static_cast<int>(std::lround(em_y * 16)), shift};
    const int             frame = mgPortFrameCount();
    if (const auto found = cache.find(key); found != cache.end()) {
        found->second.used = frame;
        return found->second.texture == gfx::kNullTexture ? nullptr : &found->second;
    }
    Ink                        ink;
    std::vector<unsigned char> coverage;
    if (!ttffont::RenderGlyph(ch, std::get<1>(key) / 16.0f, std::get<2>(key) / 16.0f,
                              static_cast<float>(shift) / kShiftSteps, 0.0f, coverage, ink.width, ink.height, ink.x0,
                              ink.y0) ||
        ink.width <= 0 || ink.height <= 0) {
        // Remembered as missing: a character the font cannot draw, or a size it will not.
        cache.emplace(key, ink);
        return nullptr;
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

// Draws the font cell `uv` of the retail font page as ChronicleTwoMessage into `dst`, both given as
// position and size; false leaves the cell to the bitmap.
bool DrawSharp(mgCDrawPrim &prim, const mgRect<int> &dst, const mgRect<int> &uv, const RGBAQ_TYPE &color) {
    if (!ConfigGet().font_sharp || uv.right <= 0 || uv.bottom <= 0 || dst.right <= 0 || dst.bottom <= 0 ||
        uv.left % FONT_TEX_CHAR_W != 0 || uv.top % FONT_TEX_CHAR_H != 0 || uv.bottom != FONT_TEX_CHAR_H) {
        return false;
    }
    const int      font_no = uv.top / FONT_TEX_CHAR_H * FONT_TEX_COLUMNS + uv.left / FONT_TEX_CHAR_W;
    const int      count = std::clamp(GetYoyakuTblNum(), 0, static_cast<int>(sizeof(FONT_TBL_BIN::yoyaku_tbl) / 2));
    const char32_t ch = FontPortCharacter(LanguageCode, font_no, {GetYoyakuTblTop(), static_cast<std::size_t>(count) * 2});
    if (ch == 0 || !Ready()) {
        return false;
    }
    if (ch == 0x20 || ch == 0x3000) {
        return true;
    }
    const FontPortPage page = FontPortPageOf(LanguageCode);
    const auto         mapping = gfx::GetUiMapping(mgPortCurrentTarget());
    // Logical pixels a texel: retail stretches the cell's texels over dst.
    const float        texel_x = static_cast<float>(dst.right) / static_cast<float>(uv.right);
    const float        texel_y = static_cast<float>(dst.bottom) / static_cast<float>(uv.bottom);
    const float        pen_x = static_cast<float>(dst.left) + page.pen_x * texel_x;
    const float        base_y = static_cast<float>(dst.top) + page.baseline * texel_y;
    const std::uint8_t tint[] = {static_cast<std::uint8_t>(color.r), static_cast<std::uint8_t>(color.g),
                                 static_cast<std::uint8_t>(color.b), static_cast<std::uint8_t>(color.a)};
    // Vertex units a logical pixel, and the pen in target pixels.
    const gfx::Vertex2D origin = mgPortPrimVertex(prim, pen_x, base_y, 0, 0, 0, tint);
    const gfx::Vertex2D step = mgPortPrimVertex(prim, pen_x + 1, base_y + 1, 0, 0, 0, tint);
    const float         to_px_x = (step.x - origin.x) * mapping.scale_x;
    const float         to_px_y = (step.y - origin.y) * mapping.scale_y;
    const float         em_x = kUnitsPerEm * page.texels_per_unit_x * texel_x * to_px_x;
    const float         em_y = kUnitsPerEm * page.texels_per_unit_y * texel_y * to_px_y;
    const float         pen_px = origin.x * mapping.scale_x + mapping.offset_x;
    const float         base_px = std::round(origin.y * mapping.scale_y + mapping.offset_y);
    const float         pen_whole = std::floor(pen_px);
    const int           shift = std::min(kShiftSteps - 1, static_cast<int>((pen_px - pen_whole) * kShiftSteps));
    if (!std::isfinite(em_x) || !std::isfinite(em_y) || !std::isfinite(pen_px) || !std::isfinite(base_px)) {
        return false;
    }
    const Ink *ink = GetInk(ch, em_x, em_y, shift);
    if (!ink) {
        return false;
    }
    // The image sits on whole target pixels, so each texel lands on one pixel and nothing is resampled.
    auto vertex = [&](float px, float py, float u, float v) {
        gfx::Vertex2D out = origin;
        out.x = (px - mapping.offset_x) / mapping.scale_x;
        out.y = (py - mapping.offset_y) / mapping.scale_y;
        out.u = u;
        out.v = v;
        return out;
    };
    const float                        left = pen_whole + static_cast<float>(ink->x0 - 1);
    const float                        top = base_px + static_cast<float>(ink->y0 - 1);
    const float                        right = left + static_cast<float>(ink->width + 2);
    const float                        bottom = top + static_cast<float>(ink->height + 2);
    const auto                         w = static_cast<float>(ink->width + 2);
    const auto                         h = static_cast<float>(ink->height + 2);
    const std::array<gfx::Vertex2D, 4> quad = {vertex(left, top, 0, 0), vertex(right, top, w, 0),
                                               vertex(right, bottom, w, h), vertex(left, bottom, 0, h)};
    gfx::TextureBinding                binding;
    binding.texture = ink->texture;
    binding.filter = gfx::Filter::Linear;
    mgPortPrimSubmitTexture(prim, quad, binding);
    return true;
}

} // namespace

// Every font cell the game draws passes through here: CFont::DrawChar, its outlines and shadows
// (set2DSprite_Fuchi draws one per offset, in retail's colours) and the event stopwatch.
PORT_OVERRIDE void set2DSpriteEasyFont(mgCDrawPrim *prim, mgRect<int> dst, mgRect<int> uv, RGBAQ_TYPE *color) {
    if (DrawSharp(*prim, dst, uv, *color)) {
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
