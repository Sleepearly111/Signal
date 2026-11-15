#include "adva_replay.h"
#include "adva_learn.h"
#include "adc.h"
#include "app_config.h"
#include "dac.h"
#include "dsp_utils.h"
#include "tim.h"
#include "arm_math.h"
#include "stm32f4xx_hal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

extern void UI_SendToScreen(const char *fmt, ...);

#define ADC_VREF                 3.3f
#define ADC_MIDPOINT             1.65f
#define INPUT_FREQ_MIN_HZ     1000.0f
#define INPUT_FREQ_VALID_MIN_HZ 700.0f
#define INPUT_FREQ_MAX_HZ    50000.0f
#define HARMONIC_COUNT_MAX       12U
#define DAC_OUTPUT_MIDPOINT      1.65f
#define ASSUMED_INPUT_VPP         2.0f
#define DAC_TIMER_CLOCK_HZ  84000000U
#define DDS_SAMPLE_RATE_HZ    1200000U
#define DDS_TIMER_DIVISOR          70U
#define DDS_BUFFER_LENGTH          256U
#define DDS_HALF_LENGTH           (DDS_BUFFER_LENGTH / 2U)
#define REPORT_INTERVAL_MS      1000U
#define DPLL_KP                   0.10f
#define DPLL_KI                   0.001f
#define DPLL_I_LIMIT_RAD          0.015f
#define DPLL_STEP_LIMIT_RAD       0.050f
#define SHAPED_PLL_DEADBAND_RAD    0.070f
#define DPLL_MAX_FREQ_HZ       10000.0f
#define CONTEST_FREQ_STEP_HZ    100.0f
#define FREQ_CHANGE_CONFIRM_FRAMES 3U
#define AMPLITUDE_SMOOTH_ALPHA    0.05f
#define FLL_STARTUP_TRIM_PPM      -40.0f  /* -0.8 Hz measured at 20 kHz */

static float32_t s_fft_input[DSP_FFT_SIZE];
static float32_t s_fft_mag[DSP_FFT_SIZE / 2U + 1U];
static float32_t s_output_amp[HARMONIC_COUNT_MAX];
static float32_t s_filtered_output_amp[HARMONIC_COUNT_MAX];
static float32_t s_output_phase[HARMONIC_COUNT_MAX];
static float32_t s_play_phase[HARMONIC_COUNT_MAX];
static uint16_t s_dac_wave[DSP_DAC_LENGTH_MAX];
static uint16_t s_dds_buffer[DDS_BUFFER_LENGTH];
static volatile uint32_t s_dds_phase;
static volatile uint32_t s_dds_phase_step;
static volatile uint32_t s_dds_wrap_count;
static volatile uint32_t s_dds_render_max_cycles;

static uint8_t s_running;
static uint8_t s_dac_active;
static uint8_t s_wave_type;
static uint8_t s_last_type;
static uint32_t s_dac_length;
static float s_last_freq;
static float s_confirmed_target_freq;
static float s_candidate_target_freq;
static uint8_t s_candidate_target_count;
static float s_last_signature;
static float s_synth_vpp;
static float s_synth_rms;
static uint32_t s_report_index;
static uint32_t s_dac_start_count;
static uint32_t s_tim6_divisor;
static float s_actual_output_freq;
static uint32_t s_last_report_tick;
static float s_dpll_phase_error;
static float s_dpll_phase_step;
static float s_dpll_integrator;
static volatile int8_t s_shaped_nudge_request;
static volatile int8_t s_shaped_nudge_applied;
static volatile uint8_t s_shaped_nudge_remaining;
static volatile uint32_t s_shaped_nominal_divisor;

/* FLL fractional frequency trim: sigma-delta nudge via DMA TC callback.
 * One nudge-restore pair shifts frequency by f_nom / divisor (≈571 Hz
 * at 40 kHz / div=70).  The duty cycle gives arbitrarily fine resolution. */
static float                 s_fll_freq_trim;      /* Hz, signed */
static float                 s_fll_nudge_accum;    /* sigma-delta accumulator */
static volatile int8_t       s_fll_nudge_state;    /* 0=idle, 1=slowing, -1=speeding */

/* ── FLL volatile counters (ISR-safe, no printf in ISR) ── */
static volatile uint32_t s_fll_tc_count;        /* DMA TC = waveform cycles */
static volatile uint32_t s_fll_nudge_count;     /* nudges applied */
static volatile uint32_t s_fll_restore_count;   /* restores */
static volatile int8_t   s_fll_last_nudge_dir;  /* last nudge direction */
static volatile uint32_t s_fll_last_arr;        /* last ARR value written */

/* ── FLL stats (read from main loop, printed once/sec) ── */
static uint32_t s_fll_stat_last_ms;

static void replay_process(void);
static void choose_dac_timing(float freq_hz);
static void synthesize_waveform(uint32_t length);
static void dac_start(uint32_t length);
static void dds_update_phase_step(void);
static void dds_render_block(uint32_t offset, uint32_t count);

static void dds_update_phase_step(void)
{
    float output_hz = s_last_freq + s_fll_freq_trim;
    if (output_hz < INPUT_FREQ_MIN_HZ - 200.0f) {
        output_hz = INPUT_FREQ_MIN_HZ - 200.0f;
    }
    if (output_hz > INPUT_FREQ_MAX_HZ + 200.0f) {
        output_hz = INPUT_FREQ_MAX_HZ + 200.0f;
    }

    double scaled = (double)output_hz * 4294967296.0 /
                    (double)DDS_SAMPLE_RATE_HZ;
    if (scaled < 1.0) scaled = 1.0;
    if (scaled > 4294967295.0) scaled = 4294967295.0;
    s_dds_phase_step = (uint32_t)(scaled + 0.5);
    s_actual_output_freq = (float)((double)s_dds_phase_step *
                                   (double)DDS_SAMPLE_RATE_HZ /
                                   4294967296.0);
}

