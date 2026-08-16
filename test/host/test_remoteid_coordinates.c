/**
 * @file test_remoteid_coordinates.c
 * @brief Unit tests and Property 7 PBT for joint latitude/longitude validity.
 *
 * Validates that has_position is true if and only if BOTH latitude AND longitude
 * are individually non-sentinel, correctly scaled, and within their intervals.
 * A single invalid member SHALL invalidate the pair.
 * Same rule applies to operator location (has_operator_location).
 *
 * Feature: code-quality-review, Property 7
 * **Validates: Requirements 5.5, 5.6, 5.7, 12.1, 12.4**
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
 * Helper: CRC-8/DVB-S2 for test frame construction
 * ======================================================================== */

static const uint8_t test_crc8_table[256] = {
    0x00, 0xD5, 0x7F, 0xAA, 0xFE, 0x2B, 0x81, 0x54,
    0x29, 0xFC, 0x56, 0x83, 0xD7, 0x02, 0xA8, 0x7D,
    0x52, 0x87, 0x2D, 0xF8, 0xAC, 0x79, 0xD3, 0x06,
    0x7B, 0xAE, 0x04, 0xD1, 0x85, 0x50, 0xFA, 0x2F,
    0xA4, 0x71, 0xDB, 0x0E, 0x5A, 0x8F, 0x25, 0xF0,
    0x8D, 0x58, 0xF2, 0x27, 0x73, 0xA6, 0x0C, 0xD9,
    0xF6, 0x23, 0x89, 0x5C, 0x08, 0xDD, 0x77, 0xA2,
    0xDF, 0x0A, 0xA0, 0x75, 0x21, 0xF4, 0x5E, 0x8B,
    0x9D, 0x48, 0xE2, 0x37, 0x63, 0xB6, 0x1C, 0xC9,
    0xB4, 0x61, 0xCB, 0x1E, 0x4A, 0x9F, 0x35, 0xE0,
    0xCF, 0x1A, 0xB0, 0x65, 0x31, 0xE4, 0x4E, 0x9B,
    0xE6, 0x33, 0x99, 0x4C, 0x18, 0xCD, 0x67, 0xB2,
    0x39, 0xEC, 0x46, 0x93, 0xC7, 0x12, 0xB8, 0x6D,
    0x10, 0xC5, 0x6F, 0xBA, 0xEE, 0x3B, 0x91, 0x44,
    0x6B, 0xBE, 0x14, 0xC1, 0x95, 0x40, 0xEA, 0x3F,
    0x42, 0x97, 0x3D, 0xE8, 0xBC, 0x69, 0xC3, 0x16,
    0xEF, 0x3A, 0x90, 0x45, 0x11, 0xC4, 0x6E, 0xBB,
    0xC6, 0x13, 0xB9, 0x6C, 0x38, 0xED, 0x47, 0x92,
    0xBD, 0x68, 0xC2, 0x17, 0x43, 0x96, 0x3C, 0xE9,
    0x94, 0x41, 0xEB, 0x3E, 0x6A, 0xBF, 0x15, 0xC0,
    0x4B, 0x9E, 0x34, 0xE1, 0xB5, 0x60, 0xCA, 0x1F,
    0x62, 0xB7, 0x1D, 0xC8, 0x9C, 0x49, 0xE3, 0x36,
    0x19, 0xCC, 0x66, 0xB3, 0xE7, 0x32, 0x98, 0x4D,
    0x30, 0xE5, 0x4F, 0x9A, 0xCE, 0x1B, 0xB1, 0x64,
    0x72, 0xA7, 0x0D, 0xD8, 0x8C, 0x59, 0xF3, 0x26,
    0x5B, 0x8E, 0x24, 0xF1, 0xA5, 0x70, 0xDA, 0x0F,
    0x20, 0xF5, 0x5F, 0x8A, 0xDE, 0x0B, 0xA1, 0x74,
    0x09, 0xDC, 0x76, 0xA3, 0xF7, 0x22, 0x88, 0x5D,
    0xD6, 0x03, 0xA9, 0x7C, 0x28, 0xFD, 0x57, 0x82,
    0xFF, 0x2A, 0x80, 0x55, 0x01, 0xD4, 0x7E, 0xAB,
    0x84, 0x51, 0xFB, 0x2E, 0x7A, 0xAF, 0x05, 0xD0,
    0xAD, 0x78, 0xD2, 0x07, 0x53, 0x86, 0x2C, 0xF9
};

