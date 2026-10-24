#include "common.h"
#include "mw_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "font.hpp"
#include "font_port.hpp"
#include "mg_drawprim.hpp"
#include "mglib.hpp"
#include "nd_meswin.hpp"

// Message windows with the message font's own letter widths (docs/PC.md "Message font"). Retail lays a message out
// with every half-width character in the same cell; here the laid-out table, the line widths and the bubble follow
// each character's width in the font, while FontPortProportional.

namespace {

// Each laid-out window's pens in logical pixels, one per table entry: the table holds whole pixels, and a kerned pen
// falls between them.
std::unordered_map<const ClsMes *, std::vector<float> > pens;

// Set while retail lays a message out, so it measures the names and items it inserts with its own widths and the
// kerning that follows moves each line once.
bool retail_widths = false;

// How far kerning moves what follows a half-width character: its advance in the message font less retail's, which is
// font_w / 2 for a space and font_w * half_font_w_percent for anything else.
float KernShift(const ClsMes &mes, int font_no) {
    if (retail_widths) {
        return 0.0f;
    }
    const int space = GetHalfFontNo(' ');
    if (font_no == MES_CODE_SPACE || font_no == space) {
        return (FontPortHalfAdvance(space) - 1.0f) * static_cast<float>(mes.font_w / 2);
    }
    return (FontPortHalfAdvance(font_no) - 1.0f) * static_cast<float>(static_cast<int>(mes.font_w * mes.half_font_w_percent));
}

// The fraction of a pixel a laid-out character's kerned pen lies right of its table position.
float PenFraction(const ClsMes &mes, int index) {
    const auto found = pens.find(&mes);
    if (found == pens.end() || index < 0 || static_cast<std::size_t>(index) >= found->second.size()) {
        return 0.0f;
    }
    // A window laid out since without kerning, or another one at the same address, no longer matches its pens.
    const float fraction = found->second[static_cast<std::size_t>(index)] - static_cast<float>(mes.tbl[index].x);
    return fraction >= 0.0f && fraction < 1.0f ? fraction : 0.0f;
}

// Moves each laid-out character by how much the half-width characters before it on its line widen or narrow, and
// each line's width by its whole shift; then sizes the text and the bubble again from those widths as MakeMesWin did
// from retail's.
void Kern(ClsMes &mes) {
    if (!FontPortProportional()) {
        pens.erase(&mes);
        return;
    }
    std::vector<float> &pen = pens[&mes];
    pen.assign(static_cast<std::size_t>(std::clamp(mes.tbl_num, 0, static_cast<int>(MES_WIN_TBL_MAX))), 0.0f);
    const int space = GetHalfFontNo(' ');
    float     shift = 0.0f;
    int       line = 0;

    for (std::size_t index = 0; index < pen.size(); index++) {
        MES_WIN_TBL &entry = mes.tbl[index];
        const int    x = entry.x;
        pen[index] = static_cast<float>(x) + shift;
        entry.x = static_cast<s16>(std::floor(pen[index]));

        if (entry.code == MES_CODE_NEWLINE || entry.code == MES_CODE_PAGE || entry.code == MES_CODE_END) {
            if (line < MES_LINE_MAX && mes.line_w[line] >= 0) {
                mes.line_w[line] += static_cast<int>(std::lround(shift));
            }
            line++;
            shift = 0.0f;
        } else if (mes.CheckHalfFont(entry.code) != 0) {
            // A space the text sets a width for keeps it (the European text narrows its spaces line by line).
            const bool is_space = entry.code == MES_CODE_SPACE || entry.code == space;
            if (!is_space || index + 1 >= pen.size() || mes.tbl[index + 1].x - x == mes.font_w / 2) {
                shift += KernShift(mes, entry.code);
            }
        }
    }

    mes.text_w = 0;
    for (int index = 0; index < MES_LINE_MAX; index++) {
        if (mes.line_w[index] >= 0 && mes.text_w < mes.line_w[index]) {
            mes.text_w = mes.line_w[index];
        }
    }
    for (int index = 0; index < MES_LINE_MAX; index++) {
        mes.line_indent[index] = (mes.text_w - mes.line_w[index]) / 2;
    }

    if (mes.text_w < 45) {
        mes.fukidashi_w = 105;
        return;
    }
    mes.fukidashi_w = mes.text_w + 60;
    // Wider again where a page's last line is the widest.
    int lines = 0;
    for (int page = 0; page < mes.page_num && page < MES_PAGE_MAX; page++) {
        lines += mes.page_chars[page];
        if (lines > 0 && lines <= MES_LINE_MAX && mes.text_w <= mes.line_w[lines - 1]) {
            mes.fukidashi_w += 20;
            break;
        }
    }
}

} // namespace

