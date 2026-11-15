#include "adva_learn.h"
#include "ADS8688.h"
#include "stm32f4xx_hal.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

extern void DDS_SetFrequency(uint32_t freq_hz);
extern void VGA_SetGain_Linear(float gain);
extern void UI_SendToScreen(const char *fmt, ...);

#define AD637_ADC_INDEX          1U
#define AD637_VPP_FACTOR         2.828f
#define LEARN_TARGET_VPP_MV   2000.0f
#define CAL_AVERAGE_SAMPLES     32U
#define MEAS_AVERAGE_SAMPLES    64U
#define CAL_MAX_ATTEMPTS         4U
#define SETTLE_TIME_MS          30U
#define OUTPUT_SETTLE_TIME_MS  250U
#define FIRST_WARMUP_TIME_MS   1500U
#define FIRST_CHECK_INTERVAL_MS 250U
#define FIRST_STABLE_REL_LIMIT   0.02f
#define FIRST_STABLE_REQUIRED       3U
#define FIRST_CHECK_MAX            16U
#define MIN_VALID_VPP_MV        10.0f
#define CAL_SAFE_GAIN_MAX        4.0f
#define SAFE_IDLE_GAIN           0.01f

/* Temporary preset used while tuning replay/DPLL without repeating the
 * two-phase AD637 sweep after every reset.  KEY0 still starts a fresh scan and
 * replaces this model; KEY1 can replay it immediately after power-up. */
#define PRESET_FILTER_TYPE       FILTER_LOW_PASS
#define PRESET_MODEL_GAIN        0.7607f
#define PRESET_MODEL_F0_HZ   34600.0f
#define PRESET_MODEL_Q           0.150f
#define PRESET_MODEL_ERROR       0.0094f

typedef enum {
    LEARN_IDLE = 0,
    LEARN_CALIBRATE_INPUT,
    LEARN_WAIT_REWIRE,
    LEARN_MEASURE_OUTPUT,
    LEARN_FIT_MODEL,
    LEARN_DONE
} LearnState;

static LearnState s_state;
static uint32_t s_index;
static uint8_t s_done;
static float s_input_vpp_mv[SWEEP_POINT_COUNT];
static float s_output_vpp_mv[SWEEP_POINT_COUNT];
static float s_gain_setting[SWEEP_POINT_COUNT];
static float s_mag_response[SWEEP_POINT_COUNT];
static AdvLearnModel s_model;

static uint32_t sweep_frequency(uint32_t index)
{
    return SWEEP_FREQ_START_HZ + index * SWEEP_FREQ_STEP_HZ;
}

static float read_ad637_vpp_mv(uint32_t samples)
{
    float sum_mv = 0.0f;

    for (uint32_t i = 0; i < samples; ++i) {
        ads8688_sample_request = 1U;
        ADS8688_Service();
        sum_mv += ADS8688_CodeToMilliVolt(v_suoxiang[AD637_ADC_INDEX]);
    }

    return fabsf(sum_mv / (float)samples) * AD637_VPP_FACTOR;
}

static float calibrate_input(uint32_t freq_hz, float initial_gain,
                             float *measured_vpp_mv)
{
    float gain = initial_gain;
    float vpp_mv = 0.0f;

    DDS_SetFrequency(freq_hz);
    for (uint32_t attempt = 0; attempt < CAL_MAX_ATTEMPTS; ++attempt) {
        VGA_SetGain_Linear(gain);
        HAL_Delay(SETTLE_TIME_MS);
        vpp_mv = read_ad637_vpp_mv(CAL_AVERAGE_SAMPLES);

        if (vpp_mv < MIN_VALID_VPP_MV) {
            break;
        }

        float error = (vpp_mv - LEARN_TARGET_VPP_MV) / LEARN_TARGET_VPP_MV;
        if (fabsf(error) <= 0.03f) {
            break;
        }

        float correction = LEARN_TARGET_VPP_MV / vpp_mv;
        if (correction > 1.5f) correction = 1.5f;
        if (correction < 0.5f) correction = 0.5f;
        gain *= correction;
        if (gain > CAL_SAFE_GAIN_MAX) gain = CAL_SAFE_GAIN_MAX;
        if (gain < 0.001f) gain = 0.001f;
    }

    *measured_vpp_mv = vpp_mv;
    return gain;
}

