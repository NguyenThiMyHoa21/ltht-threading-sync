#include <stdio.h>
#include <pthread.h>

static void *hello(void *arg) {
    int id = *(int *)arg;
    printf("Hello tu thread %d\n", id);
    return NULL;
}

int main(void) {
    pthread_t threads[4];
    int ids[4];

    for (int i = 0; i < 4; i++) {
        ids[i] = i + 1;
        pthread_create(&threads[i], NULL, hello, &ids[i]);
    }

    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    return 0;
}
