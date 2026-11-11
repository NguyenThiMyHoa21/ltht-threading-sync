#ifndef SYNC_TRACER_H
#define SYNC_TRACER_H

#include <pthread.h>
#include <stdint.h>

#define SYNC_TRACE_CAP 128

typedef enum {
    EV_POOL = 0,
    EV_QUEUE,
    EV_MUTEX,
    EV_COND,
    EV_SEM,
    EV_RWLOCK,
    EV_ATOMIC
} SyncEventType;

typedef struct {
    uint64_t seq;
    double elapsed_ms;
    long tid;
    SyncEventType type;
    char actor[24];
    char detail[96];
} SyncEvent;

typedef struct {
    SyncEvent ring[SYNC_TRACE_CAP];
    int head;
    int count;
    uint64_t total_events;
    uint64_t start_ns;
    pthread_mutex_t mutex;
} SyncTracer;

int sync_tracer_init(SyncTracer *tracer);
void sync_tracer_reset(SyncTracer *tracer);
void sync_tracer_destroy(SyncTracer *tracer);
void sync_tracer_log(SyncTracer *tracer, SyncEventType type, const char *actor, const char *fmt, ...);
int sync_tracer_recent(SyncTracer *tracer, SyncEvent *out, int max_out, uint64_t *out_total);
const char *sync_event_type_name(SyncEventType type);

#endif
