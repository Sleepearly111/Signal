/*
 * tjc_display.c
 *
 * USART1 is owned exclusively by this module. UART callbacks only enqueue RX
 * bytes or mark TX completion; all parsing and display transactions run in the
 * foreground through TjcDisplay_Service().
 */

#include "tjc_display.h"
#include "tjc_display_config.h"
#include "tjc_transport.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#if ((TJC_UART_RX_RING_SIZE & (TJC_UART_RX_RING_SIZE - 1U)) != 0U)
#error "TJC_UART_RX_RING_SIZE must be a power of two"
#endif

typedef enum
{
    TJC_TX_IDLE = 0,
    TJC_TX_CLEAR,
    TJC_TX_RESET_CLEAR,
    TJC_TX_ADDT_COMMAND,
    TJC_TX_WAIT_READY,
    TJC_TX_PAYLOAD,
    TJC_TX_WAIT_FINISHED,
    TJC_TX_TEXT,
    TJC_TX_PAGE_QUERY
} TjcTxState;

typedef enum
{
    TJC_ACTIVE_NONE = 0,
    TJC_ACTIVE_TIME,
    TJC_ACTIVE_TIME_RESET,
    TJC_ACTIVE_SPECTRUM,
    TJC_ACTIVE_SPECTRUM_RESET,
    TJC_ACTIVE_TEXT
} TjcActiveView;

/** Adapts one completed calibrated time window to the configured display columns. */
static bool TjcDisplay_BuildTimePayload(const TjcDisplayTimeSubmission *submission,
                                        TjcTimePeriodMode period_mode,
                                        uint8_t *payload,
                                        uint16_t point_count);
/** Cubic display-only resampling of one completed calibrated voltage window. */
static bool TjcDisplay_InterpolateTimeSample(const float *samples, uint16_t sample_count,
                                             float position, float *value);
/** Maps one normalized display position to a safe spectrum sample. */
static uint16_t TjcDisplay_SpectrumPermilleToCenter(uint16_t permille, uint16_t count);
/** Maps 0..1000 Vpk height to the measured 0..255 spectrum byte direction. */
static uint8_t TjcDisplay_SpectrumPermilleToPeakByte(uint16_t permille);
/** Places completed upstream spectrum lines without running FFT or peak detection. */
static bool TjcDisplay_BuildSpectrumPayload(const TjcDisplaySpectrumSubmission *submission,
                                             uint8_t *payload,
                                             uint16_t point_count);
/** 接收字节已从 UART 传输层进入前台，交给页面协议解析。 */
static void TjcDisplay_OnTransportRxByte(void *context, uint8_t byte);
/** UART 传输层报告一笔异步发送完成。 */
static void TjcDisplay_OnTransportTxComplete(void *context);
/** UART 传输层报告错误，页面状态机将在前台中止当前事务。 */
static void TjcDisplay_OnTransportError(void *context);
/** Consumes FE/FD transparent-transfer replies before starting the next phase. */
static bool TjcDisplay_ConsumeAddtReplyByte(uint8_t byte);
static void TjcDisplay_ParseRxByte(uint8_t byte);
static void TjcDisplay_HandlePageId(uint8_t page_id);
static void TjcDisplay_StartNextTransaction(void);
static bool TjcDisplay_StartCommand(const char *command, TjcTxState next_state);
static bool TjcDisplay_StartRaw(const uint8_t *data, uint16_t length,
                                TjcTxState next_state);
static void TjcDisplay_AbortTransaction(void);
static void TjcDisplay_AdvanceText(void);
static void TjcDisplay_FormatEmptySpectrumText(char rows[][TJC_UART_TEXT_BUFFER_SIZE]);
static bool TjcDisplay_ActiveTextIsStale(void);
static void TjcDisplay_ActivateTextRows(uint8_t page_id, uint32_t generation,
                                        const char rows[][TJC_UART_TEXT_BUFFER_SIZE],
                                        uint8_t count);

/* TJC 页面层只保留页面/事务状态；USART1、RX 环和 HAL 收发由 TjcTransport 独占。 */
static TjcTransport s_transport;
static uint8_t s_tx_complete;
static uint8_t s_tx_error;

static TjcTxState s_tx_state;
static TjcActiveView s_active_view;
/* A page change never interrupts a transparent payload mid-stream. It suppresses
 * all remaining commands, especially text writes, once the current transfer has
 * safely completed or its in-flight command returns. */
static uint8_t s_discard_active;
static uint32_t s_deadline_tick;
static uint32_t s_next_page_query_tick;
static uint8_t s_current_page_id = 0xFFU;
static uint8_t s_time_request_pending;
static TjcTimePeriodMode s_requested_period = TJC_TIME_PERIOD_ONE;
static uint8_t s_spectrum_reset_pending;

