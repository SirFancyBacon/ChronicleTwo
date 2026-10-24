#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <vector>

// SDL must precede the game's register-name macros.
#include <SDL3/SDL.h>

#include "font.hpp"
#include "font_outline.hpp"
#include "font_port.hpp"
#include "gfx_fixture.hpp"
#include "mainloop.hpp"
#include "mg_drawprim_port.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"
#include "nd_meswin.hpp"
#include "platform/config.hpp"
#include "platform/paths.hpp"
#include "platform/ttffont.hpp"

namespace {

std::array<std::uint8_t, 188> AsciiTable() {
    std::array<std::uint8_t, 188> table{};
    for (int i = 0; i < 94; ++i) {
        table[i * 2] = static_cast<std::uint8_t>(i + 0x20);
        table[i * 2 + 1] = 0x20;
    }
    return table;
}

// The retail tables' half-width entries: ASCII from the space to the tilde, without the backslash.
std::vector<std::uint8_t> RetailAsciiTable() {
    std::vector<std::uint8_t> table;
    for (int c = 0x20; c <= 0x7E; ++c) {
        if (c != '\\') {
            table.push_back(static_cast<std::uint8_t>(c));
            table.push_back(0x20);
        }
    }
    return table;
}

std::filesystem::path ShippedFont() {
    return PathsExecutable().parent_path() / "fonts" / "ChronicleTwoMessage-Regular.ttf";
}

using Entry = std::tuple<int, int, int, unsigned, int>;

std::vector<Entry> Entries(const ClsMes &mes) {
    std::vector<Entry> out;
    for (int i = 0; i < mes.tbl_num; ++i) {
        const auto &entry = mes.tbl[i];
        out.emplace_back(entry.code, entry.x, entry.y, entry.color, entry.wait);
    }
    return out;
}

// The lookup table is game state, so use a synthetic equivalent of the ASCII table, not disc assets.
struct FontState {
    std::array<unsigned char, FONT_TBL_BIN_SIZE> saved{};
    int                                          language = LanguageCode;
    mgRENDER_INFO                               *render = mgDrawManager.render_info;
    sceVif1Packet                               *packet = mgVif1Packet;
    mgRENDER_INFO                                info{};
    sceVif1Packet                                vif{};

    FontState() {
        auto *header = reinterpret_cast<FONT_TBL_BIN *>(GetYoyakuTblTop() - 8);
        std::memcpy(saved.data(), header, saved.size());
        std::memset(header, 0, sizeof(*header));
        header->half_font_num = header->yoyaku_num = 94;
        header->kanji_top_no = 0xffff;
        const auto table = AsciiTable();
        std::memcpy(GetYoyakuTblTop(), table.data(), table.size());
        LanguageCode = LANG_ENGLISH;
        mgDrawManager.render_info = &info;
        mgVif1Packet = &vif;
    }

    ~FontState() {
        std::memcpy(GetYoyakuTblTop() - 8, saved.data(), saved.size());
        LanguageCode = language;
        mgDrawManager.render_info = render;
        mgVif1Packet = packet;
        mgPortSetPrimSink({});
    }
};

} // namespace

TEST(FontPort, MapsEveryPalFontPage) {
    auto table = AsciiTable();
    for (int language = LANG_ENGLISH; language <= LANG_SPANISH; ++language) {
        for (int i = 0; i < 94; ++i) {
            EXPECT_EQ(FontPortCharacter(language, i, table), static_cast<char32_t>(i + 0x20));
        }
    }
    // The European pages: alphabetical_chara_tbl's letters, then FontGaijiConvTbl's symbols.
    EXPECT_EQ(FontPortCharacter(LANG_FRENCH, FONT_NO_ALPHABETICAL_TOP, table), char32_t{0xA1});      // ¡
    EXPECT_EQ(FontPortCharacter(LANG_GERMAN, FONT_NO_ALPHABETICAL_TOP + 32, table), char32_t{0xDF}); // ß
    EXPECT_EQ(FontPortCharacter(LANG_SPANISH, FONT_NO_FONT_GAIJI_TOP - 1, table), char32_t{0x153});  // œ
    EXPECT_EQ(FontPortCharacter(LANG_ITALIAN, FONT_NO_FONT_GAIJI_TOP, table), char32_t{0x25CB});     // ○
    EXPECT_EQ(FontPortCharacter(LANG_FRENCH, FONT_NO_FONT_GAIJI_TOP + 19, table), char32_t{0xFF65}); // half-width ・
    EXPECT_EQ(FontPortCharacter(LANG_FRENCH, FONT_NO_FONT_GAIJI_END - 1, table), char32_t{0x2019});  // ’
    EXPECT_EQ(FontPortCharacter(LANG_FRENCH, FONT_NO_FONT_GAIJI_END, table), 0u);

    // The English page: its full-width symbols by their Shift-JIS codes; kana and kanji keep the bitmap.
    std::array<std::uint8_t, 2 * 5> english = {'A', ' ', 0x81, 0x79, 0x81, 0x67, 0x82, 0xA0, 0x88, 0x9F};
    EXPECT_EQ(FontPortCharacter(LANG_ENGLISH, 0, english), char32_t{'A'});
    EXPECT_EQ(FontPortCharacter(LANG_ENGLISH, 1, english), char32_t{0x3010}); // 【
    EXPECT_EQ(FontPortCharacter(LANG_ENGLISH, 2, english), char32_t{0xE001}); // the full-width “
    EXPECT_EQ(FontPortCharacter(LANG_ENGLISH, 3, english), 0u);               // あ
    EXPECT_EQ(FontPortCharacter(LANG_ENGLISH, 4, english), 0u);               // a kanji
    EXPECT_EQ(FontPortCharacter(LANG_FRENCH, 1, english), 0u);                // a European page draws no Shift-JIS symbols

    EXPECT_EQ(FontPortCharacter(LANG_JAPANESE, 33, table), 0u);
    EXPECT_EQ(FontPortCharacter(LANG_ENGLISH, -1, table), 0u);
    EXPECT_EQ(FontPortCharacter(LANG_ENGLISH, 94, table), 0u);
    EXPECT_EQ(FontPortCharacter(LANG_ENGLISH, 3, {table.data(), 3}), 0u);
}

