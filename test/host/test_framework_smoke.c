/**
 * @file test_framework_smoke.c
 * @brief Smoke test to verify Unity framework compiles and runs on host.
 */

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_smoke_true(void) {
    TEST_ASSERT_TRUE(1);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_smoke_true);
    return UNITY_END();
}
