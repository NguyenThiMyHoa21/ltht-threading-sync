#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include "ui.h"
#include "web_assets.h"
#include "web_server.h"

static volatile sig_atomic_t g_web_stop = 0;

static void handle_sigint(int sig) {
    (void)sig;
    g_web_stop = 1;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} JsonBuf;

static int jb_ensure(JsonBuf *jb, size_t extra) {
    if (jb->len + extra + 1 <= jb->cap) return 0;
    size_t ncap = jb->cap ? jb->cap * 2 : 8192;
    while (ncap < jb->len + extra + 1) ncap *= 2;
    char *p = realloc(jb->data, ncap);
    if (!p) return -1;
    jb->data = p;
    jb->cap = ncap;
    return 0;
}

static void jb_fmt(JsonBuf *jb, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int need = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (need < 0 || jb_ensure(jb, (size_t)need) != 0) {
        va_end(args);
        return;
    }
    vsnprintf(jb->data + jb->len, jb->cap - jb->len, fmt, args);
    jb->len += (size_t)need;
    va_end(args);
}

static void jb_escaped_str(JsonBuf *jb, const char *s) {
    jb_fmt(jb, "\"");
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        if (*p == '"' || *p == '\\') jb_fmt(jb, "\\%c", *p);
        else if (*p < 0x20) jb_fmt(jb, " ");
        else jb_fmt(jb, "%c", *p);
    }
    jb_fmt(jb, "\"");
}

