/**
 * @file probe_use_after_free.c
 * @brief Sanitizer probe: deliberately accesses memory after free.
 *
 * This test MUST fail under AddressSanitizer. If it passes, ASan is not active
 * or the gate is misconfigured.
 *
 * Validates: Requirements 5.8, 6.6, 11.3, 12.6
 * Gate: host-sanitizers (ASan+UBSan)
 */

#include <stdlib.h>
#include <stdio.h>

/* Prevent the compiler from optimizing away the read */
volatile int sink;

int main(void) {
    /* Allocate and initialize */
    int *ptr = (int *)malloc(sizeof(int) * 4);
    if (!ptr) {
        fprintf(stderr, "probe_use_after_free: malloc failed\n");
        return 1;
    }

    ptr[0] = 42;
    ptr[1] = 99;

    /* Free the memory */
    free(ptr);

    /*
     * INTENTIONAL BUG: read from freed memory.
     * ASan must detect this heap-use-after-free.
     */
    sink = ptr[0];

    printf("probe_use_after_free: UNEXPECTED PASS — ASan did not detect use-after-free\n");
    return 0;
}
