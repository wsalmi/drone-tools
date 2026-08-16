/**
 * @file test_ctest_infrastructure.c
 * @brief Infrastructure validation test for CTest discovery, doubles isolation,
 *        and PBT configuration.
 *
 * This test fails if:
 *   - PBT_MIN_TRIALS is not defined or not 100
 *   - PBT_SEED is not defined (reproducibility)
 *   - A production CMakeLists.txt links mock/fake/stub libraries
 *   - A test executable in the build is not registered with CTest
 *
 * Requirements traced: 4.4, 4.5, 4.6, 10.7, 12.3
 */

#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------------------
 * 1. PBT configuration compile-time checks
 * --------------------------------------------------------------------------- */

void test_pbt_min_trials_is_defined_and_correct(void) {
#ifndef PBT_MIN_TRIALS
    TEST_FAIL_MESSAGE("PBT_MIN_TRIALS is not defined");
#else
    TEST_ASSERT_EQUAL_INT(100, PBT_MIN_TRIALS);
#endif
}

void test_pbt_seed_is_defined(void) {
#ifndef PBT_SEED
    TEST_FAIL_MESSAGE("PBT_SEED is not defined — reproducibility requires a seed");
#else
    /* PBT_SEED=0 means time-based (still defined); any value is acceptable */
    TEST_ASSERT_TRUE_MESSAGE(PBT_SEED >= 0,
        "PBT_SEED must be a non-negative integer");
#endif
}

/* ---------------------------------------------------------------------------
 * 2. Doubles isolation — no mock/fake/stub in production CMakeLists.txt
 * --------------------------------------------------------------------------- */

/**
 * Prohibited tokens in production component CMakeLists.txt files.
 * These indicate test doubles being linked to the firmware target.
 */
static const char *PROHIBITED_LINK_TOKENS[] = {
    "hal_mocks",
    "esp_idf_mocks",
    "hal_display_mock",
    "hal_sd_mock",
    "service_mocks",
    "test_helpers",
    "theft",
    NULL
};

/**
 * Production CMakeLists.txt paths relative to PROJECT_ROOT.
 * These files define what gets linked into the firmware.
 */
static const char *PRODUCTION_CMAKE_FILES[] = {
    "components/hw_hal/CMakeLists.txt",
    "components/domain/CMakeLists.txt",
    "components/services/CMakeLists.txt",
    "CMakeLists.txt",
    NULL
};

/**
 * Check whether a file contains any of the prohibited tokens.
 * Returns the first prohibited token found, or NULL if clean.
 */
static const char *check_file_for_prohibited_tokens(const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) {
        /* File not found is acceptable — conditional components */
        return NULL;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        for (int i = 0; PROHIBITED_LINK_TOKENS[i] != NULL; i++) {
            if (strstr(line, PROHIBITED_LINK_TOKENS[i]) != NULL) {
                fclose(f);
                return PROHIBITED_LINK_TOKENS[i];
            }
        }
    }
    fclose(f);
    return NULL;
}

