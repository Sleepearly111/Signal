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
#define REPORT_INTERVAL_MS      1000U
#define DPLL_KP                   0.10f
#define DPLL_KI                   0.001f
#define DPLL_I_LIMIT_RAD          0.015f
#define DPLL_STEP_LIMIT_RAD       0.050f
#define CONTEST_FREQ_STEP_HZ    100.0f
#define AMPLITUDE_SMOOTH_ALPHA    0.05f

static float32_t s_fft_input[DSP_FFT_SIZE];
static float32_t s_fft_mag[DSP_FFT_SIZE / 2U + 1U];
static float32_t s_output_amp[HARMONIC_COUNT_MAX];
static float32_t s_filtered_output_amp[HARMONIC_COUNT_MAX];
static float32_t s_output_phase[HARMONIC_COUNT_MAX];
static float32_t s_play_phase[HARMONIC_COUNT_MAX];
static uint16_t s_dac_wave[DSP_DAC_LENGTH_MAX];

static uint8_t s_running;
static uint8_t s_dac_active;
static uint8_t s_wave_type;
static uint8_t s_last_type;
static uint32_t s_dac_length;
static float s_last_freq;
static float s_last_signature;
static float s_synth_vpp;
static float s_synth_rms;
static uint32_t s_report_index;
static uint32_t s_tim6_divisor;
static float s_actual_output_freq;
static uint32_t s_last_report_tick;
static float s_dpll_phase_error;
static float s_dpll_phase_step;
static float s_dpll_integrator;

static void replay_process(void);
static void choose_dac_timing(float freq_hz);
static void synthesize_waveform(uint32_t length);
static void dac_start(uint32_t length);

static float wrap_phase(float phase)
{
    while (phase > (float)M_PI) phase -= 2.0f * (float)M_PI;
    while (phase < -(float)M_PI) phase += 2.0f * (float)M_PI;
    return phase;
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
    s_last_signature = 0.0f;
    s_synth_vpp = 0.0f;
    s_synth_rms = 0.0f;
    s_report_index = 0U;
    s_tim6_divisor = 0U;
    s_actual_output_freq = 0.0f;
    s_last_report_tick = 0U;
    s_dpll_phase_error = 0.0f;
    s_dpll_phase_step = 0.0f;
    s_dpll_integrator = 0.0f;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    memset(s_fft_input, 0, sizeof(s_fft_input));
    memset(s_fft_mag, 0, sizeof(s_fft_mag));
    memset(s_filtered_output_amp, 0, sizeof(s_filtered_output_amp));
    memset(s_play_phase, 0, sizeof(s_play_phase));
    memset(s_dac_wave, 0, sizeof(s_dac_wave));
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
    printf("[REPLAY] Stopped\r\n");
}

uint8_t AdvReplay_IsRunning(void) { return s_running; }