static uint8_t s_time_available;
static uint8_t s_spectrum_available;
static uint8_t s_time_text_available;
static uint8_t s_spectrum_text_available;
/* These flags request complete cle/addt/payload transactions, not ordinary text refreshes. */
static uint8_t s_time_pending;
static uint8_t s_spectrum_pending;
static uint32_t s_time_text_generation;
static uint32_t s_spectrum_text_generation;
static uint32_t s_time_text_sent_generation;
static uint32_t s_spectrum_text_sent_generation;
static uint8_t s_active_text_page_id = 0xFFU;
static uint32_t s_active_text_generation;
static uint32_t s_last_text_batch_tick;
/* pending 缓存允许测量结果随时更新；active 缓存只在透明传输完成后才可复用。 */
static uint8_t s_time_payload[TJC_TIME_POINT_COUNT];
static uint8_t s_spectrum_payload[TJC_SPECTRUM_POINT_COUNT];
static uint8_t s_active_payload[TJC_TIME_POINT_COUNT];
/* Pending time and spectrum views have independent text snapshots. The active
 * transaction copies one of them into s_text_rows before transmission. */
static char s_time_text_rows[TJC_TIME_TEXT_ROW_COUNT][TJC_UART_TEXT_BUFFER_SIZE];
static char s_spectrum_text_rows[TJC_SPECTRUM_TEXT_ROW_COUNT][TJC_UART_TEXT_BUFFER_SIZE];
static char s_text_rows[TJC_MAX_TEXT_ROW_COUNT][TJC_UART_TEXT_BUFFER_SIZE];
static const char *s_text_components[TJC_MAX_TEXT_ROW_COUNT];
static uint8_t s_text_count;
static uint8_t s_text_index;
static char s_command[TJC_UART_COMMAND_BUFFER_SIZE];

static uint8_t s_rx_frame[8];
static uint8_t s_rx_frame_length;
/* Remaining FF bytes required after an FE/FD transparent-transfer marker. */
static uint8_t s_addt_reply_ff_remaining;
static uint8_t s_event_state;
static uint8_t s_event_command;

uint8_t TjcDisplay_MapTimeVoltage(float signal_v, float display_full_scale_v)
{
    float normalized;
    float screen_value;

    if ((!isfinite(signal_v)) || (!isfinite(display_full_scale_v)) ||
        (display_full_scale_v <= 0.0f))
    {
        return 128U;
    }

    /* The HMI Waveform uses byte 0 at the bottom and byte 255 at the top. */
    normalized = signal_v / display_full_scale_v;
    screen_value = 127.5f + (127.5f * normalized) +
                   (TJC_TIME_VERTICAL_OFFSET_PX * 255.0f /
                    (float)TJC_TIME_WAVEFORM_HEIGHT);

    if (screen_value <= 0.0f)
    {
        return 0U;
    }
    if (screen_value >= 255.0f)
    {
        return 255U;
    }

    return (uint8_t)(screen_value + 0.5f);
}

void TjcDisplay_Init(UART_HandleTypeDef *uart)
{
    if (uart == NULL)
    {
        return;
    }

    s_tx_complete = 0U;
    s_tx_error = 0U;
    s_tx_state = TJC_TX_IDLE;
    s_active_view = TJC_ACTIVE_NONE;
    s_discard_active = 0U;
    s_current_page_id = 0xFFU;
    s_time_request_pending = 0U;
    s_requested_period = TJC_TIME_PERIOD_ONE;
    s_spectrum_reset_pending = 0U;
    s_time_available = 0U;
    s_spectrum_available = 0U;
    s_time_text_available = 0U;
    s_spectrum_text_available = 0U;
    s_time_pending = 0U;
    s_spectrum_pending = 0U;
    s_time_text_generation = 0U;
    s_spectrum_text_generation = 0U;
    s_time_text_sent_generation = 0U;
    s_spectrum_text_sent_generation = 0U;
    s_active_text_page_id = 0xFFU;
    s_active_text_generation = 0U;
    s_last_text_batch_tick = HAL_GetTick() - TJC_TEXT_REFRESH_INTERVAL_MS;
    s_next_page_query_tick = HAL_GetTick();
    s_rx_frame_length = 0U;
    s_addt_reply_ff_remaining = 0U;
    s_event_state = 0U;

    TjcTransport_Init(&s_transport, uart,
                      TjcDisplay_OnTransportRxByte,
                      TjcDisplay_OnTransportTxComplete,
                      TjcDisplay_OnTransportError,
                      NULL);
}