/**
 * @brief Compute CRC-8/DVB-S2 over data (same as production remoteid_crc8).
 */
static uint8_t test_compute_crc8(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0x00;
    for (uint16_t i = 0; i < len; i++) {
        crc = test_crc8_table[crc ^ data[i]];
    }
    return crc;
}

/**
 * @brief Set the CRC byte (byte 24) of a 25-byte message.
 */
static void set_msg_crc(uint8_t *msg)
{
    msg[24] = test_compute_crc8(msg, 24);
}

/* ========================================================================
 * Helper: Build a minimal WiFi frame with Location + Basic ID messages
 * ======================================================================== */

/** WiFi header: OUI(3) + OUI_Type(1) + counter(1) = 5 bytes */
#define WIFI_HDR_LEN   5
/** Single ASTM F3411 message size */
#define MSG_SIZE       25

/**
 * @brief Build a WiFi frame containing a Basic ID and a Location message.
 *
 * The Location message embeds the given raw lat/lon values.
 * Returns the total frame length.
 */
static uint16_t build_wifi_location_frame(uint8_t *buf, uint16_t buf_size,
                                          int32_t lat_raw, int32_t lon_raw)
{
    /* Need: header(5) + BasicID(25) + Location(25) = 55 bytes */
    uint16_t frame_len = WIFI_HDR_LEN + 2 * MSG_SIZE;
    if (buf_size < frame_len) return 0;

    memset(buf, 0, frame_len);

    /* WiFi header */
    buf[0] = 0xFA; buf[1] = 0x0B; buf[2] = 0xBC; /* OUI */
    buf[3] = 0x0D; /* OUI Type */
    buf[4] = 2;    /* message count */

    /* Message 0: Basic ID (type 0) */
    uint8_t *msg0 = &buf[WIFI_HDR_LEN];
    msg0[0] = 0x00; /* type=0, version=0 */
    msg0[1] = 0x10; /* ID type = Serial (1 << 4) */
    /* Fake UAS ID "TEST1234" */
    memcpy(&msg0[2], "TEST1234", 8);
    set_msg_crc(msg0);

    /* Message 1: Location (type 1) */
    uint8_t *msg1 = &buf[WIFI_HDR_LEN + MSG_SIZE];
    msg1[0] = 0x10; /* type=1, version=0 */
    msg1[1] = 0x20; /* status: airborne */

    /* Write lat at offset 5 (LE int32) */
    uint32_t ulat = (uint32_t)lat_raw;
    msg1[5] = (uint8_t)(ulat & 0xFF);
    msg1[6] = (uint8_t)((ulat >> 8) & 0xFF);
    msg1[7] = (uint8_t)((ulat >> 16) & 0xFF);
    msg1[8] = (uint8_t)((ulat >> 24) & 0xFF);

    /* Write lon at offset 9 (LE int32) */
    uint32_t ulon = (uint32_t)lon_raw;
    msg1[9]  = (uint8_t)(ulon & 0xFF);
    msg1[10] = (uint8_t)((ulon >> 8) & 0xFF);
    msg1[11] = (uint8_t)((ulon >> 16) & 0xFF);
    msg1[12] = (uint8_t)((ulon >> 24) & 0xFF);
    set_msg_crc(msg1);

    return frame_len;
}

/**
 * @brief Build a WiFi frame containing a Basic ID and a System message.
 *
 * The System message embeds the given raw operator lat/lon values.
 * Returns the total frame length.
 */
