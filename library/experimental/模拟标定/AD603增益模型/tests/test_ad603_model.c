#include "ad603_model.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static int nearly_equal(double actual, double expected, double epsilon)
{
    return fabs(actual - expected) <= epsilon;
}

static void test_increasing_forward_and_reverse(void)
{
    const double voltage[] = {0.2, 0.5, 1.0};
    const double gain_db[] = {-20.0, -5.0, 20.0};
    ad603_model_t model;
    double result = 0.0;

    assert(ad603_model_init(&model, voltage, gain_db, 3U) == AD603_OK);
    assert(model.voltage_table == voltage);
    assert(model.gain_table == gain_db);
    assert(model.size == 3U);
    assert(model.gain_direction == AD603_GAIN_INCREASING);

    assert(ad603_model_gain_from_voltage(&model, 0.2, &result) == AD603_OK);
    assert(nearly_equal(result, -20.0, 1.0e-12));
    assert(ad603_model_gain_from_voltage(&model, 0.5, &result) == AD603_OK);
    assert(nearly_equal(result, -5.0, 1.0e-12));
    assert(ad603_model_gain_from_voltage(&model, 1.0, &result) == AD603_OK);
    assert(nearly_equal(result, 20.0, 1.0e-12));
    assert(ad603_model_gain_from_voltage(&model, 0.35, &result) == AD603_OK);
    assert(nearly_equal(result, -12.5, 1.0e-12));
    assert(ad603_model_gain_from_voltage(&model, 0.75, &result) == AD603_OK);
    assert(nearly_equal(result, 7.5, 1.0e-12));

    assert(ad603_model_voltage_from_gain(&model, -12.5, &result) == AD603_OK);
    assert(nearly_equal(result, 0.35, 1.0e-12));
    assert(ad603_model_voltage_from_gain(&model, 7.5, &result) == AD603_OK);
    assert(nearly_equal(result, 0.75, 1.0e-12));
}

static void test_decreasing_reverse(void)
{
    const double voltage[] = {0.2, 0.5, 1.0};
    const double gain_db[] = {20.0, -5.0, -20.0};
    ad603_model_t model;
    double result = 0.0;

    assert(ad603_model_init(&model, voltage, gain_db, 3U) == AD603_OK);
    assert(model.gain_direction == AD603_GAIN_DECREASING);
    assert(ad603_model_gain_from_voltage(&model, 0.35, &result) == AD603_OK);
    assert(nearly_equal(result, 7.5, 1.0e-12));
    assert(ad603_model_voltage_from_gain(&model, 7.5, &result) == AD603_OK);
    assert(nearly_equal(result, 0.35, 1.0e-12));
    assert(ad603_model_voltage_from_gain(&model, -12.5, &result) == AD603_OK);
    assert(nearly_equal(result, 0.75, 1.0e-12));
}

static void test_non_monotonic_reverse_failure(void)
{
    const double voltage[] = {0.2, 0.5, 1.0};
    const double gain_db[] = {-20.0, 10.0, 0.0};
    ad603_model_t model;
    double result = 123.0;

    assert(ad603_model_init(&model, voltage, gain_db, 3U) == AD603_OK);
    assert(model.gain_direction == AD603_GAIN_NON_MONOTONIC);
    assert(ad603_model_gain_from_voltage(&model, 0.75, &result) == AD603_OK);
    assert(nearly_equal(result, 5.0, 1.0e-12));
    result = 123.0;
    assert(ad603_model_voltage_from_gain(&model, 5.0, &result) ==
           AD603_NON_MONOTONIC);
    assert(result == 123.0);
}

static void test_out_of_range(void)
{
    const double voltage[] = {0.2, 0.5, 1.0};
    const double gain_db[] = {-20.0, -5.0, 20.0};
    ad603_model_t model;
    double result = 123.0;

    assert(ad603_model_init(&model, voltage, gain_db, 3U) == AD603_OK);
    assert(ad603_model_gain_from_voltage(&model, 0.1, &result) ==
           AD603_OUT_OF_RANGE);
    assert(result == 123.0);
    assert(ad603_model_gain_from_voltage(&model, 1.1, &result) ==
           AD603_OUT_OF_RANGE);
    assert(result == 123.0);
    assert(ad603_model_voltage_from_gain(&model, -21.0, &result) ==
           AD603_OUT_OF_RANGE);
    assert(result == 123.0);
    assert(ad603_model_voltage_from_gain(&model, 21.0, &result) ==
           AD603_OUT_OF_RANGE);
    assert(result == 123.0);
}

