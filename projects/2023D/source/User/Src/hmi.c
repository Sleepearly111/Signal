#include "hmi.h"
#include "main.h"
#include "usart.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define HMI_COMMAND_BUFFER_SIZE 128U
#define HMI_START_FRAME_LENGTH  6U

static const uint8_t hmi_command_end[3] = {0xFFU, 0xFFU, 0xFFU};
static const uint8_t hmi_start_frame[HMI_START_FRAME_LENGTH] = {
  0x55U, 0xAAU, 0x01U, 0x00U, 0x0DU, 0x0AU
};

static uint8_t hmi_rx_byte;
static uint8_t hmi_start_frame_index;
static bool hmi_receive_started;

static bool HMI_SendCommand(const char *command)
{
  size_t length;

  if (command == NULL) return false;
  length = strlen(command);
  if (length == 0U) return false;

  if (HAL_UART_Transmit(&huart1, (uint8_t *)command,
                        (uint16_t)length, 100U) != HAL_OK)
    return false;

  return HAL_UART_Transmit(&huart1, (uint8_t *)hmi_command_end,
                           sizeof(hmi_command_end), 100U) == HAL_OK;
}

static void HMI_ArmReceive(void)
{
  HAL_UART_Receive_IT(&huart1, &hmi_rx_byte, 1U);
}

static void HMI_EnsureReceiveStarted(void)
{
  if (hmi_receive_started) return;
  hmi_receive_started = true;
  hmi_start_frame_index = 0U;
  HMI_ArmReceive();
}

static void HMI_ParseByte(uint8_t byte)
{
  if (byte == hmi_start_frame[hmi_start_frame_index]) {
    hmi_start_frame_index++;
    if (hmi_start_frame_index >= HMI_START_FRAME_LENGTH) {
      hmi_start_frame_index = 0U;
      /* 不修改主循环，复用现有KEY0启动路径。 */
      HAL_GPIO_EXTI_Callback(KEY0_Pin);
    }
  } else if (byte == hmi_start_frame[0]) {
    hmi_start_frame_index = 1U;
  } else {
    hmi_start_frame_index = 0U;
  }
}

static void HMI_SetStartButton(bool enabled, const char *caption)
{
  char command[HMI_COMMAND_BUFFER_SIZE];

  snprintf(command, sizeof(command), "bStart.en=%u", enabled ? 1U : 0U);
  HMI_SendCommand(command);

  snprintf(command, sizeof(command), "bStart.txt=\"%s\"", caption);
  HMI_SendCommand(command);
}

void HMI_SetText(const char *obj, const char *txt)
{
  char command[HMI_COMMAND_BUFFER_SIZE];
  int length;

  if (obj == NULL || txt == NULL) return;
  if (strchr(txt, '"') != NULL) return;

  HMI_EnsureReceiveStarted();
  length = snprintf(command, sizeof(command), "%s.txt=\"%s\"", obj, txt);
  if (length <= 0 || (size_t)length >= sizeof(command)) return;
  HMI_SendCommand(command);
}

void HMI_ShowStatus(const char *status)
{
  HMI_SetText("tStatus", status);

  if (strstr(status, "CAPTURING") != NULL ||
      strstr(status, "SELFTEST") != NULL) {
    HMI_SetStartButton(false, "Testing...");
  } else if (strstr(status, "IDLE") != NULL) {
    HMI_SetStartButton(true, "Start");
  } else {
    HMI_SetStartButton(true, "Retest");
  }
}

void HMI_ShowResult(const EstResult_t *result)
{
  static const char *const type_names[] = {
    "UNKNOWN", "AM", "FM", "CW", "2ASK", "2FSK", "2PSK"
  };
  char line[HMI_COMMAND_BUFFER_SIZE];

  if (result == NULL) return;

  HMI_SetText("tP1", "-");
  HMI_SetText("tP2", "-");
  HMI_SetText("tP3", "-");

  if ((unsigned)result->type <
      (sizeof(type_names) / sizeof(type_names[0])))
    HMI_SetText("tType", type_names[result->type]);
  else
    HMI_SetText("tType", "UNKNOWN");

  switch (result->type) {
  case MOD_CW:
    HMI_SetText("tP1", "No modulation");
    break;

  case MOD_AM:
    snprintf(line, sizeof(line), "F = %.3f kHz", result->F_hz / 1000.0f);
    HMI_SetText("tP1", line);
    snprintf(line, sizeof(line), "ma = %.2f", result->ma);
    HMI_SetText("tP2", line);
    break;

  case MOD_FM:
    snprintf(line, sizeof(line), "F = %.3f kHz", result->F_hz / 1000.0f);
    HMI_SetText("tP1", line);
    snprintf(line, sizeof(line), "mf = %.2f", result->mf);
    HMI_SetText("tP2", line);
    snprintf(line, sizeof(line), "df = %.3f kHz",
             result->dfmax / 1000.0f);
    HMI_SetText("tP3", line);
    break;

  case MOD_2ASK:
  case MOD_2PSK:
    snprintf(line, sizeof(line), "Rc = %.2f kbps",
             result->Rc_bps / 1000.0f);
    HMI_SetText("tP1", line);
    break;

  case MOD_2FSK:
    snprintf(line, sizeof(line), "Rc = %.2f kbps",
             result->Rc_bps / 1000.0f);
    HMI_SetText("tP1", line);
    snprintf(line, sizeof(line), "h = %.2f", result->h);
    HMI_SetText("tP2", line);
    break;

  default:
    break;
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != USART1) return;
  HMI_ParseByte(hmi_rx_byte);
  HMI_ArmReceive();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != USART1) return;
  HAL_UART_AbortReceive(huart);
  HMI_ArmReceive();
}

void dbg_printf(const char *fmt, ...)
{
  char buffer[160];
  va_list args;
  int length;

  va_start(args, fmt);
  length = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  if (length >= (int)sizeof(buffer)) length = sizeof(buffer) - 1;
  if (length > 0)
    HAL_UART_Transmit(&huart2, (uint8_t *)buffer,
                      (uint16_t)length, 100U);
}
