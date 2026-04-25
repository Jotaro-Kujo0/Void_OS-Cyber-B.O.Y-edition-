// theme.h
#pragma once
#include "config.h"

// Primary palette
#define T_BG        C_BLACK
#define T_PANEL     C_MGRAY
#define T_BORDER    C_BORDER
#define T_FG        C_PGREEN
#define T_DIM       C_DGREEN
#define T_ACCENT    0x07FF    // cyan highlight
#define T_WARN      C_AMBER
#define T_ERR       C_RED
#define T_SEL_BG    0x0140    // selected item background
#define T_SEL_FG    C_PGREEN

// Typography sizes (TFT_eSPI font sizes)
#define FONT_SM     1
#define FONT_MD     2
#define FONT_LG     3

// Layout
#define ROW_H       ((SCR_H - STATS_H) / 3)  // 97px per app row
#define APP_PAD     6