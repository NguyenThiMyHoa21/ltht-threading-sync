#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

typedef struct {
    double user_ms;
    double sys_ms;
    long nvcsw;
    long nivcsw;
} SysUsageSnap;

typedef struct {
    uint64_t bytes;
    uint64_t lines;
    uint64_t hits;
    uint32_t crc32;
} FileScanStats;

uint64_t time_now_ns(void);
double time_elapsed_ms(uint64_t start_ns);
void sleep_ms(int ms);
long current_tid(void);

SysUsageSnap sys_rusage_snap(void);
SysUsageSnap sys_rusage_diff(SysUsageSnap before, SysUsageSnap after);

uint64_t workload_count_primes(int limit);
uint64_t workload_hash_rounds(int seed, int rounds);

/**
 * Recursively collect up to max_files regular files under root_dir using opendir/lstat.
 * Returns number of collected file paths.
 */
int sys_collect_files(const char *root_dir, char out_paths[][256], int max_files);

/**
 * Scan one file using POSIX open/read/close, computing bytes, lines, keyword hits, and CRC32.
 */
int sys_scan_file(const char *path, const char *pattern, FileScanStats *out);

#endif
