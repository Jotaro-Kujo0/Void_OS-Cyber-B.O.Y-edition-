// screens/stat_screen.c
#include "screen.h"
#include "widgets/label.h"
#include "widgets/progressbar.h"

// Colors — classic Pipboy phosphor green
#define C_GREEN  RGB(57, 255, 20)
#define C_DIM    RGB(20, 80,  10)
#define C_BLACK  RGB(0,  0,   0)

// Static storage for this screen's widgets
static Widget w_title, w_str_label, w_str_bar;
static Widget w_per_label, w_per_bar;
static Widget w_end_label, w_end_bar;

// Static data (one per widget, named clearly)
static LabelData      ld_title, ld_str, ld_per, ld_end;
static ProgressBarData pb_str, pb_per, pb_end;
static Screen          stat_scr;

static void stat_enter(Screen *s) {
    // Could trigger animations, load data from SD, etc.
    pb_str.target = 0.75f;
    pb_per.target = 0.60f;
    pb_end.target = 0.90f;
}

Screen *create_stat_screen(void) {
    stat_scr = (Screen){0};
    stat_scr.on_enter = stat_enter;

    // Title
    w_title = make_label(10, 8, "[ S.P.E.C.I.A.L ]", C_GREEN);
    screen_add_widget(&stat_scr, &w_title);

    // STR row
    w_str_label = make_label(10, 30, "STR", C_GREEN);
    screen_add_widget(&stat_scr, &w_str_label);

    // Reuse make_progressbar but bind pb_str manually
    w_str_bar = make_progressbar(50, 32, 150, 10, C_GREEN, C_DIM);
    w_str_bar.data = &pb_str;
    pb_str = (ProgressBarData){
        .value=0.0f, .target=0.75f, .animated=true,
        .fg_color=C_GREEN, .bg_color=C_DIM
    };
    screen_add_widget(&stat_scr, &w_str_bar);

    // PER row
    w_per_label = make_label(10, 50, "PER", C_GREEN);
    screen_add_widget(&stat_scr, &w_per_label);
    w_per_bar = make_progressbar(50, 52, 150, 10, C_GREEN, C_DIM);
    w_per_bar.data = &pb_per;
    pb_per = (ProgressBarData){
        .value=0.0f, .target=0.60f, .animated=true,
        .fg_color=C_GREEN, .bg_color=C_DIM
    };
    screen_add_widget(&stat_scr, &w_per_bar);

    // END row
    w_end_label = make_label(10, 70, "END", C_GREEN);
    screen_add_widget(&stat_scr, &w_end_label);
    w_end_bar = make_progressbar(50, 72, 150, 10, C_GREEN, C_DIM);
    w_end_bar.data = &pb_end;
    pb_end = (ProgressBarData){
        .value=0.0f, .target=0.90f, .animated=true,
        .fg_color=C_GREEN, .bg_color=C_DIM
    };
    screen_add_widget(&stat_scr, &w_end_bar);

    return &stat_scr;
}