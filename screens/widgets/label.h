#pragma once
#include "widget.h"
typedef struct {
    char     text[32];
    uint16_t color;
} LabelData;
Widget make_label(int16_t x, int16_t y, const char *text, uint16_t color);
