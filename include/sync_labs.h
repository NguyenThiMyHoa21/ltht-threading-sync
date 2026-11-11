#ifndef SYNC_LABS_H
#define SYNC_LABS_H

#include <pthread.h>
#include <stdint.h>
#include "common.h"
#include "sync_tracer.h"
#include "thread_pool.h"

#define LAB_MAX_WORKERS 16
#define LAB_MAX_QUEUE_VIS 24
#define LAB_MAX_PC_SLOTS 16
#define LAB_MAX_PC_ACTORS 8
#define LAB_MAX_READERS 8
#define LAB_MAX_WRITERS 4
#define LAB_NUM_PHIL 5
#define LAB_MAX_BARRIER_THREADS 8

typedef enum {
    LAB_IDLE = 0,
    LAB_POOL = 1,
    LAB_BENCH = 2,
    LAB_RACE = 3,
    LAB_PC = 4,
    LAB_RW = 5,
    LAB_PHIL = 6,
    LAB_BARRIER = 7,
    LAB_CACHE = 8,
    LAB_DEADLOCK = 9
} LabMode;

typedef struct {
    char name[28];
    int running;
    int done;
    long long expected;
    long long actual;
    long long lost;
    double elapsed_ms;
    double ns_per_op;
    double user_ms;
    double sys_ms;
    long nvcsw;
    long nivcsw;
} RaceMethodStat;

typedef struct {
    char name[28];
    int done;
    long long total_ops;
    double elapsed_ms;
    double mops_per_sec;
    long nvcsw;
    uint64_t checksum;
} CacheMethodStat;

typedef struct {
    LabMode mode;
    int running;
    double elapsed_ms;
    SysUsageSnap usage;

    /* 1. Thread Pool + Real POSIX File Scanner */
    struct {
        int workers;
        int total_tasks;
        int completed;
        int queued;
        int queue_cap;
        TaskType workload_type;
        int queue_ids[LAB_MAX_QUEUE_VIS];
        int queue_vis_len;
        WorkerState worker_states[LAB_MAX_WORKERS];
        double throughput;
        uint64_t total_bytes;
        uint64_t total_lines;
        uint64_t total_hits;
        uint32_t combined_crc;
        char scan_dir[64];
        char scan_pattern[32];
    } pool;

    /* 2. Single vs Multi-thread Benchmark (CPU Prime or Real File Scan) */
    struct {
        int workers;
        int tasks;
        int bench_kind; /* 0=CPU Prime, 1=POSIX File Scan */
        int phase;      /* 0=idle, 1=single, 2=multi, 3=done */
        int single_done;
        int multi_done;
        double single_ms;
        double multi_ms;
        double speedup;
        double efficiency_pct;
        uint64_t single_checksum;
        uint64_t multi_checksum;
        int checksum_match;
        SysUsageSnap single_usage;
        SysUsageSnap multi_usage;
    } bench;

    /* 3. Race vs Mutex vs Spinlock vs C11 Atomic + OS Usage */
    struct {
        int threads;
        int loops;
        RaceMethodStat items[4];
    } race;

    /* 4. Producer-Consumer (Semaphore + Mutex on /var/log/dpkg.log) */
    struct {
        int producers;
        int consumers;
        int buf_cap;
        int total_items;
        int real_log_mode;
        int slots[LAB_MAX_PC_SLOTS];
        int slot_tag[LAB_MAX_PC_SLOTS]; /* 1=install,2=config,3=status,4=other */
        int head;
        int tail;
        int count;
        int produced;
        int consumed;
        int sem_empty_val;
        int sem_full_val;
        int cnt_install;
        int cnt_configure;
        int cnt_status;
        int cnt_other;
        int prod_state[LAB_MAX_PC_ACTORS];
        int prod_item[LAB_MAX_PC_ACTORS];
        int cons_state[LAB_MAX_PC_ACTORS];
        int cons_item[LAB_MAX_PC_ACTORS];
    } pc;

    /* 5. Readers-Writers (pthread_rwlock_t) */
    struct {
        int readers;
        int writers;
        int rounds;
        int shared_version;
        int shared_value;
        int active_readers;
        int active_writers;
        int total_reads;
        int total_writes;
        int reader_state[LAB_MAX_READERS];
        int reader_reads[LAB_MAX_READERS];
        int writer_state[LAB_MAX_WRITERS];
        int writer_writes[LAB_MAX_WRITERS];
    } rw;

    /* 6. Dining Philosophers (TSan-clean Ordered Forks + Butler) */
    struct {
        int target_meals;
        int state[LAB_NUM_PHIL];
        int meals[LAB_NUM_PHIL];
        int fork_owner[LAB_NUM_PHIL];
        int total_meals;
    } phil;

    /* 7. Multi-Stage Barrier Pipeline (pthread_barrier_t) */
    struct {
        int workers;
        int phase; /* 0..3, 4=done */
        int worker_stage[LAB_MAX_BARRIER_THREADS];
        int barrier_hits[LAB_MAX_BARRIER_THREADS];
        uint64_t final_checksum;
    } barrier;

    /* 8. Concurrent Hash KV Cache (Global Mutex vs RWLock vs Striped Locks) */
    struct {
        int threads;
        int ops_per_thread;
        int read_pct;
        CacheMethodStat items[3];
    } cache;

    /* 9. Deadlock Detection (pthread_mutex_timedlock) vs Ordered Lock */
    struct {
        int rounds;
        int phase; /* 1=timedlock recovery, 2=ordered lock, 3=done */
        int timedlock_timeouts;
        int timedlock_done;
        double timedlock_ms;
        int ordered_timeouts;
        int ordered_done;
        double ordered_ms;
    } deadlock;
} LabSnapshot;

typedef struct {
    pthread_mutex_t lock;
    SyncTracer tracer;
    LabSnapshot snap;
    pthread_t runner;
    int has_runner;
    int stop_requested;
    uint64_t run_start_ns;
    SysUsageSnap run_start_usage;
    int run_params[4];
    char run_str1[128];
    char run_str2[32];
} LabContext;

int lab_init(LabContext *ctx);
void lab_destroy(LabContext *ctx);
void lab_stop(LabContext *ctx);
void lab_snapshot(LabContext *ctx, LabSnapshot *out);
int lab_is_running(LabContext *ctx);

int lab_start_pool(LabContext *ctx, int workers, int tasks, TaskType type, int queue_cap,
                   const char *scan_dir, const char *pattern);
int lab_start_bench(LabContext *ctx, int workers, int tasks, int bench_kind);
int lab_start_race(LabContext *ctx, int threads, int loops);
int lab_start_pc(LabContext *ctx, int producers, int consumers, int buf_cap, int total_items);
int lab_start_rw(LabContext *ctx, int readers, int writers, int rounds);
int lab_start_phil(LabContext *ctx, int target_meals);
int lab_start_barrier(LabContext *ctx, int workers);
int lab_start_cache(LabContext *ctx, int threads, int ops_per_thread, int read_pct);
int lab_start_deadlock(LabContext *ctx, int rounds);

#endif
