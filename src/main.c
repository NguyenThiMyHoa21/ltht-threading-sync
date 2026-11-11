#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "sync_labs.h"
#include "ui.h"
#include "web_server.h"

static const char *worker_status_str(WorkerStatus st) {
    switch (st) {
        case WORKER_RUNNING: return UI_BGREEN  "RUNNING" UI_RESET;
        case WORKER_STOPPED: return UI_DIM     "STOPPED" UI_RESET;
        default:             return UI_BYELLOW "WAITING" UI_RESET;
    }
}

static void render_pool_frame(LabContext *ctx, const LabSnapshot *s) {
    char bar[256];
    ui_progress_bar(s->pool.completed, s->pool.total_tasks, 36, bar, sizeof(bar));
    int pct = s->pool.total_tasks > 0 ? (s->pool.completed * 100 / s->pool.total_tasks) : 100;
    if (pct > 100) pct = 100;

    const char *wname = s->pool.workload_type == TASK_FS_SCAN ? "POSIX Scan"
                      : (s->pool.workload_type == TASK_CPU_PRIME ? "CPU Prime"
                      : (s->pool.workload_type == TASK_CPU_HASH ? "CPU Hash" : "Sim I/O"));

    ui_home();
    ui_header("Thread Pool + POSIX File Scanner Dashboard");

    printf("  Mode: " UI_BCYAN "%-10s" UI_RESET
           "  Workers: " UI_WHITE UI_BOLD "%d" UI_RESET
           "  Queue: " UI_BYELLOW "%2d/%-3d" UI_RESET
           "  Rate: " UI_BGREEN "%.0f/s" UI_RESET
           "  Time: " UI_MAGENTA "%.2fs\033[K\n" UI_RESET,
           wname, s->pool.workers, s->pool.queued, s->pool.queue_cap,
           s->pool.throughput, s->elapsed_ms / 1000.0);

    if (s->pool.workload_type == TASK_FS_SCAN) {
        printf("  Dir : " UI_WHITE "%s" UI_RESET "  Bytes: " UI_BCYAN "%lluKB" UI_RESET
               "  Lines: %llu  Hits('%s'): " UI_BGREEN "%llu" UI_RESET "  CRC: %08x\033[K\n",
               s->pool.scan_dir, (unsigned long long)(s->pool.total_bytes / 1024),
               (unsigned long long)s->pool.total_lines, s->pool.scan_pattern,
               (unsigned long long)s->pool.total_hits, s->pool.combined_crc);
    }

    printf("  Progress: " UI_BGREEN "%s" UI_RESET " %3d%% (%d/%d) | ctx_sw=%ld\033[K\n\n",
           bar, pct, s->pool.completed, s->pool.total_tasks,
           s->usage.nvcsw + s->usage.nivcsw);

    printf("  \xE2\x94\x8C\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xAC"
           "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xAC"
           "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xAC"
           "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xAC"
           "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xAC"
           "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x90\033[K\n");
    printf("  \xE2\x94\x82 Wrk \xE2\x94\x82 TID   \xE2\x94\x82 Status  \xE2\x94\x82 Current Item    \xE2\x94\x82 Done  \xE2\x94\x82 Busy ms  \xE2\x94\x82\033[K\n");
    printf("  \xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xBC"
           "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xBC"
           "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xBC"
           "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xBC"
           "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xBC"
           "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xA4\033[K\n");

    for (int i = 0; i < s->pool.workers && i < LAB_MAX_WORKERS; i++) {
        const WorkerState *w = &s->pool.worker_states[i];
        printf("  \xE2\x94\x82 %2d  \xE2\x94\x82 %-5ld \xE2\x94\x82 %s \xE2\x94\x82 %-15.15s \xE2\x94\x82 %4d  \xE2\x94\x82 %8.1f \xE2\x94\x82\033[K\n",
               i + 1, w->tid, worker_status_str(w->status), w->current_label, w->completed_tasks, w->busy_ms);
    }
    printf("  \xE2\x94\x94\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xB4"
           "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xB4"
           "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xB4"
           "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xB4"
           "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xB4"
           "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x98\033[K\n");

    ui_draw_tracer(&ctx->tracer, 5);
    ui_clear_below();
}