static float measure_first_output_when_stable(void)
{
    HAL_Delay(FIRST_WARMUP_TIME_MS);

    float previous = read_ad637_vpp_mv(MEAS_AVERAGE_SAMPLES);
    float current = previous;
    uint32_t stable_count = 0U;

    printf("[LEARN:B] 1kHz预热完成，开始检查AD637稳定性\r\n");
    for (uint32_t check = 1U; check <= FIRST_CHECK_MAX; ++check) {
        HAL_Delay(FIRST_CHECK_INTERVAL_MS);
        current = read_ad637_vpp_mv(MEAS_AVERAGE_SAMPLES);

        float denominator = fmaxf(fabsf(previous), MIN_VALID_VPP_MV);
        float relative_change = fabsf(current - previous) / denominator;
        if (relative_change <= FIRST_STABLE_REL_LIMIT) {
            ++stable_count;
        } else {
            stable_count = 0U;
        }

        printf("[LEARN:B] 1kHz稳定检查 %lu/%u: Vout=%.1fmVpp 变化=%.2f%% 连续稳定=%lu/%u\r\n",
               (unsigned long)check, (unsigned)FIRST_CHECK_MAX, current,
               relative_change * 100.0f, (unsigned long)stable_count,
               (unsigned)FIRST_STABLE_REQUIRED);

        if (stable_count >= FIRST_STABLE_REQUIRED) {
            printf("[LEARN:B] 1kHz已稳定，保存当前测量值\r\n");
            return current;
        }
        previous = current;
    }

    printf("[LEARN:B] 警告：1kHz在最长等待时间内未完全稳定，保存最后测量值\r\n");
    return current;
}

static float response_shape(uint8_t type, float freq_hz, float f0_hz, float q)
{
    float x = freq_hz / f0_hz;
    float x2 = x * x;
    float den = sqrtf((1.0f - x2) * (1.0f - x2) + (x / q) * (x / q));
    if (den < 1.0e-9f) den = 1.0e-9f;

    switch (type) {
    case FILTER_LOW_PASS:  return 1.0f / den;
    case FILTER_HIGH_PASS: return x2 / den;
    case FILTER_BAND_PASS: return (x / q) / den;
    case FILTER_BAND_STOP: return fabsf(1.0f - x2) / den;
    default:               return 0.0f;
    }
}

static void classify_filter(void)
{
    float low = 0.0f;
    float high = 0.0f;
    const uint32_t edge_count = 5U;

    for (uint32_t i = 0; i < edge_count; ++i) {
        low += s_mag_response[i];
        high += s_mag_response[SWEEP_POINT_COUNT - 1U - i];
    }
    low /= (float)edge_count;
    high /= (float)edge_count;

    uint8_t pass_low = (low >= 0.5f) ? 1U : 0U;
    uint8_t pass_high = (high >= 0.5f) ? 1U : 0U;

    if (pass_low && !pass_high) s_model.filter_type = FILTER_LOW_PASS;
    else if (!pass_low && pass_high) s_model.filter_type = FILTER_HIGH_PASS;
    else if (!pass_low && !pass_high) s_model.filter_type = FILTER_BAND_PASS;
    else s_model.filter_type = FILTER_BAND_STOP;

    printf("[LEARN] edge response: low=%.3f high=%.3f type=%u\r\n",
           low, high, s_model.filter_type);
}

static void fit_second_order_model(void)
{
    float best_error = FLT_MAX;
    float best_gain = 1.0f;
    float best_f0 = 10000.0f;
    float best_q = 0.707f;

    /* Coarse deterministic grid; measured magnitudes are retained for interpolation. */
    for (uint32_t f_idx = 0; f_idx < SWEEP_POINT_COUNT; f_idx += 4U) {
        float f0 = (float)sweep_frequency(f_idx);
        float q = 0.15f;

        for (uint32_t q_idx = 0; q_idx < 44U; ++q_idx) {
            float log_gain_sum = 0.0f;
            uint32_t used = 0U;

            for (uint32_t i = 0; i < SWEEP_POINT_COUNT; i += 16U) {
                float measured = s_mag_response[i];
                float shape = response_shape(s_model.filter_type,
                                             (float)sweep_frequency(i), f0, q);
                if (measured > 1.0e-4f && shape > 1.0e-4f) {
                    log_gain_sum += logf(measured / shape);
                    ++used;
                }
            }

            if (used > 2U) {
                float gain = expf(log_gain_sum / (float)used);
                float error = 0.0f;
                for (uint32_t i = 0; i < SWEEP_POINT_COUNT; i += 16U) {
                    float measured = fmaxf(s_mag_response[i], 1.0e-4f);
                    float predicted = fmaxf(gain * response_shape(
                        s_model.filter_type, (float)sweep_frequency(i), f0, q),
                        1.0e-4f);
                    float e = logf(measured) - logf(predicted);
                    error += e * e;
                }
                if (error < best_error) {
                    best_error = error;
                    best_gain = gain;
                    best_f0 = f0;
                    best_q = q;
                }
            }
            q *= 1.12f;
        }
    }

    s_model.gain = best_gain;
    s_model.f0_hz = best_f0;
    s_model.q = best_q;
    s_model.valid = 1U;

    printf("[LEARN] model: type=%u K=%.4f f0=%.1fHz Q=%.3f err=%.4f\r\n",
           s_model.filter_type, s_model.gain, s_model.f0_hz,
           s_model.q, best_error);
}

