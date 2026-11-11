#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "thread_pool.h"

static uint64_t execute_task_work(const Task *task, FileScanStats *fs_out) {
    if (fs_out) memset(fs_out, 0, sizeof(*fs_out));
    switch (task->type) {
        case TASK_FS_SCAN: {
            FileScanStats fs;
            if (sys_scan_file(task->path, task->pattern, &fs) == 0) {
                if (fs_out) *fs_out = fs;
                return fs.crc32;
            }
            return 0;
        }
        case TASK_CPU_PRIME:
            return workload_count_primes(task->workload_param > 0 ? task->workload_param : 360000);
        case TASK_CPU_HASH:
            return workload_hash_rounds(task->id, task->workload_param > 0 ? task->workload_param : 32000000);
        case TASK_SIM_IO:
        default:
            sleep_ms(task->duration_ms);
            return (uint64_t)task->duration_ms;
    }
}

static const char *path_basename_short(const char *path) {
    if (!path || !*path) return "-";
    const char *slash = strrchr(path, '/');
    return slash ? (slash + 1) : path;
}

static void set_worker_state(ThreadPool *pool, int index,
                             WorkerStatus status, int task_id,
                             TaskType type, const char *label,
                             double add_busy_ms, uint64_t result,
                             int increment_completed) {
    pthread_mutex_lock(&pool->state_mutex);
    pool->worker_states[index].status = status;
    pool->worker_states[index].current_task = task_id;
    pool->worker_states[index].current_type = type;
    snprintf(pool->worker_states[index].current_label,
             sizeof(pool->worker_states[index].current_label),
             "%s", label ? label : "-");
    pool->worker_states[index].busy_ms += add_busy_ms;
    if (increment_completed) {
        pool->worker_states[index].completed_tasks++;
        pool->worker_states[index].last_result = result;
    }
    pthread_mutex_unlock(&pool->state_mutex);
}

static void *worker_main(void *arg) {
    WorkerArg *worker = (WorkerArg *)arg;
    ThreadPool *pool = worker->pool;
    int index = worker->worker_id - 1;
    char actor[24];
    snprintf(actor, sizeof(actor), "Worker-%02d", worker->worker_id);

    pthread_mutex_lock(&pool->state_mutex);
    pool->worker_states[index].tid = current_tid();
    pool->worker_states[index].status = WORKER_WAITING;
    snprintf(pool->worker_states[index].current_label, 48, "-");
    pthread_mutex_unlock(&pool->state_mutex);

    sync_tracer_log(pool->tracer, EV_POOL, actor, "Started (tid=%ld)", current_tid());

    while (1) {
        Task task;
        int got_task = task_queue_pop(&pool->queue, &task, &pool->shutdown, actor);
        if (!got_task) break;

        pthread_mutex_lock(&pool->queue.mutex);
        int aborted = pool->force_abort;
        pthread_mutex_unlock(&pool->queue.mutex);
        if (aborted) break;

        char lbl[48];
        if (task.type == TASK_FS_SCAN) {
            snprintf(lbl, sizeof(lbl), "%.40s", path_basename_short(task.path));
        } else {
            snprintf(lbl, sizeof(lbl), "Task #%d", task.id);
        }

        set_worker_state(pool, index, WORKER_RUNNING, task.id, task.type, lbl, 0.0, 0, 0);
        uint64_t t0 = time_now_ns();
        FileScanStats fs;
        uint64_t res = execute_task_work(&task, &fs);
        double elapsed = time_elapsed_ms(t0);

        pthread_mutex_lock(&pool->completed_mutex);
        pool->completed_tasks++;
        if (task.type == TASK_FS_SCAN) {
            pool->total_bytes += fs.bytes;
            pool->total_lines += fs.lines;
            pool->total_hits += fs.hits;
            /* Commutative XOR fold guarantees deterministic checksum regardless of thread order */
            pool->combined_crc ^= (fs.crc32 ^ ((uint32_t)task.id * 2654435761U));
        }
        pthread_mutex_unlock(&pool->completed_mutex);

        if (task.type == TASK_FS_SCAN) {
            sync_tracer_log(pool->tracer, EV_POOL, actor,
                            "Scanned %.22s (%lluB, hits=%llu, crc=%08x)",
                            lbl, (unsigned long long)fs.bytes,
                            (unsigned long long)fs.hits, fs.crc32);
        } else {
            sync_tracer_log(pool->tracer, EV_POOL, actor,
                            "Done Task #%d in %.1f ms (res=%llu)",
                            task.id, elapsed, (unsigned long long)res);
        }

        set_worker_state(pool, index, WORKER_WAITING, 0, TASK_SIM_IO, "-", elapsed, res, 1);
    }

    set_worker_state(pool, index, WORKER_STOPPED, 0, TASK_SIM_IO, "-", 0.0, 0, 0);
    sync_tracer_log(pool->tracer, EV_POOL, actor, "Stopped cleanly");
    return NULL;
}

int thread_pool_init(ThreadPool *pool, int thread_count) {
    return thread_pool_init_ex(pool, thread_count, QUEUE_CAPACITY, NULL);
}

