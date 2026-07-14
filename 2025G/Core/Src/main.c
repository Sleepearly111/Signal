/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  *
  * 2025 电赛 G 题 — 电路模型探究装置 (基础部分 2/3/4)
  * 硬件: STM32F407, ADS8688(ADC), AD9833(DDS), AD603(VGA), AD8052(AMP)
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dac.h"
#include "dma.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "arm_math.h"
#include "math.h"
#include <stdio.h>
#include <string.h>

/* 复用驱动 */
#include "AD9833.h"
#include "ADS8688.h"

/* 2025G 应用模块 */
#include "app_config.h"
#include "dds_output.h"
#include "adc_measure.h"
#include "transfer_calc.h"
#include "ui_basic.h"
#include "adva_learn.h"
#include "adva_replay.h"
#include "dsp_utils.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
extern DAC_HandleTypeDef hdac;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ===== USART1 中断回调: 串口屏逐字节接收 ===== */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        uint8_t byte;
        HAL_UART_Receive(huart, &byte, 1, 0);
        UI_UART_RxCallback(byte);
        __HAL_UART_ENABLE_IT(huart, UART_IT_RXNE);
    }
}

/* ===== 按键 EXTI 回调 ===== */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    static uint32_t key_last_tick = 0;

    /* 10ms 简单消抖（勿在中断里用 HAL_Delay） */
    uint32_t now = HAL_GetTick();
    if (now - key_last_tick < 10) {
        return;
    }
    key_last_tick = now;

    /* 按键上拉输入、下降沿触发，读回低电平才确认真正按下 */
    if (GPIO_Pin == KEY0_Pin) {
        if (HAL_GPIO_ReadPin(KEY0_GPIO_Port, KEY0_Pin) == GPIO_PIN_RESET) {
            extern volatile uint8_t key;
            key = 1;            /* 通知标定按键循环 */
            UI_KeyCallback(0);  /* KEY0 → 基本(3) */
        }
    } else if (GPIO_Pin == KEY1_Pin) {
        if (HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == GPIO_PIN_RESET) {
            UI_KeyCallback(1);  /* KEY1 → 基本(4) */
        }
    }
}

/* ===== 闭环校准: 把当前频率的输出幅度调到 target_vpp ===== */
/* 对当前频率输出信号，测实际 Vpp；不足则逐次加 VGA 增益直到达标。返回实际 Vpp */
static float CalibrateToVpp(uint32_t freq_hz, float target_vpp)
{
    float current_gain_linear = 1.0f;  /* 从 0dB 起步 */
    float measured_vpp = 0.0f;
    uint8_t max_attempts = 10;

    DDS_SetFrequency(freq_hz);

    for (uint8_t attempt = 0; attempt < max_attempts; attempt++) {
        VGA_SetGain_Linear(current_gain_linear);
        HAL_Delay(50);  /* 等输出稳定 */

        ADC_Measure_Start(0);  /* ADS8688 CH0 */
        while (!ADC_Measure_IsDone()) {
            ADS8688_Service();
        }

        ADC_MeasureResult res = ADC_Measure_GetResult();
        measured_vpp = res.vpp;

        printf("[CAL] freq=%luHz gain=%.2f vpp=%.3fV\r\n",
               freq_hz, current_gain_linear, measured_vpp);

        if (measured_vpp >= target_vpp) {
            break;  /* 达标 */
        }

        float needed = target_vpp / (measured_vpp + 0.001f);
        current_gain_linear *= needed;

        if (current_gain_linear > 100.0f) {
            current_gain_linear = 100.0f;  /* AD603 最大约 42dB ≈ 126倍 */
            break;
        }
    }

    return measured_vpp;
}

/* ===== 把【装置自身输出】闭环调到 target_vpp（基本3/4 用） =====
 * 题目说明2: 基本3/4不能测电路输出。所以这里只测 CH0=装置自身输出(允许),
 * 双向比例逼近目标;电路输出端不连接。返回实测装置输出 Vpp。 */