static void dds_render_block(uint32_t offset, uint32_t count)
{
    uint32_t start_cycles = DWT->CYCCNT;
    uint32_t phase = s_dds_phase;
    uint32_t step = s_dds_phase_step;
    uint32_t wraps = 0U;

    for (uint32_t i = 0U; i < count; ++i) {
        s_dds_buffer[offset + i] = s_dac_wave[phase >> 24U];
        uint32_t next = phase + step;
        if (next < phase) ++wraps;
        phase = next;
    }

    s_dds_phase = phase;
    s_dds_wrap_count += wraps;
    uint32_t elapsed = DWT->CYCCNT - start_cycles;
    if (elapsed > s_dds_render_max_cycles) {
        s_dds_render_max_cycles = elapsed;
    }
}

static float wrap_phase(float phase)
{
    while (phase > (float)M_PI) phase -= 2.0f * (float)M_PI;
    while (phase < -(float)M_PI) phase += 2.0f * (float)M_PI;
    return phase;
}

/* Return the currently played table position with sub-sample resolution.
 * DMA tells us which DAC word is being held; TIM6->CNT tells us how far the
 * timer has progressed toward the next word.  Re-read CNT if a DMA transfer
 * happened during the snapshot so the two values describe the same sample. */
static float dac_playback_position(uint32_t length)
{
    if (length == 0U) return 0.0f;

    uint32_t remaining_before = __HAL_DMA_GET_COUNTER(&hdma_dac2);
    uint32_t timer_count = TIM6->CNT;
    uint32_t remaining_after = __HAL_DMA_GET_COUNTER(&hdma_dac2);
    if (remaining_after != remaining_before) {
        timer_count = TIM6->CNT;
    }

    uint32_t remaining = remaining_after % length;
    uint32_t transfers = length - remaining;
    uint32_t current_index = (transfers + length - 1U) % length;
    uint32_t divisor = TIM6->ARR + 1U;
    float fraction = (divisor > 0U) ?
                     (float)timer_count / (float)divisor : 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    return (float)current_index + fraction;
}

/* Average rising zero-crossing periods before the FFT window is applied.
 * Hysteresis arms the detector only after a clear negative excursion, which
 * prevents ADC noise around zero from creating extra crossings. */
static float measure_frequency_zero_crossing(const float32_t *data,
                                             uint32_t length,
                                             float input_vpp,
                                             float *phase_at_frame_end)
{
    float hysteresis = input_vpp * 0.08f;
    if (hysteresis < 0.01f) hysteresis = 0.01f;

    uint8_t armed = 0U;
    uint32_t crossing_count = 0U;
    float first_crossing = 0.0f;
    float last_crossing = 0.0f;

    for (uint32_t i = 1U; i < length; ++i) {
        if (!armed) {
            if (data[i] <= -hysteresis) armed = 1U;
            continue;
        }

        if (data[i - 1U] < 0.0f && data[i] >= 0.0f) {
            float denominator = data[i] - data[i - 1U];
            float fraction = 0.0f;
            if (fabsf(denominator) > 1.0e-9f) {
                fraction = -data[i - 1U] / denominator;
            }
            float crossing = (float)(i - 1U) + fraction;
            if (crossing_count == 0U) first_crossing = crossing;
            last_crossing = crossing;
            ++crossing_count;
            armed = 0U;
        }
    }

    if (crossing_count < 2U || last_crossing <= first_crossing) return 0.0f;
    float frequency = (float)(crossing_count - 1U) *
                      (float)DSP_SAMPLE_RATE /
                      (last_crossing - first_crossing);
    if (phase_at_frame_end) {
        /* A rising zero crossing is phase zero for the sine convention used
         * by synthesize_waveform().  Refer it to the final captured sample;
         * using the last crossing avoids magnifying a small frequency error
         * over the complete ADC frame. */
        *phase_at_frame_end = fmodf(2.0f * (float)M_PI * frequency *
                                   ((float)(length - 1U) - last_crossing) /
                                   (float)DSP_SAMPLE_RATE,
                                   2.0f * (float)M_PI);
    }
    return frequency;
}

static const char *wave_type_name(uint8_t type)
{
    switch (type) {
    case WAVE_SINE:     return "正弦波";
    case WAVE_SQUARE:   return "矩形波";
    case WAVE_TRIANGLE: return "三角波";
    default:            return "未知波形";
    }
}

static const char *filter_type_name(uint8_t type)
{
    switch (type) {
    case FILTER_LOW_PASS:  return "低通";
    case FILTER_HIGH_PASS: return "高通";
    case FILTER_BAND_PASS: return "带通";
    case FILTER_BAND_STOP: return "带阻";
    default:               return "未知";
    }
}

static float find_fundamental(float *amplitude, uint32_t *peak_bin)
{
    uint32_t first = (uint32_t)(INPUT_FREQ_MIN_HZ * DSP_FFT_SIZE /
                                (float)DSP_SAMPLE_RATE);
    uint32_t last = (uint32_t)(INPUT_FREQ_MAX_HZ * DSP_FFT_SIZE /
                               (float)DSP_SAMPLE_RATE);
    if (first < 1U) first = 1U;
    if (last >= DSP_FFT_SIZE / 2U) last = DSP_FFT_SIZE / 2U - 1U;

    uint32_t best = first;
    for (uint32_t k = first + 1U; k <= last; ++k) {
        if (s_fft_mag[k] > s_fft_mag[best]) best = k;
    }

    float delta = 0.0f;
    if (best > first && best < last) {
        float y0 = s_fft_mag[best - 1U];
        float y1 = s_fft_mag[best];
        float y2 = s_fft_mag[best + 1U];
        float den = 2.0f * (2.0f * y1 - y0 - y2);
        if (fabsf(den) > 1.0e-12f) delta = (y0 - y2) / den;
    }

    *amplitude = s_fft_mag[best];
    *peak_bin = best;
    return ((float)best + delta) * (float)DSP_SAMPLE_RATE /
           (float)DSP_FFT_SIZE;
}

