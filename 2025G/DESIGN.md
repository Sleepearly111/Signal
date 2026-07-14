# 2025 电赛 G 题 — 电路模型探究装置 设计文档

## 硬件平台

| 器件 | 用途 |
|------|------|
| STM32F407VGT6 | 主控，168MHz Cortex-M4F |
| ADS8688 | 16bit ADC，测输出幅度（学习时测电路响应） |
| AD9833 ×1 | DDS 正弦信号源 |
| AD603 | VGA 程控增益，STM32 内部 DAC 驱动 |
| AD8052 | 固定 5× 运放，最终推到 ≥3Vpp |
| 串口屏 | UI 交互，USART1 |
| KEY0/KEY1 | 触发基本(3)/(4) |

## 信号链

```
AD9833(0.6Vpp) → AD603(0~42dB) → AD8052(×5) → 输出(0~4Vpp)
                      ↑
              STM32 内部 DAC CH1 (0~1V)
```

## 复用来源

- `../test0/User/Src/AD9833.c` — DDS 驱动
- `../test0/User/Src/ADS8688.c` — ADC 驱动
- `../test0/Core/` — STM32 HAL 外设驱动

## 模块架构

```
main.c
  ├── dds_output.c    — AD9833 频率 + AD603 VGA 控制
  ├── adc_measure.c   — ADS8688 采样 → RMS → Vpp
  ├── transfer_calc.c — H(s) 运行时计算
  └── ui_basic.c      — 串口屏 + 按键
```

## 基础部分实现

| 要求 | 流程 | 关键模块 |
|------|------|---------|
| (2) Vpp≥3V | 扫频输出 → ADS8688 测 → 不够调 VGA → 再测 | dds_output + adc_measure |
| (3) 1kHz→2Vpp | H(s) 算输入幅度 → 设 AD9833+AD603 → 测校准 | transfer_calc + dds_output |
| (4) 可变频/压 | 用户设 f+V → 查 H(s) → 设输出 → 闭环校准 | 全部 |

## TODO 实测标定点

- [ ] AD603: Vg vs dB 曲线（app_config.h 中的 SLOPE/OFFSET）
- [ ] AD9833 实测 Vpp（可能不是精确 0.6Vpp）
- [ ] AD8052 实际放大倍数
- [ ] ADS8688 测量采样点数/速率优化