static char *build_state_json(LabContext *ctx, size_t *out_len) {
    LabSnapshot s;
    lab_snapshot(ctx, &s);

    SyncEvent evs[40];
    uint64_t total_ev = 0;
    int nev = sync_tracer_recent(&ctx->tracer, evs, 40, &total_ev);

    JsonBuf jb = {NULL, 0, 0};
    jb_fmt(&jb, "{\"mode\":%d,\"running\":%d,\"elapsed_ms\":%.2f,"
                "\"usage\":{\"user_ms\":%.2f,\"sys_ms\":%.2f,\"nvcsw\":%ld,\"nivcsw\":%ld},",
           (int)s.mode, s.running, s.elapsed_ms,
           s.usage.user_ms, s.usage.sys_ms, s.usage.nvcsw, s.usage.nivcsw);

    /* 1. pool */
    jb_fmt(&jb, "\"pool\":{\"workers\":%d,\"total_tasks\":%d,\"completed\":%d,"
                "\"queued\":%d,\"queue_cap\":%d,\"workload_type\":%d,\"throughput\":%.2f,"
                "\"total_bytes\":%llu,\"total_lines\":%llu,\"total_hits\":%llu,\"combined_crc\":%u,"
                "\"scan_dir\":",
           s.pool.workers, s.pool.total_tasks, s.pool.completed,
           s.pool.queued, s.pool.queue_cap, (int)s.pool.workload_type, s.pool.throughput,
           (unsigned long long)s.pool.total_bytes, (unsigned long long)s.pool.total_lines,
           (unsigned long long)s.pool.total_hits, s.pool.combined_crc);
    jb_escaped_str(&jb, s.pool.scan_dir);
    jb_fmt(&jb, ",\"scan_pattern\":");
    jb_escaped_str(&jb, s.pool.scan_pattern);
    jb_fmt(&jb, ",\"queue_ids\":[");
    for (int i = 0; i < s.pool.queue_vis_len; i++) {
        jb_fmt(&jb, "%s%d", i ? "," : "", s.pool.queue_ids[i]);
    }
    jb_fmt(&jb, "],\"worker_states\":[");
    for (int i = 0; i < s.pool.workers && i < LAB_MAX_WORKERS; i++) {
        const WorkerState *w = &s.pool.worker_states[i];
        jb_fmt(&jb, "%s{\"id\":%d,\"tid\":%ld,\"status\":%d,\"current_task\":%d,"
                    "\"completed_tasks\":%d,\"busy_ms\":%.2f,\"label\":",
               i ? "," : "", i + 1, w->tid, (int)w->status, w->current_task,
               w->completed_tasks, w->busy_ms);
        jb_escaped_str(&jb, w->current_label);
        jb_fmt(&jb, "}");
    }
    jb_fmt(&jb, "]},");

    /* 2. bench */
    jb_fmt(&jb, "\"bench\":{\"workers\":%d,\"tasks\":%d,\"kind\":%d,\"phase\":%d,"
                "\"single_done\":%d,\"multi_done\":%d,\"single_ms\":%.2f,"
                "\"multi_ms\":%.2f,\"speedup\":%.3f,\"efficiency_pct\":%.2f,"
                "\"single_chk\":%llu,\"multi_chk\":%llu,\"match\":%d,"
                "\"s_nvcsw\":%ld,\"m_nvcsw\":%ld,\"s_usr\":%.2f,\"m_usr\":%.2f},",
           s.bench.workers, s.bench.tasks, s.bench.bench_kind, s.bench.phase,
           s.bench.single_done, s.bench.multi_done, s.bench.single_ms,
           s.bench.multi_ms, s.bench.speedup, s.bench.efficiency_pct,
           (unsigned long long)s.bench.single_checksum,
           (unsigned long long)s.bench.multi_checksum, s.bench.checksum_match,
           s.bench.single_usage.nvcsw, s.bench.multi_usage.nvcsw,
           s.bench.single_usage.user_ms, s.bench.multi_usage.user_ms);

    /* 3. race */
    jb_fmt(&jb, "\"race\":{\"threads\":%d,\"loops\":%d,\"items\":[", s.race.threads, s.race.loops);
    int rcount = (s.mode == LAB_RACE) ? 4 : 0;
    for (int i = 0; i < rcount; i++) {
        const RaceMethodStat *r = &s.race.items[i];
        jb_fmt(&jb, "%s{\"name\":", i ? "," : "");
        jb_escaped_str(&jb, r->name);
        jb_fmt(&jb, ",\"running\":%d,\"done\":%d,\"expected\":%lld,\"actual\":%lld,"
                    "\"lost\":%lld,\"elapsed_ms\":%.3f,\"ns_per_op\":%.3f,"
                    "\"user_ms\":%.2f,\"sys_ms\":%.2f,\"nvcsw\":%ld,\"nivcsw\":%ld}",
               r->running, r->done, r->expected, r->actual, r->lost, r->elapsed_ms,
               r->ns_per_op, r->user_ms, r->sys_ms, r->nvcsw, r->nivcsw);
    }
    jb_fmt(&jb, "]},");

    /* 4. producer-consumer */
    jb_fmt(&jb, "\"pc\":{\"producers\":%d,\"consumers\":%d,\"buf_cap\":%d,\"total_items\":%d,"
                "\"real_log\":%d,\"produced\":%d,\"consumed\":%d,\"sem_empty\":%d,\"sem_full\":%d,"
                "\"cnt_install\":%d,\"cnt_configure\":%d,\"cnt_status\":%d,\"cnt_other\":%d,\"slots\":[",
           s.pc.producers, s.pc.consumers, s.pc.buf_cap, s.pc.total_items,
           s.pc.real_log_mode, s.pc.produced, s.pc.consumed, s.pc.sem_empty_val, s.pc.sem_full_val,
           s.pc.cnt_install, s.pc.cnt_configure, s.pc.cnt_status, s.pc.cnt_other);
    for (int i = 0; i < s.pc.buf_cap && i < LAB_MAX_PC_SLOTS; i++) {
        jb_fmt(&jb, "%s{\"id\":%d,\"tag\":%d}", i ? "," : "", s.pc.slots[i], s.pc.slot_tag[i]);
    }
    jb_fmt(&jb, "],\"prod\":[");
    for (int i = 0; i < s.pc.producers && i < LAB_MAX_PC_ACTORS; i++) {
        jb_fmt(&jb, "%s{\"id\":%d,\"state\":%d,\"item\":%d}",
               i ? "," : "", i + 1, s.pc.prod_state[i], s.pc.prod_item[i]);
    }
    jb_fmt(&jb, "],\"cons\":[");
    for (int i = 0; i < s.pc.consumers && i < LAB_MAX_PC_ACTORS; i++) {
        jb_fmt(&jb, "%s{\"id\":%d,\"state\":%d,\"item\":%d}",
               i ? "," : "", i + 1, s.pc.cons_state[i], s.pc.cons_item[i]);
    }
    jb_fmt(&jb, "]},");

    /* 5. rwlock */
    jb_fmt(&jb, "\"rw\":{\"readers\":%d,\"writers\":%d,\"version\":%d,\"value\":%d,"
                "\"active_readers\":%d,\"active_writers\":%d,\"total_reads\":%d,\"total_writes\":%d,"
                "\"reader_list\":[",
           s.rw.readers, s.rw.writers, s.rw.shared_version, s.rw.shared_value,
           s.rw.active_readers, s.rw.active_writers, s.rw.total_reads, s.rw.total_writes);
    for (int i = 0; i < s.rw.readers && i < LAB_MAX_READERS; i++) {
        jb_fmt(&jb, "%s{\"id\":%d,\"state\":%d,\"count\":%d}",
               i ? "," : "", i + 1, s.rw.reader_state[i], s.rw.reader_reads[i]);
    }
    jb_fmt(&jb, "],\"writer_list\":[");
    for (int i = 0; i < s.rw.writers && i < LAB_MAX_WRITERS; i++) {
        jb_fmt(&jb, "%s{\"id\":%d,\"state\":%d,\"count\":%d}",
               i ? "," : "", i + 1, s.rw.writer_state[i], s.rw.writer_writes[i]);
    }
    jb_fmt(&jb, "]},");

    /* 6. philosophers */
    jb_fmt(&jb, "\"phil\":{\"target_meals\":%d,\"total_meals\":%d,\"phils\":[",
           s.phil.target_meals, s.phil.total_meals);
    for (int i = 0; i < LAB_NUM_PHIL; i++) {
        jb_fmt(&jb, "%s{\"id\":%d,\"state\":%d,\"meals\":%d}",
               i ? "," : "", i + 1, s.phil.state[i], s.phil.meals[i]);
    }
    jb_fmt(&jb, "],\"forks\":[");
    for (int i = 0; i < LAB_NUM_PHIL; i++) {
        jb_fmt(&jb, "%s%d", i ? "," : "", s.phil.fork_owner[i]);
    }
    jb_fmt(&jb, "]},");

    /* 7. barrier */
    jb_fmt(&jb, "\"barrier\":{\"workers\":%d,\"phase\":%d,\"checksum\":%llu,\"list\":[",
           s.barrier.workers, s.barrier.phase, (unsigned long long)s.barrier.final_checksum);
    for (int i = 0; i < s.barrier.workers && i < LAB_MAX_BARRIER_THREADS; i++) {
        jb_fmt(&jb, "%s{\"id\":%d,\"stage\":%d,\"hits\":%d}",
               i ? "," : "", i + 1, s.barrier.worker_stage[i], s.barrier.barrier_hits[i]);
    }
    jb_fmt(&jb, "]},");

    /* 8. kv cache */
    jb_fmt(&jb, "\"cache\":{\"threads\":%d,\"ops\":%d,\"read_pct\":%d,\"items\":[",
           s.cache.threads, s.cache.ops_per_thread, s.cache.read_pct);
    int ccnt = (s.mode == LAB_CACHE) ? 3 : 0;
    for (int i = 0; i < ccnt; i++) {
        const CacheMethodStat *c = &s.cache.items[i];
        jb_fmt(&jb, "%s{\"name\":", i ? "," : "");
        jb_escaped_str(&jb, c->name);
        jb_fmt(&jb, ",\"done\":%d,\"ops\":%lld,\"elapsed_ms\":%.2f,\"mops\":%.3f,\"nvcsw\":%ld}",
               c->done, c->total_ops, c->elapsed_ms, c->mops_per_sec, c->nvcsw);
    }
    jb_fmt(&jb, "]},");

    /* 9. deadlock */
    jb_fmt(&jb, "\"deadlock\":{\"rounds\":%d,\"phase\":%d,\"t_timeouts\":%d,\"t_done\":%d,"
                "\"t_ms\":%.2f,\"o_timeouts\":%d,\"o_done\":%d,\"o_ms\":%.2f},",
           s.deadlock.rounds, s.deadlock.phase, s.deadlock.timedlock_timeouts,
           s.deadlock.timedlock_done, s.deadlock.timedlock_ms,
           s.deadlock.ordered_timeouts, s.deadlock.ordered_done, s.deadlock.ordered_ms);

    /* events */
    jb_fmt(&jb, "\"total_events\":%llu,\"events\":[", (unsigned long long)total_ev);
    for (int i = 0; i < nev; i++) {
        const SyncEvent *e = &evs[i];
        jb_fmt(&jb, "%s{\"seq\":%llu,\"elapsed_ms\":%.2f,\"tid\":%ld,\"type\":",
               i ? "," : "", (unsigned long long)e->seq, e->elapsed_ms, e->tid);
        jb_escaped_str(&jb, sync_event_type_name(e->type));
        jb_fmt(&jb, ",\"actor\":");
        jb_escaped_str(&jb, e->actor);
        jb_fmt(&jb, ",\"detail\":");
        jb_escaped_str(&jb, e->detail);
        jb_fmt(&jb, "}");
    }
    jb_fmt(&jb, "]}");

    if (out_len) *out_len = jb.len;
    return jb.data;
}