void AdvReplay_Init(void)
{
    s_running = 0U;
    s_dac_active = 0U;
    s_wave_type = 0U;
    s_last_type = 0U;
    s_dac_length = 0U;
    s_last_freq = 0.0f;
    s_confirmed_target_freq = 0.0f;
    s_candidate_target_freq = 0.0f;
    s_candidate_target_count = 0U;
    s_last_signature = 0.0f;
    s_synth_vpp = 0.0f;
    s_synth_rms = 0.0f;
    s_report_index = 0U;
    s_dac_start_count = 0U;
    s_tim6_divisor = 0U;
    s_actual_output_freq = 0.0f;
    s_last_report_tick = 0U;
    s_dpll_phase_error = 0.0f;
    s_dpll_phase_step = 0.0f;
    s_dpll_integrator = 0.0f;
    s_shaped_nudge_request = 0;
    s_shaped_nudge_applied = 0;
    s_shaped_nudge_remaining = 0U;
    s_shaped_nominal_divisor = 0U;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    memset(s_fft_input, 0, sizeof(s_fft_input));
    memset(s_fft_mag, 0, sizeof(s_fft_mag));
    memset(s_filtered_output_amp, 0, sizeof(s_filtered_output_amp));
    memset(s_play_phase, 0, sizeof(s_play_phase));
    memset(s_dac_wave, 0, sizeof(s_dac_wave));
    memset(s_dds_buffer, 0, sizeof(s_dds_buffer));
    s_dds_phase = 0U;
    s_dds_phase_step = 0U;
    s_dds_wrap_count = 0U;
    s_dds_render_max_cycles = 0U;
}

void AdvReplay_Start(void)
{
    if (s_running) return;
    if (!AdvLearn_GetModel()->valid) {
        printf("[REPLAY] Refused: no learned model\r\n");
        UI_SendToScreen("message.txt=\"Learn model first\"");
        return;
    }

    s_running = 1U;
    s_last_freq = 0.0f;
    s_confirmed_target_freq = 0.0f;
    s_candidate_target_freq = 0.0f;
    s_candidate_target_count = 0U;
    s_last_type = 0U;
    s_last_signature = 0.0f;
    adc1_dma_finish_flag = 0U;
    if (ADC_DMA_Trig(DSP_FFT_SIZE) != HAL_OK) {
        s_running = 0U;
        printf("[REPLAY] ADC DMA start failed\r\n");
        return;
    }
    printf("[REPLAY] PB1 ADC started at %luSPS\r\n",
           (unsigned long)DSP_SAMPLE_RATE);
}

void AdvReplay_Stop(void)
{
    s_running = 0U;
    HAL_TIM_Base_Stop(&htim3);
    HAL_ADC_Stop_DMA(&hadc1);
    if (s_dac_active) {
        HAL_DAC_Stop_DMA(&hdac, DAC_CHANNEL_2);
        TIM6->CR1 &= ~TIM_CR1_CEN;
        s_dac_active = 0U;
    }
    s_shaped_nudge_request = 0;
    s_shaped_nudge_applied = 0;
    s_shaped_nudge_remaining = 0U;
    printf("[REPLAY] Stopped\r\n");
}

uint8_t AdvReplay_IsRunning(void) { return s_running; }
uint32_t AdvReplay_GetDacStartCount(void) { return s_dac_start_count; }

void AdvReplay_AdjustFrequency(int delta)
{
    if (delta == 0) return;
    if (!s_dac_active) return;

    float trim_before = s_fll_freq_trim;

    /* Fine fractional frequency trim via sigma-delta nudge in TC ISR.
     * Each call accumulates ~0.1 Hz.  Range: ±200 Hz. */
    s_fll_freq_trim += (float)delta * 0.1f;

    if (s_fll_freq_trim > 200.0f)  s_fll_freq_trim = 200.0f;
    if (s_fll_freq_trim < -200.0f) s_fll_freq_trim = -200.0f;
    dds_update_phase_step();

    printf("[FLL_CMD] delta=%d trim_before=%.3f trim_after=%.3f\r\n",
           (int)delta, (double)trim_before, (double)s_fll_freq_trim);
}

void AdvReplay_Service(void)
{
    /* ── FLL statistics (every 1 second, from main loop — no ISR printf) ── */
    {
        uint32_t now = HAL_GetTick();
        if (now - s_fll_stat_last_ms >= 1000U) {
            uint32_t dt_ms = now - s_fll_stat_last_ms;
            s_fll_stat_last_ms = now;

            uint32_t wraps, render_max_cycles;
            __disable_irq();
            wraps = s_dds_wrap_count;
            s_dds_wrap_count = 0U;
            render_max_cycles = s_dds_render_max_cycles;
            s_dds_render_max_cycles = 0U;
            __enable_irq();

            float real_freq = (dt_ms > 0U)
                ? (float)wraps * 1000.0f / (float)dt_ms : 0.0f;

            printf("[FLL_STAT] mode=DDS trim=%.3f phase_step=%lu "
                   "ARR=%lu wraps=%lu dt=%.3fs measured=%.3f "
                   "commanded=%.3f sample_rate=%lu render_max=%.2fus\r\n",
                   (double)s_fll_freq_trim,
                   (unsigned long)s_dds_phase_step,
                   (unsigned long)TIM6->ARR,
                   (unsigned long)wraps, (double)dt_ms * 0.001,
                   (double)real_freq, (double)s_actual_output_freq,
                   (unsigned long)DDS_SAMPLE_RATE_HZ,
                   (double)render_max_cycles * 1000000.0 /
                   (double)HAL_RCC_GetHCLKFreq());
        }
    }

    if (!s_running || !adc1_dma_finish_flag) return;

    adc1_dma_finish_flag = 0U;
    replay_process();

    if (s_running && ADC_DMA_Trig(DSP_FFT_SIZE) != HAL_OK) {
        printf("[REPLAY] ADC DMA restart failed\r\n");
        AdvReplay_Stop();
    }
}

