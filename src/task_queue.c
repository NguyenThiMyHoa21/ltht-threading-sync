#include <string.h>
#include "task_queue.h"

int task_queue_init(TaskQueue *queue) {
    return task_queue_init_ex(queue, QUEUE_CAPACITY, NULL);
}

int task_queue_init_ex(TaskQueue *queue, int capacity, SyncTracer *tracer) {
    if (!queue) return -1;
    memset(queue, 0, sizeof(*queue));
    if (capacity < 1) capacity = 1;
    if (capacity > QUEUE_CAPACITY) capacity = QUEUE_CAPACITY;
    queue->capacity = capacity;
    queue->tracer = tracer;

    if (pthread_mutex_init(&queue->mutex, NULL) != 0) return -1;
    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        return -1;
    }
    if (pthread_cond_init(&queue->not_full, NULL) != 0) {
        pthread_cond_destroy(&queue->not_empty);
        pthread_mutex_destroy(&queue->mutex);
        return -1;
    }
    return 0;
}

void task_queue_destroy(TaskQueue *queue) {
    if (!queue) return;
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
}

int task_queue_push(TaskQueue *queue, Task task) {
    pthread_mutex_lock(&queue->mutex);

    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    while (queue->count == queue->capacity && !queue->shutdown) {
        sync_tracer_log(queue->tracer, EV_COND, "Dispatcher",
                        "Queue full (%d/%d) -> cond_wait(not_full)",
                        queue->count, queue->capacity);
        pthread_cond_wait(&queue->not_full, &queue->mutex);
    }

    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    queue->items[queue->rear] = task;
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->count++;
    queue->total_pushed++;

    sync_tracer_log(queue->tracer, EV_QUEUE, "Dispatcher",
                    "Push Task #%d (q=%d/%d) + signal(not_empty)",
                    task.id, queue->count, queue->capacity);

    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    return 0;
}

int task_queue_pop(TaskQueue *queue, Task *task, int *shutdown_flag, const char *actor_name) {
    const char *actor = actor_name ? actor_name : "Worker";
    pthread_mutex_lock(&queue->mutex);

    while (queue->count == 0 && !(*shutdown_flag)) {
        sync_tracer_log(queue->tracer, EV_COND, actor,
                        "Queue empty -> cond_wait(not_empty)");
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }

    if (queue->count == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }

    *task = queue->items[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    queue->count--;
    queue->total_popped++;

    sync_tracer_log(queue->tracer, EV_MUTEX, actor,
                    "Pop Task #%d (q=%d/%d) + signal(not_full)",
                    task->id, queue->count, queue->capacity);

    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    return 1;
}

int task_queue_snapshot(TaskQueue *queue, int *out_ids, int max_ids, int *out_count, int *out_cap) {
    if (!queue) return -1;
    pthread_mutex_lock(&queue->mutex);
    int cnt = queue->count;
    int cap = queue->capacity;
    int n = (out_ids && max_ids > 0) ? (cnt < max_ids ? cnt : max_ids) : 0;
    for (int i = 0; i < n; i++) {
        int idx = (queue->front + i) % cap;
        out_ids[i] = queue->items[idx].id;
    }
    if (out_count) *out_count = cnt;
    if (out_cap) *out_cap = cap;
    pthread_mutex_unlock(&queue->mutex);
    return n;
}