static uint16_t build_wifi_system_frame(uint8_t *buf, uint16_t buf_size,
                                        int32_t op_lat_raw, int32_t op_lon_raw)
{
    /* Need: header(5) + BasicID(25) + System(25) = 55 bytes */
    uint16_t frame_len = WIFI_HDR_LEN + 2 * MSG_SIZE;
    if (buf_size < frame_len) return 0;

    memset(buf, 0, frame_len);

    /* WiFi header */
    buf[0] = 0xFA; buf[1] = 0x0B; buf[2] = 0xBC;
    buf[3] = 0x0D;
    buf[4] = 2;

    /* Message 0: Basic ID (type 0) */
    uint8_t *msg0 = &buf[WIFI_HDR_LEN];
    msg0[0] = 0x00;
    msg0[1] = 0x10;
    memcpy(&msg0[2], "TEST1234", 8);
    set_msg_crc(msg0);

    /* Message 1: System (type 4) */
    uint8_t *msg1 = &buf[WIFI_HDR_LEN + MSG_SIZE];
    msg1[0] = 0x40; /* type=4, version=0 */

    /* Write operator lat at offset 2 (LE int32) */
    uint32_t ulat = (uint32_t)op_lat_raw;
    msg1[2] = (uint8_t)(ulat & 0xFF);
    msg1[3] = (uint8_t)((ulat >> 8) & 0xFF);
    msg1[4] = (uint8_t)((ulat >> 16) & 0xFF);
    msg1[5] = (uint8_t)((ulat >> 24) & 0xFF);

    /* Write operator lon at offset 6 (LE int32) */
    uint32_t ulon = (uint32_t)op_lon_raw;
    msg1[6] = (uint8_t)(ulon & 0xFF);
    msg1[7] = (uint8_t)((ulon >> 8) & 0xFF);
    msg1[8] = (uint8_t)((ulon >> 16) & 0xFF);
    msg1[9] = (uint8_t)((ulon >> 24) & 0xFF);
    set_msg_crc(msg1);

    return frame_len;
}

/* ========================================================================
 * Individual coordinate validity logic (mirror of production for test oracle)
 * ======================================================================== */

static bool test_lat_valid(int32_t lat_raw)
{
    if (lat_raw == 0) return false;  /* sentinel */
    if (lat_raw < REMOTEID_LAT_RAW_MIN || lat_raw > REMOTEID_LAT_RAW_MAX) return false;
    return true;
}

static bool test_lon_valid(int32_t lon_raw)
{
    if (lon_raw == 0) return false;  /* sentinel */
    if (lon_raw < REMOTEID_LON_RAW_MIN || lon_raw > REMOTEID_LON_RAW_MAX) return false;
    return true;
}

/* ========================================================================
 * Unit Tests: Boundary Table — 4 validity combinations
 * ======================================================================== */

/**
 * @brief Both lat AND lon valid: has_position must be true.
 */
void test_both_valid_position(void)
{
    uint8_t buf[64];
    int32_t lat = 377700000;  /* ~37.77° (San Francisco) */
    int32_t lon = -1224200000; /* ~-122.42° */

    uint16_t len = build_wifi_location_frame(buf, sizeof(buf), lat, lon);
    TEST_ASSERT_GREATER_THAN(0, len);

    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_position);
}

/**
 * @brief Lat invalid (sentinel 0), lon valid: has_position must be false.
 */
void test_lat_invalid_lon_valid_no_position(void)
{
    uint8_t buf[64];
    int32_t lat = 0;           /* sentinel = invalid */
    int32_t lon = -1224200000; /* valid */

    uint16_t len = build_wifi_location_frame(buf, sizeof(buf), lat, lon);
    TEST_ASSERT_GREATER_THAN(0, len);

    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(out.has_position);
}

/**
 * @brief Lat valid, lon invalid (sentinel 0): has_position must be false.
 */
void test_lat_valid_lon_invalid_no_position(void)
{
    uint8_t buf[64];
    int32_t lat = 377700000;  /* valid */
    int32_t lon = 0;          /* sentinel = invalid */

    uint16_t len = build_wifi_location_frame(buf, sizeof(buf), lat, lon);
    TEST_ASSERT_GREATER_THAN(0, len);

    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(out.has_position);
}

/**
 * @brief Both lat AND lon invalid (sentinel 0): has_position must be false.
 */
void test_both_invalid_no_position(void)
{
    uint8_t buf[64];
    int32_t lat = 0;  /* sentinel */
    int32_t lon = 0;  /* sentinel */

    uint16_t len = build_wifi_location_frame(buf, sizeof(buf), lat, lon);
    TEST_ASSERT_GREATER_THAN(0, len);

    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(out.has_position);
}

/* ========================================================================
 * Unit Tests: Edge cases at ±90/±180 boundaries
 * ======================================================================== */

/**
 * @brief Exactly at +90° latitude (boundary valid).
 */
void test_lat_exactly_plus_90(void)
{
    uint8_t buf[64];
    int32_t lat = 900000000;   /* +90.0° exactly */
    int32_t lon = 100000000;   /* valid */

    uint16_t len = build_wifi_location_frame(buf, sizeof(buf), lat, lon);
    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_position);
}

