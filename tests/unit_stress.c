#include <assert.h>
#include <stdio.h>
#include "common.h"
#include "sync_labs.h"

static void wait_lab(LabContext *ctx) {
    LabSnapshot s;
    for (int i = 0; i < 600; i++) {
        lab_snapshot(ctx, &s);
        if (!s.running) return;
        sleep_ms(20);
    }
    assert(0 && "Lab timed out!");
}

int main(void) {
    LabContext ctx;
    assert(lab_init(&ctx) == 0);
    LabSnapshot s;

    printf("[TEST 1] POSIX File Scan (/usr/include, 60 files, qcap=4)...\n");
    assert(lab_start_pool(&ctx, 4, 60, TASK_FS_SCAN, 4, "/usr/include", "define") == 0);
    wait_lab(&ctx);
    lab_snapshot(&ctx, &s);
    assert(s.pool.completed == s.pool.total_tasks);
    assert(s.pool.total_bytes > 0);
    assert(s.pool.total_hits > 0);
    printf("  OK: files=%d bytes=%llu hits=%llu crc=%08x\n",
           s.pool.completed, (unsigned long long)s.pool.total_bytes,
           (unsigned long long)s.pool.total_hits, s.pool.combined_crc);

    printf("[TEST 2] 1T vs MT File Scan Benchmark (Checksum must match)...\n");
    assert(lab_start_bench(&ctx, 4, 80, 1) == 0);
    wait_lab(&ctx);
    lab_snapshot(&ctx, &s);
    assert(s.bench.phase == 3);
    assert(s.bench.checksum_match == 1);
    printf("  OK: 1T=%.1fms MT=%.1fms speedup=%.2fx\n",
           s.bench.single_ms, s.bench.multi_ms, s.bench.speedup);

    printf("[TEST 3] 3-Stage Barrier Pipeline...\n");
    assert(lab_start_barrier(&ctx, 4) == 0);
    wait_lab(&ctx);
    lab_snapshot(&ctx, &s);
    assert(s.barrier.phase == 4);
    for (int i = 0; i < 4; i++) assert(s.barrier.barrier_hits[i] == 3);
    printf("  OK: checksum=0x%llx\n", (unsigned long long)s.barrier.final_checksum);

    printf("[TEST 4] Concurrent KV Cache (Mutex vs RWLock vs Striping)...\n");
    assert(lab_start_cache(&ctx, 4, 50000, 85) == 0);
    wait_lab(&ctx);
    lab_snapshot(&ctx, &s);
    for (int i = 0; i < 3; i++) assert(s.cache.items[i].done == 1);
    printf("  OK: mutex=%.2fM rwlock=%.2fM striped=%.2fM\n",
           s.cache.items[0].mops_per_sec, s.cache.items[1].mops_per_sec, s.cache.items[2].mops_per_sec);

    printf("[TEST 5] Producer-Consumer (/var/log/dpkg.log)...\n");
    assert(lab_start_pc(&ctx, 2, 2, 4, 10) == 0);
    wait_lab(&ctx);
    lab_snapshot(&ctx, &s);
    assert(s.pc.produced == 10 && s.pc.consumed == 10);
    printf("  OK: produced=%d consumed=%d\n", s.pc.produced, s.pc.consumed);

    printf("[TEST 6] Readers-Writers (pthread_rwlock_t)...\n");
    assert(lab_start_rw(&ctx, 3, 2, 3) == 0);
    wait_lab(&ctx);
    lab_snapshot(&ctx, &s);
    assert(s.rw.shared_version == 6);
    assert(s.rw.total_reads == 9);
    printf("  OK: version=%d reads=%d\n", s.rw.shared_version, s.rw.total_reads);

    printf("[TEST 7] Deadlock Detection & Recovery (timedlock vs ordered)...\n");
    assert(lab_start_deadlock(&ctx, 3) == 0);
    wait_lab(&ctx);
    lab_snapshot(&ctx, &s);
    assert(s.deadlock.phase == 3);
    assert(s.deadlock.timedlock_done == 6);
    assert(s.deadlock.ordered_done == 6);
    assert(s.deadlock.ordered_timeouts == 0);
    printf("  OK: timeouts_recovered=%d ordered_timeouts=%d\n",
           s.deadlock.timedlock_timeouts, s.deadlock.ordered_timeouts);

    printf("[TEST 8] Dining Philosophers (Ordered + Butler)...\n");
    assert(lab_start_phil(&ctx, 2) == 0);
    wait_lab(&ctx);
    lab_snapshot(&ctx, &s);
    assert(s.phil.total_meals == 10);
    printf("  OK: meals=%d\n", s.phil.total_meals);

    lab_destroy(&ctx);
    printf("ALL 8 SYSTEM TESTS PASSED!\n");
    return 0;
}
