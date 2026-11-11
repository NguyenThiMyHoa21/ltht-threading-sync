#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include "common.h"

uint64_t time_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

double time_elapsed_ms(uint64_t start_ns) {
    uint64_t now = time_now_ns();
    if (now <= start_ns) return 0.0;
    return (double)(now - start_ns) / 1000000.0;
}

void sleep_ms(int ms) {
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

long current_tid(void) {
#ifdef SYS_gettid
    return (long)syscall(SYS_gettid);
#else
    return 0;
#endif
}

static double tv_to_ms(struct timeval tv) {
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

SysUsageSnap sys_rusage_snap(void) {
    struct rusage ru;
    SysUsageSnap s;
    memset(&s, 0, sizeof(s));
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        s.user_ms = tv_to_ms(ru.ru_utime);
        s.sys_ms = tv_to_ms(ru.ru_stime);
        s.nvcsw = ru.ru_nvcsw;
        s.nivcsw = ru.ru_nivcsw;
    }
    return s;
}

SysUsageSnap sys_rusage_diff(SysUsageSnap before, SysUsageSnap after) {
    SysUsageSnap d;
    d.user_ms = after.user_ms >= before.user_ms ? (after.user_ms - before.user_ms) : 0.0;
    d.sys_ms = after.sys_ms >= before.sys_ms ? (after.sys_ms - before.sys_ms) : 0.0;
    d.nvcsw = after.nvcsw >= before.nvcsw ? (after.nvcsw - before.nvcsw) : 0;
    d.nivcsw = after.nivcsw >= before.nivcsw ? (after.nivcsw - before.nivcsw) : 0;
    return d;
}

uint64_t workload_count_primes(int limit) {
    if (limit < 2) return 0;
    uint64_t count = 1;
    for (int n = 3; n <= limit; n += 2) {
        int is_prime = 1;
        for (int d = 3; d * d <= n; d += 2) {
            if (n % d == 0) {
                is_prime = 0;
                break;
            }
        }
        if (is_prime) count++;
    }
    return count;
}

uint64_t workload_hash_rounds(int seed, int rounds) {
    uint64_t h = 14695981039346656037ULL ^ (uint64_t)(uint32_t)seed;
    for (int i = 0; i < rounds; i++) {
        h ^= (uint64_t)(i + 1) * 0x9e3779b97f4a7c15ULL;
        h *= 1099511628211ULL;
        h ^= (h >> 33);
    }
    return h;
}

static void collect_dir_rec(const char *dir, int depth, char out_paths[][256], int max_files, int *count) {
    if (depth > 5 || *count >= max_files) return;
    DIR *dp = opendir(dir);
    if (!dp) return;

    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL && *count < max_files) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        char full[256];
        int len = snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);
        if (len <= 0 || len >= (int)sizeof(full)) continue;

        struct stat st;
        if (lstat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            collect_dir_rec(full, depth + 1, out_paths, max_files, count);
        } else if (S_ISREG(st.st_mode)) {
            snprintf(out_paths[*count], 256, "%s", full);
            (*count)++;
        }
    }
    closedir(dp);
}

int sys_collect_files(const char *root_dir, char out_paths[][256], int max_files) {
    if (!root_dir || !out_paths || max_files <= 0) return 0;
    int count = 0;
    collect_dir_rec(root_dir, 0, out_paths, max_files, &count);
    return count;
}

static uint32_t crc32_update(uint32_t crc, const unsigned char *buf, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

int sys_scan_file(const char *path, const char *pattern, FileScanStats *out) {
    if (!path || !out) return -1;
    memset(out, 0, sizeof(*out));

    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    size_t pat_len = (pattern && *pattern) ? strlen(pattern) : 0;
    if (pat_len > 31) pat_len = 31;

    unsigned char buf[16384 + 32];
    size_t carry = 0;
    uint32_t crc = 0;

    while (1) {
        ssize_t n = read(fd, buf + carry, 16384);
        if (n <= 0) break;

        size_t chunk_len = (size_t)n;
        out->bytes += chunk_len;
        crc = crc32_update(crc, buf + carry, chunk_len);

        for (size_t i = 0; i < chunk_len; i++) {
            if (buf[carry + i] == '\n') out->lines++;
        }

        if (pat_len > 0) {
            size_t avail = carry + chunk_len;
            for (size_t i = 0; i + pat_len <= avail; i++) {
                if (memcmp(buf + i, pattern, pat_len) == 0) {
                    out->hits++;
                }
            }
            size_t keep = pat_len > 1 ? pat_len - 1 : 0;
            if (avail >= keep && keep > 0) {
                memmove(buf, buf + avail - keep, keep);
                carry = keep;
            } else {
                carry = avail < keep ? avail : 0;
            }
        }
    }

    close(fd);
    out->crc32 = crc;
    return 0;
}