void test_no_doubles_in_production_cmake(void) {
    /* Determine project root from compile-time macro or fallback */
    const char *project_root = NULL;

#ifdef PROJECT_ROOT_PATH
    project_root = PROJECT_ROOT_PATH;
#else
    /* Fallback: assume build/host is two levels below project root */
    project_root = "../..";
#endif

    char filepath[512];
    for (int i = 0; PRODUCTION_CMAKE_FILES[i] != NULL; i++) {
        snprintf(filepath, sizeof(filepath), "%s/%s",
                 project_root, PRODUCTION_CMAKE_FILES[i]);

        const char *found = check_file_for_prohibited_tokens(filepath);
        if (found) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Production file '%s' links test double '%s'",
                     PRODUCTION_CMAKE_FILES[i], found);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

/* ---------------------------------------------------------------------------
 * 3. CTest discovery — all test executables are registered
 *
 * Strategy: Compare known test executables against CTest test list.
 * We validate this by checking that CTestTestfile.cmake in the build dir
 * contains add_test entries for every known test target.
 *
 * The expected test names are the ones defined by add_unit_test/add_pbt_test.
 * --------------------------------------------------------------------------- */

/**
 * Minimum expected test targets that MUST be discovered by CTest.
 * This list should be updated when new test executables are added.
 */
static const char *EXPECTED_CTEST_TARGETS[] = {
    "test_framework_smoke",
    "test_pbt_smoke",
    "test_config_store",
    "test_aircraft_registry",
    "test_hal_gps",
    "test_protocol_signatures",
    "test_mavlink_decoder",
    "test_hw_manager",
    "test_remoteid_decoder",
    "test_geolocation_service",
    "test_elrs_decoder",
    "test_protocol_classifier",
    "test_telemetry_decoder",
    "test_detection_service",
    "test_spectrum_analyzer",
    "test_pilot_locator",
    "test_alert_engine",
    "test_ui_manager",
    "test_screen_scanner",
    "test_screen_map",
    "test_data_logger",
    "test_data_pipeline",
    NULL
};

void test_all_executables_discovered_by_ctest(void) {
    /* Read CTestTestfile.cmake from the build directory */
    const char *ctest_file = NULL;

#ifdef CTEST_TESTFILE_PATH
    ctest_file = CTEST_TESTFILE_PATH;
#else
    ctest_file = "CTestTestfile.cmake";
#endif

    FILE *f = fopen(ctest_file, "r");
    if (!f) {
        /* If we can't open the file, we're likely running standalone.
         * Skip gracefully — the real validation happens in CI via ctest -N. */
        TEST_IGNORE_MESSAGE(
            "CTestTestfile.cmake not found in working dir; "
            "run from build/host/ or set CTEST_TESTFILE_PATH");
        return;
    }

    /* Read entire file into memory */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = (char *)malloc((size_t)fsize + 1);
    TEST_ASSERT_NOT_NULL_MESSAGE(content, "Failed to allocate memory for CTestTestfile");
    size_t read_bytes = fread(content, 1, (size_t)fsize, f);
    content[read_bytes] = '\0';
    fclose(f);

    /* Check each expected target is present.
     * CTestTestfile.cmake uses the format: add_test(NAME "path/to/exe")
     * The test name appears either unquoted or quoted depending on CMake version.
     * We search for the name as a substring — sufficient to confirm registration. */
    for (int i = 0; EXPECTED_CTEST_TARGETS[i] != NULL; i++) {
        if (strstr(content, EXPECTED_CTEST_TARGETS[i]) == NULL) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Test target '%s' is NOT registered with CTest",
                     EXPECTED_CTEST_TARGETS[i]);
            free(content);
            TEST_FAIL_MESSAGE(msg);
        }
    }

    free(content);
}

/* ---------------------------------------------------------------------------
 * 4. Verify test doubles are explicitly test-only targets
 * --------------------------------------------------------------------------- */

void test_doubles_are_test_only_targets(void) {
    /* This is a compile-time structural assertion.
     * The host CMakeLists.txt defines mock libraries (esp_idf_mocks, hal_mocks, etc.)
     * which are ONLY available in the test/host build tree.
     *
     * The firmware build (top-level CMakeLists.txt using idf.py) uses
     * idf_component_register() which never references these targets.
     *
     * We verify by confirming the production CMAKE files (which use
     * idf_component_register) don't reference any mock/fake target name.
     */
    const char *project_root = NULL;

#ifdef PROJECT_ROOT_PATH
    project_root = PROJECT_ROOT_PATH;
#else
    project_root = "../..";
#endif

    /* Verify that the main CMakeLists.txt (firmware) only uses idf_component_register
     * and does not contain add_library calls for mock targets */
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/CMakeLists.txt", project_root);

    FILE *f = fopen(filepath, "r");
    if (!f) {
        TEST_IGNORE_MESSAGE("Top-level CMakeLists.txt not found from working dir");
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        /* Production firmware CMakeLists should not define mock libraries */
        TEST_ASSERT_NULL_MESSAGE(
            strstr(line, "add_library(hal_mocks"),
            "Firmware CMakeLists.txt defines hal_mocks — doubles must be test-only");
        TEST_ASSERT_NULL_MESSAGE(
            strstr(line, "add_library(esp_idf_mocks"),
            "Firmware CMakeLists.txt defines esp_idf_mocks — doubles must be test-only");
    }
    fclose(f);
}

/* ---------------------------------------------------------------------------
 * Main
 * --------------------------------------------------------------------------- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pbt_min_trials_is_defined_and_correct);
    RUN_TEST(test_pbt_seed_is_defined);
    RUN_TEST(test_no_doubles_in_production_cmake);
    RUN_TEST(test_all_executables_discovered_by_ctest);
    RUN_TEST(test_doubles_are_test_only_targets);
    return UNITY_END();
}