TEST(FontPort, PageGeometryIsTheFittedOne) {
    std::ifstream file(CT_MESSAGE_FONT_PARAMETERS);
    ASSERT_TRUE(file);
    const auto english = nlohmann::json::parse(file)["english_page"];
    const auto affine = english["affine"];
    const auto page = FontPortPageOf(LANG_ENGLISH);
    EXPECT_FLOAT_EQ(page.texels_per_unit_x, affine[0].get<float>() / 64.0f);
    EXPECT_FLOAT_EQ(page.texels_per_unit_y, affine[3].get<float>() / 64.0f);
    EXPECT_FLOAT_EQ(page.pen_x, affine[4].get<float>() / 64.0f);
    EXPECT_FLOAT_EQ(page.baseline, english["baseline"].get<float>() - affine[5].get<float>() / 64.0f);
    for (int language : {LANG_FRENCH, LANG_GERMAN, LANG_ITALIAN, LANG_SPANISH}) {
        const auto european = FontPortPageOf(language);
        EXPECT_FLOAT_EQ(european.texels_per_unit_x, 1.0f / 64.0f);
        EXPECT_FLOAT_EQ(european.baseline, 16.0f);
    }
}

// Every character the painter can ask for is in the shipped font, so no cell falls back to its bitmap.
TEST(FontPort, EveryMappedCharacterIsInTheShippedFont) {
    ASSERT_TRUE(ttffont::Load(ShippedFont()));
    std::vector<unsigned char> alpha;
    int                        w, h, x0, y0;
    auto                       draws = [&](char32_t ch) {
        return ch == 0x20 || ch == 0x3000 || ttffont::RenderGlyph(ch, 20.0f, 20.0f, 0.0f, 0.0f, alpha, w, h, x0, y0);
    };
    const auto ascii = RetailAsciiTable();
    ASSERT_EQ(ascii.size() / 2, static_cast<std::size_t>(FONT_NO_ALPHABETICAL_TOP));
    for (int no = 0; no < FONT_NO_FONT_GAIJI_END; ++no) {
        const char32_t ch = FontPortCharacter(LANG_FRENCH, no, ascii);
        ASSERT_NE(ch, 0u) << no;
        EXPECT_TRUE(draws(ch)) << "font number " << no;
    }
    std::vector<std::uint8_t> codes;
    for (int code = 0x8140; code <= 0x82FF; ++code) {
        codes.push_back(static_cast<std::uint8_t>(code >> 8));
        codes.push_back(static_cast<std::uint8_t>(code));
    }
    int mapped = 0;
    for (int no = 0; no < static_cast<int>(codes.size() / 2); ++no) {
        if (const char32_t ch = FontPortCharacter(LANG_ENGLISH, no, codes)) {
            ++mapped;
            EXPECT_TRUE(draws(ch)) << "Shift-JIS " << std::hex << (0x8140 + no);
        }
    }
    EXPECT_EQ(mapped, 119);
}