int thread_pool_init_ex(ThreadPool *pool, int thread_count, int queue_cap, SyncTracer *tracer) {
    if (!pool || thread_count <= 0) return -1;
    memset(pool, 0, sizeof(*pool));

    pool->thread_count = thread_count;
    pool->tracer = tracer;
    pool->start_ns = time_now_ns();

    if (task_queue_init_ex(&pool->queue, queue_cap, tracer) != 0) return -1;
    if (pthread_mutex_init(&pool->completed_mutex, NULL) != 0) {
        task_queue_destroy(&pool->queue);
        return -1;
    }
    if (pthread_mutex_init(&pool->state_mutex, NULL) != 0) {
        pthread_mutex_destroy(&pool->completed_mutex);
        task_queue_destroy(&pool->queue);
        return -1;
    }

    pool->threads = calloc((size_t)thread_count, sizeof(pthread_t));
    pool->worker_args = calloc((size_t)thread_count, sizeof(WorkerArg));
    pool->worker_states = calloc((size_t)thread_count, sizeof(WorkerState));
    if (!pool->threads || !pool->worker_args || !pool->worker_states) {
        free(pool->threads);
        free(pool->worker_args);
        free(pool->worker_states);
        pthread_mutex_destroy(&pool->state_mutex);
        pthread_mutex_destroy(&pool->completed_mutex);
        task_queue_destroy(&pool->queue);
        return -1;
    }

    for (int i = 0; i < thread_count; i++) {
        pool->worker_states[i].status = WORKER_WAITING;
        pool->worker_args[i].pool = pool;
        pool->worker_args[i].worker_id = i + 1;
        if (pthread_create(&pool->threads[i], NULL, worker_main, &pool->worker_args[i]) != 0) {
            thread_pool_force_abort(pool);
            for (int j = 0; j < i; j++) pthread_join(pool->threads[j], NULL);
            thread_pool_destroy(pool);
            return -1;
        }
    }
    return 0;
}

int thread_pool_submit(ThreadPool *pool, Task task) {
    if (!pool || pool->shutdown) return -1;
    int rc = task_queue_push(&pool->queue, task);
    if (rc == 0) {
        pthread_mutex_lock(&pool->completed_mutex);
        pool->submitted_tasks++;
        pthread_mutex_unlock(&pool->completed_mutex);
    }
    return rc;
}

void thread_pool_request_shutdown(ThreadPool *pool) {
    if (!pool) return;
    pthread_mutex_lock(&pool->queue.mutex);
    pool->shutdown = 1;
    pool->queue.shutdown = 1;
    pthread_cond_broadcast(&pool->queue.not_empty);
    pthread_cond_broadcast(&pool->queue.not_full);
    pthread_mutex_unlock(&pool->queue.mutex);
}

void thread_pool_force_abort(ThreadPool *pool) {
    if (!pool) return;
    pthread_mutex_lock(&pool->queue.mutex);
    pool->force_abort = 1;
    pool->shutdown = 1;
    pool->queue.shutdown = 1;
    pool->queue.count = 0;
    pool->queue.front = 0;
    pool->queue.rear = 0;
    pthread_cond_broadcast(&pool->queue.not_empty);
    pthread_cond_broadcast(&pool->queue.not_full);
    pthread_mutex_unlock(&pool->queue.mutex);
}

void thread_pool_join(ThreadPool *pool) {
    if (!pool) return;
    for (int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    pool->end_ns = time_now_ns();
}

void thread_pool_shutdown(ThreadPool *pool) {
    thread_pool_request_shutdown(pool);
    thread_pool_join(pool);
}

void thread_pool_destroy(ThreadPool *pool) {
    if (!pool) return;
    free(pool->threads);
    free(pool->worker_args);
    free(pool->worker_states);
    pool->threads = NULL;
    pool->worker_args = NULL;
    pool->worker_states = NULL;
    pthread_mutex_destroy(&pool->state_mutex);
    pthread_mutex_destroy(&pool->completed_mutex);
    task_queue_destroy(&pool->queue);
}

int thread_pool_completed_tasks(ThreadPool *pool) {
    if (!pool) return 0;
    pthread_mutex_lock(&pool->completed_mutex);
    int value = pool->completed_tasks;
    pthread_mutex_unlock(&pool->completed_mutex);
    return value;
}

int thread_pool_queue_count(ThreadPool *pool) {
    if (!pool) return 0;
    pthread_mutex_lock(&pool->queue.mutex);
    int count = pool->queue.count;
    pthread_mutex_unlock(&pool->queue.mutex);
    return count;
}

void thread_pool_worker_snapshot(ThreadPool *pool, int index, WorkerState *state) {
    if (!pool || !state || index < 0 || index >= pool->thread_count) return;
    pthread_mutex_lock(&pool->state_mutex);
    *state = pool->worker_states[index];
    pthread_mutex_unlock(&pool->state_mutex);
}

void thread_pool_totals_snapshot(ThreadPool *pool, uint64_t *bytes, uint64_t *lines, uint64_t *hits, uint32_t *crc) {
    if (!pool) return;
    pthread_mutex_lock(&pool->completed_mutex);
    if (bytes) *bytes = pool->total_bytes;
    if (lines) *lines = pool->total_lines;
    if (hits) *hits = pool->total_hits;
    if (crc) *crc = pool->combined_crc;
    pthread_mutex_unlock(&pool->completed_mutex);
}