void AdvLearn_Init(void)
{
    s_state = LEARN_IDLE;
    s_index = 0U;
    s_done = 0U;
    memset(s_input_vpp_mv, 0, sizeof(s_input_vpp_mv));
    memset(s_output_vpp_mv, 0, sizeof(s_output_vpp_mv));
    memset(s_gain_setting, 0, sizeof(s_gain_setting));
    memset(s_mag_response, 0, sizeof(s_mag_response));
    memset(&s_model, 0, sizeof(s_model));

    s_model.valid = 1U;
    s_model.filter_type = PRESET_FILTER_TYPE;
    s_model.gain = PRESET_MODEL_GAIN;
    s_model.f0_hz = PRESET_MODEL_F0_HZ;
    s_model.q = PRESET_MODEL_Q;
    for (uint32_t i = 0U; i < SWEEP_POINT_COUNT; ++i) {
        s_mag_response[i] = s_model.gain * response_shape(
            s_model.filter_type, (float)sweep_frequency(i),
            s_model.f0_hz, s_model.q);
    }
    printf("[LEARN] preset model loaded: type=%u K=%.4f f0=%.1fHz Q=%.3f err=%.4f\r\n",
           s_model.filter_type, s_model.gain, s_model.f0_hz,
           s_model.q, (double)PRESET_MODEL_ERROR);
    printf("[LEARN] Skip learning for DPLL test: press KEY1 to replay; KEY0 relearns\r\n");
}

void AdvLearn_Start(void)
{
    if (s_state != LEARN_IDLE) return;

    s_index = 0U;
    s_done = 0U;
    memset(&s_model, 0, sizeof(s_model));
    memset(s_input_vpp_mv, 0, sizeof(s_input_vpp_mv));
    memset(s_output_vpp_mv, 0, sizeof(s_output_vpp_mv));
    memset(s_gain_setting, 0, sizeof(s_gain_setting));
    memset(s_mag_response, 0, sizeof(s_mag_response));
    s_state = LEARN_CALIBRATE_INPUT;
    printf("[LEARN] Phase A: connect AD637 to device output (%u points)\r\n",
           (unsigned)SWEEP_POINT_COUNT);
    UI_SendToScreen("message.txt=\"A: scan device output\"");
}

void AdvLearn_ContinueAfterRewire(void)
{
    if (s_state != LEARN_WAIT_REWIRE) return;
    s_index = 0U;
    s_state = LEARN_MEASURE_OUTPUT;
    printf("[LEARN] Phase B: scanning unknown-circuit output\r\n");
    UI_SendToScreen("message.txt=\"B: scan circuit output\"");
}