/* 主循环显示服务：消费 ISR 接收字节，并推进 addt 透明传输和文本发送状态机。 */
void TjcDisplay_Service(void)
{
    if (s_transport.uart == NULL)
    {
        return;
    }

    TjcTransport_Service(&s_transport);

    if (s_tx_error != 0U)
    {
        TjcDisplay_AbortTransaction();
        s_tx_error = 0U;
    }

    if ((s_tx_state == TJC_TX_WAIT_READY || s_tx_state == TJC_TX_WAIT_FINISHED) &&
        ((int32_t)(HAL_GetTick() - s_deadline_tick) >= 0))
    {
        TjcDisplay_AbortTransaction();
    }

    if (s_tx_complete != 0U)
    {
        s_tx_complete = 0U;

        if (s_tx_state == TJC_TX_CLEAR)
        {
            (void)snprintf(s_command, sizeof(s_command), "addt %u,%u,%u",
                           (unsigned int)((s_active_view == TJC_ACTIVE_TIME) ?
                                          TJC_TIME_WAVEFORM_ID : TJC_SPECTRUM_WAVEFORM_ID),
                           (unsigned int)((s_active_view == TJC_ACTIVE_TIME) ?
                                          TJC_TIME_WAVEFORM_CHANNEL : TJC_SPECTRUM_WAVEFORM_CHANNEL),
                           (unsigned int)((s_active_view == TJC_ACTIVE_TIME) ?
                                          TJC_TIME_POINT_COUNT : TJC_SPECTRUM_POINT_COUNT));
            if (!TjcDisplay_StartCommand(s_command, TJC_TX_ADDT_COMMAND))
            {
                TjcDisplay_AbortTransaction();
            }
        }
        else if (s_tx_state == TJC_TX_RESET_CLEAR)
        {
            if (TjcDisplay_ActiveTextIsStale())
            {
                TjcDisplay_AbortTransaction();
            }
            else
            {
                (void)snprintf(s_command, sizeof(s_command), "%s.txt=\"%.63s\"",
                               s_text_components[s_text_index], s_text_rows[s_text_index]);
                if (!TjcDisplay_StartCommand(s_command, TJC_TX_TEXT))
                {
                    TjcDisplay_AbortTransaction();
                }
            }
        }
        else if (s_tx_state == TJC_TX_ADDT_COMMAND)
        {
            s_addt_reply_ff_remaining = 0U;
            s_tx_state = TJC_TX_WAIT_READY;
            s_deadline_tick = HAL_GetTick() + TJC_TIME_UART_TIMEOUT_MS;
        }
        else if (s_tx_state == TJC_TX_PAYLOAD)
        {
            s_addt_reply_ff_remaining = 0U;
            s_tx_state = TJC_TX_WAIT_FINISHED;
            s_deadline_tick = HAL_GetTick() + TJC_TIME_UART_TIMEOUT_MS;
        }
        else if (s_tx_state == TJC_TX_TEXT)
        {
            if (TjcDisplay_ActiveTextIsStale())
            {
                TjcDisplay_AbortTransaction();
            }
            else
            {
                TjcDisplay_AdvanceText();
            }
        }
        else if (s_tx_state == TJC_TX_PAGE_QUERY)
        {
            s_tx_state = TJC_TX_IDLE;
        }
    }

    if (s_tx_state == TJC_TX_IDLE)
    {
        TjcDisplay_StartNextTransaction();
    }
}

TjcDisplayStatus TjcDisplay_SubmitTime(const TjcDisplayTimeSubmission *submission)
{
    if ((s_transport.uart == NULL) || (submission == NULL))
    {
        return TJC_TIME_STATUS_BAD_ARGUMENT;
    }

    s_time_text_available = 1U;
    s_time_text_generation++;
    (void)memcpy(s_time_text_rows, submission->text.rows, sizeof(s_time_text_rows));

    if ((submission->valid == 0U) ||
        !TjcDisplay_BuildTimePayload(submission, s_requested_period,
                                     s_time_payload, TJC_TIME_POINT_COUNT))
    {
        s_time_available = 0U;
        s_time_pending = 0U;
        return TJC_TIME_STATUS_WAVEFORM_INVALID;
    }

    s_time_available = 1U;
    return TJC_TIME_STATUS_OK;
}

TjcDisplayStatus TjcDisplay_SubmitSpectrum(const TjcDisplaySpectrumSubmission *submission)
{
#if (TJC_SPECTRUM_ENABLED == 0U)
    (void)submission;
    return TJC_TIME_STATUS_SPECTRUM_NOT_CONFIGURED;
#else
    if ((s_transport.uart == NULL) || (submission == NULL))
    {
        return TJC_TIME_STATUS_BAD_ARGUMENT;
    }

    s_spectrum_text_available = 1U;
    s_spectrum_text_generation++;
    (void)memcpy(s_spectrum_text_rows, submission->text.rows, sizeof(s_spectrum_text_rows));

    if ((submission->valid == 0U) ||
        !TjcDisplay_BuildSpectrumPayload(submission, s_spectrum_payload,
                                         TJC_SPECTRUM_POINT_COUNT))
    {
        s_spectrum_available = 0U;
        s_spectrum_pending = 0U;
        return TJC_TIME_STATUS_SPECTRUM_INVALID;
    }

    s_spectrum_available = 1U;
    return TJC_TIME_STATUS_OK;
#endif
}

uint8_t TjcDisplay_GetCurrentPageId(void)
{
    return s_current_page_id;
}

void TjcDisplay_RequestTimeWaveformRefresh(void)
{
    if ((s_time_available != 0U) && (s_current_page_id == TJC_TIME_PAGE_ID))
    {
        s_time_pending = 1U;
    }
}

void TjcDisplay_RequestSpectrumWaveformRefresh(void)
{
    if ((s_spectrum_available != 0U) && (s_current_page_id == TJC_SPECTRUM_PAGE_ID))
    {
        s_spectrum_pending = 1U;
    }
}

