#include "esp_system.h"

uint32_t esp_get_free_heap_size(void)
{
    return 256U * 1024U;
}
