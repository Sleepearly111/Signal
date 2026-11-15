#include "beat_fll_controller.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static beat_fll_config_t test_config(void)
{
    beat_fll_config_t config = {
        0.2f, 0.4f, 1.0f, 0.05f, 0.2f,
        0.1f, 0.5f, 3U, 2U, 1
    };
    return config;
}

static beat_fll_output_t update(
    beat_fll_controller_t *controller,
    float beat_hz,
    bool fresh,
    bool valid
)
{
    beat_fll_observation_t observation = {beat_hz, fresh, valid};
    beat_fll_output_t output;
    assert(beat_fll_update(controller, &observation, &output) == BEAT_FLL_OK);
    return output;
}

static void assert_float(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.00001f);
}

static void test_config_validation(void)
{
    beat_fll_controller_t controller;
    beat_fll_config_t config = test_config();

    assert(beat_fll_init(NULL, &config) == BEAT_FLL_BAD_ARGUMENT);
    config.unlock_hz = config.lock_hz;
    assert(beat_fll_init(&controller, &config) == BEAT_FLL_BAD_CONFIG);
    config = test_config();
    config.initial_direction = 0;
    assert(beat_fll_init(&controller, &config) == BEAT_FLL_BAD_CONFIG);
}

static void test_start_stop_and_stale_input(void)
{
    beat_fll_controller_t controller;
    beat_fll_config_t config = test_config();
    beat_fll_output_t output;

    assert(beat_fll_init(&controller, &config) == BEAT_FLL_OK);
    assert(controller.state == BEAT_FLL_IDLE);
    assert(beat_fll_start(&controller) == BEAT_FLL_OK);
    output = update(&controller, 2.0f, false, true);
    assert(!output.observation_used);
    assert_float(output.adjustment, 0.0f);
    assert(beat_fll_stop(&controller) == BEAT_FLL_OK);
    assert(controller.state == BEAT_FLL_IDLE);
}

static void test_coarse_fine_and_reversal(void)
{
    beat_fll_controller_t controller;
    beat_fll_config_t config = test_config();
    beat_fll_output_t output;

    assert(beat_fll_init(&controller, &config) == BEAT_FLL_OK);
    assert(beat_fll_start(&controller) == BEAT_FLL_OK);

    output = update(&controller, 2.0f, true, true);
    assert_float(output.adjustment, 0.5f);
    output = update(&controller, 2.1f, true, true);
    assert_float(output.adjustment, 0.5f);
    output = update(&controller, 2.31f, true, true);
    assert_float(output.adjustment, -0.5f);
    output = update(&controller, 0.7f, true, true);
    assert_float(output.adjustment, -0.1f);
    output = update(&controller, 0.76f, true, true);
    assert_float(output.adjustment, 0.1f);
}

static void test_lock_requires_fresh_consecutive_samples(void)
{
    beat_fll_controller_t controller;
    beat_fll_config_t config = test_config();
    beat_fll_output_t output;

    assert(beat_fll_init(&controller, &config) == BEAT_FLL_OK);
    assert(beat_fll_start(&controller) == BEAT_FLL_OK);
    output = update(&controller, 0.1f, true, true);
    assert(output.state == BEAT_FLL_SEARCH);
    (void)update(&controller, 0.1f, false, true);
    output = update(&controller, 0.1f, true, true);
    assert(output.state == BEAT_FLL_SEARCH);
    output = update(&controller, 0.1f, true, true);
    assert(output.state == BEAT_FLL_HOLD);
    assert(output.state_changed);
    assert_float(output.adjustment, 0.0f);
}

static void test_hold_unlock_hysteresis(void)
{
    beat_fll_controller_t controller;
    beat_fll_config_t config = test_config();
    beat_fll_output_t output;

    assert(beat_fll_init(&controller, &config) == BEAT_FLL_OK);
    assert(beat_fll_start(&controller) == BEAT_FLL_OK);
    (void)update(&controller, 0.1f, true, true);
    (void)update(&controller, 0.1f, true, true);
    (void)update(&controller, 0.1f, true, true);

    output = update(&controller, 0.5f, true, true);
    assert(output.state == BEAT_FLL_HOLD);
    output = update(&controller, 0.8f, false, true);
    assert(output.state == BEAT_FLL_HOLD);
    output = update(&controller, 0.3f, true, true);
    assert(output.state == BEAT_FLL_HOLD);
    (void)update(&controller, 0.5f, true, true);
    output = update(&controller, 0.5f, true, true);
    assert(output.state == BEAT_FLL_SEARCH);
    assert(output.state_changed);

    output = update(&controller, 1.5f, true, false);
    assert(!output.observation_used);
    assert_float(output.adjustment, 0.0f);
}

int main(void)
{
    test_config_validation();
    test_start_stop_and_stale_input();
    test_coarse_fine_and_reversal();
    test_lock_requires_fresh_consecutive_samples();
    test_hold_unlock_hysteresis();
    puts("beat_fll_controller tests passed");
    return 0;
}