bool TjcDisplay_TakeTimePeriodRequest(TjcTimePeriodMode *period_mode)
{
    if ((period_mode == NULL) || (s_time_request_pending == 0U))
    {
        return false;
    }

    *period_mode = s_requested_period;
    s_time_request_pending = 0U;
    return true;
}

void TjcDisplay_OnUartRxComplete(UART_HandleTypeDef *uart)
{
    TjcTransport_OnUartRxComplete(&s_transport, uart);
}

void TjcDisplay_OnUartTxComplete(UART_HandleTypeDef *uart)
{
    TjcTransport_OnUartTxComplete(&s_transport, uart);
}

void TjcDisplay_OnUartError(UART_HandleTypeDef *uart)
{
    TjcTransport_OnUartError(&s_transport, uart);
}

static bool TjcDisplay_InterpolateTimeSample(const float *samples, uint16_t sample_count,
                                             float position, float *value)
{
    uint16_t index_1;
    uint16_t index_0;
    uint16_t index_2;
    uint16_t index_3;
    float fraction;
    float sample_0;
    float sample_1;
    float sample_2;
    float sample_3;

    if ((samples == NULL) || (value == NULL) || (sample_count < 2U) ||
        !isfinite(position) || (position < 0.0f) ||
        (position > (float)(sample_count - 1U)))
    {
        return false;
    }

    index_1 = (uint16_t)position;
    index_0 = (index_1 > 0U) ? (uint16_t)(index_1 - 1U) : index_1;
    index_2 = ((uint16_t)(index_1 + 1U) < sample_count) ?
              (uint16_t)(index_1 + 1U) : index_1;
    index_3 = ((uint16_t)(index_2 + 1U) < sample_count) ?
              (uint16_t)(index_2 + 1U) : index_2;
    fraction = position - (float)index_1;
    sample_0 = samples[index_0];
    sample_1 = samples[index_1];
    sample_2 = samples[index_2];
    sample_3 = samples[index_3];
    if (!isfinite(sample_0) || !isfinite(sample_1) ||
        !isfinite(sample_2) || !isfinite(sample_3))
    {
        return false;
    }

    /* Catmull-Rom interpolation smooths display rendering only. */
    *value = 0.5f * ((2.0f * sample_1) +
                     ((-sample_0 + sample_2) * fraction) +
                     ((2.0f * sample_0 - 5.0f * sample_1 +
                       4.0f * sample_2 - sample_3) * fraction * fraction) +
                     ((-sample_0 + 3.0f * sample_1 -
                       3.0f * sample_2 + sample_3) *
                      fraction * fraction * fraction));
    return isfinite(*value);
}

static bool TjcDisplay_BuildTimePayload(const TjcDisplayTimeSubmission *submission,
                                        TjcTimePeriodMode period_mode,
                                        uint8_t *payload,
                                        uint16_t point_count)
{
    float samples_per_period;
    float source_span;
    float start;

    if ((submission == NULL) || (payload == NULL) ||
        (submission->voltage_samples_v == NULL) ||
        (submission->sample_count != TJC_DISPLAY_TIME_SOURCE_SAMPLE_COUNT) ||
        (point_count != TJC_TIME_POINT_COUNT) ||
        !isfinite(submission->sample_rate_hz) || !isfinite(submission->fundamental_hz) ||
        (submission->sample_rate_hz <= 0.0f) || (submission->fundamental_hz <= 0.0f) ||
        ((period_mode != TJC_TIME_PERIOD_ONE) && (period_mode != TJC_TIME_PERIOD_THREE)))
    {
        return false;
    }

    samples_per_period = submission->sample_rate_hz / submission->fundamental_hz;
    source_span = samples_per_period * (float)period_mode;
    if (!isfinite(samples_per_period) || !isfinite(source_span) || (source_span < 2.0f) ||
        (source_span > (float)submission->sample_count))
    {
        return false;
    }

    /* The display adapter chooses the newest whole 1P/3P interval only from
       the upstream-confirmed period; it never searches for a period itself. */
    start = (float)submission->sample_count - source_span;
    for (uint16_t column = 0U; column < point_count; column++)
    {
        const float position = start + (source_span - 1.0f) * (float)column /
                               (float)(point_count - 1U);
        float value;

        if (!TjcDisplay_InterpolateTimeSample(submission->voltage_samples_v,
                                              submission->sample_count,
                                              position, &value))
        {
            return false;
        }

        payload[point_count - 1U - column] =
            TjcDisplay_MapTimeVoltage(value, TJC_TIME_FULL_SCALE_V);
    }

    return true;
}

static uint16_t TjcDisplay_SpectrumPermilleToCenter(uint16_t permille, uint16_t count)
{
    if (permille >= 1000U)
    {
        return (uint16_t)(count - 2U);
    }

    return (uint16_t)(1U + (((uint32_t)permille * (count - 3U) + 500U) / 1000U));
}

static uint8_t TjcDisplay_SpectrumPermilleToPeakByte(uint16_t permille)
{
    if (permille >= 1000U)
    {
        return 255U;
    }

    return (uint8_t)(((uint32_t)permille * 255U + 500U) / 1000U);
}

