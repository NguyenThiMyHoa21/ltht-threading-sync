#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include <stdint.h>
#include "sync_tracer.h"
#include "task_queue.h"

typedef struct ThreadPool ThreadPool;

typedef enum {
    WORKER_WAITING = 0,
    WORKER_RUNNING = 1,
    WORKER_STOPPED = 2
} WorkerStatus;

typedef struct {
    ThreadPool *pool;
    int worker_id;
} WorkerArg;

typedef struct {
    WorkerStatus status;
    long tid;
    int current_task;
    TaskType current_type;
    int completed_tasks;
    double busy_ms;
    uint64_t last_result;
    char current_label[48];
} WorkerState;

struct ThreadPool {
    pthread_t *threads;
    WorkerArg *worker_args;
    WorkerState *worker_states;
    int thread_count;
    int shutdown;
    int force_abort;
    int submitted_tasks;
    int completed_tasks;
    uint64_t total_bytes;
    uint64_t total_lines;
    uint64_t total_hits;
    uint32_t combined_crc;
    uint64_t start_ns;
    uint64_t end_ns;
    pthread_mutex_t completed_mutex;
    pthread_mutex_t state_mutex;
    TaskQueue queue;
    SyncTracer *tracer;
};

int thread_pool_init(ThreadPool *pool, int thread_count);
int thread_pool_init_ex(ThreadPool *pool, int thread_count, int queue_cap, SyncTracer *tracer);
int thread_pool_submit(ThreadPool *pool, Task task);
void thread_pool_request_shutdown(ThreadPool *pool);
void thread_pool_force_abort(ThreadPool *pool);
void thread_pool_join(ThreadPool *pool);
void thread_pool_shutdown(ThreadPool *pool);
void thread_pool_destroy(ThreadPool *pool);
int thread_pool_completed_tasks(ThreadPool *pool);
int thread_pool_queue_count(ThreadPool *pool);
void thread_pool_worker_snapshot(ThreadPool *pool, int index, WorkerState *state);
void thread_pool_totals_snapshot(ThreadPool *pool, uint64_t *bytes, uint64_t *lines, uint64_t *hits, uint32_t *crc);

#endif