static void render_bench_frame(LabContext *ctx, const LabSnapshot *s) {
    char b1[180], b2[180];
    ui_progress_bar(s->bench.single_done, s->bench.tasks, 26, b1, sizeof(b1));
    ui_progress_bar(s->bench.multi_done, s->bench.tasks, 26, b2, sizeof(b2));

    ui_home();
    ui_header("Benchmark: 1-Thread vs Multi-Thread Speedup");
    printf("  Type: %s | Tasks: %d | Workers: %d\033[K\n\n",
           s->bench.bench_kind == 1 ? "POSIX File Scan (/usr/include)" : "CPU Prime (450,000)",
           s->bench.tasks, s->bench.workers);

    printf("  [Phase 1] 1 Thread   : " UI_BYELLOW "%s" UI_RESET " (%3d/%3d) %.1f ms\033[K\n",
           b1, s->bench.single_done, s->bench.tasks, s->bench.single_ms);
    printf("  [Phase 2] %2d Threads : " UI_BGREEN "%s" UI_RESET " (%3d/%3d) %.1f ms\033[K\n\n",
           s->bench.workers, b2, s->bench.multi_done, s->bench.tasks, s->bench.multi_ms);

    if (s->bench.phase == 3) {
        printf("  " UI_BGREEN UI_BOLD "[RESULT] Speedup=%.2fx | Efficiency=%.1f%% | Checksum %s\033[K\n" UI_RESET,
               s->bench.speedup, s->bench.efficiency_pct,
               s->bench.checksum_match ? "MATCH" : "DIFF");
    }
    ui_draw_tracer(&ctx->tracer, 6);
    ui_clear_below();
}

static void render_barrier_frame(LabContext *ctx, const LabSnapshot *s) {
    ui_home();
    ui_header("Multi-Stage Barrier Pipeline (pthread_barrier_t)");
    printf("  Phase: %d / 4 | Final Checksum: 0x%llx\033[K\n\n",
           s->barrier.phase, (unsigned long long)s->barrier.final_checksum);
    for (int i = 0; i < s->barrier.workers; i++) {
        printf("  StageThread-%d : Stage=%d | Barriers passed=%d/3\033[K\n",
               i + 1, s->barrier.worker_stage[i], s->barrier.barrier_hits[i]);
    }
    ui_draw_tracer(&ctx->tracer, 6);
    ui_clear_below();
}

static void render_race_frame(LabContext *ctx, const LabSnapshot *s) {
    ui_home();
    ui_header("OS Synchronization Profiler (getrusage + Context Switches)");
    printf("  Config: %d threads x %d loops (Expected=%lld)\033[K\n\n",
           s->race.threads, s->race.loops,
           (long long)s->race.threads * (long long)s->race.loops);
    printf("  %-21s %-11s %-9s %-8s %-9s %-7s\033[K\n",
           "Mechanism", "Actual", "Lost", "Time", "Usr/Sys", "nvcsw");
    printf("  --------------------------------------------------------------------\033[K\n");
    for (int i = 0; i < 4; i++) {
        const RaceMethodStat *r = &s->race.items[i];
        const char *col = r->done ? (r->lost == 0 ? UI_BGREEN : UI_BRED) : UI_BYELLOW;
        printf("  %-21s %s%-11lld %-9lld" UI_RESET " %6.1fms %4.0f/%-4.0f %-7ld\033[K\n",
               r->name[0] ? r->name : "-", col,
               r->done ? r->actual : 0LL, r->done ? r->lost : 0LL,
               r->elapsed_ms, r->user_ms, r->sys_ms, r->nvcsw);
    }
    ui_draw_tracer(&ctx->tracer, 5);
    ui_clear_below();
}

static void render_cache_frame(LabContext *ctx, const LabSnapshot *s) {
    ui_home();
    ui_header("Concurrent KV Cache Contention (Mutex vs RWLock vs Lock Striping)");
    printf("  Threads: %d | Ops/Thread: %d | Read%%: %d%%\033[K\n\n",
           s->cache.threads, s->cache.ops_per_thread, s->cache.read_pct);
    printf("  %-26s %-14s %-11s %-8s\033[K\n", "Lock Strategy", "Throughput", "Elapsed", "nvcsw");
    printf("  ----------------------------------------------------------------\033[K\n");
    for (int i = 0; i < 3; i++) {
        const CacheMethodStat *c = &s->cache.items[i];
        printf("  %-26s " UI_BGREEN "%6.2f M ops/s" UI_RESET "  %8.1f ms  %-8ld\033[K\n",
               c->name[0] ? c->name : "-", c->mops_per_sec, c->elapsed_ms, c->nvcsw);
    }
    ui_draw_tracer(&ctx->tracer, 5);
    ui_clear_below();
}