// The rims' profiles: a soft half-pixel edge at their measured radius, and the glow fading out.
TEST(FontOutline, RimsFadeOutAtTheirMeasuredReach) {
    EXPECT_FLOAT_EQ(FontOutlineOpacity(FontOutline::Thin, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(FontOutlineOpacity(FontOutline::Thin, 0.6f), 1.0f);
    EXPECT_NEAR(FontOutlineOpacity(FontOutline::Thin, 0.85f), 0.5f, 1e-5f);
    EXPECT_FLOAT_EQ(FontOutlineOpacity(FontOutline::Thin, FontOutlineReach(FontOutline::Thin)), 0.0f);
    EXPECT_NEAR(FontOutlineOpacity(FontOutline::Thick, 1.67f), 0.5f, 1e-5f);
    EXPECT_GT(FontOutlineReach(FontOutline::Thick), FontOutlineReach(FontOutline::Thin));
    EXPECT_FLOAT_EQ(FontOutlineOpacity(FontOutline::Glow, 0.0f), 1.0f);
    EXPECT_NEAR(FontOutlineOpacity(FontOutline::Glow, 1.25f), 0.5f, 1e-5f);
    EXPECT_FLOAT_EQ(FontOutlineOpacity(FontOutline::Glow, 2.5f), 0.0f);
    EXPECT_LT(FontOutlineAlpha(FontOutline::Glow), 0.5f);
    EXPECT_FLOAT_EQ(FontOutlineAlpha(FontOutline::Thin), 1.0f);
}

// A rim is round in logical pixels, so where a logical pixel is 4 target pixels across and 2 down it
// reaches twice as far across, and the glyph stays fully covered inside it.
TEST(FontOutline, RimIsRoundInLogicalPixels) {
    constexpr int                   size = 4;
    const std::vector<std::uint8_t> square(size * size, 255);
    const auto                      rim = FontOutlineRim(square, size, size, 4.0f, 2.0f, FontOutline::Thin);
    EXPECT_EQ(rim.margin_x, 6);
    EXPECT_EQ(rim.margin_y, 4);
    const int width = size + 2 * rim.margin_x;
    ASSERT_EQ(rim.alpha.size(), static_cast<std::size_t>(width * (size + 2 * rim.margin_y)));
    auto      at = [&](int x, int y) { return rim.alpha[static_cast<std::size_t>(y + rim.margin_y) * width + x + rim.margin_x]; };
    const int middle = size / 2;
    EXPECT_EQ(at(middle, middle), 255);
    EXPECT_EQ(at(size - 1 + 2, middle), 255); // half a logical pixel right
    EXPECT_EQ(at(size - 1 + 6, middle), 0);   // a logical pixel and a half right
    EXPECT_EQ(at(middle, size - 1 + 1), 255); // half a logical pixel down
    EXPECT_EQ(at(middle, size - 1 + 3), 0);   // a logical pixel and a half down
    // Corners are round: the diagonal pixel as far out as the side one is fainter.
    EXPECT_LT(at(size - 1 + 4, size - 1 + 2), at(size - 1 + 4, middle));
}

TEST(FontPortGpu, SharpDrawingKeepsRetailLayoutAndLineBreaks) {
    const auto root = std::filesystem::temp_directory_path() /
                      ("dc2_font_" + std::to_string(dc::test::ProcessId()));
    ASSERT_FALSE(std::filesystem::exists(root));
    std::filesystem::create_directories(root / "saves");

    struct Cleanup {
        std::filesystem::path root;

        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{root};

    PathsSetSaveRoot(root / "saves");
    auto select = [&](const char *choice) {
        std::ofstream(root / "config.json") << R"({"video":{"text_font":")" << choice << "\"}}";
        EXPECT_TRUE(ConfigLoad());
    };
    dc::test::GfxFixture                     gfx(1440, 1080, 1.0f, dc::test::GfxOptions{.offscreen = true});
    FontState                                state;
    std::vector<gfx::TextureHandle>          textures;
    std::vector<std::vector<gfx::Vertex2D> > geometry;
    mgPortSetPrimSink([&](gfx::Primitive, std::span<const gfx::Vertex2D> vertices, const gfx::TextureBinding &texture,
                          const gfx::DrawState &) {
        textures.push_back(texture.texture);
        geometry.emplace_back(vertices.begin(), vertices.end());
    });

    char  direct[] = "Wi 0129!\nA long menu item";
    CFont font;
    select("original");
    int old_w = 0;
    int old_h = 0;
    font.CalcDrawWH(direct, &old_w, &old_h);
    font.DrawDirect(direct, 40, 30);
    const auto original_textures = textures;
    textures.clear();

    ClsMes mes;
    char   text[] = "Wide Wi, 0129!\nSecond line (X) done";
    ASSERT_EQ(mes.MakeMesWinTbl(text), 1);
    const auto original = Entries(mes);
    const int  old_font_w = mes.font_w;
    const int  old_font_h = mes.font_h;
    select("sharp");
    ASSERT_TRUE(ConfigGet().font_sharp);
    int new_w = 0;
    int new_h = 0;
    font.CalcDrawWH(direct, &new_w, &new_h);
    EXPECT_EQ(new_w, old_w);
    EXPECT_EQ(new_h, old_h);
    EXPECT_EQ(old_w, 16 * 7); // Half-cell advance is 7, not 8 or the TrueType advance.
    EXPECT_EQ(old_h, 48);
    font.DrawDirect(direct, 40, 30);
    ASSERT_FALSE(textures.empty());
    EXPECT_NE(textures, original_textures); // The sharp painter actually ran; this is not fallback equality.
    EXPECT_NE(textures.front(), gfx::kNullTexture);
    ASSERT_EQ(mes.MakeMesWinTbl(text), 1);
    EXPECT_EQ(Entries(mes), original);
    EXPECT_EQ(mes.font_w, old_font_w);
    EXPECT_EQ(mes.font_h, old_font_h);
    mes.reveal_num = mes.tbl_num - 1;
    mes.DrawFont();
    EXPECT_EQ(Entries(mes), original);

    // A tokenized page also keeps its explicit page/line/space/position controls and icon cells.
    std::array<u16, 16> message = {1, 0, 7, 3, 0, 0xf700 | 30, 33, 0xff02, 55, 0xfd06,
                                   0xff00, 0xf900 | 5, 34, 0xff03, 35, 0xff01};
    mes.SetBuff(reinterpret_cast<short *>(message.data()));
    select("original");
    ASSERT_EQ(mes.MakeMesWinTbl(7), 1);
    const auto tokenized = Entries(mes);
    select("sharp");
    ASSERT_EQ(mes.MakeMesWinTbl(7), 1);
    EXPECT_EQ(Entries(mes), tokenized);
    EXPECT_TRUE(std::ranges::any_of(tokenized, [](const Entry &e) { return std::get<0>(e) == MES_CODE_PAGE; }));

    // Shadows are retail's copies through the painter; outlines are one rim each; the white-and-blue
    // style's blue is five steps of one extrusion. The fill comes last with its alpha multiplied, each
    // a quad of the glyph's image on whole pixels inside the cell.
    constexpr std::array<int, 9> passes = {1, 2, 2, 2, 3, 2, 2, 7, 2};
    char                         letter[] = "W";
    font.SetColor(23, 45, 67, 96);
    font.alpha = 64;
    const auto mapping = gfx::GetUiMapping(mgPortCurrentTarget());
    auto       on_pixel = [](float logical, float scale, float offset) {
        const float pixel = logical * scale + offset;
        return std::abs(pixel - std::round(pixel)) < 0.01f;
    };
    for (int style = 0; style < FUCHI_NUM; ++style) {
        geometry.clear();
        textures.clear();
        font.SetFuchi(style);
        font.DrawDirect(letter, 40, 30);
        ASSERT_EQ(geometry.size(), static_cast<std::size_t>(passes[style])) << "style " << style;
        const auto &fill = geometry.back();
        ASSERT_EQ(fill.size(), 4u);
        EXPECT_EQ(fill[0].color[0], 23);
        EXPECT_EQ(fill[0].color[1], 45);
        EXPECT_EQ(fill[0].color[2], 67);
        EXPECT_EQ(fill[0].color[3], 48);
        const auto texture = gfx::GetTextureInfo(textures.back());
        ASSERT_TRUE(texture.has_value());
        // One texel a target pixel, on whole pixels: nothing is resampled.
        EXPECT_NEAR(static_cast<float>(texture->width), (fill[1].x - fill[0].x) * mapping.scale_x, 0.01f);
        EXPECT_NEAR(static_cast<float>(texture->height), (fill[2].y - fill[1].y) * mapping.scale_y, 0.01f);
        EXPECT_TRUE(on_pixel(fill[0].x, mapping.scale_x, mapping.offset_x));
        EXPECT_TRUE(on_pixel(fill[0].y, mapping.scale_y, mapping.offset_y));
        // Inside the half cell at 40, give or take the border and the English page's pen, left of the cell.
        EXPECT_GE(fill[0].x, 39.0f);
        EXPECT_LE(fill[1].x, 49.0f);
        if (style == FUCHI_OUTLINE || style == FUCHI_OUTLINE_THICK || style == FUCHI_OUTLINE_WIDE) {
            const auto &rim = geometry.front();
            EXPECT_EQ(rim[0].color[0], 0);
            EXPECT_LT(rim[0].x, fill[0].x - 0.5f); // the rim reaches past the glyph
            EXPECT_TRUE(on_pixel(rim[0].x, mapping.scale_x, mapping.offset_x));
        }
    }
}