/**
 * @brief Exactly at -90° latitude (boundary valid).
 */
void test_lat_exactly_minus_90(void)
{
    uint8_t buf[64];
    int32_t lat = -900000000;  /* -90.0° exactly */
    int32_t lon = 100000000;   /* valid */

    uint16_t len = build_wifi_location_frame(buf, sizeof(buf), lat, lon);
    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_position);
}

/**
 * @brief One epsilon beyond +90° latitude (invalid).
 */
void test_lat_beyond_plus_90(void)
{
    uint8_t buf[64];
    int32_t lat = 900000001;   /* just past +90° */
    int32_t lon = 100000000;   /* valid */

    uint16_t len = build_wifi_location_frame(buf, sizeof(buf), lat, lon);
    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(out.has_position);
}

/**
 * @brief One epsilon beyond -90° latitude (invalid).
 */
void test_lat_beyond_minus_90(void)
{
    uint8_t buf[64];
    int32_t lat = -900000001;  /* just past -90° */
    int32_t lon = 100000000;   /* valid */

    uint16_t len = build_wifi_location_frame(buf, sizeof(buf), lat, lon);
    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(out.has_position);
}

/**
 * @brief Exactly at +180° longitude (boundary valid).
 */
void test_lon_exactly_plus_180(void)
{
    uint8_t buf[64];
    int32_t lat = 100000000;    /* valid */
    int32_t lon = 1800000000;   /* +180.0° exactly */

    uint16_t len = build_wifi_location_frame(buf, sizeof(buf), lat, lon);
    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_position);
}

/**
 * @brief Exactly at -180° longitude (boundary valid).
 */
void test_lon_exactly_minus_180(void)
{
    uint8_t buf[64];
    int32_t lat = 100000000;     /* valid */
    int32_t lon = -1800000000;   /* -180.0° exactly */

    uint16_t len = build_wifi_location_frame(buf, sizeof(buf), lat, lon);
    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_position);
}

/**
 * @brief One epsilon beyond +180° longitude (invalid).
 */
void test_lon_beyond_plus_180(void)
{
    uint8_t buf[64];
    int32_t lat = 100000000;    /* valid */
    int32_t lon = 1800000001;   /* just past +180° */

    uint16_t len = build_wifi_location_frame(buf, sizeof(buf), lat, lon);
    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(out.has_position);
}

/**
 * @brief One epsilon beyond -180° longitude (invalid).
 */
void test_lon_beyond_minus_180(void)
{
    uint8_t buf[64];
    int32_t lat = 100000000;     /* valid */
    int32_t lon = -1800000001;   /* just past -180° */

    uint16_t len = build_wifi_location_frame(buf, sizeof(buf), lat, lon);
    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(out.has_position);
}

/* ========================================================================
 * Unit Tests: Operator location (System message) — same logic
 * ======================================================================== */

/**
 * @brief Both operator lat AND lon valid: has_operator_location must be true.
 */
void test_operator_both_valid(void)
{
    uint8_t buf[64];
    int32_t lat = 377700000;
    int32_t lon = -1224200000;

    uint16_t len = build_wifi_system_frame(buf, sizeof(buf), lat, lon);
    TEST_ASSERT_GREATER_THAN(0, len);

    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(out.has_operator_location);
}

/**
 * @brief Operator lat invalid (sentinel), lon valid: has_operator_location must be false.
 */
void test_operator_lat_invalid(void)
{
    uint8_t buf[64];
    int32_t lat = 0;            /* sentinel */
    int32_t lon = -1224200000;  /* valid */

    uint16_t len = build_wifi_system_frame(buf, sizeof(buf), lat, lon);
    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(out.has_operator_location);
}

/**
 * @brief Operator lat valid, lon invalid (sentinel): has_operator_location must be false.
 */
void test_operator_lon_invalid(void)
{
    uint8_t buf[64];
    int32_t lat = 377700000;  /* valid */
    int32_t lon = 0;          /* sentinel */

    uint16_t len = build_wifi_system_frame(buf, sizeof(buf), lat, lon);
    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(out.has_operator_location);
}

/**
 * @brief Operator both invalid: has_operator_location must be false.
 */