static void render_pc_frame(LabContext *ctx, const LabSnapshot *s) {
    ui_home();
    ui_header("Log Stream Pipeline (/var/log/dpkg.log + Semaphore + Mutex)");
    printf("  Produced: %d/%d  Consumed: %d/%d  sem_empty=%d  sem_full=%d\033[K\n",
           s->pc.produced, s->pc.total_items, s->pc.consumed, s->pc.total_items,
           s->pc.sem_empty_val, s->pc.sem_full_val);
    printf("  Parsed events: install=%d  configure=%d  status=%d  other=%d\033[K\n\n",
           s->pc.cnt_install, s->pc.cnt_configure, s->pc.cnt_status, s->pc.cnt_other);
    printf("  Buffer Slots (%d): ", s->pc.buf_cap);
    for (int i = 0; i < s->pc.buf_cap && i < LAB_MAX_PC_SLOTS; i++) {
        if (s->pc.slots[i] > 0) printf(UI_BCYAN "[#%02d]" UI_RESET, s->pc.slots[i]);
        else printf(UI_DIM "[ .. ]" UI_RESET);
    }
    printf("\033[K\n");
    ui_draw_tracer(&ctx->tracer, 6);
    ui_clear_below();
}

static void render_rw_frame(LabContext *ctx, const LabSnapshot *s) {
    ui_home();
    ui_header("Readers-Writers (pthread_rwlock_t)");
    printf("  Shared Data: v%d=%d | Active R:%d W:%d | Total R:%d W:%d\033[K\n\n",
           s->rw.shared_version, s->rw.shared_value, s->rw.active_readers,
           s->rw.active_writers, s->rw.total_reads, s->rw.total_writes);
    const char *rs[] = {"IDLE", "WAIT", "READ", "DONE"};
    const char *ws[] = {"IDLE", "WAIT", "WRITE", "DONE"};
    for (int i = 0; i < s->rw.readers; i++)
        printf("  Reader #%d: %-5s (reads=%d)\033[K\n", i + 1, rs[s->rw.reader_state[i]], s->rw.reader_reads[i]);
    for (int i = 0; i < s->rw.writers; i++)
        printf("  Writer #%d: %-5s (writes=%d)\033[K\n", i + 1, ws[s->rw.writer_state[i]], s->rw.writer_writes[i]);
    ui_draw_tracer(&ctx->tracer, 6);
    ui_clear_below();
}

static void render_deadlock_frame(LabContext *ctx, const LabSnapshot *s) {
    ui_home();
    ui_header("Deadlock Detection (pthread_mutex_timedlock) vs Ordered Lock");
    printf("  [Phase 1] AB-BA + timedlock(90ms): timeouts=%d  recovered=%d  time=%.1f ms\033[K\n",
           s->deadlock.timedlock_timeouts, s->deadlock.timedlock_done, s->deadlock.timedlock_ms);
    printf("  [Phase 2] Canonical Order (M1->M2): timeouts=%d  committed=%d  time=%.1f ms\033[K\n",
           s->deadlock.ordered_timeouts, s->deadlock.ordered_done, s->deadlock.ordered_ms);
    ui_draw_tracer(&ctx->tracer, 7);
    ui_clear_below();
}

static void render_phil_frame(LabContext *ctx, const LabSnapshot *s) {
    const char *st[] = {"THINKING", "HUNGRY  ", "EATING  ", "DONE    "};
    ui_home();
    ui_header("Dining Philosophers (Canonical Order + Butler Semaphore)");
    printf("  Total meals: %d / %d\033[K\n\n", s->phil.total_meals, s->phil.target_meals * LAB_NUM_PHIL);
    for (int i = 0; i < LAB_NUM_PHIL; i++) {
        printf("  Philosopher %d: %s  Meals: %d/%d\033[K\n",
               i + 1, st[s->phil.state[i]], s->phil.meals[i], s->phil.target_meals);
    }
    ui_draw_tracer(&ctx->tracer, 6);
    ui_clear_below();
}

static void monitor_tui_until_done(LabContext *ctx, void (*render_fn)(LabContext *, const LabSnapshot *), int interactive) {
    ui_clear();
    ui_hide_cursor();
    while (1) {
        LabSnapshot s;
        lab_snapshot(ctx, &s);
        render_fn(ctx, &s);
        if (!s.running) break;
        sleep_ms(80);
    }
    ui_show_cursor();
    printf("\n  " UI_BGREEN UI_BOLD "[DONE] Hoan tat mo phong!\033[K\n" UI_RESET);
    if (interactive) ui_wait_enter();
}