static void replay_process(void)
{
    uint32_t process_start_cycles = DWT->CYCCNT;
    float input_min = ADC_VREF;
    float input_max = 0.0f;
    float input_sum = 0.0f;
    for (uint32_t i = 0; i < DSP_FFT_SIZE; ++i) {
        float voltage = (float)ADC1_ConvertedValue[i] * ADC_VREF / 4095.0f;
        if (voltage < input_min) input_min = voltage;
        if (voltage > input_max) input_max = voltage;
        input_sum += voltage;
        s_fft_input[i] = voltage - ADC_MIDPOINT;
    }

    (void)dsp_remove_dc(s_fft_input, DSP_FFT_SIZE);
    float input_square_sum = 0.0f;
    for (uint32_t i = 0; i < DSP_FFT_SIZE; ++i) {
        input_square_sum += s_fft_input[i] * s_fft_input[i];
    }
    float input_rms = sqrtf(input_square_sum / (float)DSP_FFT_SIZE);
    float input_mean = input_sum / (float)DSP_FFT_SIZE;
    float input_vpp = input_max - input_min;
    float zero_cross_phase = 0.0f;
    float zero_cross_freq = measure_frequency_zero_crossing(
        s_fft_input, DSP_FFT_SIZE, input_vpp, &zero_cross_phase);
    dsp_flat_top_window(s_fft_input, DSP_FFT_SIZE);
    dsp_fft_magnitude(s_fft_input, s_fft_mag, DSP_FFT_SIZE);

    float fundamental_amp;
    uint32_t fundamental_bin;
    float fft_freq = find_fundamental(&fundamental_amp, &fundamental_bin);
    float freq = fft_freq;
    const char *frequency_method = "FFT";
    if (zero_cross_freq >= INPUT_FREQ_VALID_MIN_HZ &&
        zero_cross_freq <= INPUT_FREQ_MAX_HZ &&
        zero_cross_freq >= fft_freq * 0.7f &&
        zero_cross_freq <= fft_freq * 1.3f) {
        freq = zero_cross_freq;
        frequency_method = "过零周期";
    }
    if (fundamental_amp < 0.02f || freq < INPUT_FREQ_VALID_MIN_HZ ||
        freq > INPUT_FREQ_MAX_HZ) {
        printf("\r\n[复现%lu] 未检测到有效输入\r\n"
               "  PB1实测: 平均=%.4fV 最小=%.4fV 最大=%.4fV Vpp=%.4fV 交流RMS=%.4fV\r\n"
               "  FFT最强分量: %.1fHz 过零测频=%.1fHz 幅度=%.4fV（有效门限0.0200V）\r\n"
               "  提示: PB1应为约1.65V偏置且波形必须保持在0~3.3V\r\n",
               (unsigned long)++s_report_index, input_mean, input_min,
               input_max, input_vpp, input_rms, fft_freq, zero_cross_freq,
               fundamental_amp);
        return;
    }

    /* The problem frequency grid has a 100 Hz minimum step.  Quantizing the
     * replay target prevents harmless measurement jitter from selecting a
     * different DAC table length/divisor on every ADC frame. */
    float target_freq = roundf(freq / CONTEST_FREQ_STEP_HZ) *
                        CONTEST_FREQ_STEP_HZ;
    if (target_freq < INPUT_FREQ_MIN_HZ) target_freq = INPUT_FREQ_MIN_HZ;
    if (target_freq > INPUT_FREQ_MAX_HZ) target_freq = INPUT_FREQ_MAX_HZ;

    /* Do not restart DAC/FLL for one bad frequency frame.  The first valid
     * target is immediate; later grid changes need three matching frames. */
    if (s_confirmed_target_freq == 0.0f) {
        s_confirmed_target_freq = target_freq;
        s_candidate_target_freq = target_freq;
        s_candidate_target_count = 0U;
    } else if (target_freq != s_confirmed_target_freq) {
        if (target_freq == s_candidate_target_freq) {
            if (s_candidate_target_count < FREQ_CHANGE_CONFIRM_FRAMES) {
                s_candidate_target_count++;
            }
        } else {
            s_candidate_target_freq = target_freq;
            s_candidate_target_count = 1U;
        }

        if (s_candidate_target_count >= FREQ_CHANGE_CONFIRM_FRAMES) {
            printf("[REPLAY] frequency change confirmed: %.1f -> %.1fHz\r\n",
                   (double)s_confirmed_target_freq, (double)target_freq);
            s_confirmed_target_freq = target_freq;
            s_candidate_target_count = 0U;
        } else {
            target_freq = s_confirmed_target_freq;
        }
    } else {
        s_candidate_target_freq = target_freq;
        s_candidate_target_count = 0U;
    }

    /* The contest source is specified as 2 Vpp.  PB1 includes a fixed
     * attenuation/level-shift network, so retain its frequency and harmonic
     * information but restore the whole spectrum to the known physical Vpp.
     * Validation above deliberately uses the unscaled spectrum so noise can
     * never be amplified into a false valid signal. */
    float input_scale = ASSUMED_INPUT_VPP / input_vpp;
    for (uint32_t k = 0U; k <= DSP_FFT_SIZE / 2U; ++k) {
        s_fft_mag[k] *= input_scale;
    }
    fundamental_amp *= input_scale;
    float normalized_input_rms = input_rms * input_scale;

    float second_ratio = 0.0f;
    float third_ratio = 0.0f;
    for (uint32_t h = 0; h < HARMONIC_COUNT_MAX; ++h) {
        uint32_t order = h + 1U;
        float input_harmonic_freq = freq * (float)order;
        float output_harmonic_freq = target_freq * (float)order;
        uint32_t bin = (uint32_t)lroundf(input_harmonic_freq * DSP_FFT_SIZE /
                                        (float)DSP_SAMPLE_RATE);

        s_output_amp[h] = 0.0f;
        s_output_phase[h] = 0.0f;
        if (bin == 0U || bin >= DSP_FFT_SIZE / 2U) continue;

        float input_amp = s_fft_mag[bin];
        float input_phase = dsp_find_phase(DSP_FFT_SIZE, bin);
        float model_mag;
        float model_phase;
        if (!AdvLearn_GetResponse(output_harmonic_freq,
                                  &model_mag, &model_phase)) continue;

        s_output_amp[h] = input_amp * model_mag;
        s_output_phase[h] = input_phase + model_phase;

        if (order == 2U) second_ratio = input_amp / fundamental_amp;
        if (order == 3U) third_ratio = input_amp / fundamental_amp;
    }

    if (second_ratio < 0.03f && third_ratio < 0.03f) {
        s_wave_type = WAVE_SINE;
    } else if (second_ratio < 0.05f && third_ratio < 0.18f) {
        s_wave_type = WAVE_TRIANGLE;
    } else {
        s_wave_type = WAVE_SQUARE;
    }

    /* The FFT bins are not coherent with the measured fundamental (for
     * example 8 kHz lies between 1 MHz / 2048 bins).  Their magnitudes are
     * adequate for waveform classification, but their individual phases do
     * not preserve the relative phase of square/triangle harmonics.  Once a
     * standard source waveform is identified, rebuild its spectrum from the
     * exact Fourier series at the rising-zero-crossing phase, then apply the
     * learned circuit response to every harmonic. */
    if (zero_cross_freq > 0.0f) {
        const float source_peak = ASSUMED_INPUT_VPP * 0.5f;
        for (uint32_t h = 0U; h < HARMONIC_COUNT_MAX; ++h) {
            uint32_t order = h + 1U;
            float input_peak = 0.0f;
            float canonical_phase = zero_cross_phase * (float)order;

            if (s_wave_type == WAVE_SINE) {
                if (order == 1U) input_peak = source_peak;
            } else if (s_wave_type == WAVE_SQUARE) {
                if ((order & 1U) != 0U) {
                    input_peak = 4.0f * source_peak /
                                 ((float)M_PI * (float)order);
                }
            } else if (s_wave_type == WAVE_TRIANGLE) {
                if ((order & 1U) != 0U) {
                    input_peak = 8.0f * source_peak /
                                 ((float)M_PI * (float)M_PI *
                                  (float)order * (float)order);
                    if (((order - 1U) / 2U & 1U) != 0U) {
                        canonical_phase += (float)M_PI;
                    }
                }
            }

            s_output_amp[h] = 0.0f;
            s_output_phase[h] = 0.0f;
            if (input_peak > 0.0f) {
                float model_mag;
                float model_phase;
                if (AdvLearn_GetResponse(target_freq * (float)order,
                                         &model_mag, &model_phase)) {
                    s_output_amp[h] = input_peak * model_mag;
                    s_output_phase[h] =
                        wrap_phase(canonical_phase + model_phase);
                }
            }
        }
    }

    float signature = second_ratio + third_ratio;
    uint32_t now_ms = HAL_GetTick();
    uint32_t previous_length = s_dac_length;
    uint32_t previous_divisor = s_tim6_divisor;
    uint8_t frequency_changed =
        (s_last_freq == 0.0f || fabsf(target_freq - s_last_freq) >= 0.05f) ?
        1U : 0U;
    choose_dac_timing(target_freq);
    uint8_t timing_changed = (s_dac_length != previous_length ||
                              s_tim6_divisor != previous_divisor) ? 1U : 0U;
    /* This problem is frequency-only: keep the legacy phase DPLL disabled
     * over the complete 1-50 kHz range.  AD630 FLL owns all fine timing. */
    uint8_t pll_enabled = 0U;
    uint8_t pll_mode_changed = 0U;
    uint8_t signal_changed = (!s_dac_active || frequency_changed ||
        timing_changed ||
        pll_mode_changed ||
        s_wave_type != s_last_type) ? 1U : 0U;

    for (uint32_t h = 0U; h < HARMONIC_COUNT_MAX; ++h) {
        if (signal_changed) {
            s_filtered_output_amp[h] = s_output_amp[h];
        } else {
            s_filtered_output_amp[h] += AMPLITUDE_SMOOTH_ALPHA *
                (s_output_amp[h] - s_filtered_output_amp[h]);
        }
        s_output_amp[h] = s_filtered_output_amp[h];
    }

    s_last_freq = target_freq;
    s_last_type = s_wave_type;
    s_last_signature = signature;
    float processing_seconds = (float)(DWT->CYCCNT - process_start_cycles) /
                               (float)HAL_RCC_GetHCLKFreq();

    if (signal_changed) {
        /* On first lock or a real timing change, align once.  The sine phase
         * is already referred to the final ADC sample; FFT harmonic phases
         * still refer to the beginning of the frame. */
        for (uint32_t h = 0U; h < HARMONIC_COUNT_MAX; ++h) {
            float harmonic_freq = target_freq * (float)(h + 1U);
            /* All canonical harmonics above are referred to the final ADC
             * sample, so only compensate processing time before DAC start. */
            float advance = processing_seconds;
            s_output_phase[h] = wrap_phase(s_output_phase[h] +
                2.0f * (float)M_PI * harmonic_freq * advance);
            s_play_phase[h] = s_output_phase[h];
        }
        s_dpll_phase_error = 0.0f;
        s_dpll_phase_step = 0.0f;
        s_dpll_integrator = 0.0f;
        synthesize_waveform(s_dac_length);
        dac_start(s_dac_length);
    } else if (pll_enabled &&
               (s_wave_type == WAVE_SQUARE ||
                s_wave_type == WAVE_TRIANGLE) &&
               s_dac_length > 0U) {
        /* Square/triangle PLL: never rewrite the multi-harmonic table while
         * DMA is reading it.  Request one whole-period timer nudge at a DMA
         * cycle boundary; restore the nominal divisor one period later. */
        float sample_position = dac_playback_position(s_dac_length);
        float target_phase = wrap_phase(s_output_phase[0] +
            2.0f * (float)M_PI * freq * processing_seconds);
        float playing_phase = wrap_phase(s_play_phase[0] +
            2.0f * (float)M_PI * sample_position /
            (float)s_dac_length);
        s_dpll_phase_error = wrap_phase(target_phase - playing_phase);
        s_dpll_phase_step = 0.0f;
        s_dpll_integrator = 0.0f;

        if (s_shaped_nudge_request == 0 &&
            s_shaped_nudge_applied == 0 &&
            s_shaped_nudge_remaining == 0U) {
            float absolute_error = fabsf(s_dpll_phase_error);
            uint8_t correction_periods = 0U;
            if (absolute_error > 45.0f * (float)M_PI / 180.0f) {
                correction_periods = 4U;
            } else if (absolute_error > 15.0f * (float)M_PI / 180.0f) {
                correction_periods = 2U;
            } else if (absolute_error > SHAPED_PLL_DEADBAND_RAD) {
                correction_periods = 1U;
            }

            if (correction_periods > 0U) {
                s_shaped_nudge_request =
                    (s_dpll_phase_error > 0.0f) ? -1 : 1;
                s_shaped_nudge_remaining = correction_periods;
            }
        }
    } else if (pll_enabled && s_wave_type == WAVE_SINE &&
               s_dac_length > 0U) {
        /* Continuous DPLL: compare the live input/circuit target phase with
         * the phase of the sample currently being consumed by DAC DMA.  The
         * PI correction advances the table phase a little at a time; TIM6 and
         * DAC DMA remain running, so there is no periodic hard phase reset. */
        float sample_position = dac_playback_position(s_dac_length);
        float target_phase = wrap_phase(s_output_phase[0] +
            2.0f * (float)M_PI * freq * processing_seconds);
        float playing_phase = wrap_phase(s_play_phase[0] +
            2.0f * (float)M_PI * sample_position /
            (float)s_dac_length);
        s_dpll_phase_error = wrap_phase(target_phase - playing_phase);
        s_dpll_integrator += DPLL_KI * s_dpll_phase_error;
        if (s_dpll_integrator > DPLL_I_LIMIT_RAD)
            s_dpll_integrator = DPLL_I_LIMIT_RAD;
        if (s_dpll_integrator < -DPLL_I_LIMIT_RAD)
            s_dpll_integrator = -DPLL_I_LIMIT_RAD;
        s_dpll_phase_step = DPLL_KP * s_dpll_phase_error +
                            s_dpll_integrator;
        if (s_dpll_phase_step > DPLL_STEP_LIMIT_RAD)
            s_dpll_phase_step = DPLL_STEP_LIMIT_RAD;
        if (s_dpll_phase_step < -DPLL_STEP_LIMIT_RAD)
            s_dpll_phase_step = -DPLL_STEP_LIMIT_RAD;

        for (uint32_t h = 0U; h < HARMONIC_COUNT_MAX; ++h) {
            s_play_phase[h] = wrap_phase(s_play_phase[h] +
                (float)(h + 1U) * s_dpll_phase_step);
            s_output_phase[h] = s_play_phase[h];
        }
        synthesize_waveform(s_dac_length);
    } else if (!pll_enabled) {
        /* Above 10 kHz the ADC frame provides too few samples per cycle for
         * a reliable phase detector.  Keep the quantized DAC timing and the
         * existing table untouched so noisy phase estimates cannot pull or
         * restart the output.  The two independent clocks may drift slowly,
         * but the displayed waveform remains continuous. */
        s_dpll_phase_error = 0.0f;
        s_dpll_phase_step = 0.0f;
        s_dpll_integrator = 0.0f;
    }

    uint8_t report_due = signal_changed ||
        ((uint32_t)(now_ms - s_last_report_tick) >= REPORT_INTERVAL_MS);
    if (!report_due) return;
    s_last_report_tick = now_ms;

    const AdvLearnModel *model = AdvLearn_GetModel();
    float fundamental_gain = 0.0f;
    float fundamental_phase = 0.0f;
    (void)AdvLearn_GetResponse(target_freq,
                               &fundamental_gain, &fundamental_phase);
    printf("\r\n[复现%lu] 检测并更新DAC输出\r\n"
           "  PB1原始实测: 平均=%.4fV 最小=%.4fV 最大=%.4fV Vpp=%.4fV 交流RMS=%.4fV\r\n"
           "  输入幅度归一化: 原始Vpp=%.4fV 目标Vpp=%.4fV 校准系数=%.4f 归一化RMS=%.4fV\r\n"
           "  输入识别: %s 主频=%.1fHz 测频方法=%s FFT参考=%.1fHz 基波幅度=%.4fV 二次/基波=%.3f 三次/基波=%.3f\r\n"
           "  题目频率量化: %.1fHz（按100Hz步进锁定）\r\n"
           "  学习模型: %s K=%.4f f0=%.1fHz Q=%.3f\r\n"
           "  模型在主频处: 增益=%.4f 相移=%.1f度\r\n"
           "  预计未知电路输出: 主频=%.1fHz 基波幅度=%.4fV 合成Vpp=%.4fV 交流RMS=%.4fV\r\n"
           "  DAC输出: PA5 点数=%lu TIM6分频=%lu 目标频率=%.2fHz 实际频率=%.2fHz 误差=%.3fHz 状态=%s\r\n"
           "  数字PLL: %s 相位误差=%.2f度 本帧修正=%.3f度 积分项=%.3f度\r\n"
           "  相位同步: %s\r\n",
           (unsigned long)++s_report_index, input_mean, input_min, input_max,
           input_vpp, input_rms, input_vpp, (double)ASSUMED_INPUT_VPP,
           input_scale, normalized_input_rms, wave_type_name(s_wave_type), freq,
           frequency_method, fft_freq, fundamental_amp, second_ratio, third_ratio,
           target_freq,
           filter_type_name(model->filter_type), model->gain, model->f0_hz,
           model->q, fundamental_gain,
           fundamental_phase * 180.0f / (float)M_PI, target_freq,
           s_output_amp[0],
           s_synth_vpp, s_synth_rms, (unsigned long)s_dac_length,
           (unsigned long)s_tim6_divisor, target_freq, s_actual_output_freq,
           s_actual_output_freq - target_freq,
           s_dac_active ? "运行" : "失败",
           (pll_enabled && (s_wave_type == WAVE_SINE ||
                            s_wave_type == WAVE_SQUARE ||
                            s_wave_type == WAVE_TRIANGLE)) ?
               "低频闭环" :
               (pll_enabled ? "当前波形未启用" : "高频关闭"),
           s_dpll_phase_error * 180.0f / (float)M_PI,
           s_dpll_phase_step * 180.0f / (float)M_PI,
           s_dpll_integrator * 180.0f / (float)M_PI,
           (pll_enabled && s_wave_type == WAVE_SINE) ?
               "已叠加未知电路相移，正弦波PLL连续微调" :
           (pll_enabled && s_wave_type == WAVE_SQUARE) ?
               "矩形波保持波表不变，仅在完整周期边界微调定时器" :
           (pll_enabled && s_wave_type == WAVE_TRIANGLE) ?
               "三角波保持波表不变，仅在完整周期边界微调定时器" :
               "已叠加未知电路相移，固定100Hz量化频率，不再修改波表");
    UI_SendToScreen("t0.txt=\"%.2fkHz\"", target_freq / 1000.0f);
    UI_SendToScreen("t1.txt=\"Replay type %u\"", s_wave_type);
}

