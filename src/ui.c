#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"

void ui_clear(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}

void ui_home(void) {
    printf("\033[H");
}

void ui_clear_below(void) {
    printf("\033[J");
    fflush(stdout);
}

void ui_hide_cursor(void) {
    printf("\033[?25l");
    fflush(stdout);
}

void ui_show_cursor(void) {
    printf("\033[?25h");
    fflush(stdout);
}

void ui_header(const char *subtitle) {
    printf(UI_BCYAN UI_BOLD);
    printf("  \xE2\x95\x94");
    for (int i = 0; i < 68; i++) printf("\xE2\x95\x90");
    printf("\xE2\x95\x97\n");

    printf("  \xE2\x95\x91   THREADING & SYNCHRONIZATION LAB v3.0  (Linux Systems / C11)    \xE2\x95\x91\n");
    printf("  \xE2\x95\x91   " UI_WHITE "POSIX File Scan + Sync Profiler + KV Cache + Web Dashboard" UI_BCYAN "     \xE2\x95\x91\n");

    printf("  \xE2\x95\x9A");
    for (int i = 0; i < 68; i++) printf("\xE2\x95\x90");
    printf("\xE2\x95\x9D\n" UI_RESET);

    if (subtitle && *subtitle) {
        printf("  " UI_CYAN "\xE2\x96\xB8 " UI_WHITE "%s\033[K\n" UI_RESET, subtitle);
    }
    printf("\033[K\n");
}

void ui_section(const char *title) {
    printf("  " UI_CYAN UI_BOLD "\xE2\x94\x8C\xE2\x94\x80 %s \xE2\x94\x80\xE2\x94\x80\033[K\n" UI_RESET, title ? title : "");
}

void ui_progress_bar(int current, int total, int width, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (total <= 0) total = 1;
    if (current < 0) current = 0;
    if (current > total) current = total;

    int filled = (current * width) / total;
    size_t pos = 0;
    for (int i = 0; i < width && pos + 4 < out_size; i++) {
        const char *glyph = (i < filled) ? "\xE2\x96\x88" : "\xE2\x96\x91";
        memcpy(out + pos, glyph, 3);
        pos += 3;
    }
    out[pos] = '\0';
}

void ui_wait_enter(void) {
    printf("\n" UI_DIM "  Nhan ENTER de quay lai menu...\033[K" UI_RESET);
    fflush(stdout);
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {}
}

int ui_read_int(const char *prompt, int default_value, int min_value, int max_value) {
    char line[128];
    while (1) {
        printf("%s" UI_DIM " [mac dinh %d]" UI_RESET ": ", prompt, default_value);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) return default_value;

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (len == 0) return default_value;

        char *end = NULL;
        long value = strtol(line, &end, 10);
        if (end != line && *end == '\0' && value >= min_value && value <= max_value) {
            return (int)value;
        }
        printf(UI_BRED "  Gia tri khong hop le. Hay nhap tu %d den %d.\n" UI_RESET,
               min_value, max_value);
    }
}

void ui_print_menu(void) {
    printf("  " UI_WHITE UI_BOLD "MAIN MENU\n" UI_RESET);
    printf("  \xE2\x94\x8C");
    for (int i = 0; i < 68; i++) printf("\xE2\x94\x80");
    printf("\xE2\x94\x90\n");

    printf("  \xE2\x94\x82 " UI_BGREEN  "[1]" UI_RESET " Thread Pool & POSIX File Scanner (/usr/include / CPU / IO)  \xE2\x94\x82\n");
    printf("  \xE2\x94\x82 " UI_BCYAN   "[2]" UI_RESET " Benchmark (1T vs MT) & 3-Stage Barrier (pthread_barrier_t) \xE2\x94\x82\n");
    printf("  \xE2\x94\x82 " UI_BYELLOW "[3]" UI_RESET " OS Sync Profiler (getrusage) & Concurrent KV Cache Striping \xE2\x94\x82\n");
    printf("  \xE2\x94\x82 " UI_MAGENTA "[4]" UI_RESET " Log Stream Producer-Consumer (/var/log/dpkg.log + sem_t)   \xE2\x94\x82\n");
    printf("  \xE2\x94\x82 " UI_CYAN    "[5]" UI_RESET " RWLock, Deadlock Recovery (timedlock) & Dining Philosophers  \xE2\x94\x82\n");
    printf("  \xE2\x94\x82 " UI_BGREEN UI_BOLD "[6]" UI_RESET UI_BOLD " Web Control Center (Mo Chrome/Edge: http://127.0.0.1:8080)" UI_RESET "\xE2\x94\x82\n");
    printf("  \xE2\x94\x82 " UI_BLUE    "[7]" UI_RESET " Kien truc he thong & Standalone Test Demos                 \xE2\x94\x82\n");
    printf("  \xE2\x94\x82 " UI_BRED    "[0]" UI_RESET " Thoat                                                        \xE2\x94\x82\n");

    printf("  \xE2\x94\x94");
    for (int i = 0; i < 68; i++) printf("\xE2\x94\x80");
    printf("\xE2\x94\x98\n");
}

static const char *event_color(SyncEventType type) {
    switch (type) {
        case EV_MUTEX:  return UI_BYELLOW;
        case EV_COND:   return UI_MAGENTA;
        case EV_SEM:    return UI_BCYAN;
        case EV_RWLOCK: return UI_BGREEN;
        case EV_ATOMIC: return UI_GREEN;
        case EV_QUEUE:  return UI_CYAN;
        default:        return UI_DIM;
    }
}

void ui_draw_tracer(SyncTracer *tracer, int max_rows) {
    SyncEvent evs[12];
    int cap = max_rows > 12 ? 12 : (max_rows < 1 ? 1 : max_rows);
    uint64_t total = 0;
    int n = sync_tracer_recent(tracer, evs, cap, &total);

    printf("\n  " UI_WHITE UI_BOLD "LIVE SYNCHRONIZATION TRACE" UI_RESET
           UI_DIM " (total events: %llu)\033[K\n" UI_RESET,
           (unsigned long long)total);
    printf("  \xE2\x94\x8C");
    for (int i = 0; i < 68; i++) printf("\xE2\x94\x80");
    printf("\xE2\x94\x90\033[K\n");

    for (int i = 0; i < cap; i++) {
        if (i < n) {
            const SyncEvent *e = &evs[i];
            printf("  \xE2\x94\x82 " UI_DIM "%7.1fms" UI_RESET " %s%-6s" UI_RESET
                   " " UI_WHITE "%-11.11s" UI_RESET " %-39.39s \xE2\x94\x82\033[K\n",
                   e->elapsed_ms,
                   event_color(e->type), sync_event_type_name(e->type),
                   e->actor, e->detail);
        } else {
            printf("  \xE2\x94\x82 %-66s \xE2\x94\x82\033[K\n", "");
        }
    }
    printf("  \xE2\x94\x94");
    for (int i = 0; i < 68; i++) printf("\xE2\x94\x80");
    printf("\xE2\x94\x98\033[K\n");
}
