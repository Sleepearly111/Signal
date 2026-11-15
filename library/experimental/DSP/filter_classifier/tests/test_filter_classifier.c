#include "filter_classifier.h"

#include <assert.h>
#include <stdio.h>

static const float frequencies[] = {
    500.0F, 1000.0F, 2000.0F, 4000.0F, 8000.0F, 16000.0F, 20000.0F
};

static filter_classifier_config_t make_config(void)
{
    filter_classifier_config_t config = {-3.0F, -10.0F, 7.0F, 1U};
    return config;
}

static void expect_type(const float *gain, filter_type_t expected)
{
    filter_classifier_result_t result;
    filter_classifier_config_t config = make_config();
    assert(filter_classifier_classify(
               frequencies,
               gain,
               7U,
               &config,
               &result
           ) == FILTER_CLASSIFIER_OK);
    assert(result.type == expected);
    assert(result.confidence_margin_db >= 0.0F);
}

static void test_four_types(void)
{
    const float lp[] = {0.0F, -0.2F, -1.0F, -4.0F, -10.0F, -18.0F, -22.0F};
    const float hp[] = {-22.0F, -18.0F, -10.0F, -4.0F, -1.0F, -0.2F, 0.0F};
    const float bp[] = {-20.0F, -12.0F, -2.0F, 0.0F, -2.0F, -12.0F, -20.0F};
    const float bs[] = {0.0F, -1.0F, -8.0F, -20.0F, -8.0F, -1.0F, 0.0F};

    expect_type(lp, FILTER_TYPE_LOW_PASS);
    expect_type(hp, FILTER_TYPE_HIGH_PASS);
    expect_type(bp, FILTER_TYPE_BAND_PASS);
    expect_type(bs, FILTER_TYPE_BAND_STOP);
}

static void test_unknown_and_invalid(void)
{
    const float flat[] = {-1.0F, -1.0F, -1.0F, -1.0F, -1.0F, -1.0F, -1.0F};
    const float bad_frequencies[] = {
        500.0F, 1000.0F, 900.0F, 4000.0F, 8000.0F, 16000.0F, 20000.0F
    };
    filter_classifier_result_t result;
    filter_classifier_config_t config = make_config();

    assert(filter_classifier_classify(
               frequencies,
               flat,
               7U,
               &config,
               &result
           ) == FILTER_CLASSIFIER_AMBIGUOUS);
    assert(result.type == FILTER_TYPE_UNKNOWN);
    assert(filter_classifier_classify(
               bad_frequencies,
               flat,
               7U,
               &config,
               &result
           ) == FILTER_CLASSIFIER_INVALID_DATA);
    config.edge_window_points = 4U;
    assert(filter_classifier_classify(
               frequencies,
               flat,
               7U,
               &config,
               &result
           ) == FILTER_CLASSIFIER_INVALID_ARGUMENT);
}

int main(void)
{
    test_four_types();
    test_unknown_and_invalid();
    (void)puts("filter_classifier tests passed");
    return 0;
}