static void choose_dac_timing(float freq_hz)
{
    (void)freq_hz;
    s_dac_length = DSP_DAC_LENGTH_MAX;
    s_tim6_divisor = DDS_TIMER_DIVISOR;
}

static void synthesize_waveform(uint32_t length)
{
    uint32_t clipped = 0U;
    float min_value = ADC_VREF;
    float max_value = 0.0f;
    float square_sum = 0.0f;
    for (uint32_t i = 0; i < length; ++i) {
        float theta = 2.0f * (float)M_PI * (float)i / (float)length;
        float signal = DAC_OUTPUT_MIDPOINT;

        for (uint32_t h = 0; h < HARMONIC_COUNT_MAX; ++h) {
            uint32_t order = h + 1U;
            signal += s_output_amp[h] *
                      arm_sin_f32(theta * (float)order + s_output_phase[h]);
        }

        if (signal < 0.0f) { signal = 0.0f; ++clipped; }
        if (signal > ADC_VREF) { signal = ADC_VREF; ++clipped; }
        if (signal < min_value) min_value = signal;
        if (signal > max_value) max_value = signal;
        float ac = signal - DAC_OUTPUT_MIDPOINT;
        square_sum += ac * ac;
        s_dac_wave[i] = (uint16_t)lroundf(signal * 4095.0f / ADC_VREF);
    }
    s_synth_vpp = max_value - min_value;
    s_synth_rms = sqrtf(square_sum / (float)length);
    if (clipped) printf("[REPLAY] Warning: %lu DAC samples clipped\r\n",
                        (unsigned long)clipped);
}