void test_operator_both_invalid(void)
{
    uint8_t buf[64];
    int32_t lat = 0;
    int32_t lon = 0;

    uint16_t len = build_wifi_system_frame(buf, sizeof(buf), lat, lon);
    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(out.has_operator_location);
}

/**
 * @brief Operator lat out-of-range: has_operator_location must be false.
 */
void test_operator_lat_out_of_range(void)
{
    uint8_t buf[64];
    int32_t lat = 900000001;   /* past +90° */
    int32_t lon = 100000000;   /* valid */

    uint16_t len = build_wifi_system_frame(buf, sizeof(buf), lat, lon);
    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(out.has_operator_location);
}

/* ========================================================================
 * Property 7 PBT: Joint coordinate validity
 *
 * For any latitude/longitude pair, has_position SHALL be true if and only if
 * latitude and longitude are individually non-sentinel, correctly scaled,
 * and within their intervals; a single invalid member SHALL invalidate the pair.
 * Same for operator location.
 * ======================================================================== */

typedef struct {
    int32_t lat_raw;
    int32_t lon_raw;
} coord_pair_t;

static enum theft_alloc_res alloc_coord_pair(struct theft *t, void *env, void **output)
{
    (void)env;
    coord_pair_t *pair = malloc(sizeof(*pair));
    if (!pair) return THEFT_ALLOC_ERROR;

    /* Generate coordinates with bias towards interesting values.
     * Use 8-bit draws combined to avoid UBSan issues. */
    uint32_t strategy = theft_random_bits(t, 3);

    if (strategy == 0) {
        /* Sentinel values */
        pair->lat_raw = 0;
        pair->lon_raw = 0;
    } else if (strategy == 1) {
        /* One sentinel, one valid */
        pair->lat_raw = 0;
        /* Generate a valid longitude: non-zero, in range */
        uint32_t lo = theft_random_bits(t, 16);
        uint32_t hi = theft_random_bits(t, 15);
        int32_t val = (int32_t)((hi << 16) | lo);
        /* Clamp to valid lon range */
        if (val == 0) val = 1;
        if (val > REMOTEID_LON_RAW_MAX) val = REMOTEID_LON_RAW_MAX;
        if (val < REMOTEID_LON_RAW_MIN) val = REMOTEID_LON_RAW_MIN;
        pair->lon_raw = val;
    } else if (strategy == 2) {
        /* Valid lat, sentinel lon */
        uint32_t lo = theft_random_bits(t, 16);
        uint32_t hi = theft_random_bits(t, 14);
        int32_t val = (int32_t)((hi << 16) | lo);
        if (val == 0) val = 1;
        if (val > REMOTEID_LAT_RAW_MAX) val = REMOTEID_LAT_RAW_MAX;
        if (val < REMOTEID_LAT_RAW_MIN) val = REMOTEID_LAT_RAW_MIN;
        pair->lat_raw = val;
        pair->lon_raw = 0;
    } else if (strategy == 3) {
        /* Boundary values: exactly at ±90 / ±180 */
        uint32_t which = theft_random_bits(t, 2);
        int32_t lat_choices[] = { REMOTEID_LAT_RAW_MIN, REMOTEID_LAT_RAW_MAX, 1, -1 };
        int32_t lon_choices[] = { REMOTEID_LON_RAW_MIN, REMOTEID_LON_RAW_MAX, 1, -1 };
        pair->lat_raw = lat_choices[which];
        pair->lon_raw = lon_choices[which];
    } else if (strategy == 4) {
        /* Out-of-range values */
        uint32_t which = theft_random_bits(t, 2);
        if (which == 0) {
            pair->lat_raw = 900000001;
            pair->lon_raw = 100000000;
        } else if (which == 1) {
            pair->lat_raw = -900000001;
            pair->lon_raw = -100000000;
        } else if (which == 2) {
            pair->lat_raw = 100000000;
            pair->lon_raw = 1800000001;
        } else {
            pair->lat_raw = 100000000;
            pair->lon_raw = -1800000001;
        }
    } else {
        /* Fully random int32 values */
        uint32_t lo1 = theft_random_bits(t, 16);
        uint32_t hi1 = theft_random_bits(t, 16);
        pair->lat_raw = (int32_t)((hi1 << 16) | lo1);

        uint32_t lo2 = theft_random_bits(t, 16);
        uint32_t hi2 = theft_random_bits(t, 16);
        pair->lon_raw = (int32_t)((hi2 << 16) | lo2);
    }

