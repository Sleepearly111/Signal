#include "signal_stats.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOUNDARY_COUNT ((size_t)65536u)
#define RANDOM_COUNT ((size_t)257u)

static size_t histogram[SIGNAL_STATS_U12_BIN_COUNT];
static uint16_t boundary_u12[BOUNDARY_COUNT];
static float boundary_f32[BOUNDARY_COUNT];
static float boundary_workspace[BOUNDARY_COUNT];

static int failures = 0;

#define CHECK_TRUE(condition)                                                          \
    do {                                                                               \
        if (!(condition)) {                                                            \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);      \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

#define CHECK_STATUS(expected, expression) CHECK_TRUE((expression) == (expected))

static int compare_f32(const void *left, const void *right)
{
    const float a = *(const float *)left;
    const float b = *(const float *)right;
    return (a > b) - (a < b);
}

static size_t reference_rank(size_t count, float percentile)
{
    const double scaled = (double)percentile * (double)count;
    size_t rank;

    if (percentile <= 0.0f) {
        return 0u;
    }
    rank = (size_t)scaled;
    if ((double)rank < scaled) {
        ++rank;
    }
    return rank - 1u;
}

static unsigned int lcg_next(unsigned int *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static void test_empty_input(void)
{
    float result = 123.0f;
    float workspace = 0.0f;

    CHECK_STATUS(SIGNAL_STATS_STATUS_EMPTY_INPUT,
                 signal_stats_percentile_u12(NULL, 0u, 0.5f, histogram,
                                             SIGNAL_STATS_U12_BIN_COUNT, &result));
    CHECK_STATUS(SIGNAL_STATS_STATUS_EMPTY_INPUT,
                 signal_stats_percentile_f32(NULL, 0u, 0.5f, &workspace, 1u, &result));
    CHECK_STATUS(SIGNAL_STATS_STATUS_EMPTY_INPUT,
                 signal_stats_mid_fraction_u16(NULL, 0u, 0.0f, 1.0f, &result));
    CHECK_STATUS(SIGNAL_STATS_STATUS_EMPTY_INPUT,
                 signal_stats_mid_fraction_f32(NULL, 0u, 0.0f, 1.0f, &result));
    CHECK_TRUE(result == 123.0f);
}

static void test_single_element(void)
{
    const uint16_t u12[] = {2048u};
    const float f32[] = {-3.5f};
    float workspace[1];
    float result = 0.0f;

    CHECK_STATUS(SIGNAL_STATS_STATUS_OK,
                 signal_stats_percentile_u12(u12, 1u, 0.0f, histogram,
                                             SIGNAL_STATS_U12_BIN_COUNT, &result));
    CHECK_TRUE(result == 2048.0f);
    CHECK_STATUS(SIGNAL_STATS_STATUS_OK,
                 signal_stats_percentile_f32(f32, 1u, 1.0f, workspace, 1u, &result));
    CHECK_TRUE(result == -3.5f);
}

static void test_ordered_arrays(void)
{
    const uint16_t u12[] = {0u, 1u, 2u, 3u, 4u};
    const float f32[] = {-2.0f, -1.0f, 0.0f, 1.0f, 2.0f};
    const uint16_t fraction_u16[] = {0u, 3u, 4u, 5u, 6u, 7u, 10u};
    const float fraction_f32[] = {0.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 10.0f};
    float workspace[5];
    float result = 0.0f;

    CHECK_STATUS(SIGNAL_STATS_STATUS_OK,
                 signal_stats_percentile_u12(u12, 5u, 0.5f, histogram,
                                             SIGNAL_STATS_U12_BIN_COUNT, &result));
    CHECK_TRUE(result == 2.0f);
    CHECK_STATUS(SIGNAL_STATS_STATUS_OK,
                 signal_stats_percentile_f32(f32, 5u, 0.75f, workspace, 5u, &result));
    CHECK_TRUE(result == 1.0f);
    CHECK_STATUS(SIGNAL_STATS_STATUS_OK,
                 signal_stats_mid_fraction_u16(fraction_u16, 7u, 0.0f, 10.0f, &result));
    CHECK_TRUE(result > 0.42f && result < 0.44f);
    CHECK_STATUS(SIGNAL_STATS_STATUS_OK,
                 signal_stats_mid_fraction_f32(fraction_f32, 7u, 0.0f, 10.0f, &result));
    CHECK_TRUE(result > 0.42f && result < 0.44f);
}

static void test_random_array(void)
{
    uint16_t u12[RANDOM_COUNT];
    float f32[RANDOM_COUNT];
    float sorted[RANDOM_COUNT];
    float workspace[RANDOM_COUNT];
    unsigned int state = 0x5A17u;
    const float percentile = 0.83f;
    float result = 0.0f;
    size_t i;

    for (i = 0u; i < RANDOM_COUNT; ++i) {
        const unsigned int value = lcg_next(&state);
        u12[i] = (uint16_t)(value & 0x0FFFu);
        f32[i] = (float)((int)(value % 20001u) - 10000) / 32.0f;
        sorted[i] = f32[i];
    }
    qsort(sorted, RANDOM_COUNT, sizeof(sorted[0]), compare_f32);

    CHECK_STATUS(SIGNAL_STATS_STATUS_OK,
                 signal_stats_percentile_u12(u12, RANDOM_COUNT, percentile, histogram,
                                             SIGNAL_STATS_U12_BIN_COUNT, &result));
    {
        uint16_t sorted_u12[RANDOM_COUNT];
        memcpy(sorted_u12, u12, sizeof(sorted_u12));
        for (i = 1u; i < RANDOM_COUNT; ++i) {
            const uint16_t key = sorted_u12[i];
            size_t j = i;
            while (j > 0u && sorted_u12[j - 1u] > key) {
                sorted_u12[j] = sorted_u12[j - 1u];
                --j;
            }
            sorted_u12[j] = key;
        }
        CHECK_TRUE(result == (float)sorted_u12[reference_rank(RANDOM_COUNT, percentile)]);
    }

    CHECK_STATUS(SIGNAL_STATS_STATUS_OK,
                 signal_stats_percentile_f32(f32, RANDOM_COUNT, percentile, workspace,
                                             RANDOM_COUNT, &result));
    CHECK_TRUE(result == sorted[reference_rank(RANDOM_COUNT, percentile)]);
}

static void test_boundary_length(void)
{
    float result = 0.0f;
    size_t i;

    for (i = 0u; i < BOUNDARY_COUNT; ++i) {
        boundary_u12[i] = (uint16_t)(i % SIGNAL_STATS_U12_BIN_COUNT);
        boundary_f32[i] = (float)i;
    }

    CHECK_STATUS(SIGNAL_STATS_STATUS_OK,
                 signal_stats_percentile_u12(boundary_u12, BOUNDARY_COUNT, 0.5f, histogram,
                                             SIGNAL_STATS_U12_BIN_COUNT, &result));
    CHECK_TRUE(result == 2047.0f);
    CHECK_STATUS(SIGNAL_STATS_STATUS_OK,
                 signal_stats_percentile_f32(boundary_f32, BOUNDARY_COUNT, 0.5f,
                                             boundary_workspace, BOUNDARY_COUNT, &result));
    CHECK_TRUE(result == 32767.0f);
}

static void test_validation_errors(void)
{
    const uint16_t invalid_u12[] = {4096u};
    const float valid_f32[] = {1.0f, 2.0f};
    const float non_finite[] = {1.0f, INFINITY};
    float workspace[2];
    float result = 91.0f;

    CHECK_STATUS(SIGNAL_STATS_STATUS_WORKSPACE_TOO_SMALL,
                 signal_stats_percentile_u12(invalid_u12, 1u, 0.5f, histogram,
                                             SIGNAL_STATS_U12_BIN_COUNT - 1u, &result));
    CHECK_STATUS(SIGNAL_STATS_STATUS_SAMPLE_OUT_OF_RANGE,
                 signal_stats_percentile_u12(invalid_u12, 1u, 0.5f, histogram,
                                             SIGNAL_STATS_U12_BIN_COUNT, &result));
    CHECK_STATUS(SIGNAL_STATS_STATUS_WORKSPACE_TOO_SMALL,
                 signal_stats_percentile_f32(valid_f32, 2u, 0.5f, workspace, 1u, &result));
    CHECK_STATUS(SIGNAL_STATS_STATUS_PERCENTILE_OUT_OF_RANGE,
                 signal_stats_percentile_f32(valid_f32, 2u, 1.1f, workspace, 2u, &result));
    CHECK_STATUS(SIGNAL_STATS_STATUS_NON_FINITE_INPUT,
                 signal_stats_percentile_f32(non_finite, 2u, 0.5f, workspace, 2u, &result));
    CHECK_TRUE(result == 91.0f);
}

int main(void)
{
    test_empty_input();
    test_single_element();
    test_ordered_arrays();
    test_random_array();
    test_boundary_length();
    test_validation_errors();

    if (failures != 0) {
        fprintf(stderr, "DSP-STATS-001: %d assertion(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("DSP-STATS-001: all host tests passed");
    return EXIT_SUCCESS;
}
