#include <stdio.h>
#include <pthread.h>

#define THREADS 4
#define LOOPS 1000000

static long counter = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void *increase(void *arg) {
    (void)arg;
    for (int i = 0; i < LOOPS; i++) {
        pthread_mutex_lock(&mutex);
        counter++;
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

int main(void) {
    pthread_t t[THREADS];

    for (int i = 0; i < THREADS; i++) pthread_create(&t[i], NULL, increase, NULL);
    for (int i = 0; i < THREADS; i++) pthread_join(t[i], NULL);

    printf("Gia tri mong doi: %ld\n", (long)THREADS * LOOPS);
    printf("Gia tri thuc te : %ld\n", counter);
    printf("Mutex bao ve bien counter khoi race condition → gia tri luon dung.\n");

    pthread_mutex_destroy(&mutex);
    return 0;
}