    *output = pair;
    return THEFT_ALLOC_OK;
}

static void free_coord_pair(void *instance, void *env)
{
    (void)env;
    free(instance);
}

/**
 * Property check: has_position == (lat_valid(lat) && lon_valid(lon))
 */
static enum theft_trial_res prop_joint_coordinate_validity(struct theft *t, void *arg)
{
    (void)t;
    coord_pair_t *pair = (coord_pair_t *)arg;

    /* Expected result: position valid iff both individually valid */
    bool lat_ok = test_lat_valid(pair->lat_raw);
    bool lon_ok = test_lon_valid(pair->lon_raw);
    bool expected_has_position = lat_ok && lon_ok;

    /* Build a WiFi Location frame with these coordinates */
    uint8_t buf[64];
    uint16_t len = build_wifi_location_frame(buf, sizeof(buf), pair->lat_raw, pair->lon_raw);
    if (len == 0) return THEFT_TRIAL_SKIP;

    remoteid_data_t out;
    remoteid_data_init(&out);
    esp_err_t err = remoteid_decode_wifi(buf, len, &out);
    if (err != ESP_OK) {
        /* If decoding fails, we can't test the property. Skip. */
        return THEFT_TRIAL_SKIP;
    }

    /* Check aircraft position */
    if (out.has_position != expected_has_position) {
        return THEFT_TRIAL_FAIL;
    }

    /* If position invalid, no partial publication should be possible:
     * has_position must be false (already checked above). */

    /* Also test operator location with the same pair */
    uint8_t buf2[64];
    uint16_t len2 = build_wifi_system_frame(buf2, sizeof(buf2), pair->lat_raw, pair->lon_raw);
    if (len2 == 0) return THEFT_TRIAL_SKIP;

    remoteid_data_t out2;
    remoteid_data_init(&out2);
    esp_err_t err2 = remoteid_decode_wifi(buf2, len2, &out2);
    if (err2 != ESP_OK) {
        return THEFT_TRIAL_SKIP;
    }

    if (out2.has_operator_location != expected_has_position) {
        return THEFT_TRIAL_FAIL;
    }

    return THEFT_TRIAL_PASS;
}

void test_pbt_property7_joint_coordinate_validity(void)
{
    struct theft_type_info type_info = {
        .alloc = alloc_coord_pair,
        .free = free_coord_pair,
    };

    struct theft_run_config config = {
        .name = "Property 7: Joint coordinate validity",
        .prop1 = prop_joint_coordinate_validity,
        .type_info = { &type_info },
        .trials = PBT_MIN_TRIALS,
        .seed = (PBT_SEED != 0) ? (theft_seed)PBT_SEED : theft_seed_of_time(),
    };

    enum theft_run_res result = theft_run(&config);
    TEST_ASSERT_EQUAL_MESSAGE(THEFT_RUN_PASS, result,
        "Property 7 failed: has_position is not equivalent to "
        "(lat_valid AND lon_valid) for some coordinate pair");
}

/* ========================================================================
 * Test Entry Point
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Boundary table: four validity combinations */
    RUN_TEST(test_both_valid_position);
    RUN_TEST(test_lat_invalid_lon_valid_no_position);
    RUN_TEST(test_lat_valid_lon_invalid_no_position);
    RUN_TEST(test_both_invalid_no_position);

    /* Edge cases: exactly at ±90/±180 and one epsilon beyond */
    RUN_TEST(test_lat_exactly_plus_90);
    RUN_TEST(test_lat_exactly_minus_90);
    RUN_TEST(test_lat_beyond_plus_90);
    RUN_TEST(test_lat_beyond_minus_90);
    RUN_TEST(test_lon_exactly_plus_180);
    RUN_TEST(test_lon_exactly_minus_180);
    RUN_TEST(test_lon_beyond_plus_180);
    RUN_TEST(test_lon_beyond_minus_180);

    /* Operator location: same joint validity logic */
    RUN_TEST(test_operator_both_valid);
    RUN_TEST(test_operator_lat_invalid);
    RUN_TEST(test_operator_lon_invalid);
    RUN_TEST(test_operator_both_invalid);
    RUN_TEST(test_operator_lat_out_of_range);

    /* Property 7 PBT */
    RUN_TEST(test_pbt_property7_joint_coordinate_validity);

    return UNITY_END();
}