static float SetDeviceOutputVpp(uint32_t freq_hz, float target_vpp)
{
    float gain = 1.0f;
    float measured = 0.0f;

    DDS_SetFrequency(freq_hz);

    for (uint8_t attempt = 0; attempt < 8; attempt++) {
        VGA_SetGain_Linear(gain);
        HAL_Delay(50);

        ADC_Measure_Start(0);  /* CH0 = 装置自身输出 */
        while (!ADC_Measure_IsDone()) {
            ADS8688_Service();
        }
        measured = ADC_Measure_GetResult().vpp;

        printf("[SET] freq=%luHz gain=%.3f dev_vpp=%.3fV (目标%.3f)\r\n",
               freq_hz, gain, measured, target_vpp);

        if (measured < 0.001f) break;

        float err = (measured - target_vpp) / target_vpp;
        if (fabsf(err) < 0.02f) break;   /* 收敛到 2% 即可 */

        gain *= target_vpp / measured;   /* 双向比例调整(可增可减) */
        if (gain > 126.0f) gain = 126.0f;
        if (gain < 0.001f) gain = 0.001f;
    }

    return measured;
}

/* ===== 基本(2): 设定频率(步长100Hz,最高>=1MHz)，输出稳定 3.5Vpp(要求>=3V) ===== */
static void RunBasic2(void)
{
    uint32_t freq = g_set_freq_hz;

    printf("=== 基本(2): %luHz → 输出 %.3fVpp ===\r\n", freq, OUTPUT_VPP_TARGET);

    float vpp = CalibrateToVpp(freq, OUTPUT_VPP_TARGET);

    printf("  实测输出: %.3fVpp (目标 %.3fVpp, 要求 >=%.2fV)\r\n",
           vpp, OUTPUT_VPP_TARGET, OUTPUT_VPP_MIN);

    UI_UpdateDisplay(freq, vpp);
    g_mode = MODE_IDLE;
}

/* ===== 基本(3): 1kHz → 已知电路输出 2Vpp =====
 *
 * 合规架构（题目"说明2": 装置与电路输出端【无反馈连接】,不能测电路输出）:
 *   1. 电路增益 |H(1kHz)| ← CircuitGain(基本1实测表 或 H(s)公式)
 *   2. 需要的装置输出 = 2V / |H|
 *   3. 对【装置自身输出】闭环调准(测CH0,允许)
 *   4. 电路按 H(s) 开环产生 2V；电路输出只接示波器
 *
 * 参考交叉校验(仅量级对照): |H(1kHz)|≈2.52, 需装置输出≈0.79Vpp(2/2.52)。
 */
static void RunBasic3(void)
{
    uint32_t freq = 1000;
    float circuit_target = 2.0f;

    printf("=== 基本(3): 1kHz → 已知电路 2Vpp ===\r\n");

    float hgain = CircuitGain(freq);
    if (hgain < 1e-6f) hgain = 1e-6f;
    float dev_target = circuit_target / hgain;      /* 需要的装置输出 */
    if (dev_target > OUTPUT_VPP_MAX) dev_target = OUTPUT_VPP_MAX;
    printf("  |H(1kHz)|=%.4f → 需装置输出 %.3fVpp\r\n", hgain, dev_target);

    float dev_actual = SetDeviceOutputVpp(freq, dev_target);
    float circuit_est = dev_actual * hgain;         /* 预计电路输出(未实测,仅显示) */

    printf("  装置输出实测=%.3fVpp → 预计电路输出≈%.3fVpp\r\n", dev_actual, circuit_est);

    UI_UpdateDisplay(freq, circuit_est);
    g_mode = MODE_IDLE;
}

/* ===== 基本(4): 用户设频(100~3kHz)+设压(1~2V) → 已知电路输出 =====
 * 合规架构同基本3: 纯前馈(用 |H| 反推需要的装置输出) + 对装置自身输出闭环。
 * 全程不测电路输出、电路输出端不连接装置。 */
