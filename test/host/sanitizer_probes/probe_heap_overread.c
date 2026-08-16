/**
 * @file probe_heap_overread.c
 * @brief Sanitizer probe: deliberately reads past a heap allocation.
 *
 * This test MUST fail under AddressSanitizer. If it passes, ASan is not active
 * or the gate is misconfigured.
 *
 * Validates: Requirements 5.8, 11.3, 12.6
 * Gate: host-sanitizers (ASan+UBSan)
 */

#include <stdlib.h>
#include <stdio.h>

/* Prevent the compiler from optimizing away the read */
volatile int sink;

int main(void) {
    /* Allocate a small buffer on the heap */
    char *buf = (char *)malloc(8);
    if (!buf) {
        fprintf(stderr, "probe_heap_overread: malloc failed\n");
        return 1;
    }

    /* Write valid data */
    for (int i = 0; i < 8; i++) {
        buf[i] = (char)(i + 'A');
    }

    /*
     * INTENTIONAL BUG: read 1 byte past the allocation.
     * ASan must detect this heap-buffer-overflow.
     */
    sink = buf[8];

    free(buf);

    printf("probe_heap_overread: UNEXPECTED PASS — ASan did not detect over-read\n");
    return 0;
}
