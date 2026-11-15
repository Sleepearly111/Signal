#ifndef BEAT_FLL_CONTROLLER_H
#define BEAT_FLL_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BEAT_FLL_IDLE = 0,
    BEAT_FLL_SEARCH,
    BEAT_FLL_HOLD
} beat_fll_state_t;

typedef enum {
    BEAT_FLL_OK = 0,
    BEAT_FLL_BAD_ARGUMENT,
    BEAT_FLL_BAD_CONFIG
} beat_fll_status_t;

typedef struct {
    float lock_hz;
    float unlock_hz;
    float coarse_threshold_hz;
    float fine_reverse_margin_hz;
    float coarse_reverse_margin_hz;
    float fine_step;
    float coarse_step;
    uint16_t lock_confirm_count;
    uint16_t unlock_confirm_count;
    int8_t initial_direction;
} beat_fll_config_t;

typedef struct {
    float beat_hz;
    bool fresh;
    bool valid;
} beat_fll_observation_t;

typedef struct {
    beat_fll_state_t state;
    float adjustment;
    bool state_changed;
    bool observation_used;
} beat_fll_output_t;

typedef struct {
    beat_fll_config_t config;
    beat_fll_state_t state;
    float previous_beat_hz;
    uint16_t lock_count;
    uint16_t unlock_count;
    int8_t direction;
    bool has_previous;
} beat_fll_controller_t;

beat_fll_status_t beat_fll_init(
    beat_fll_controller_t *controller,
    const beat_fll_config_t *config
);

beat_fll_status_t beat_fll_start(beat_fll_controller_t *controller);

beat_fll_status_t beat_fll_stop(beat_fll_controller_t *controller);

beat_fll_status_t beat_fll_update(
    beat_fll_controller_t *controller,
    const beat_fll_observation_t *observation,
    beat_fll_output_t *output
);

#endif