static void RunBasic4(void)
{
    uint32_t freq = g_set_freq_hz;
    float circuit_target = g_set_vpp;

    printf("=== 基本(4): %luHz → 已知电路 %.3fVpp ===\r\n", freq, circuit_target);

    float hgain = CircuitGain(freq);
    if (hgain < 1e-6f) hgain = 1e-6f;
    float dev_target = circuit_target / hgain;      /* 需要的装置输出 */
    if (dev_target > OUTPUT_VPP_MAX) dev_target = OUTPUT_VPP_MAX;
    printf("  |H(%luHz)|=%.4f → 需装置输出 %.3fVpp\r\n", freq, hgain, dev_target);

    float dev_actual = SetDeviceOutputVpp(freq, dev_target);
    float circuit_est = dev_actual * hgain;         /* 预计电路输出(未实测,仅显示) */

    printf("  装置输出实测=%.3fVpp → 预计电路输出≈%.3fVpp\r\n", dev_actual, circuit_est);

    UI_UpdateDisplay(freq, circuit_est);
    g_mode = MODE_IDLE;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_TIM3_Init();
  MX_TIM7_Init();
  MX_DAC_Init();
  MX_TIM6_Init();
  /* USER CODE BEGIN 2 */
  setvbuf(stdout, NULL, _IONBF, 0);   /* printf 无缓冲，立即发送 */

  printf("2025G 电路模型探究装置 启动\r\n");

  /* ---- 应用模块初始化 ---- */
  set_clock();         /* 给AD9833提供时钟 MCO2(PC9) */
  DDS_Output_Init();
  ADC_Measure_Init();

  ADS8688_Status_t ads_status = ads8688_set();
  if (ads_status != ADS8688_OK) {
      printf("ADS8688 init failed: %d\r\n", (int)ads_status);
  } else {
      printf("ADS8688 OK\r\n");
  }

  dsp_init();        /* DSP工具初始化(发挥部分FFT/窗) */
  UI_Init();
  AdvLearn_Init();   /* 发挥(1)学习模块初始化 */
  AdvReplay_Init();  /* 发挥(2)复现模块初始化 */
  UI_UpdateDisplay(1000, 2.0f);

  /* DAC CH1 显式配置（CubexMX 只配了 CH2） */
  {
      DAC_ChannelConfTypeDef ch1_cfg = {0};
      ch1_cfg.DAC_Trigger = DAC_TRIGGER_NONE;
      ch1_cfg.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
      HAL_DAC_ConfigChannel(&hdac, &ch1_cfg, DAC_CHANNEL_1);
  }

  /* ===== TODO: 标定④ — Vg=1V固定输出，测完删 ===== */
  {
      uint32_t dac_val = (uint32_t)(0.9f * 4095.0f / 3.3f);
      HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, dac_val);
      HAL_DAC_Start(&hdac, DAC_CHANNEL_1);
      DDS_SetFrequency(1000);
      printf("[CAL4] Vg=0.9V (DAC=%lu/4095) 输入Vpp=? 输出Vpp=?\r\n", dac_val);
      while (1);
  }
  /* ===== 标定④ end ===== */

  HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, GPIO_PIN_SET);  /* 就绪指示 */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    ADS8688_Service();       /* 维持 ADC 数据就绪 */
    ADC_Measure_Service();   /* 采集中则收集数据点 */
    AdvLearn_Service();      /* 发挥(1)学习状态机推进(IDLE时快速返回) */
    AdvReplay_Service();     /* 发挥(2)复现状态机推进(非激活时快速返回) */

    /* 非复现模式时自动停止 DAC 输出 */
    if (g_mode != MODE_REPLAY && AdvReplay_IsRunning()) {
        AdvReplay_Stop();
    }

    switch (g_mode) {
    case MODE_CALIBRATE:
        HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
        RunBasic2();
        HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
        break;

    case MODE_BASIC3:
        HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
        RunBasic3();
        HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
        break;

    case MODE_BASIC4:
        HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
        RunBasic4();
        HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
        break;

    case MODE_LEARN:
        HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
        AdvLearn_Start();   /* 首次进入学习模式,启动扫频(幂等) */
        if (AdvLearn_IsDone()) {
            HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
            g_mode = MODE_IDLE;
        }
        break;

    case MODE_REPLAY:
        HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
        AdvReplay_Start();  /* 首次进入复现模式,启动(幂等) */
        /* 复现连续运行, 0x00 或按键切回 IDLE 才停止 */
        break;

    default:
        break;
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
