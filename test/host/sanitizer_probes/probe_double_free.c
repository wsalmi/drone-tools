/**
 * @file probe_double_free.c
 * @brief Sanitizer probe: deliberately frees the same allocation twice.
 *
 * This test MUST fail under AddressSanitizer. If it passes, ASan is not active
 * or the gate is misconfigured.
 *
 * Validates: Requirements 6.6, 7.8, 11.3, 12.6
 * Gate: host-sanitizers (ASan+UBSan)
 */

#include <stdlib.h>
#include <stdio.h>

int main(void) {
    /* Allocate and use memory normally */
    char *buf = (char *)malloc(16);
    if (!buf) {
        fprintf(stderr, "probe_double_free: malloc failed\n");
        return 1;
    }

    buf[0] = 'X';

    /* First free — valid */
    free(buf);

    /*
     * INTENTIONAL BUG: free the same pointer again.
     * ASan must detect this double-free / invalid-free.
     */
    free(buf);

    printf("probe_double_free: UNEXPECTED PASS — ASan did not detect double free\n");
    return 0;
}