static bool TjcDisplay_BuildSpectrumPayload(const TjcDisplaySpectrumSubmission *submission,
                                             uint8_t *payload,
                                             uint16_t point_count)
{
    if ((submission == NULL) || (payload == NULL) || (point_count < 3U) ||
        (submission->line_count == 0U) ||
        (submission->line_count > TJC_DISPLAY_MAX_SPECTRUM_LINES))
    {
        return false;
    }

    for (uint8_t index = 0U; index < submission->line_count; index++)
    {
        const TjcDisplaySpectrumLine *line = &submission->lines[index];

        if ((line->harmonic_order == 0U) || !isfinite(line->frequency_hz) ||
            !isfinite(line->amplitude_vpk) || (line->frequency_hz <= 0.0f) ||
            (line->amplitude_vpk < 0.0f))
        {
            return false;
        }
    }

    /* Plot each upstream-confirmed line on the fixed 0..500 kHz, 0..125 mVpk
       axes. Values outside the contest measurement band are not displayed. */
    (void)memset(payload, 0, point_count);
    for (uint8_t index = 0U; index < submission->line_count; index++)
    {
        const TjcDisplaySpectrumLine *line = &submission->lines[index];
        float x_ratio;
        float height;
        uint16_t x_permille;
        uint16_t height_permille;
        uint16_t center;
        uint8_t peak;

        if ((line->frequency_hz < TJC_SPECTRUM_MIN_FREQUENCY_HZ) ||
            (line->frequency_hz > TJC_SPECTRUM_MAX_FREQUENCY_HZ))
        {
            continue;
        }

        x_ratio = line->frequency_hz / TJC_SPECTRUM_MAX_FREQUENCY_HZ;
        height = line->amplitude_vpk / TJC_SPECTRUM_FULL_SCALE_AMPLITUDE_V;
        x_permille = (x_ratio >= 1.0f) ? 1000U :
                     (uint16_t)(x_ratio * 1000.0f + 0.5f);
        height_permille = (height >= 1.0f) ? 1000U :
                           (uint16_t)(height * 1000.0f + 0.5f);
        center = TjcDisplay_SpectrumPermilleToCenter(
            (uint16_t)(1000U - x_permille), point_count);
        peak = TjcDisplay_SpectrumPermilleToPeakByte(height_permille);

        if (peak > payload[center])
        {
            payload[center] = peak;
        }
    }

    return true;
}

static void TjcDisplay_OnTransportRxByte(void *context, uint8_t byte)
{
    (void)context;
    TjcDisplay_ParseRxByte(byte);
}

static void TjcDisplay_OnTransportTxComplete(void *context)
{
    (void)context;
    s_tx_complete = 1U;
}

static void TjcDisplay_OnTransportError(void *context)
{
    (void)context;
    s_tx_error = 1U;
}

static bool TjcDisplay_ConsumeAddtReplyByte(uint8_t byte)
{
    const uint8_t marker = (s_tx_state == TJC_TX_WAIT_READY) ?
                           TJC_ADDT_READY : TJC_ADDT_FINISHED;

    if ((s_tx_state != TJC_TX_WAIT_READY) && (s_tx_state != TJC_TX_WAIT_FINISHED))
    {
        return false;
    }

    if (s_addt_reply_ff_remaining == 0U)
    {
        if (byte == marker)
        {
            s_addt_reply_ff_remaining = 3U;
            return true;
        }
        return false;
    }

    if (byte != TJC_COMMAND_END)
    {
        /* A repeated marker restarts reply framing; other bytes are parsed normally. */
        s_addt_reply_ff_remaining = (byte == marker) ? 3U : 0U;
        return (byte == marker);
    }

    s_addt_reply_ff_remaining--;
    if (s_addt_reply_ff_remaining != 0U)
    {
        return true;
    }

    if (s_tx_state == TJC_TX_WAIT_READY)
    {
        const uint8_t *payload = s_active_payload;
        const uint16_t count = (s_active_view == TJC_ACTIVE_TIME) ?
                               TJC_TIME_POINT_COUNT : TJC_SPECTRUM_POINT_COUNT;

        if (!TjcDisplay_StartRaw(payload, count, TJC_TX_PAYLOAD))
        {
            TjcDisplay_AbortTransaction();
        }
    }
    else if ((s_text_count == 0U) || TjcDisplay_ActiveTextIsStale())
    {
        TjcDisplay_AbortTransaction();
    }
    else
    {
        s_text_index = 0U;
        (void)snprintf(s_command, sizeof(s_command), "%s.txt=\"%.63s\"",
                       s_text_components[s_text_index], s_text_rows[s_text_index]);
        if (!TjcDisplay_StartCommand(s_command, TJC_TX_TEXT))
        {
            TjcDisplay_AbortTransaction();
        }
    }

    return true;
}