static void run_basic_demo(const char *binary, const char *title) {
    ui_clear();
    ui_header(title);
    FILE *chk = fopen(binary + 2, "r");
    if (!chk) {
        printf("  " UI_BRED "[!] Khong tim thay '%s'. Hay chay 'make' truoc.\n" UI_RESET, binary);
        ui_wait_enter();
        return;
    }
    fclose(chk);
    int rc = system(binary);
    if (rc != 0) printf(UI_BRED "\n  [!] Exit code %d\n" UI_RESET, rc);
    ui_wait_enter();
}

static void show_arch_and_tests_menu(void) {
    while (1) {
        ui_clear();
        ui_header("Kien truc he thong & Standalone Test Demos");
        printf("  Main/Scanner ---> [ Task Queue (Mutex + 2 CondVars) ] ---> Workers 1..N\n");
        printf("  OS Metrics   ---> [ getrusage + SyncTracer RingBuf  ] ---> TUI / Web :8080\n\n");
        printf("  [1] thread_basic   [2] race_condition   [3] mutex_demo\n");
        printf("  [4] condition_demo [5] semaphore_demo   [0] Quay lai\n");
        int c = ui_read_int("\n  Lua chon", 0, 0, 5);
        if (c == 0) break;
        const char *bins[] = {"", "./bin/thread_basic", "./bin/race_condition",
                              "./bin/mutex_demo", "./bin/condition_demo", "./bin/semaphore_demo"};
        run_basic_demo(bins[c], bins[c]);
    }
}