static void dac_start(uint32_t length)
{
    s_shaped_nudge_request = 0;
    s_shaped_nudge_applied = 0;
    s_shaped_nudge_remaining = 0U;
    s_shaped_nominal_divisor = s_tim6_divisor;
    s_fll_nudge_state = 0;
    s_fll_nudge_accum = 0.0f;
    s_fll_tc_count      = 0U;
    s_fll_nudge_count   = 0U;
    s_fll_restore_count = 0U;
    s_fll_last_nudge_dir = 0;
    s_fll_last_arr       = s_tim6_divisor - 1U;
    s_fll_stat_last_ms   = HAL_GetTick();

    /* Warm start removes both deterministic integer timer quantization and
     * the measured board crystal ppm offset.  AD630 SEARCH then only needs
     * to refine the remaining small residual. */
    {
        float crystal_trim_hz = s_last_freq *
                                FLL_STARTUP_TRIM_PPM * 1.0e-6f;
        s_fll_freq_trim = crystal_trim_hz;
        dds_update_phase_step();
        printf("[FLL] DDS warm start crystal=%.1fppm "
               "trim=%+.3fHz; SEARCH starts now\r\n",
               (double)FLL_STARTUP_TRIM_PPM,
               (double)s_fll_freq_trim);
    }

    if (s_dac_active) {
        HAL_DAC_Stop_DMA(&hdac, DAC_CHANNEL_2);
        TIM6->CR1 &= ~TIM_CR1_CEN;
        s_dac_active = 0U;
    }

    __HAL_RCC_TIM6_CLK_ENABLE();
    TIM6->CR1 = 0U;
    TIM6->PSC = 0U;
    TIM6->ARR = DDS_TIMER_DIVISOR - 1U;
    TIM6->CR2 = TIM_CR2_MMS_1;
    TIM6->EGR = TIM_EGR_UG;

    DAC_ChannelConfTypeDef config = {0};
    config.DAC_Trigger = DAC_TRIGGER_T6_TRGO;
    config.DAC_OutputBuffer = DAC_OUTPUTBUFFER_DISABLE;
    if (HAL_DAC_ConfigChannel(&hdac, &config, DAC_CHANNEL_2) != HAL_OK) {
        printf("[REPLAY] DAC CH2 config failed\r\n");
        return;
    }
    s_dds_phase = 0U;
    dds_render_block(0U, DDS_BUFFER_LENGTH);
    s_dds_wrap_count = 0U;
    s_dds_render_max_cycles = 0U;
    if (HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_2, (uint32_t *)s_dds_buffer,
                          DDS_BUFFER_LENGTH, DAC_ALIGN_12B_R) != HAL_OK) {
        printf("[REPLAY] DAC DMA start failed\r\n");
        return;
    }
    /* Enable TC interrupt only — fires FLL sigma-delta nudge.
     * HT is disabled: not needed, reduces ISR load. */
    __HAL_DMA_ENABLE_IT(&hdma_dac2, DMA_IT_HT);
    __HAL_DMA_ENABLE_IT(&hdma_dac2, DMA_IT_TC);
    TIM6->CR1 |= TIM_CR1_CEN;
    s_dac_active = 1U;
    ++s_dac_start_count;
    printf("[REPLAY] DAC启动 #%lu：波形=%s 点数=%lu 分频=%lu\r\n",
           (unsigned long)s_dac_start_count, wave_type_name(s_wave_type),
           (unsigned long)s_dac_length,
           (unsigned long)s_tim6_divisor);
}