PORT_OVERRIDE void ClsMes::MakeMesWin(int message) {
    const bool lays_out = message >= 0 && mes_no != message;
    retail_widths = true;
    PortRetailMakeMesWin(message);
    retail_widths = false;
    if (lays_out) {
        Kern(*this);
    }
}

PORT_OVERRIDE void ClsMes::MakeMesWin(char *str, int open, int reset_fade) {
    retail_widths = true;
    PortRetailMakeMesWin(str, open, reset_fade);
    retail_widths = false;
    if (str != NULL) {
        Kern(*this);
    }
}

PORT_OVERRIDE void ClsMes::DrawFont() {
    int index;
    int line;
    int x;
    int y;
    int dx;
    int dy;
    int left;
    int right;
    int bottom;
    int top;

    if (MesAbsDrawOff != 0) {
        return;
    }

    mgRect<int> rect(0, 0, 0, 0);
    mgCDrawPrim prim;
    MySetPrim(&prim, MES_PRIM_SPRITE, 0);
    int origin_y = fptosi(draw_off_y);
    prim.offset_x = fptosi(draw_off_x) * 16;
    prim.offset_y = origin_y * 16;
    prim.Begin(MG_PRIM_SPRITE);

    for (index = page_top; index < reveal_num; index++) {
        line = tbl[index].y / font_h;

        if (line_shade[line] == MES_SHADE_HIDDEN) {
            continue;
        }

        x = tbl[index].x;
        y = tbl[index].y;

        if (line_pos_on[line] != 0) {
            x += line_pos[line][0];
            y = line_pos[line][1];
        } else {
            if (scissor_on != 0) {
                left = scissor.x;
                right = left + scissor.width;
                top = scissor.y;
                bottom = top + scissor.height;

                if (left < 0) {
                    left = 0;
                }

                if (right < 0) {
                    right = 0;
                }

                if (left > mgScreenWidth - 1) {
                    left = mgScreenWidth - 1;
                }

                if (right > mgScreenWidth - 1) {
                    right = mgScreenWidth - 1;
                }

                if (top < 0) {
                    top = 0;
                }

                if (bottom < 0) {
                    bottom = 0;
                }

                if (top > mgScreenHeight - 1) {
                    top = mgScreenHeight - 1;
                }

                if (bottom > mgScreenHeight - 1) {
                    bottom = mgScreenHeight - 1;
                }

                prim.Direct(SCE_GS_SCISSOR_1, (unsigned long) left | ((unsigned long) right << 16) |
                                                  ((unsigned long) top << 32) | ((unsigned long) bottom << 48));
            }

            x += text_x;
            y += text_y;
            CalcCenteringXY(&dx, &dy);
            x += dx;
            y += dy;

            if (line_indent_on != 0) {
                x += line_indent[line];
            }
        }

        // A kerned pen falls between pixels: glyphs take the rest through the painter, pictures the nearer pixel.
        const float fraction = PenFraction(*this, index);

        if (tbl[index].code >= MES_CODE_GAIJI && tbl[index].code < 0xFD32) {
            RGBAQ_TYPE gaiji_color;
            int        gaiji_outline;

            if (tbl[index].code == 0xFD26 || tbl[index].code == 0xFD27 || tbl[index].code == 0xFD28) {
                gaiji_color = GetFontColor(index, &gaiji_outline);
            } else {
                gaiji_color.r = gaiji_color.g = gaiji_color.b = 0x80;
                gaiji_color.a = alpha * 128 / 128;
            }

            MySetTex(const_cast<char *>("gaiji"), &prim);
            DrawGaiji_sub(&prim, tbl[index].code, x + static_cast<int>(std::lround(fraction)), y, gaiji_color, font_h);
        } else {
            int        outline = 1;
            RGBAQ_TYPE glyph_color = GetFontColor(index, &outline);
            int        digit = GetDigitNo(tbl[index].code);

            if (digit_font == 1 && digit != -1) {
                MySetTex(const_cast<char *>("gaiji"), &prim);
                DrawDigit(&prim, digit, x + static_cast<int>(std::lround(fraction)), y, alpha, &glyph_color);
            } else {
                CFont::alpha = alpha;
                const FontPortPen kerned(fraction);

                if (0 <= line_alpha[line]) {
                    DrawChar(&prim, tbl[index].code, x, y, outline, glyph_color, line_alpha[line]);
                } else {
                    DrawChar(&prim, tbl[index].code, x, y, outline, glyph_color, alpha);
                }
            }
        }

        last_x = x;
        last_y = y;

        if (scissor_on != 0 && line_pos_on[line] == 0) {
            prim.Direct(SCE_GS_SCISSOR_1, ((unsigned long) (mgScreenWidth - 1) << 16) |
                                              ((unsigned long) (mgScreenHeight - 1) << 48));
        }
    }

    prim.End();
}