static void TjcDisplay_ParseRxByte(uint8_t byte)
{
    if (TjcDisplay_ConsumeAddtReplyByte(byte))
    {
        return;
    }

    /* The raw four-byte printh event stream is independent of FF-terminated
       standard TJC frames. */
    if (s_event_state == 0U)
    {
        s_event_state = (byte == TJC_EVENT_HEADER_0) ? 1U : 0U;
    }
    else if (s_event_state == 1U)
    {
        s_event_state = (byte == TJC_EVENT_HEADER_1) ? 2U :
                        ((byte == TJC_EVENT_HEADER_0) ? 1U : 0U);
    }
    else if (s_event_state == 2U)
    {
        s_event_command = byte;
        s_event_state = 3U;
    }
    else
    {
        if ((s_event_command == TJC_EVENT_TIME_MODE) &&
            (byte == TJC_EVENT_TIME_ONE_PERIOD || byte == TJC_EVENT_TIME_THREE_PERIOD) &&
            (s_current_page_id == TJC_TIME_PAGE_ID))
        {
            s_requested_period = (byte == TJC_EVENT_TIME_ONE_PERIOD) ?
                                 TJC_TIME_PERIOD_ONE : TJC_TIME_PERIOD_THREE;
            s_time_request_pending = 1U;
        }
        s_event_state = 0U;
    }

    /* Only page frames are enabled in the HMI contract. Synchronize on 0x66 so
       raw printh events cannot contaminate the FF-terminated frame parser. */
    if (s_rx_frame_length == 0U)
    {
        if (byte == TJC_RX_PAGE_ID)
        {
            s_rx_frame[0] = byte;
            s_rx_frame_length = 1U;
        }
        return;
    }

    s_rx_frame[s_rx_frame_length++] = byte;
    if (s_rx_frame_length < 5U)
    {
        return;
    }

    if ((s_rx_frame[0] == TJC_RX_PAGE_ID) &&
        (s_rx_frame[2] == TJC_RX_FRAME_END) &&
        (s_rx_frame[3] == TJC_RX_FRAME_END) &&
        (s_rx_frame[4] == TJC_RX_FRAME_END))
    {
        TjcDisplay_HandlePageId(s_rx_frame[1]);
    }
    s_rx_frame_length = 0U;
}

static void TjcDisplay_HandlePageId(uint8_t page_id)
{
    const uint8_t previous_page_id = s_current_page_id;

    s_current_page_id = page_id;

    if (((s_active_view == TJC_ACTIVE_TIME) && (page_id != TJC_TIME_PAGE_ID)) ||
        (((s_active_view == TJC_ACTIVE_SPECTRUM) ||
          (s_active_view == TJC_ACTIVE_SPECTRUM_RESET)) &&
         (page_id != TJC_SPECTRUM_PAGE_ID)) ||
        ((s_active_view == TJC_ACTIVE_TEXT) && (page_id != s_active_text_page_id)))
    {
        s_discard_active = 1U;
    }

    if ((page_id == TJC_TIME_PAGE_ID) && (previous_page_id != page_id))
    {
        if (s_time_available != 0U)
        {
            s_time_pending = 1U;
        }
    }
    else if ((page_id == TJC_SPECTRUM_PAGE_ID) && (previous_page_id != page_id))
    {
        s_spectrum_reset_pending = 1U;
        if (s_spectrum_available != 0U)
        {
            s_spectrum_pending = 1U;
        }
    }
}