/* ── DMA TC callback: sigma-delta nudge + volatile counters ONLY.
 * NO printf, NO HAL_UART, NO blocking calls.  ISR must complete in < 1 µs. ── */
#if 0
void HAL_DACEx_ConvCpltCallbackCh2(DAC_HandleTypeDef *dac)
{
    if (dac != &hdac || !s_dac_active || s_shaped_nominal_divisor < 2U) {
        return;
    }

    /* ── Cycle counter ── */
    s_fll_tc_count++;

    /* ── Restore any previous nudge ── */
    uint8_t restored = 0U;
    if (s_shaped_nudge_applied != 0) {
        TIM6->ARR = s_shaped_nominal_divisor - 1U;
        s_shaped_nudge_applied = 0;
        restored = 1U;
    }
    if (s_fll_nudge_state != 0) {
        TIM6->ARR = s_shaped_nominal_divisor - 1U;
        s_fll_nudge_state = 0;
        s_fll_restore_count++;
        restored = 1U;
    }

    /* ── Shaped nudge (square/triangle DPLL) ── */
    int8_t request = s_shaped_nudge_request;
    if (request != 0 && s_shaped_nudge_remaining > 0U) {
        int32_t d = (int32_t)s_shaped_nominal_divisor + request;
        if (d < 2) d = 2;
        TIM6->ARR = (uint32_t)d - 1U;
        s_shaped_nudge_applied = request;
        --s_shaped_nudge_remaining;
        if (s_shaped_nudge_remaining == 0U) {
            s_shaped_nudge_request = 0;
        }
        return;
    }

    /* ── FLL sigma-delta nudge (all wave types, all freqs) ── */
    if (s_fll_freq_trim != 0.0f && !restored) {
        float nudge_fraction;
        float divisor = (float)s_shaped_nominal_divisor;

        /* Exact one-cycle frequency increments:
         *   faster (d-1): +f/(d-1)
         *   slower (d+1): -f/(d+1)
         * Convert requested Hz trim to the required fraction of all cycles. */
        if (s_fll_freq_trim > 0.0f) {
            nudge_fraction = -s_fll_freq_trim * (divisor - 1.0f)
                           / s_actual_output_freq;
        } else {
            nudge_fraction = -s_fll_freq_trim * (divisor + 1.0f)
                           / s_actual_output_freq;
        }
        if (nudge_fraction > 0.5f)  nudge_fraction = 0.5f;
        if (nudge_fraction < -0.5f) nudge_fraction = -0.5f;

        /* A nudge is always followed by a forced nominal restore cycle.
         * Only non-restore cycles reach this accumulator.  To obtain an
         * all-cycle fraction p, request p/(1-p) on eligible cycles. */
        {
            float p = fabsf(nudge_fraction);
            s_fll_nudge_accum += p / (1.0f - p);
        }

        if (s_fll_nudge_accum >= 1.0f) {
            s_fll_nudge_accum -= 1.0f;
            s_fll_nudge_count++;

            if (nudge_fraction > 0.0f) {
                TIM6->ARR = s_shaped_nominal_divisor;
                s_fll_nudge_state    = 1;
                s_fll_last_nudge_dir = 1;
            } else {
                uint32_t f = s_shaped_nominal_divisor - 2U;
                if (f < 1U) f = 1U;
                TIM6->ARR = f;
                s_fll_nudge_state    = -1;
                s_fll_last_nudge_dir = -1;
            }
            s_fll_last_arr = TIM6->ARR;
        }
    }
}
#endif

void HAL_DACEx_ConvHalfCpltCallbackCh2(DAC_HandleTypeDef *dac)
{
    if (dac == &hdac && s_dac_active) {
        dds_render_block(0U, DDS_HALF_LENGTH);
    }
}

void HAL_DACEx_ConvCpltCallbackCh2(DAC_HandleTypeDef *dac)
{
    if (dac == &hdac && s_dac_active) {
        dds_render_block(DDS_HALF_LENGTH, DDS_HALF_LENGTH);
    }
}