void AdvReplay_Service(void)
{
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

    /* For a sine, a rising crossing gives a steadier input phase than the
     * nearest FFT bin in a non-coherent frame.  Add the learned circuit phase
     * because replay reproduces the circuit output; it does not invert it. */
    if (s_wave_type == WAVE_SINE && zero_cross_freq > 0.0f) {
        float model_mag_unused;
        float model_phase;
        if (AdvLearn_GetResponse(target_freq,
                                 &model_mag_unused, &model_phase)) {
            s_output_phase[0] = zero_cross_phase + model_phase;
        }
    }

    float signature = second_ratio + third_ratio;
    uint32_t now_ms = HAL_GetTick();
    uint32_t previous_length = s_dac_length;
    uint32_t previous_divisor = s_tim6_divisor;
    choose_dac_timing(target_freq);
    uint8_t timing_changed = (s_dac_length != previous_length ||
                              s_tim6_divisor != previous_divisor) ? 1U : 0U;
    uint8_t signal_changed = (!s_dac_active || timing_changed ||
        s_wave_type != s_last_type ||
        fabsf(signature - s_last_signature) >= 0.02f) ? 1U : 0U;

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
            float advance = processing_seconds;
            if (!(s_wave_type == WAVE_SINE && h == 0U)) {
                advance += (float)(DSP_FFT_SIZE - 1U) /
                           (float)DSP_SAMPLE_RATE;
            }
            s_output_phase[h] = wrap_phase(s_output_phase[h] +
                2.0f * (float)M_PI * harmonic_freq * advance);
            s_play_phase[h] = s_output_phase[h];
        }
        s_dpll_phase_error = 0.0f;
        s_dpll_phase_step = 0.0f;
        s_dpll_integrator = 0.0f;
        synthesize_waveform(s_dac_length);
        dac_start(s_dac_length);
    } else if (s_wave_type == WAVE_SINE && s_dac_length > 0U) {
        /* Continuous DPLL: compare the live input/circuit target phase with
         * the phase of the sample currently being consumed by DAC DMA.  The
         * PI correction advances the table phase a little at a time; TIM6 and
         * DAC DMA remain running, so there is no periodic hard phase reset. */
        uint32_t remaining = __HAL_DMA_GET_COUNTER(&hdma_dac2);
        uint32_t sample_index =
            (s_dac_length - (remaining % s_dac_length)) % s_dac_length;
        float target_phase = wrap_phase(s_output_phase[0] +
            2.0f * (float)M_PI * freq * processing_seconds);
        float playing_phase = wrap_phase(s_play_phase[0] +
            2.0f * (float)M_PI * (float)sample_index /
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
           "  数字PLL: 相位误差=%.2f度 本帧修正=%.3f度 积分项=%.3f度\r\n"
           "  相位同步: 已叠加未知电路相移，PLL连续微调，计时参数不变时不重启DAC\r\n",
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
           s_dpll_phase_error * 180.0f / (float)M_PI,
           s_dpll_phase_step * 180.0f / (float)M_PI,
           s_dpll_integrator * 180.0f / (float)M_PI);
    UI_SendToScreen("t0.txt=\"%.2fkHz\"", target_freq / 1000.0f);
    UI_SendToScreen("t1.txt=\"Replay type %u\"", s_wave_type);
}

static void choose_dac_timing(float freq_hz)
{
    uint32_t max_length = (uint32_t)((float)DSP_DAC_UPDATE_MAX / freq_hz);
    if (max_length > DSP_DAC_LENGTH_MAX) max_length = DSP_DAC_LENGTH_MAX;
    if (max_length < 16U) max_length = 16U;

    float best_error = 1.0e30f;
    uint32_t best_length = 16U;
    uint32_t best_divisor = 2U;
    float best_frequency = 0.0f;

    for (uint32_t length = 16U; length <= max_length; ++length) {
        float ideal_divisor = (float)DAC_TIMER_CLOCK_HZ /
                              (freq_hz * (float)length);
        uint32_t divisor = (uint32_t)lroundf(ideal_divisor);
        if (divisor < 2U || divisor > 65535U) continue;
        float actual = (float)DAC_TIMER_CLOCK_HZ /
                       ((float)divisor * (float)length);
        float error = fabsf(actual - freq_hz);
        if (error < best_error ||
            (fabsf(error - best_error) < 1.0e-6f && length > best_length)) {
            best_error = error;
            best_length = length;
            best_divisor = divisor;
            best_frequency = actual;
        }
    }

    s_dac_length = best_length;
    s_tim6_divisor = best_divisor;
    s_actual_output_freq = best_frequency;
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
    if (s_dac_active) {
        HAL_DAC_Stop_DMA(&hdac, DAC_CHANNEL_2);
        TIM6->CR1 &= ~TIM_CR1_CEN;
        s_dac_active = 0U;
    }

    __HAL_RCC_TIM6_CLK_ENABLE();
    TIM6->CR1 = 0U;
    TIM6->PSC = 0U;
    TIM6->ARR = s_tim6_divisor - 1U;
    TIM6->CR2 = TIM_CR2_MMS_1;
    TIM6->EGR = TIM_EGR_UG;

    DAC_ChannelConfTypeDef config = {0};
    config.DAC_Trigger = DAC_TRIGGER_T6_TRGO;
    config.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
    if (HAL_DAC_ConfigChannel(&hdac, &config, DAC_CHANNEL_2) != HAL_OK) {
        printf("[REPLAY] DAC CH2 config failed\r\n");
        return;
    }
    if (HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_2, (uint32_t *)s_dac_wave,
                          length, DAC_ALIGN_12B_R) != HAL_OK) {
        printf("[REPLAY] DAC DMA start failed\r\n");
        return;
    }
    __HAL_DMA_DISABLE_IT(&hdma_dac2, DMA_IT_HT | DMA_IT_TC);
    TIM6->CR1 |= TIM_CR1_CEN;
    s_dac_active = 1U;
}
