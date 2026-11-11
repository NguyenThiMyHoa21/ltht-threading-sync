#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "sync_tracer.h"

int sync_tracer_init(SyncTracer *tracer) {
    if (!tracer) return -1;
    memset(tracer, 0, sizeof(*tracer));
    tracer->start_ns = time_now_ns();
    return pthread_mutex_init(&tracer->mutex, NULL) == 0 ? 0 : -1;
}

void sync_tracer_reset(SyncTracer *tracer) {
    if (!tracer) return;
    pthread_mutex_lock(&tracer->mutex);
    tracer->head = 0;
    tracer->count = 0;
    tracer->total_events = 0;
    tracer->start_ns = time_now_ns();
    pthread_mutex_unlock(&tracer->mutex);
}

void sync_tracer_destroy(SyncTracer *tracer) {
    if (!tracer) return;
    pthread_mutex_destroy(&tracer->mutex);
}

void sync_tracer_log(SyncTracer *tracer, SyncEventType type, const char *actor, const char *fmt, ...) {
    if (!tracer) return;

    SyncEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.tid = current_tid();
    ev.type = type;
    snprintf(ev.actor, sizeof(ev.actor), "%s", actor ? actor : "system");

    va_list args;
    va_start(args, fmt);
    vsnprintf(ev.detail, sizeof(ev.detail), fmt ? fmt : "", args);
    va_end(args);

    pthread_mutex_lock(&tracer->mutex);
    ev.elapsed_ms = time_elapsed_ms(tracer->start_ns);
    tracer->total_events++;
    ev.seq = tracer->total_events;
    int idx = (tracer->head + tracer->count) % SYNC_TRACE_CAP;
    if (tracer->count == SYNC_TRACE_CAP) {
        tracer->ring[tracer->head] = ev;
        tracer->head = (tracer->head + 1) % SYNC_TRACE_CAP;
    } else {
        tracer->ring[idx] = ev;
        tracer->count++;
    }
    pthread_mutex_unlock(&tracer->mutex);
}

int sync_tracer_recent(SyncTracer *tracer, SyncEvent *out, int max_out, uint64_t *out_total) {
    if (!tracer || !out || max_out <= 0) return 0;
    pthread_mutex_lock(&tracer->mutex);
    int n = tracer->count < max_out ? tracer->count : max_out;
    int start = (tracer->head + tracer->count - n) % SYNC_TRACE_CAP;
    for (int i = 0; i < n; i++) {
        out[i] = tracer->ring[(start + i) % SYNC_TRACE_CAP];
    }
    if (out_total) *out_total = tracer->total_events;
    pthread_mutex_unlock(&tracer->mutex);
    return n;
}

const char *sync_event_type_name(SyncEventType type) {
    switch (type) {
        case EV_POOL:   return "POOL";
        case EV_QUEUE:  return "QUEUE";
        case EV_MUTEX:  return "MUTEX";
        case EV_COND:   return "COND";
        case EV_SEM:    return "SEM";
        case EV_RWLOCK: return "RWLOCK";
        case EV_ATOMIC: return "ATOMIC";
        default:        return "SYNC";
    }
}
