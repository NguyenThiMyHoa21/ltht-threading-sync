#ifndef UI_H
#define UI_H

#include <stddef.h>
#include "sync_tracer.h"

#define UI_RESET   "\033[0m"
#define UI_BOLD    "\033[1m"
#define UI_DIM     "\033[2m"
#define UI_CYAN    "\033[36m"
#define UI_BCYAN   "\033[96m"
#define UI_BLUE    "\033[34m"
#define UI_GREEN   "\033[32m"
#define UI_BGREEN  "\033[92m"
#define UI_YELLOW  "\033[33m"
#define UI_BYELLOW "\033[93m"
#define UI_RED     "\033[31m"
#define UI_BRED    "\033[91m"
#define UI_MAGENTA "\033[35m"
#define UI_WHITE   "\033[97m"

void ui_clear(void);
void ui_home(void);
void ui_clear_below(void);
void ui_hide_cursor(void);
void ui_show_cursor(void);
void ui_header(const char *subtitle);
void ui_section(const char *title);
void ui_progress_bar(int current, int total, int width, char *out, size_t out_size);
void ui_wait_enter(void);
int ui_read_int(const char *prompt, int default_value, int min_value, int max_value);
void ui_print_menu(void);
void ui_draw_tracer(SyncTracer *tracer, int max_rows);

#endif