static int send_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static void send_response(int fd, const char *status, const char *ctype,
                          const void *body, size_t body_len, int keep_alive) {
    char hdr[512];
    int hlen = snprintf(hdr, sizeof(hdr),
                        "HTTP/1.1 %s\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %zu\r\n"
                        "Cache-Control: no-store\r\n"
                        "Connection: %s\r\n\r\n",
                        status, ctype, body_len,
                        keep_alive ? "keep-alive" : "close");
    if (hlen > 0 && send_all(fd, hdr, (size_t)hlen) == 0 && body && body_len > 0) {
        send_all(fd, body, body_len);
    }
}

static int query_int(const char *query, const char *key, int def_val) {
    if (!query || !key) return def_val;
    size_t klen = strlen(key);
    const char *p = query;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == query || p[-1] == '&') && p[klen] == '=') {
            return atoi(p + klen + 1);
        }
        p += klen;
    }
    return def_val;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void query_str(const char *query, const char *key, char *out, size_t out_sz, const char *def_val) {
    snprintf(out, out_sz, "%s", def_val ? def_val : "");
    if (!query || !key || out_sz == 0) return;
    size_t klen = strlen(key);
    const char *p = query;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == query || p[-1] == '&') && p[klen] == '=') {
            const char *src = p + klen + 1;
            size_t pos = 0;
            while (*src && *src != '&' && pos + 1 < out_sz) {
                if (*src == '%' && src[1] && src[2]) {
                    int h1 = hex_val(src[1]), h2 = hex_val(src[2]);
                    if (h1 >= 0 && h2 >= 0) {
                        out[pos++] = (char)((h1 << 4) | h2);
                        src += 3;
                        continue;
                    }
                } else if (*src == '+') {
                    out[pos++] = ' ';
                    src++;
                    continue;
                }
                out[pos++] = *src++;
            }
            out[pos] = '\0';
            return;
        }
        p += klen;
    }
}

