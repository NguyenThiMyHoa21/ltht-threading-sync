#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <pthread.h>
#include <stdint.h>
#include "sync_tracer.h"

#define QUEUE_CAPACITY 128

typedef enum {
    TASK_SIM_IO = 0,
    TASK_CPU_PRIME = 1,
    TASK_CPU_HASH = 2,
    TASK_FS_SCAN = 3
} TaskType;

typedef struct {
    int id;
    TaskType type;
    int duration_ms;
    int workload_param;
    char path[256];
    char pattern[32];
} Task;

typedef struct {
    Task items[QUEUE_CAPACITY];
    int front;
    int rear;
    int count;
    int capacity;
    int shutdown;
    int total_pushed;
    int total_popped;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    SyncTracer *tracer;
} TaskQueue;

int task_queue_init(TaskQueue *queue);
int task_queue_init_ex(TaskQueue *queue, int capacity, SyncTracer *tracer);
void task_queue_destroy(TaskQueue *queue);
int task_queue_push(TaskQueue *queue, Task task);
int task_queue_pop(TaskQueue *queue, Task *task, int *shutdown_flag, const char *actor_name);
int task_queue_snapshot(TaskQueue *queue, int *out_ids, int max_ids, int *out_count, int *out_cap);

#endif
