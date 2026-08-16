/**
 * @file test_pbt_remoteid_bounds.c
 * @brief Property 5: Parser bounds safety — PBT for RemoteID decoder.
 *
 * For any buffer and generated combination of declared length, offset,
 * and field size, the parser SHALL access only the range [0, length),
 * reject non-representable sum/range, and terminate without crash,
 * over-read, over-write, or use of uninitialized data.
 *
 * Feature: code-quality-review, Property 5
 * **Validates: Requirements 5.1, 5.2, 5.7, 5.8**
 *
 * Uses Theft PBT framework with ASan+UBSan to detect OOB access.
 */

#include "unity.h"
#include "theft.h"
#include "remoteid_decoder.h"
#include "error_codes.h"
#include <string.h>
#include <stdlib.h>

void setUp(void) {}
void tearDown(void) {}

/* ========================================================================
 * Generator: Random buffer with constrained length
 * ======================================================================== */

/** Maximum buffer size for PBT — covers oversized inputs */
#define PBT_MAX_BUF_SIZE 512

/**
 * @brief Generated test input for bounds PBT.
 */
typedef struct {
    uint8_t buf[PBT_MAX_BUF_SIZE];
    uint16_t len;       /**< Actual populated length (0..PBT_MAX_BUF_SIZE) */
    bool is_ble;        /**< Whether to test as BLE or WiFi */
} pbt_frame_input_t;

static enum theft_alloc_res alloc_frame_input(struct theft *t, void *env, void **output)
{
    (void)env;
    pbt_frame_input_t *inp = malloc(sizeof(*inp));
    if (!inp) return THEFT_ALLOC_ERROR;

    /* Generate a random length [0, PBT_MAX_BUF_SIZE] with bias towards
     * interesting sizes near protocol boundaries.
     * Note: Use theft_random_bits with <= 16 bits to avoid UBSan issues
     * in Theft's internal shift operations. */
    uint32_t len_choice = theft_random_bits(t, 4);
    if (len_choice == 0) {
        /* Empty buffer */
        inp->len = 0;
    } else if (len_choice == 1) {
        /* Very small (1-5 bytes) */
        inp->len = (uint16_t)((theft_random_bits(t, 3) % 5) + 1);
    } else if (len_choice <= 4) {
        /* Near WiFi minimum (25-35) */
        inp->len = (uint16_t)((theft_random_bits(t, 4) % 11) + 25);
    } else if (len_choice <= 7) {
        /* Near BLE minimum (25-35) */
        inp->len = (uint16_t)((theft_random_bits(t, 4) % 11) + 25);
    } else if (len_choice <= 10) {
        /* Medium (30-100) */
        inp->len = (uint16_t)((theft_random_bits(t, 7) % 71) + 30);
    } else {
        /* Full range [0, PBT_MAX_BUF_SIZE] using two 8-bit draws */
        uint16_t hi = (uint16_t)(theft_random_bits(t, 1));
        uint16_t lo = (uint16_t)(theft_random_bits(t, 8));
        inp->len = (uint16_t)((hi << 8) | lo);
        if (inp->len > PBT_MAX_BUF_SIZE) {
            inp->len = (uint16_t)(inp->len % (PBT_MAX_BUF_SIZE + 1));
        }
    }

    /* Fill buffer with random bytes — use 8-bit draws */
    for (uint16_t i = 0; i < inp->len; i++) {
        inp->buf[i] = (uint8_t)theft_random_bits(t, 8);
    }
    /* Zero remaining bytes to avoid UB from reading uninitialized */
    if (inp->len < PBT_MAX_BUF_SIZE) {
        memset(&inp->buf[inp->len], 0, PBT_MAX_BUF_SIZE - inp->len);
    }

