/**
 * @file test_pbt_smoke.c
 * @brief Smoke test to verify Theft PBT framework compiles and runs on host.
 */

#include "unity.h"
#include "theft.h"

void setUp(void) {}
void tearDown(void) {}

static enum theft_trial_res prop_always_true(struct theft *t, void *arg) {
    (void)t;
    (void)arg;
    return THEFT_TRIAL_PASS;
}

static enum theft_alloc_res alloc_uint(struct theft *t, void *env, void **output) {
    (void)env;
    uint32_t *val = malloc(sizeof(uint32_t));
    if (!val) return THEFT_ALLOC_ERROR;
    *val = theft_random_bits(t, 32);
    *output = val;
    return THEFT_ALLOC_OK;
}

static void free_uint(void *instance, void *env) {
    (void)env;
    free(instance);
}

void test_pbt_smoke(void) {
    struct theft_type_info type_info = {
        .alloc = alloc_uint,
        .free = free_uint,
    };

    struct theft_run_config config = {
        .name = __func__,
        .prop1 = prop_always_true,
        .type_info = { &type_info },
        .trials = PBT_MIN_TRIALS,
    };

    enum theft_run_res result = theft_run(&config);
    TEST_ASSERT_EQUAL(THEFT_RUN_PASS, result);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pbt_smoke);
    return UNITY_END();
}
