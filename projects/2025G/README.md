# 2025 G · 电路模型探究装置

[官方题目](https://res.nuedc-training.com.cn/topic/2025/topic_126.html) · [GitHub 原工程](../../2025G/) · [新增应用层快照](source/)

## 硬件和代码对应

| 路径 | 硬件连接与作用 | 源码入口 |
| --- | --- | --- |
| 正弦激励 | 两路 AD9833 使用实际 10 MHz 模块时钟。A 路 FSYNC/SCLK/SDATA 为 PB12/PB13/PB15。B 路为 PD12/PD13/PD15 | `User/Src/AD9833.c`、`Src/dds_output.c` |
| 增益控制 | PA4 / DAC CH1 输出增益控制电压至 AD603 调理板，电压范围按该板标定 | `Src/dds_output.c`、`Inc/app_config.h` |
| 测幅与扫频 | 输入和输出测点各自根据台架流程接至 AD637 测 RMS，再由 ADS8688 鉴读直流。ADS8688 控制与软件串行线位于 PD0–PD5 | `User/Src/ads8688.c`、`Src/adva_learn.c` |
| 外部波形采样 | 有偏置与限幅的模拟信号接 PB1 / ADC1_IN9。TIM3 触发，DMA 收集 2048 点，记录配置为 1 MSPS | `Src/adva_replay.c` |
| 周期播放 | PA5 / DAC CH2 经缓冲与适当滤波接输出或示波器。TIM6 与 DMA 循环播放波表 | `Src/adva_replay.c` |
| 串口与按键 | USART1 PA9/PA10，USART2 PA2/PA3，按键 PE4/PE3 | `source/app.ioc` |

AD637 的直流读数用于需要高频测幅的路径。基于 RMS 换算 Vpp 时，要先确认被测信号近似纯正弦。现场的 ADS8688 板标记了异常通道，丝印和程序索引也曾经历调整，应按板卡实测探测通道。LTC1068 与 PC6/TIM8 只是项目内的滤波候选。

## 进展与边界

既有复盘记录了 DDS 激励、AD603 幅值调节、扫频、低通样机判型与回放链路。旧回放路径仍记录过 40 kHz 正弦不稳定。后续 AD630 拍频 FLL 用于同频稳频，记录了 PA5 输出、串接隔直后送鉴相器、AD630 输出经低通送 ADS8688 的调试方案。两份记录对应不同阶段，同频稳定与固定相位是两项指标。

应用层快照来源提交 `9fce5a7`。完整的旧公开工程沿用原目录。换版时复核控制量极性、DDS 时钟、采样偏置、测幅链、实际源码中的通道配置和仪器接法。

应用层快照须配合对应板级工程。部分旧工程引用上游加窗实现，复原工程时请按其来源和许可取得对应实现。

[项目复盘](retrospective.md) · [FLL/DDS 专项记录](fll-dds-log.md)
