#include "lut_interp.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static int nearly_equal(double actual, double expected, double epsilon)
{
    return fabs(actual - expected) <= epsilon;
}

static void test_simple_linear(void)
{
    const double x_table[] = {0.0, 10.0};
    const double y_table[] = {0.0, 100.0};
    lut_interp_t lut;
    double y = -1.0;

    assert(lut_interp_init(&lut, x_table, y_table, 2U) == LUT_OK);
    assert(lut_interp_linear(&lut, 5.0, &y) == LUT_OK);
    assert(nearly_equal(y, 50.0, 1.0e-12));
}

static void test_multiple_points(void)
{
    const double x_table[] = {0.0, 2.0, 5.0, 10.0};
    const double y_table[] = {0.0, 4.0, 25.0, 100.0};
    lut_interp_t lut;
    double y = -1.0;

    assert(lut_interp_init(&lut, x_table, y_table, 4U) == LUT_OK);
    assert(lut_interp_linear(&lut, 3.5, &y) == LUT_OK);
    assert(nearly_equal(y, 14.5, 1.0e-12));
    assert(lut_interp_linear(&lut, 7.5, &y) == LUT_OK);
    assert(nearly_equal(y, 62.5, 1.0e-12));
}

static void test_endpoints(void)
{
    const double x_table[] = {-2.0, 1.0, 8.0};
    const double y_table[] = {3.0, 6.0, -4.0};
    lut_interp_t lut;
    double y = 0.0;

    assert(lut_interp_init(&lut, x_table, y_table, 3U) == LUT_OK);
    assert(lut_interp_linear(&lut, -2.0, &y) == LUT_OK);
    assert(y == 3.0);
    assert(lut_interp_linear(&lut, 8.0, &y) == LUT_OK);
    assert(y == -4.0);
}

static void test_invalid_initialization(void)
{
    const double x_valid[] = {0.0, 1.0};
    const double y_valid[] = {0.0, 1.0};
    const double y_three[] = {0.0, 1.0, 2.0};
    const double x_duplicate[] = {0.0, 1.0, 1.0};
    const double x_decreasing[] = {0.0, 2.0, 1.0};
    const double x_nonfinite[] = {0.0, NAN};
    const double y_nonfinite[] = {0.0, INFINITY};
    lut_interp_t lut = {x_valid, y_valid, 2U};

    assert(lut_interp_init(0, x_valid, y_valid, 2U) == LUT_ERROR_NULL);
    assert(lut_interp_init(&lut, 0, y_valid, 2U) == LUT_ERROR_NULL);
    assert(lut_interp_init(&lut, x_valid, 0, 2U) == LUT_ERROR_NULL);
    assert(lut_interp_init(&lut, x_valid, y_valid, 0U) == LUT_ERROR_SIZE);
    assert(lut_interp_init(&lut, x_valid, y_valid, 1U) == LUT_ERROR_SIZE);
    assert(lut_interp_init(&lut, x_duplicate, y_three, 3U) == LUT_ERROR_ORDER);
    assert(lut_interp_init(&lut, x_decreasing, y_three, 3U) == LUT_ERROR_ORDER);
    assert(lut_interp_init(&lut, x_nonfinite, y_valid, 2U) ==
           LUT_ERROR_NONFINITE);
    assert(lut_interp_init(&lut, x_valid, y_nonfinite, 2U) ==
           LUT_ERROR_NONFINITE);
    assert(lut.x_table == x_valid && lut.y_table == y_valid && lut.size == 2U);
}

static void test_query_errors(void)
{
    const double x_table[] = {0.0, 10.0};
    const double y_table[] = {0.0, 100.0};
    lut_interp_t lut;
    lut_interp_t invalid_lut = {0, 0, 0U};
    double y = 123.0;

    assert(lut_interp_init(&lut, x_table, y_table, 2U) == LUT_OK);
    assert(lut_interp_linear(0, 5.0, &y) == LUT_ERROR_NULL);
    assert(lut_interp_linear(&lut, 5.0, 0) == LUT_ERROR_NULL);
    assert(lut_interp_linear(&invalid_lut, 5.0, &y) == LUT_ERROR_NULL);
    assert(lut_interp_linear(&lut, -0.1, &y) == LUT_ERROR_RANGE);
    assert(lut_interp_linear(&lut, 10.1, &y) == LUT_ERROR_RANGE);
    assert(lut_interp_linear(&lut, NAN, &y) == LUT_ERROR_NONFINITE);
    assert(y == 123.0);
}

static void test_floating_point_tolerance(void)
{
    const double x_table[] = {0.0, 0.3};
    const double y_table[] = {0.0, 0.9};
    lut_interp_t lut;
    double y = 0.0;

    assert(lut_interp_init(&lut, x_table, y_table, 2U) == LUT_OK);
    assert(lut_interp_linear(&lut, 0.1, &y) == LUT_OK);
    assert(nearly_equal(y, 0.3, 1.0e-12));
}

int main(void)
{
    test_simple_linear();
    test_multiple_points();
    test_endpoints();
    test_invalid_initialization();
    test_query_errors();
    test_floating_point_tolerance();
    puts("lut_interp tests passed");
    return 0;
}