typedef struct {
    int client_fd;
    LabContext *ctx;
} ClientArg;

static ssize_t read_http_request(int fd, char *buf, size_t cap) {
    size_t total = 0;
    while (total + 1 < cap) {
        ssize_t n = recv(fd, buf + total, cap - 1 - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return total > 0 ? (ssize_t)total : 0;
        total += (size_t)n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n") != NULL) return (ssize_t)total;
    }
    return -2;
}

static void *client_thread_fn(void *arg) {
    ClientArg *ca = (ClientArg *)arg;
    int fd = ca->client_fd;
    LabContext *ctx = ca->ctx;
    free(ca);

    struct timeval tv = {5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    char *req = malloc(16384);
    if (!req) {
        close(fd);
        return NULL;
    }

    while (!g_web_stop) {
        ssize_t n = read_http_request(fd, req, 16384);
        if (n == -2) {
            send_response(fd, "431 Request Header Fields Too Large", "text/plain", "Header too large", 16, 0);
            break;
        }
        if (n <= 0) break;

        char method[16] = {0}, target[512] = {0}, proto[16] = {0};
        if (sscanf(req, "%15s %511s %15s", method, target, proto) < 2) break;

        int keep_alive = (strstr(req, "Connection: close") == NULL &&
                          strstr(req, "connection: close") == NULL &&
                          strcmp(proto, "HTTP/1.1") == 0);

        char *qmark = strchr(target, '?');
        const char *query = "";
        if (qmark) {
            *qmark = '\0';
            query = qmark + 1;
        }

        if (strcmp(method, "GET") == 0 && strcmp(target, "/") == 0) {
            send_response(fd, "200 OK", "text/html; charset=utf-8",
                          WEB_INDEX_HTML, WEB_INDEX_HTML_LEN, keep_alive);
        } else if (strcmp(method, "GET") == 0 && strcmp(target, "/favicon.ico") == 0) {
            send_response(fd, "204 No Content", "image/x-icon", NULL, 0, keep_alive);
        } else if (strcmp(method, "GET") == 0 && strcmp(target, "/api/state") == 0) {
            size_t jlen = 0;
            char *json = build_state_json(ctx, &jlen);
            if (json) {
                send_response(fd, "200 OK", "application/json; charset=utf-8", json, jlen, keep_alive);
                free(json);
            } else {
                send_response(fd, "500 Internal Server Error", "text/plain", "oom", 3, 0);
                break;
            }
        } else if (strcmp(method, "POST") == 0 && strcmp(target, "/api/stop") == 0) {
            lab_stop(ctx);
            send_response(fd, "200 OK", "application/json", "{\"ok\":true}", 11, keep_alive);
        } else if (strcmp(method, "POST") == 0 && strcmp(target, "/api/pool/start") == 0) {
            int w = query_int(query, "workers", 4);
            int t = query_int(query, "tasks", 120);
            int wt = query_int(query, "workload", 3);
            int qc = query_int(query, "qcap", 16);
            char dir[64], pat[32];
            query_str(query, "dir", dir, sizeof(dir), "/usr/include");
            query_str(query, "pattern", pat, sizeof(pat), "define");
            TaskType tt = (wt == 0) ? TASK_SIM_IO : (wt == 1 ? TASK_CPU_PRIME : (wt == 2 ? TASK_CPU_HASH : TASK_FS_SCAN));
            lab_start_pool(ctx, w, t, tt, qc, dir, pat);
            send_response(fd, "200 OK", "application/json", "{\"ok\":true}", 11, keep_alive);
        } else if (strcmp(method, "POST") == 0 && strcmp(target, "/api/bench/start") == 0) {
            int w = query_int(query, "workers", 4);
            int t = query_int(query, "tasks", 240);
            int k = query_int(query, "kind", 1);
            lab_start_bench(ctx, w, t, k);
            send_response(fd, "200 OK", "application/json", "{\"ok\":true}", 11, keep_alive);
        } else if (strcmp(method, "POST") == 0 && strcmp(target, "/api/barrier/start") == 0) {
            int w = query_int(query, "workers", 4);
            lab_start_barrier(ctx, w);
            send_response(fd, "200 OK", "application/json", "{\"ok\":true}", 11, keep_alive);
        } else if (strcmp(method, "POST") == 0 && strcmp(target, "/api/race/start") == 0) {
            int th = query_int(query, "threads", 4);
            int lp = query_int(query, "loops", 1500000);
            lab_start_race(ctx, th, lp);
            send_response(fd, "200 OK", "application/json", "{\"ok\":true}", 11, keep_alive);
        } else if (strcmp(method, "POST") == 0 && strcmp(target, "/api/cache/start") == 0) {
            int th = query_int(query, "threads", 4);
            int ops = query_int(query, "ops", 200000);
            int rpct = query_int(query, "read_pct", 85);
            lab_start_cache(ctx, th, ops, rpct);
            send_response(fd, "200 OK", "application/json", "{\"ok\":true}", 11, keep_alive);
        } else if (strcmp(method, "POST") == 0 && strcmp(target, "/api/pc/start") == 0) {
            int p = query_int(query, "producers", 2);
            int c = query_int(query, "consumers", 3);
            int b = query_int(query, "buf", 8);
            int it = query_int(query, "items", 24);
            lab_start_pc(ctx, p, c, b, it);
            send_response(fd, "200 OK", "application/json", "{\"ok\":true}", 11, keep_alive);
        } else if (strcmp(method, "POST") == 0 && strcmp(target, "/api/rw/start") == 0) {
            int r = query_int(query, "readers", 4);
            int w = query_int(query, "writers", 2);
            int rnd = query_int(query, "rounds", 6);
            lab_start_rw(ctx, r, w, rnd);
            send_response(fd, "200 OK", "application/json", "{\"ok\":true}", 11, keep_alive);
        } else if (strcmp(method, "POST") == 0 && strcmp(target, "/api/deadlock/start") == 0) {
            int rnd = query_int(query, "rounds", 5);
            lab_start_deadlock(ctx, rnd);
            send_response(fd, "200 OK", "application/json", "{\"ok\":true}", 11, keep_alive);
        } else if (strcmp(method, "POST") == 0 && strcmp(target, "/api/phil/start") == 0) {
            int m = query_int(query, "meals", 4);
            lab_start_phil(ctx, m);
            send_response(fd, "200 OK", "application/json", "{\"ok\":true}", 11, keep_alive);
        } else {
            send_response(fd, "404 Not Found", "text/plain", "Not Found", 9, keep_alive);
        }

        if (!keep_alive) break;
    }

    free(req);
    shutdown(fd, SHUT_WR);
    char drain[256];
    while (recv(fd, drain, sizeof(drain), MSG_DONTWAIT) > 0) {}
    close(fd);
    return NULL;
}

int web_server_run(LabContext *ctx, int port) {
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    struct sigaction old_int, old_term;
    sigaction(SIGINT, &sa, &old_int);
    sigaction(SIGTERM, &sa, &old_term);
    g_web_stop = 0;

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno == EADDRINUSE) {
            printf("\n  " UI_BYELLOW "[!] Port %d da duoc su dung (Web Control Center co the dang chay san tai http://127.0.0.1:%d).\n" UI_RESET, port, port);
            ui_wait_enter();
        } else {
            perror("bind");
        }
        close(lfd);
        return -1;
    }
    if (listen(lfd, 64) < 0) {
        perror("listen");
        close(lfd);
        return -1;
    }

    int has_tty = isatty(STDIN_FILENO);
    ui_clear();
    ui_header("Web Control Center (POSIX TCP HTTP Server + pthread)");
    printf("  " UI_BGREEN UI_BOLD "[RUNNING]" UI_RESET " Web Server dang lang nghe tren port "
           UI_BCYAN UI_BOLD "%d\n\n" UI_RESET, port);
    printf("  Mo trinh duyet tren Windows va truy cap:\n");
    printf("    " UI_BCYAN UI_BOLD "http://127.0.0.1:%d\n\n" UI_RESET, port);
    if (has_tty) {
        printf("  " UI_YELLOW "Nhan ENTER (hoac Ctrl+C) de dung Web Server va quay lai...\n" UI_RESET);
    }
    fflush(stdout);

    while (!g_web_stop) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(lfd, &rfds);
        int maxfd = lfd;
        if (has_tty) {
            FD_SET(STDIN_FILENO, &rfds);
            if (STDIN_FILENO > maxfd) maxfd = STDIN_FILENO;
        }

        struct timeval tv = {0, 200000};
        int ready = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (has_tty && FD_ISSET(STDIN_FILENO, &rfds)) {
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF) {}
            break;
        }
        if (FD_ISSET(lfd, &rfds)) {
            int cfd = accept(lfd, NULL, NULL);
            if (cfd >= 0) {
                ClientArg *ca = malloc(sizeof(*ca));
                if (!ca) {
                    close(cfd);
                    continue;
                }
                ca->client_fd = cfd;
                ca->ctx = ctx;
                pthread_t th;
                if (pthread_create(&th, NULL, client_thread_fn, ca) == 0) {
                    pthread_detach(th);
                } else {
                    free(ca);
                    close(cfd);
                }
            }
        }
    }

    lab_stop(ctx);
    close(lfd);
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGTERM, &old_term, NULL);
    return 0;
}
