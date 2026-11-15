#include "beat_fll_controller.h"

#include <math.h>
#include <stddef.h>

static bool beat_fll_config_is_valid(const beat_fll_config_t *config)
{
    return config != NULL &&
           isfinite(config->lock_hz) &&
           isfinite(config->unlock_hz) &&
           isfinite(config->coarse_threshold_hz) &&
           isfinite(config->fine_reverse_margin_hz) &&
           isfinite(config->coarse_reverse_margin_hz) &&
           isfinite(config->fine_step) &&
           isfinite(config->coarse_step) &&
           config->lock_hz >= 0.0f &&
           config->unlock_hz > config->lock_hz &&
           config->coarse_threshold_hz >= config->unlock_hz &&
           config->fine_reverse_margin_hz >= 0.0f &&
           config->coarse_reverse_margin_hz >= 0.0f &&
           config->fine_step > 0.0f &&
           config->coarse_step >= config->fine_step &&
           config->lock_confirm_count > 0U &&
           config->unlock_confirm_count > 0U &&
           (config->initial_direction == -1 ||
            config->initial_direction == 1);
}

static void beat_fll_clear_search_history(beat_fll_controller_t *controller)
{
    controller->previous_beat_hz = 0.0f;
    controller->lock_count = 0U;
    controller->has_previous = false;
    controller->direction = controller->config.initial_direction;
}

static void beat_fll_set_output(
    const beat_fll_controller_t *controller,
    beat_fll_output_t *output
)
{
    output->state = controller->state;
    output->adjustment = 0.0f;
    output->state_changed = false;
    output->observation_used = false;
}

beat_fll_status_t beat_fll_init(
    beat_fll_controller_t *controller,
    const beat_fll_config_t *config
)
{
    if (controller == NULL || config == NULL) {
        return BEAT_FLL_BAD_ARGUMENT;
    }
    if (!beat_fll_config_is_valid(config)) {
        return BEAT_FLL_BAD_CONFIG;
    }

    controller->config = *config;
    controller->state = BEAT_FLL_IDLE;
    controller->unlock_count = 0U;
    beat_fll_clear_search_history(controller);
    return BEAT_FLL_OK;
}

beat_fll_status_t beat_fll_start(beat_fll_controller_t *controller)
{
    if (controller == NULL) {
        return BEAT_FLL_BAD_ARGUMENT;
    }

    controller->state = BEAT_FLL_SEARCH;
    controller->unlock_count = 0U;
    beat_fll_clear_search_history(controller);
    return BEAT_FLL_OK;
}

beat_fll_status_t beat_fll_stop(beat_fll_controller_t *controller)
{
    if (controller == NULL) {
        return BEAT_FLL_BAD_ARGUMENT;
    }

    controller->state = BEAT_FLL_IDLE;
    controller->unlock_count = 0U;
    beat_fll_clear_search_history(controller);
    return BEAT_FLL_OK;
}

static void beat_fll_update_search(
    beat_fll_controller_t *controller,
    const beat_fll_observation_t *observation,
    beat_fll_output_t *output
)
{
    float margin;
    float step;

    if (observation->beat_hz < controller->config.lock_hz) {
        if (controller->lock_count < UINT16_MAX) {
            ++controller->lock_count;
        }
        controller->has_previous = false;
        if (controller->lock_count >= controller->config.lock_confirm_count) {
            controller->state = BEAT_FLL_HOLD;
            controller->unlock_count = 0U;
            output->state = BEAT_FLL_HOLD;
            output->state_changed = true;
        }
        return;
    }

    controller->lock_count = 0U;
    if (observation->beat_hz >= controller->config.coarse_threshold_hz) {
        margin = controller->config.coarse_reverse_margin_hz;
        step = controller->config.coarse_step;
    } else {
        margin = controller->config.fine_reverse_margin_hz;
        step = controller->config.fine_step;
    }

    if (controller->has_previous &&
        observation->beat_hz > controller->previous_beat_hz + margin) {
        controller->direction = (int8_t)-controller->direction;
    }
    controller->previous_beat_hz = observation->beat_hz;
    controller->has_previous = true;
    output->adjustment = (float)controller->direction * step;
}

static void beat_fll_update_hold(
    beat_fll_controller_t *controller,
    const beat_fll_observation_t *observation,
    beat_fll_output_t *output
)
{
    if (observation->beat_hz > controller->config.unlock_hz) {
        if (controller->unlock_count < UINT16_MAX) {
            ++controller->unlock_count;
        }
        if (controller->unlock_count >= controller->config.unlock_confirm_count) {
            controller->state = BEAT_FLL_SEARCH;
            controller->unlock_count = 0U;
            beat_fll_clear_search_history(controller);
            output->state = BEAT_FLL_SEARCH;
            output->state_changed = true;
        }
    } else {
        controller->unlock_count = 0U;
    }
}

beat_fll_status_t beat_fll_update(
    beat_fll_controller_t *controller,
    const beat_fll_observation_t *observation,
    beat_fll_output_t *output
)
{
    if (controller == NULL || observation == NULL || output == NULL) {
        return BEAT_FLL_BAD_ARGUMENT;
    }

    beat_fll_set_output(controller, output);
    if (!observation->fresh || !observation->valid ||
        !isfinite(observation->beat_hz) || observation->beat_hz < 0.0f) {
        return BEAT_FLL_OK;
    }

    output->observation_used = true;
    if (controller->state == BEAT_FLL_SEARCH) {
        beat_fll_update_search(controller, observation, output);
    } else if (controller->state == BEAT_FLL_HOLD) {
        beat_fll_update_hold(controller, observation, output);
    }
    return BEAT_FLL_OK;
}