static void test_unit_conversions(void)
{
    double result = 0.0;

    assert(ad603_gain_db_to_linear(20.0, &result) == AD603_OK);
    assert(nearly_equal(result, 10.0, 1.0e-12));
    assert(ad603_gain_db_to_linear(-20.0, &result) == AD603_OK);
    assert(nearly_equal(result, 0.1, 1.0e-12));
    assert(ad603_gain_linear_to_db(10.0, &result) == AD603_OK);
    assert(nearly_equal(result, 20.0, 1.0e-12));
    assert(ad603_gain_linear_to_db(0.1, &result) == AD603_OK);
    assert(nearly_equal(result, -20.0, 1.0e-12));
}

static void test_invalid_inputs(void)
{
    const double voltage[] = {0.2, 0.5, 1.0};
    const double voltage_duplicate[] = {0.2, 0.5, 0.5};
    const double voltage_decreasing[] = {0.2, 1.0, 0.5};
    const double voltage_nonfinite[] = {0.2, NAN, 1.0};
    const double gain_db[] = {-20.0, -5.0, 20.0};
    const double gain_nonfinite[] = {-20.0, INFINITY, 20.0};
    ad603_model_t model = {voltage, gain_db, 3U, AD603_GAIN_INCREASING};
    double result = 123.0;

    assert(ad603_model_init(0, voltage, gain_db, 3U) == AD603_NULL);
    assert(ad603_model_init(&model, 0, gain_db, 3U) == AD603_NULL);
    assert(ad603_model_init(&model, voltage, 0, 3U) == AD603_NULL);
    assert(ad603_model_init(&model, voltage, gain_db, 0U) ==
           AD603_INVALID_SIZE);
    assert(ad603_model_init(&model, voltage, gain_db, 1U) ==
           AD603_INVALID_SIZE);
    assert(ad603_model_init(&model, voltage_duplicate, gain_db, 3U) ==
           AD603_INVALID_DATA);
    assert(ad603_model_init(&model, voltage_decreasing, gain_db, 3U) ==
           AD603_INVALID_DATA);
    assert(ad603_model_init(&model, voltage_nonfinite, gain_db, 3U) ==
           AD603_INVALID_DATA);
    assert(ad603_model_init(&model, voltage, gain_nonfinite, 3U) ==
           AD603_INVALID_DATA);
    assert(model.voltage_table == voltage && model.gain_table == gain_db &&
           model.size == 3U &&
           model.gain_direction == AD603_GAIN_INCREASING);

    assert(ad603_model_gain_from_voltage(0, 0.5, &result) == AD603_NULL);
    assert(ad603_model_gain_from_voltage(&model, 0.5, 0) == AD603_NULL);
    assert(ad603_model_voltage_from_gain(0, 0.0, &result) == AD603_NULL);
    assert(ad603_model_voltage_from_gain(&model, 0.0, 0) == AD603_NULL);
    assert(ad603_model_gain_from_voltage(&model, NAN, &result) ==
           AD603_INVALID_DATA);
    assert(ad603_model_voltage_from_gain(&model, NAN, &result) ==
           AD603_INVALID_DATA);
    assert(result == 123.0);

    assert(ad603_gain_db_to_linear(NAN, &result) == AD603_INVALID_DATA);
    assert(ad603_gain_db_to_linear(0.0, 0) == AD603_NULL);
    assert(ad603_gain_linear_to_db(0.0, &result) == AD603_INVALID_DATA);
    assert(ad603_gain_linear_to_db(-1.0, &result) == AD603_INVALID_DATA);
    assert(ad603_gain_linear_to_db(INFINITY, &result) == AD603_INVALID_DATA);
    assert(ad603_gain_linear_to_db(1.0, 0) == AD603_NULL);
    assert(result == 123.0);
}

int main(void)
{
    test_increasing_forward_and_reverse();
    test_decreasing_reverse();
    test_non_monotonic_reverse_failure();
    test_out_of_range();
    test_unit_conversions();
    test_invalid_inputs();
    puts("ad603_model tests passed");
    return 0;
}
