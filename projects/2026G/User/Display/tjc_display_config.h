/*
 * tjc_display_config.h
 *
 * Central HMI contract for the current TJC display project. The transparent
 * transfer point counts match the configured waveform widths so
 * each transmitted byte occupies one visible horizontal display column.
 */

#ifndef PRESENTATION_INC_TJC_DISPLAY_CONFIG_H_
#define PRESENTATION_INC_TJC_DISPLAY_CONFIG_H_

#include <stdint.h>

/* page0 Waveform configuration. */
#define TJC_TIME_PAGE_ID                  (0U)  /* page0 */
#define TJC_TIME_WAVEFORM_ID              (1U)  /* wv_time */
#define TJC_TIME_WAVEFORM_CHANNEL         (0U)  /* ch=1: only channel 0 */

/* wv_time uses a 610 x 280 px plot area. Send exactly one resampled time
 * value per visible horizontal display column. Set the HMI grid to gdw=122 px
 * and gdh=70 px. The fixed vertical range is -125..125 mV, so the centre line
 * is 0 mV and the complete display range is 250 mVpp. */
#define TJC_TIME_WAVEFORM_X                (185U)
#define TJC_TIME_WAVEFORM_Y                (125U)
#define TJC_TIME_WAVEFORM_WIDTH            (610U)
#define TJC_TIME_WAVEFORM_HEIGHT           (280U)
#define TJC_TIME_POINT_COUNT               (610U)
#define TJC_TIME_GRID_WIDTH_PX             (122U)
#define TJC_TIME_GRID_HEIGHT_PX            (70U)
#define TJC_TIME_FULL_SCALE_V              (0.125f)
/* Positive values move the complete time trace upward in physical screen
 * pixels. This is a display-only HMI alignment, not a signal correction. */
#define TJC_TIME_VERTICAL_OFFSET_PX        (11.0f)

/* page0 dynamic text controls: overall values, fundamental frequency, and up
 * to three fitted components. Component rows report individual Vpp values. */
#define TJC_TIME_TEXT_UPP                 "page0.t_upp"
#define TJC_TIME_TEXT_URMS                "page0.t_urms"
#define TJC_TIME_TEXT_FUNDAMENTAL         "page0.t_f1"
#define TJC_TIME_TEXT_COMPONENT_1         "page0.t_01"
#define TJC_TIME_TEXT_COMPONENT_2         "page0.t_02"
#define TJC_TIME_TEXT_COMPONENT_3         "page0.t_03"
#define TJC_TIME_TEXT_ROW_COUNT           (6U)

/* Confirmed page1 spectrum Waveform s0 configuration.
 * s0: ID=5, ch=1 (therefore channel 0). Hardware observation
 * confirms a right-to-left serial display direction, so the adapter mirrors
 * sorted positions and sends high-frequency bins first. The 610-byte transfer
 * fills the configured 610-pixel horizontal width. */
#define TJC_SPECTRUM_ENABLED              (1U)
#define TJC_SPECTRUM_PAGE_ID              (1U)  /* page1 */
#define TJC_SPECTRUM_WAVEFORM_ID          (5U)  /* s0 */
#define TJC_SPECTRUM_WAVEFORM_CHANNEL     (0U)
#define TJC_SPECTRUM_POINT_COUNT          (610U)
#define TJC_SPECTRUM_WAVEFORM_X           (185U)
#define TJC_SPECTRUM_WAVEFORM_Y           (125U)
#define TJC_SPECTRUM_WAVEFORM_WIDTH       (610U)
#define TJC_SPECTRUM_WAVEFORM_HEIGHT      (255U)
/* s0 HMI grid contract: gdw=122 px and gdh=51 px. This produces five
 * vertical 100 kHz divisions (0..500 kHz) and five horizontal 25 mV
 * amplitude divisions (0..125 mV) over the 610 x 255 px Waveform. */
#define TJC_SPECTRUM_GRID_WIDTH_PX         (122U)
#define TJC_SPECTRUM_GRID_HEIGHT_PX        (51U)
#define TJC_SPECTRUM_MIN_FREQUENCY_HZ      (10000.0f)
#define TJC_SPECTRUM_MAX_FREQUENCY_HZ      (500000.0f)
/* Upstream spectrum amplitudes and the page1 vertical axis both use Vpk. */
#define TJC_SPECTRUM_FULL_SCALE_AMPLITUDE_V (0.125f)
#define TJC_SPECTRUM_FULL_SCALE_VPK         (TJC_SPECTRUM_FULL_SCALE_AMPLITUDE_V)

/* page1 dynamic text controls: fundamental frequency, line count, and one
 * combined "frequency + amplitude" row per possible fitted component. */
#define TJC_SPECTRUM_TEXT_FUNDAMENTAL     "page1.t_f1"
#define TJC_SPECTRUM_TEXT_LINE_COUNT      "page1.t0"
#define TJC_SPECTRUM_TEXT_ROW_1           "page1.t_11"
#define TJC_SPECTRUM_TEXT_ROW_2           "page1.t_12"
#define TJC_SPECTRUM_TEXT_ROW_3           "page1.t_13"
#define TJC_SPECTRUM_TEXT_ROW_COUNT       (5U)
#define TJC_MAX_TEXT_ROW_COUNT             (TJC_TIME_TEXT_ROW_COUNT)

/* TJC transparent-transfer markers and normal command terminator. */
#define TJC_ADDT_READY                    (0xFEU)
#define TJC_ADDT_FINISHED                 (0xFDU)
#define TJC_COMMAND_END                   (0xFFU)
#define TJC_TIME_UART_TIMEOUT_MS          (200U)
#define TJC_TEXT_REFRESH_INTERVAL_MS      (100U)
#define TJC_PAGE_QUERY_INTERVAL_MS        (500U)

/* Display UART receive and transaction limits. The RX ring is ISR producer /
 * foreground consumer and therefore must use an 8-bit, power-of-two size. */
#define TJC_UART_RX_RING_SIZE             (128U)
#define TJC_UART_COMMAND_BUFFER_SIZE      (128U)
#define TJC_UART_TEXT_BUFFER_SIZE         (64U)

/* Standard TJC return-frame types. Both page preinitialize events must issue
 * `sendme`, yielding: 66 page_id FF FF FF. */
#define TJC_RX_PAGE_ID                    (0x66U)
#define TJC_RX_FRAME_END                  (0xFFU)

/* HMI button release events use raw printh bytes, with no ASCII terminator.
 * page0 1P button: printh 5A A5 01 01
 * page0 3P button: printh 5A A5 01 03
 * page1 may retain visually matching 1P/3P buttons, but their events are
 * intentionally ignored by firmware. */
#define TJC_EVENT_HEADER_0                (0x5AU)
#define TJC_EVENT_HEADER_1                (0xA5U)
#define TJC_EVENT_TIME_MODE               (0x01U)
#define TJC_EVENT_TIME_ONE_PERIOD         (0x01U)
#define TJC_EVENT_TIME_THREE_PERIOD       (0x03U)

#endif /* PRESENTATION_INC_TJC_DISPLAY_CONFIG_H_ */