static void TjcDisplay_StartNextTransaction(void)
{
    uint8_t waveform_id;
    uint8_t waveform_channel;

    if (s_transport.uart == NULL)
    {
        return;
    }

    /* The screen may boot before USART1 RX is armed and its preinitialize
       sendme can be missed. Query until the first valid page reply arrives. */
    if ((s_current_page_id == 0xFFU) && (s_rx_frame_length == 0U) &&
        ((int32_t)(HAL_GetTick() - s_next_page_query_tick) >= 0))
    {
        s_next_page_query_tick = HAL_GetTick() + TJC_PAGE_QUERY_INTERVAL_MS;
        (void)snprintf(s_command, sizeof(s_command), "sendme");
        if (!TjcDisplay_StartCommand(s_command, TJC_TX_PAGE_QUERY))
        {
            TjcDisplay_AbortTransaction();
        }
        return;
    }

    if ((s_spectrum_reset_pending != 0U) && (s_current_page_id == TJC_SPECTRUM_PAGE_ID))
    {
        s_spectrum_reset_pending = 0U;
        s_active_view = TJC_ACTIVE_SPECTRUM_RESET;
        s_text_components[0] = TJC_SPECTRUM_TEXT_FUNDAMENTAL;
        s_text_components[1] = TJC_SPECTRUM_TEXT_LINE_COUNT;
        s_text_components[2] = TJC_SPECTRUM_TEXT_ROW_1;
        s_text_components[3] = TJC_SPECTRUM_TEXT_ROW_2;
        s_text_components[4] = TJC_SPECTRUM_TEXT_ROW_3;
        TjcDisplay_FormatEmptySpectrumText(s_text_rows);
        s_active_text_page_id = TJC_SPECTRUM_PAGE_ID;
        s_active_text_generation = s_spectrum_text_generation;
        s_text_count = TJC_SPECTRUM_TEXT_ROW_COUNT;
        s_text_index = 0U;
        (void)snprintf(s_command, sizeof(s_command), "cle %u,%u",
                       (unsigned int)TJC_SPECTRUM_WAVEFORM_ID,
                       (unsigned int)TJC_SPECTRUM_WAVEFORM_CHANNEL);
        if (!TjcDisplay_StartCommand(s_command, TJC_TX_RESET_CLEAR))
        {
            TjcDisplay_AbortTransaction();
        }
        return;
    }

    if ((s_time_pending != 0U) && (s_time_available != 0U) &&
        (s_current_page_id == TJC_TIME_PAGE_ID))
    {
        s_time_pending = 0U;
        s_active_view = TJC_ACTIVE_TIME;
        waveform_id = TJC_TIME_WAVEFORM_ID;
        waveform_channel = TJC_TIME_WAVEFORM_CHANNEL;
        s_text_components[0] = TJC_TIME_TEXT_UPP;
        s_text_components[1] = TJC_TIME_TEXT_URMS;
        s_text_components[2] = TJC_TIME_TEXT_FUNDAMENTAL;
        s_text_components[3] = TJC_TIME_TEXT_COMPONENT_1;
        s_text_components[4] = TJC_TIME_TEXT_COMPONENT_2;
        s_text_components[5] = TJC_TIME_TEXT_COMPONENT_3;
        (void)memcpy(s_active_payload, s_time_payload, TJC_TIME_POINT_COUNT);
        TjcDisplay_ActivateTextRows(TJC_TIME_PAGE_ID, s_time_text_generation,
                                    s_time_text_rows, TJC_TIME_TEXT_ROW_COUNT);
        s_time_text_sent_generation = s_time_text_generation;
        s_last_text_batch_tick = HAL_GetTick();
    }
    else if ((s_spectrum_pending != 0U) && (s_spectrum_available != 0U) &&
             (s_current_page_id == TJC_SPECTRUM_PAGE_ID))
    {
        s_spectrum_pending = 0U;
        s_active_view = TJC_ACTIVE_SPECTRUM;
        waveform_id = TJC_SPECTRUM_WAVEFORM_ID;
        waveform_channel = TJC_SPECTRUM_WAVEFORM_CHANNEL;
        s_text_components[0] = TJC_SPECTRUM_TEXT_FUNDAMENTAL;
        s_text_components[1] = TJC_SPECTRUM_TEXT_LINE_COUNT;
        s_text_components[2] = TJC_SPECTRUM_TEXT_ROW_1;
        s_text_components[3] = TJC_SPECTRUM_TEXT_ROW_2;
        s_text_components[4] = TJC_SPECTRUM_TEXT_ROW_3;
        (void)memcpy(s_active_payload, s_spectrum_payload, TJC_SPECTRUM_POINT_COUNT);
        TjcDisplay_ActivateTextRows(TJC_SPECTRUM_PAGE_ID, s_spectrum_text_generation,
                                    s_spectrum_text_rows, TJC_SPECTRUM_TEXT_ROW_COUNT);
        s_spectrum_text_sent_generation = s_spectrum_text_generation;
        s_last_text_batch_tick = HAL_GetTick();
    }
    else if ((s_time_request_pending == 0U) &&
             ((int32_t)(HAL_GetTick() - s_last_text_batch_tick) >=
              (int32_t)TJC_TEXT_REFRESH_INTERVAL_MS) &&
             (s_current_page_id == TJC_TIME_PAGE_ID) &&
             (s_time_text_available != 0U) &&
             (s_time_text_generation != s_time_text_sent_generation))
    {
        s_active_view = TJC_ACTIVE_TEXT;
        s_text_components[0] = TJC_TIME_TEXT_UPP;
        s_text_components[1] = TJC_TIME_TEXT_URMS;
        s_text_components[2] = TJC_TIME_TEXT_FUNDAMENTAL;
        s_text_components[3] = TJC_TIME_TEXT_COMPONENT_1;
        s_text_components[4] = TJC_TIME_TEXT_COMPONENT_2;
        s_text_components[5] = TJC_TIME_TEXT_COMPONENT_3;
        TjcDisplay_ActivateTextRows(TJC_TIME_PAGE_ID, s_time_text_generation,
                                    s_time_text_rows, TJC_TIME_TEXT_ROW_COUNT);
        s_time_text_sent_generation = s_time_text_generation;
        s_last_text_batch_tick = HAL_GetTick();
        (void)snprintf(s_command, sizeof(s_command), "%s.txt=\"%.63s\"",
                       s_text_components[0], s_text_rows[0]);
        if (!TjcDisplay_StartCommand(s_command, TJC_TX_TEXT))
        {
            TjcDisplay_AbortTransaction();
        }
        return;
    }
    else if ((s_time_request_pending == 0U) &&
             ((int32_t)(HAL_GetTick() - s_last_text_batch_tick) >=
              (int32_t)TJC_TEXT_REFRESH_INTERVAL_MS) &&
             (s_current_page_id == TJC_SPECTRUM_PAGE_ID) &&
             (s_spectrum_text_available != 0U) &&
             (s_spectrum_text_generation != s_spectrum_text_sent_generation))
    {
        s_active_view = TJC_ACTIVE_TEXT;
        s_text_components[0] = TJC_SPECTRUM_TEXT_FUNDAMENTAL;
        s_text_components[1] = TJC_SPECTRUM_TEXT_LINE_COUNT;
        s_text_components[2] = TJC_SPECTRUM_TEXT_ROW_1;
        s_text_components[3] = TJC_SPECTRUM_TEXT_ROW_2;
        s_text_components[4] = TJC_SPECTRUM_TEXT_ROW_3;
        TjcDisplay_ActivateTextRows(TJC_SPECTRUM_PAGE_ID, s_spectrum_text_generation,
                                    s_spectrum_text_rows, TJC_SPECTRUM_TEXT_ROW_COUNT);
        s_spectrum_text_sent_generation = s_spectrum_text_generation;
        s_last_text_batch_tick = HAL_GetTick();
        (void)snprintf(s_command, sizeof(s_command), "%s.txt=\"%.63s\"",
                       s_text_components[0], s_text_rows[0]);
        if (!TjcDisplay_StartCommand(s_command, TJC_TX_TEXT))
        {
            TjcDisplay_AbortTransaction();
        }
        return;
    }
    else
    {
        return;
    }

    s_text_index = 0U;
    (void)snprintf(s_command, sizeof(s_command), "cle %u,%u",
                   (unsigned int)waveform_id, (unsigned int)waveform_channel);
    if (!TjcDisplay_StartCommand(s_command, TJC_TX_CLEAR))
    {
        TjcDisplay_AbortTransaction();
    }
}

