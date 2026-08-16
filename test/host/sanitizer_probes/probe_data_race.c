/**
 * @file probe_data_race.c
 * @brief Sanitizer probe: deliberately creates a data race between threads.
 *
 * This test MUST fail under ThreadSanitizer. If it passes, TSan is not active
 * or the gate is misconfigured.
 *
 * Validates: Requirements 6.4, 11.3, 11.7, 12.6
 * Gate: host-sanitizers (TSan)
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Shared variable accessed without synchronization from two threads.
 * TSan must detect the data race.
 */
static int shared_counter = 0;

#define ITERATIONS 100000

static void *writer_thread(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERATIONS; i++) {
        /* INTENTIONAL BUG: unsynchronized write */
        shared_counter += 1;
    }
    return NULL;
}

static void *reader_thread(void *arg) {
    (void)arg;
    volatile int local_sum = 0;
    for (int i = 0; i < ITERATIONS; i++) {
        /* INTENTIONAL BUG: unsynchronized read */
        local_sum += shared_counter;
    }
    (void)local_sum;
    return NULL;
}

int main(void) {
    pthread_t t1, t2;

    if (pthread_create(&t1, NULL, writer_thread, NULL) != 0) {
        fprintf(stderr, "probe_data_race: pthread_create failed\n");
        return 1;
    }
    if (pthread_create(&t2, NULL, reader_thread, NULL) != 0) {
        fprintf(stderr, "probe_data_race: pthread_create failed\n");
        return 1;
    }

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("probe_data_race: UNEXPECTED PASS — TSan did not detect data race\n");
    printf("shared_counter = %d\n", shared_counter);
    return 0;
}