// Retail's widths of a name and of a system message, with each half-width character's KernShift.
PORT_OVERRIDE int ClsMes::GetStrWidth(char *text) {
    if (text == NULL) {
        return -1;
    }

    float width = 0;
    int   length = strlen(text);
    int   index = 0;
    int   font_number;
    char *cursor;

    while (index < length) {
        cursor = text + index;

        if (strncmp(cursor, "[UNI0", 5) == 0) {
            width += fptosi((float) font_w * half_font_w_percent) + KernShift(*this, GetAlphabeticalFontNo_cp(cursor));
            index += 9;
        } else {
            unsigned short gaiji = GetFontGaijiFontNo(cursor);

            if (gaiji != 0) {
                if (GetFontGaijiHankaku(gaiji)) {
                    width += fptosi((float) font_w * half_font_w_percent);
                } else {
                    width += fptosi(2.0f * ((float) font_w * half_font_w_percent));
                }

                index += 2;
            } else {
                font_number = GetHalfFontNo((s8) *cursor);

                if (font_number == -2) {
                    index++;
                } else if (0 <= font_number) {
                    if (font_number == GetHalfFontNo(' ')) {
                        width += font_w / 2;
                    } else {
                        width += fptosi((float) font_w * half_font_w_percent);
                    }

                    width += KernShift(*this, font_number);
                    index++;
                } else {
                    font_number = GetFontNo(cursor);

                    if (font_number <= 0) {
                        index++;
                    } else {
                        width += font_w;
                        index += 2;
                    }
                }
            }
        }
    }

    return static_cast<int>(std::lround(width));
}

PORT_OVERRIDE int ClsMes::GetMesWidth_system(int mes_no) {
    int             inserted_width;
    unsigned short *text;
    int             code;
    float           width;
    float           maximum;

    if (mes_no < 0) {
        return -1;
    }

    if (buff_system == NULL) {
        return -1;
    }

    text = GetTextLineDataTop_system(mes_no);
    width = 0;

    if (text == NULL) {
        return -1;
    }

    maximum = 0;

    while (1) {
        code = *text++;

        switch (code) {
            case MES_CODE_END:
                if (maximum < width) {
                    maximum = width;
                }

                return static_cast<int>(std::lround(maximum));
            case MES_CODE_NEWLINE:
                if (maximum < width) {
                    maximum = width;
                }

                width = 0;
                continue;
        }

        if (code >= 0xFAEA && code <= 0xFAF9) {
            inserted_width = GetStrWidth(0xFAF9 - code);

            if (inserted_width != -1) {
                width += inserted_width;
            }
        } else if (code >= 0xFFA0 && code <= 0xFFFF) {
            width += (int) (font_w * half_font_w_percent) + KernShift(*this, GetAlphabeticalFontNo_us(code));
        } else if (code >= 0xFDE0 && code < 0xFDF8) {
            if (GetFontGaijiHankaku(code) != 0) {
                width += (int) (font_w * half_font_w_percent);
            } else {
                width += (int) (2.0f * (font_w * half_font_w_percent));
            }
        } else if (code >= MES_CODE_GAIJI && code < 0xFD32) {
            width += GetGaijiW(code);
        } else if (code >= MES_CODE_MOVE_X && code < 0xFA00) {
            width += code - MES_CODE_MOVE_X;
        } else if (CheckHalfFont(code) != 0) {
            if (code == GetHalfFontNo(' ')) {
                width += font_w / 2;
            } else {
                width += (int) (font_w * half_font_w_percent);
            }

            width += KernShift(*this, code);
        } else {
            width += font_w;
        }
    }
}