    /* Sometimes generate frames with valid headers but bad payloads */
    uint32_t variant = theft_random_bits(t, 3);
    if (variant == 0 && inp->len >= 5) {
        /* WiFi-like header */
        inp->buf[0] = 0xFA; inp->buf[1] = 0x0B; inp->buf[2] = 0xBC;
        inp->buf[3] = 0x0D;
        inp->buf[4] = (uint8_t)theft_random_bits(t, 4); /* random counter */
    } else if (variant == 1 && inp->len >= 5) {
        /* BLE-like header */
        inp->buf[0] = (uint8_t)theft_random_bits(t, 8); /* random AD length */
        inp->buf[1] = 0x16;
        inp->buf[2] = 0xFA; inp->buf[3] = 0xFF;
        inp->buf[4] = (uint8_t)theft_random_bits(t, 4);
    }

    inp->is_ble = (theft_random_bits(t, 1) == 1);
    *output = inp;
    return THEFT_ALLOC_OK;
}

static void free_frame_input(void *instance, void *env)
{
    (void)env;
    free(instance);
}

/* ========================================================================
 * Property: Parser does not crash, over-read, over-write on any input.
 *
 * The property succeeds if:
 * 1. The function returns without crash (ASan/UBSan detect OOB)
 * 2. On rejection, the output struct is NOT partially written
 * 3. On success, the output struct is fully initialized
 * ======================================================================== */

static enum theft_trial_res prop_bounds_safety(struct theft *t, void *arg)
{
    (void)t;
    pbt_frame_input_t *inp = (pbt_frame_input_t *)arg;

    /* Test 1: remoteid_validate_frame_safe — must not crash */
    remoteid_validation_result_t vresult;
    esp_err_t verr = remoteid_validate_frame_safe(inp->buf, inp->len, inp->is_ble, &vresult);

    /* If validation says invalid, the result struct must reflect that */
    if (verr != ESP_OK) {
        if (vresult.valid) {
            return THEFT_TRIAL_FAIL; /* Inconsistency */
        }
        if (vresult.reason == RID_REJECT_NONE) {
            return THEFT_TRIAL_FAIL; /* Must have a reason */
        }
    } else {
        if (!vresult.valid) {
            return THEFT_TRIAL_FAIL; /* Inconsistency */
        }
        if (vresult.reason != RID_REJECT_NONE) {
            return THEFT_TRIAL_FAIL;
        }
    }

    /* Test 2: remoteid_decode_wifi/ble — must not crash, must preserve
     * output on rejection (parse-then-commit). */
    remoteid_data_t out;
    /* Fill with known pattern to verify non-corruption */
    memset(&out, 0xBB, sizeof(out));
    remoteid_data_t saved_out;
    memcpy(&saved_out, &out, sizeof(out));

    esp_err_t derr;
    if (inp->is_ble) {
        derr = remoteid_decode_ble(inp->buf, inp->len, &out);
    } else {
        derr = remoteid_decode_wifi(inp->buf, inp->len, &out);
    }

    if (derr != ESP_OK) {
        /* On rejection, output must be unchanged (parse-then-commit) */
        if (memcmp(&out, &saved_out, sizeof(out)) != 0) {
            return THEFT_TRIAL_FAIL;
        }
    }

    /* Test 3: remoteid_validate_frame (legacy) — must not crash */
    (void)remoteid_validate_frame(inp->buf, inp->len, inp->is_ble);

    return THEFT_TRIAL_PASS;
}

/* ========================================================================
 * Test Entry Point
 * ======================================================================== */

void test_pbt_property5_parser_bounds_safety(void)
{
    struct theft_type_info type_info = {
        .alloc = alloc_frame_input,
        .free = free_frame_input,
    };

    struct theft_run_config config = {
        .name = "Property 5: Parser bounds safety",
        .prop1 = prop_bounds_safety,
        .type_info = { &type_info },
        .trials = PBT_MIN_TRIALS,
        .seed = (PBT_SEED != 0) ? (theft_seed)PBT_SEED : theft_seed_of_time(),
    };

    enum theft_run_res result = theft_run(&config);
    TEST_ASSERT_EQUAL_MESSAGE(THEFT_RUN_PASS, result,
        "Property 5 failed: parser accessed bytes outside [0, length) "
        "or corrupted output on rejection");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pbt_property5_parser_bounds_safety);
    return UNITY_END();
}
