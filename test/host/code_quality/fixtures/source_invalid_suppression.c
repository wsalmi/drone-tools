// Example source file with INVALID suppression (no CQR-ID)
#include <stdint.h>

void bad_function(const uint8_t *data, size_t len) {
    // NOLINTNEXTLINE(readability-magic-numbers)
    uint8_t header = data[4];

    // cppcheck-suppress nullPointer
    *data = 0;
}