static bool TjcDisplay_StartCommand(const char *command, TjcTxState next_state)
{
    const uint16_t length = (uint16_t)strlen(command);

    if ((s_transport.uart == NULL) || (command == NULL) ||
        ((uint32_t)length + 3U > sizeof(s_command)))
    {
        return false;
    }

    /* command 通常别名 s_command；追加结束符后交给 transport 发送。 */
    s_command[length] = TJC_COMMAND_END;
    s_command[length + 1U] = TJC_COMMAND_END;
    s_command[length + 2U] = TJC_COMMAND_END;
    if (!TjcTransport_Send(&s_transport, (const uint8_t *)s_command, (uint16_t)(length + 3U)))
    {
        return false;
    }

    s_tx_state = next_state;
    return true;
}

static bool TjcDisplay_StartRaw(const uint8_t *data, uint16_t length,
                                TjcTxState next_state)
{
    if ((s_transport.uart == NULL) || (data == NULL) || (length == 0U) ||
        !TjcTransport_Send(&s_transport, data, length))
    {
        return false;
    }

    s_tx_state = next_state;
    return true;
}

static void TjcDisplay_AbortTransaction(void)
{
    s_tx_state = TJC_TX_IDLE;
    s_addt_reply_ff_remaining = 0U;
    s_active_view = TJC_ACTIVE_NONE;
    s_discard_active = 0U;
    s_active_text_page_id = 0xFFU;
    s_active_text_generation = 0U;
    s_text_count = 0U;
    s_text_index = 0U;
}

static bool TjcDisplay_ActiveTextIsStale(void)
{
    if ((s_discard_active != 0U) || (s_current_page_id != s_active_text_page_id))
    {
        return true;
    }

    if (s_active_text_page_id == TJC_TIME_PAGE_ID)
    {
        return s_active_text_generation != s_time_text_generation;
    }
    if (s_active_text_page_id == TJC_SPECTRUM_PAGE_ID)
    {
        return s_active_text_generation != s_spectrum_text_generation;
    }

    return true;
}

static void TjcDisplay_ActivateTextRows(uint8_t page_id, uint32_t generation,
                                        const char rows[][TJC_UART_TEXT_BUFFER_SIZE],
                                        uint8_t count)
{
    (void)memcpy(s_text_rows, rows, (size_t)count * TJC_UART_TEXT_BUFFER_SIZE);
    s_active_text_page_id = page_id;
    s_active_text_generation = generation;
    s_text_count = count;
    s_text_index = 0U;
}

static void TjcDisplay_AdvanceText(void)
{
    s_text_index++;
    if ((s_text_index >= s_text_count) || TjcDisplay_ActiveTextIsStale())
    {
        TjcDisplay_AbortTransaction();
        return;
    }

    (void)snprintf(s_command, sizeof(s_command), "%s.txt=\"%.63s\"",
                   s_text_components[s_text_index], s_text_rows[s_text_index]);
    if (!TjcDisplay_StartCommand(s_command, TJC_TX_TEXT))
    {
        TjcDisplay_AbortTransaction();
    }
}

static void TjcDisplay_FormatEmptySpectrumText(char rows[][TJC_UART_TEXT_BUFFER_SIZE])
{
    for (uint8_t index = 0U; index < TJC_SPECTRUM_TEXT_ROW_COUNT; index++)
    {
        (void)snprintf(rows[index], TJC_UART_TEXT_BUFFER_SIZE, "--");
    }
}
