#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#define THREADS 6
#define MAX_INSIDE 2

static sem_t sem;

static void sleep_ms(int ms) {
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static void *worker(void *arg) {
    int id = *(int *)arg;
    printf("Thread %d dang doi tai nguyen...\n", id);
    sem_wait(&sem);
    printf("Thread %d dang SU DUNG tai nguyen.\n", id);
    sleep_ms(600);
    printf("Thread %d tra tai nguyen.\n", id);
    sem_post(&sem);
    return NULL;
}

int main(void) {
    pthread_t t[THREADS];
    int ids[THREADS];

    sem_init(&sem, 0, MAX_INSIDE);
    for (int i = 0; i < THREADS; i++) {
        ids[i] = i + 1;
        pthread_create(&t[i], NULL, worker, &ids[i]);
    }
    for (int i = 0; i < THREADS; i++) pthread_join(t[i], NULL);
    sem_destroy(&sem);
    return 0;
}
