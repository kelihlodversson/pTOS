/* Regression for native GEM box geometry and dithered line ordering.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "obdefs.h"
#include "funcdef.h"
#include "gemgraf.h"
#include "gemgsxif.h"
#include "string.h"
#include <stdio.h>

WORD ptsin[20], gl_wbox = 8, gl_hbox = 16;
static WORD lines[8][4], styles[12], line_count, style_count;
static int failures;
#define CHECK(c) do { if (!(c)) { printf("GEM geometry FAIL line %d\n", __LINE__); failures++; } } while (0)

void gsx_1code(WORD code, WORD value)
{
    CHECK(code == SET_UD_LINE_STYLE && style_count < 12);
    if (style_count < 12)
        styles[style_count++] = value;
}

void v_pline(WORD count, WORD *points)
{
    CHECK(count == 2 && line_count < 8);
    if (line_count < 8)
        memcpy(lines[line_count++], points, sizeof(lines[0]));
}

int main(void)
{
    GRECT rectangle = { 10, 20, 40, 50 };
    GRECT wrapped = { 32760, 32760, 16, 20 };
    const WORD box[10] = { 10, 20, 49, 20, 49, 69, 10, 69, 10, 20 };
    const WORD wrapped_box[10] = {
        32760, 32760, -32761, 32760, -32761, -32757,
        32760, -32757, 32760, 32760
    };
    const WORD box_lines[4][4] = {
        { 10, 20, 49, 20 }, { 49, 20, 49, 69 },
        { 49, 69, 10, 69 }, { 10, 69, 10, 20 }
    };
    const WORD box_styles[5] = {
        0x5555, (WORD)0xaaaa, (WORD)0xaaaa, (WORD)0xaaaa, (WORD)0xffff
    };
    const WORD corners[8][4] = {
        { 10, 52, 10, 20 }, { 10, 20, 26, 20 },
        { 34, 20, 49, 20 }, { 49, 20, 49, 52 },
        { 49, 38, 49, 69 }, { 49, 69, 34, 69 },
        { 26, 69, 10, 69 }, { 10, 69, 10, 38 }
    };
    const WORD corner_styles[12] = {
        0x5555, 0x5555, (WORD)0xffff,
        0x5555, (WORD)0xaaaa, (WORD)0xffff,
        (WORD)0xaaaa, (WORD)0xaaaa, (WORD)0xffff,
        (WORD)0xaaaa, (WORD)0xaaaa, (WORD)0xffff
    };

    gsx_bxpts(&rectangle);
    CHECK(!memcmp(ptsin, box, sizeof(box)));
    gsx_bxpts(&wrapped);
    CHECK(!memcmp(ptsin, wrapped_box, sizeof(wrapped_box)));
    gsx_xbox(&rectangle);
    CHECK(line_count == 4 && style_count == 5);
    CHECK(!memcmp(lines, box_lines, sizeof(box_lines)));
    CHECK(!memcmp(styles, box_styles, sizeof(box_styles)));
    line_count = style_count = 0;
    gsx_xcbox(&rectangle);
    CHECK(line_count == 8 && style_count == 12);
    CHECK(!memcmp(lines, corners, sizeof(corners)));
    CHECK(!memcmp(styles, corner_styles, sizeof(corner_styles)));
    CHECK(rectangle.g_x == 10 && rectangle.g_y == 20 &&
          rectangle.g_w == 40 && rectangle.g_h == 50);
    if (failures)
        return 1;
    puts("GEM box geometry, signed coordinates and dither ordering: PASS");
    return 0;
}
