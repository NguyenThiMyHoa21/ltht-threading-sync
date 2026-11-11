# DACK v3.0 — Threading & Synchronization Lab (Linux Systems / POSIX C11)

Đồ án cuối kỳ môn **Lập trình hệ thống (Đề 12: Threading & Synchronization)** viết thuần bằng **C11 + POSIX System Calls + `pthread` + POSIX TCP Socket**, tích hợp **Terminal UI (TUI)** và **Web Control Center** thời gian thực.

## Các bài toán & kỹ thuật Lập trình hệ thống thực tế

1. **Thread Pool + POSIX Filesystem Scanner (`opendir`/`lstat`/`open`/`read`/`close` + CRC32):**
   - Quét thật hàng trăm header trong `/usr/include` (đếm bytes, dòng, từ khóa `pthread` và tính tổng kiểm CRC32 đồng nhất), bảo vệ hàng đợi vòng bằng `pthread_mutex_t` + 2 `pthread_cond_t` (`not_empty`, `not_full`).
2. **Benchmark 1-Thread vs Multi-Thread & `pthread_barrier_t` Pipeline:**
   - So sánh hiệu năng 1 luồng vs `ThreadPool` nhiều luồng trên tác vụ đọc file thật và CPU Prime, đối chiếu **Checksum MATCH**, **Context Switches (`getrusage`)** và **3-Stage Barrier Pipeline**.
3. **OS Synchronization Profiler (`getrusage`) & Concurrent Hash KV Cache:**
   - So sánh `No Lock (Race)` vs `pthread_mutex_t` vs `pthread_spinlock_t` vs `C11 atomic_fetch_add` kèm chỉ số nhân Linux (`user_ms`, `sys_ms`, `ru_nvcsw`).
   - So sánh kiến trúc khóa cho Hash Table KV Cache: **Global Mutex** vs **Global `pthread_rwlock_t`** vs **16-Bucket Lock Striping**.
4. **Log Stream Pipeline (`/var/log/dpkg.log` + `sem_t` + `pthread_mutex_t`):**
   - Producer đọc dòng log thật từ `/var/log/dpkg.log` qua Bounded Buffer, Consumer phân loại sự kiện `install / configure / status / upgrade`.
5. **Readers–Writers (`pthread_rwlock_t`), Deadlock Recovery (`pthread_mutex_timedlock`) & Dining Philosophers:**
   - Tái hiện AB-BA Deadlock và tự thoát an toàn bằng `pthread_mutex_timedlock` (`ETIMEDOUT` + backoff), đối chiếu với Canonical Lock Ordering (`0` timeout).
   - 5 Triết gia sử dụng **Canonical Ordered Forks + Butler Semaphore**, sạch 100% dưới **ThreadSanitizer (`-fsanitize=thread`)**.

## Build, Kiểm thử & Chạy

```bash
make clean && make
make test        # chạy 8 integration test hệ thống
make asan        # kiểm tra bộ nhớ với AddressSanitizer + UBSan
make tsan        # kiểm tra Data Race & Lock Order với ThreadSanitizer
./bin/DACK_project           # giao diện TUI
./bin/DACK_project --web 8080 # Web Control Center tại http://127.0.0.1:8080
./bin/DACK_project 4 200     # quét nhanh 200 file trong /usr/include
```
