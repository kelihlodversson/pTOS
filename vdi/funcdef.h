/*
 * funcdef.h - Function definitions for VDI
 *
 * Copyright (c) 1999 Caldera, Inc. and Authors:
 *
 *
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */



#ifndef H_FUNCDEF_
#define H_FUNCDEF_

#include "portab.h"

#define OPEN_WORKSTATION        1
#define CLOSE_WORKSTATION       2
#define CLEAR_WORKSTATION       3
#define UPDATE_WORKSTATION      4
#define ESCAPE                  5
#define POLYLINE                6
#define POLYMARKER              7
#define TEXT                    8
#define FILLED_AREA             9
#define CELL_ARRAY              10
#define GDP                     11
#define CHAR_HEIGHT             12
#define CHAR_UP_VECT            13
#define SET_COLOR_REP           14
#define S_LINE_TYPE             15
#define S_LINE_WIDTH            16
#define S_LINE_COLOR            17
#define S_MARK_TYPE             18
#define S_MARK_SCALE            19
#define S_MARK_COLOR            20
#define SET_FONT                21
#define S_TEXT_COLOR            22
#define S_FILL_STYLE            23
#define S_FILL_INDEX            24
#define S_FILL_COLOR            25
#define INQUIRE_COLOR_REP       26
#define INQ_CELL_ARRAY          27
#define LOCATOR_INPUT           28
#define VALUATOR_INPUT          29
#define CHOICE_INPUT            30
#define STRING_INPUT            31
#define SET_WRITING_MODE        32
#define SET_INPUT_MODE          33
#define INQ_PL_ATTRIBUTES       35
#define INQ_PM_ATTRIBUTES       36
#define INQ_FA_ATTRIBUTES       37
#define INQ_TX_ATTRIBUTES       38
#define S_TEXT_ALIGN            39

#define OPEN_VWORKSTATION       100
#define CLOSE_VWORKSTATION      101
#define EXT_INQUIRE             102
#define CONTOUR_FILL            103
#define ST_FILLPERIMETER        104
#define ST_TXT_BACKGROUND_COLOR 105
#define ST_TXT_STYLE            106
#define ST_CH_HEIGHT            107
#define ST_LINE_END_STYLES      108
#define COPY_RASTER_FORM        109
#define TRANSFORM_FORM          110
#define ST_CUR_FORM             111
#define ST_UD_FILL_PATTERN      112
#define ST_UD_LINE_STYLE        113
#define FILL_RECTANGLE          114
#define I_INPUT_MODE            115
#define I_TEXT_EXTENT           116
#define I_TEXT_WIDTH            117
#define I_PIXEL_MAP             118
#define COPY_ALPHA_TEXT         119
#define ALTER_ALPHA_TEXT        120
#define ENABLE_COPY_ALTER       121
#define SHOW_CURSOR             122
#define HIDE_CURSOR             123
#define I_MOUSE                 124
#define EXCHANGE_BUTTON         125
#define EXCHANGE_MOUSE          126
#define EXCHANGE_CURSOR         127
#define KEYBOARD_STATE          128
#define ST_CLIPPING             129
#define I_FONT_NAME             130
#define I_FONT_INFO             131


#endif  /* FUNCDEF_H */