void AdvLearn_Service(void)
{
    if (s_state == LEARN_CALIBRATE_INPUT) {
        uint32_t freq = sweep_frequency(s_index);
        float initial_gain = (s_index == 0U) ? 1.0f : s_gain_setting[s_index - 1U];
        s_gain_setting[s_index] = calibrate_input(
            freq, initial_gain, &s_input_vpp_mv[s_index]);

        if ((s_index % 20U) == 0U || s_index + 1U == SWEEP_POINT_COUNT) {
            printf("[LEARN:A] %luHz Vin=%.1fmVpp gain=%.4f (%lu/%u)\r\n",
                   freq, s_input_vpp_mv[s_index], s_gain_setting[s_index],
                   s_index + 1U, (unsigned)SWEEP_POINT_COUNT);
        }

        if (++s_index >= SWEEP_POINT_COUNT) {
            s_state = LEARN_WAIT_REWIRE;
            DDS_SetFrequency(SWEEP_FREQ_START_HZ);
            VGA_SetGain_Linear(SAFE_IDLE_GAIN);
            printf("[LEARN] Phase A done. Move AD637 input to circuit output, then press KEY1.\r\n");
            UI_SendToScreen("message.txt=\"Move AD637, press KEY1\"");
        }
        return;
    }

    if (s_state == LEARN_MEASURE_OUTPUT) {
        uint32_t freq = sweep_frequency(s_index);
        DDS_SetFrequency(freq);
        VGA_SetGain_Linear(s_gain_setting[s_index]);
        /* AD637 was at the safe idle level while rewiring.  Its DC output,
         * especially with a large averaging capacitor, needs substantially
         * longer than an ADS8688 conversion to settle after amplitude/frequency
         * changes. */
        if (s_index == 0U) {
            s_output_vpp_mv[s_index] = measure_first_output_when_stable();
        } else {
            HAL_Delay(OUTPUT_SETTLE_TIME_MS);
            s_output_vpp_mv[s_index] = read_ad637_vpp_mv(MEAS_AVERAGE_SAMPLES);
        }

        if (s_input_vpp_mv[s_index] >= MIN_VALID_VPP_MV) {
            s_mag_response[s_index] =
                s_output_vpp_mv[s_index] / s_input_vpp_mv[s_index];
        } else {
            s_mag_response[s_index] = 0.0f;
        }

        if ((s_index % 20U) == 0U || s_index + 1U == SWEEP_POINT_COUNT) {
            printf("[LEARN:B] %luHz Vout=%.1fmVpp |H|=%.4f (%lu/%u)\r\n",
                   freq, s_output_vpp_mv[s_index], s_mag_response[s_index],
                   s_index + 1U, (unsigned)SWEEP_POINT_COUNT);
        }

        if (++s_index >= SWEEP_POINT_COUNT) s_state = LEARN_FIT_MODEL;
        return;
    }

    if (s_state == LEARN_FIT_MODEL) {
        classify_filter();
        fit_second_order_model();
        s_done = 1U;
        s_state = LEARN_DONE;
        DDS_SetFrequency(SWEEP_FREQ_START_HZ);
        VGA_SetGain_Linear(SAFE_IDLE_GAIN);
        UI_SendToScreen("rec.txt=\"%u\"", s_model.filter_type);
        UI_SendToScreen("message.txt=\"Learn complete: type %u\"",
                        s_model.filter_type);
    }
}

uint8_t AdvLearn_IsWaitingForRewire(void) { return s_state == LEARN_WAIT_REWIRE; }
uint8_t AdvLearn_IsDone(void) { return s_done; }
uint8_t AdvLearn_GetFilterType(void) { return s_model.filter_type; }
const AdvLearnModel *AdvLearn_GetModel(void) { return &s_model; }

uint8_t AdvLearn_GetResponse(float freq_hz, float *magnitude, float *phase_rad)
{
    if (!s_model.valid || freq_hz <= 0.0f) return 0U;

    float mag;
    if (freq_hz <= (float)SWEEP_FREQ_START_HZ) {
        mag = s_mag_response[0];
    } else if (freq_hz >= (float)SWEEP_FREQ_END_HZ) {
        mag = s_model.gain * response_shape(s_model.filter_type, freq_hz,
                                            s_model.f0_hz, s_model.q);
    } else {
        float position = (freq_hz - (float)SWEEP_FREQ_START_HZ) /
                         (float)SWEEP_FREQ_STEP_HZ;
        uint32_t i = (uint32_t)position;
        float fraction = position - (float)i;
        mag = s_mag_response[i] + fraction *
              (s_mag_response[i + 1U] - s_mag_response[i]);
    }

    float w = 2.0f * (float)M_PI * freq_hz;
    float w0 = 2.0f * (float)M_PI * s_model.f0_hz;
    float den_phase = atan2f(w * w0 / s_model.q, w0 * w0 - w * w);
    float num_phase = 0.0f;
    switch (s_model.filter_type) {
    case FILTER_HIGH_PASS: num_phase = (float)M_PI; break;
    case FILTER_BAND_PASS: num_phase = (float)M_PI * 0.5f; break;
    case FILTER_BAND_STOP:
        num_phase = (w0 * w0 - w * w >= 0.0f) ? 0.0f : (float)M_PI;
        break;
    default: break;
    }

    if (magnitude) *magnitude = mag;
    if (phase_rad) *phase_rad = num_phase - den_phase;
    return 1U;
}