static int interactive_menu(LabContext *ctx) {
    while (1) {
        ui_clear();
        ui_header("Do an Lap trinh he thong - Threading & Synchronization");
        ui_print_menu();
        int choice = ui_read_int("\n  Lua chon", 1, 0, 7);

        switch (choice) {
            case 1: {
                ui_clear();
                ui_header("Cau hinh Thread Pool / POSIX File Scanner");
                int w = ui_read_int("  So worker thread", 4, 1, 16);
                int wt = ui_read_int("  Workload (0=SimIO, 1=CPU Prime, 2=CPU Hash, 3=POSIX File Scan)", 3, 0, 3);
                int max_t = (wt == 3) ? 600 : 100;
                int def_t = (wt == 3) ? 200 : 12;
                int t = ui_read_int("  So task / file toi da", def_t, 1, max_t);
                int q = ui_read_int("  Queue capacity", 16, 2, 128);
                TaskType tt = wt == 0 ? TASK_SIM_IO : (wt == 1 ? TASK_CPU_PRIME : (wt == 2 ? TASK_CPU_HASH : TASK_FS_SCAN));
                if (lab_start_pool(ctx, w, t, tt, q, "/usr/include", "define") == 0) {
                    monitor_tui_until_done(ctx, render_pool_frame, 1);
                }
                break;
            }
            case 2: {
                ui_clear();
                ui_header("Benchmark & Barrier");
                printf("  [1] Benchmark 1-Thread vs Multi-Thread (POSIX File Scan / CPU Prime)\n");
                printf("  [2] 3-Stage Parallel Pipeline (pthread_barrier_t)\n");
                int sub = ui_read_int("  Chon", 1, 1, 2);
                if (sub == 1) {
                    int k = ui_read_int("  Loai test (0=CPU Prime, 1=POSIX File Scan /usr/include)", 1, 0, 1);
                    int w = ui_read_int("  So worker thread", 4, 2, 16);
                    int t = ui_read_int("  So file/tasks", k ? 300 : 16, 4, k ? 600 : 48);
                    if (lab_start_bench(ctx, w, t, k) == 0) monitor_tui_until_done(ctx, render_bench_frame, 1);
                } else {
                    int w = ui_read_int("  So stage threads", 4, 2, 8);
                    if (lab_start_barrier(ctx, w) == 0) monitor_tui_until_done(ctx, render_barrier_frame, 1);
                }
                break;
            }
            case 3: {
                ui_clear();
                ui_header("OS Sync Profiler & Concurrent KV Cache");
                printf("  [1] Race vs Mutex vs Spinlock vs C11 Atomic (co getrusage)\n");
                printf("  [2] Concurrent KV Cache (Global Mutex vs RWLock vs 16-Bucket Striping)\n");
                int sub = ui_read_int("  Chon", 1, 1, 2);
                if (sub == 1) {
                    int th = ui_read_int("  So thread", 4, 2, 16);
                    int lp = ui_read_int("  So vong lap / thread", 1500000, 50000, 5000000);
                    if (lab_start_race(ctx, th, lp) == 0) monitor_tui_until_done(ctx, render_race_frame, 1);
                } else {
                    int th = ui_read_int("  So thread", 4, 2, 16);
                    int ops = ui_read_int("  Ops / thread", 250000, 20000, 1000000);
                    int rp = ui_read_int("  Ty le Read (%)", 85, 10, 99);
                    if (lab_start_cache(ctx, th, ops, rp) == 0) monitor_tui_until_done(ctx, render_cache_frame, 1);
                }
                break;
            }
            case 4: {
                ui_clear();
                ui_header("Log Stream Pipeline (/var/log/dpkg.log + Semaphore)");
                int p = ui_read_int("  Producers", 2, 1, 8);
                int c = ui_read_int("  Consumers", 3, 1, 8);
                int b = ui_read_int("  Buffer size", 8, 2, 16);
                int n = ui_read_int("  So dong log", 24, 4, 60);
                if (lab_start_pc(ctx, p, c, b, n) == 0) monitor_tui_until_done(ctx, render_pc_frame, 1);
                break;
            }
            case 5: {
                ui_clear();
                ui_header("RWLock, Deadlock Recovery & Dining Philosophers");
                printf("  [1] Readers-Writers (pthread_rwlock_t)\n");
                printf("  [2] AB-BA Deadlock Recovery (pthread_mutex_timedlock) vs Ordered Lock\n");
                printf("  [3] Dining Philosophers (Ordered Forks + Butler Semaphore)\n");
                int sub = ui_read_int("  Chon", 2, 1, 3);
                if (sub == 1) {
                    int r = ui_read_int("  Readers", 4, 1, 8);
                    int w = ui_read_int("  Writers", 2, 1, 4);
                    int rnd = ui_read_int("  Rounds", 5, 2, 12);
                    if (lab_start_rw(ctx, r, w, rnd) == 0) monitor_tui_until_done(ctx, render_rw_frame, 1);
                } else if (sub == 2) {
                    int rnd = ui_read_int("  So giao dich / thread", 5, 2, 15);
                    if (lab_start_deadlock(ctx, rnd) == 0) monitor_tui_until_done(ctx, render_deadlock_frame, 1);
                } else {
                    int m = ui_read_int("  So bua an", 4, 1, 10);
                    if (lab_start_phil(ctx, m) == 0) monitor_tui_until_done(ctx, render_phil_frame, 1);
                }
                break;
            }
            case 6:
                web_server_run(ctx, 8080);
                break;
            case 7:
                show_arch_and_tests_menu();
                break;
            case 0:
                ui_clear();
                ui_header("Ket thuc chuong trinh");
                printf("  " UI_BGREEN "Tam biet! Chuc ban bao ve do an thanh cong.\n" UI_RESET);
                return 0;
        }
    }
}

int main(int argc, char *argv[]) {
    LabContext ctx;
    if (lab_init(&ctx) != 0) {
        fprintf(stderr, "Failed to initialize LabContext\n");
        return 1;
    }

    int rc = 0;
    if (argc == 1) {
        rc = interactive_menu(&ctx);
    } else if (argc >= 2 && strcmp(argv[1], "--web") == 0) {
        int port = (argc >= 3) ? atoi(argv[2]) : 8080;
        if (port <= 0 || port > 65535) port = 8080;
        rc = web_server_run(&ctx, port);
    } else if (argc == 3) {
        int w = atoi(argv[1]);
        int t = atoi(argv[2]);
        if (w < 1 || w > 16 || t < 1 || t > 600) {
            fprintf(stderr, "Cach dung: %s <1-16 workers> <1-600 files/tasks>\n", argv[0]);
            rc = 1;
        } else if (lab_start_pool(&ctx, w, t, TASK_FS_SCAN, 16, "/usr/include", "define") == 0) {
            monitor_tui_until_done(&ctx, render_pool_frame, 0);
        }
    } else {
        fprintf(stderr, "Cach dung:\n"
                        "  %s              # menu tuong tac TUI\n"
                        "  %s --web [8080] # chay Web Control Center\n"
                        "  %s 4 200        # quet nhanh 200 file trong /usr/include\n",
                argv[0], argv[0], argv[0]);
        rc = 1;
    }

    lab_destroy(&ctx);
    return rc;
}
