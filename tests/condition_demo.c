#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

static int ready = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

static void *worker(void *arg) {
    (void)arg;
    pthread_mutex_lock(&mutex);
    while (!ready) {
        /* In truoc khi wait, van dang giu mutex nen an toan */
        printf("Worker: chua co du lieu, dang cho...\n");
        fflush(stdout);
        pthread_cond_wait(&cond, &mutex);
    }
    printf("Worker: da nhan duoc tin hieu, bat dau xu ly.\n");
    fflush(stdout);
    pthread_mutex_unlock(&mutex);
    return NULL;
}

int main(void) {
    pthread_t t;
    pthread_create(&t, NULL, worker, NULL);

    sleep(1);
    pthread_mutex_lock(&mutex);
    ready = 1;
    printf("Main: du lieu da san sang, gui signal.\n");
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);

    pthread_join(t, NULL);
    pthread_cond_destroy(&cond);
    pthread_mutex_destroy(&mutex);
    return 0;
}
