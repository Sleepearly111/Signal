#include "transfer_response.h"

#include <assert.h>
#include <stdio.h>

static float absolute(float value)
{
    return (value < 0.0F) ? -value : value;
}

static transfer_response_config_t make_config(void)
{
    transfer_response_config_t config = {0.1F, 10.0F, 3U};
    return config;
}

static void test_prepare_and_quality_rejection(void)
{
    const transfer_response_measurement_t measurements[] = {
        {500.0F, 1.0F, 1.0F, true, false, false},
        {1000.0F, 1.0F, 0.5F, true, false, false},
        {2000.0F, 0.01F, 0.01F, true, false, false},
        {4000.0F, 1.0F, 0.25F, true, false, false},
        {8000.0F, 1.0F, 0.1F, true, false, true}
    };
    transfer_response_point_t points[5];
    transfer_response_summary_t summary;
    transfer_response_config_t config = make_config();

    assert(transfer_response_prepare(
               measurements,
               5U,
               &config,
               points,
               5U,
               &summary
           ) == TRANSFER_RESPONSE_OK);
    assert(summary.valid_count == 3U);
    assert(summary.rejected_count == 2U);
    assert(summary.peak_index == 0U);
    assert(summary.notch_index == 2U);
    assert(absolute(points[0].gain_db) < 0.0001F);
    assert(absolute(points[1].gain_db + 6.0206F) < 0.001F);
    assert(absolute(points[2].gain_db + 12.0412F) < 0.001F);
}

static void test_log_frequency_crossings(void)
{
    const transfer_response_point_t points[] = {
        {1.0F, 0.25F, -12.0F},
        {2.0F, 0.5F, -6.0F},
        {4.0F, 1.0F, 0.0F},
        {8.0F, 0.5F, -6.0F},
        {16.0F, 0.25F, -12.0F}
    };
    float crossings[2];
    size_t count = 0U;

    assert(transfer_response_find_level_crossings(
               points,
               5U,
               3.0F,
               crossings,
               2U,
               &count
           ) == TRANSFER_RESPONSE_OK);
    assert(count == 2U);
    assert(absolute(crossings[0] - 2.828427F) < 0.001F);
    assert(absolute(crossings[1] - 5.656854F) < 0.001F);
}

static void test_invalid_and_insufficient(void)
{
    transfer_response_measurement_t measurements[] = {
        {500.0F, 1.0F, 1.0F, true, false, false},
        {1000.0F, 1.0F, 0.5F, false, false, false},
        {2000.0F, 1.0F, 0.25F, false, false, false}
    };
    transfer_response_point_t points[3];
    transfer_response_summary_t summary;
    transfer_response_config_t config = make_config();

    assert(transfer_response_prepare(
               measurements,
               3U,
               &config,
               points,
               3U,
               &summary
           ) == TRANSFER_RESPONSE_INSUFFICIENT_VALID_POINTS);
    assert(summary.valid_count == 1U);
    measurements[2].frequency_hz = 900.0F;
    assert(transfer_response_prepare(
               measurements,
               3U,
               &config,
               points,
               3U,
               &summary
           ) == TRANSFER_RESPONSE_INVALID_DATA);
    assert(transfer_response_prepare(
               measurements,
               3U,
               &config,
               points,
               2U,
               &summary
           ) == TRANSFER_RESPONSE_OUTPUT_TOO_SMALL);
}

int main(void)
{
    test_prepare_and_quality_rejection();
    test_log_frequency_crossings();
    test_invalid_and_insufficient();
    (void)puts("transfer_response tests passed");
    return 0;
}
