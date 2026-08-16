// Example source file with valid suppression
#include <stdint.h>

void process_frame(const uint8_t *data, size_t len) {
    // NOLINTNEXTLINE(readability-magic-numbers) CQR-STATIC-003: protocol-defined offset
    uint8_t header = data[4];

    #pragma GCC diagnostic push
    // CQR-STATIC-004: legacy macro compatibility
    #pragma GCC diagnostic ignored "-Wpedantic"
    ESP_LOGI("TAG", "header=%d", header);
    #pragma GCC diagnostic pop
}
