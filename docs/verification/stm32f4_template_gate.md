# STM32F4 新题模板准入检查

> 检查日期：2026-07-28
>
>
> 当前结论：**已准入为本队比赛板基线；指定 FPGA/AD9280 组合为 L3**

## 1. 当前对象

canonical 路径中已有：

- `fpga_bridge.ioc`；
- CubeMX 生成的 `Core/`、`Drivers/`、启动文件和链接脚本；
- CMake 工程与 Arm GNU 工具链文件；
- SPI3、DMA、USART、ADC、DAC、TIM 和 GPIO 的板级配置。

目录已建立独立 Git 仓库。新题必须复制到独立项目目录后再开发，不能直接污染
canonical 基线。

历史 `projects/AI生成项目/stm32f4-template/` candidate 和 2025G 工程仍是只读来源，
不再是当前默认入口。

## 2. 本轮核对

### 2.1 CubeMX 与 FPGA 通信配置

- STM32F407ZGT6，HSE 8 MHz，SYSCLK 168 MHz；
- SPI3 位于 PC10/PC11/PC12，Mode 0、MSB first、约 656.25 kbit/s；
- PB0 为 `FPGA_CS_N`，初始高；PD6 为 `FPGA_DRDY`，上升沿 EXTI、下拉；
- RX 为 DMA1 Stream0/Channel0，TX 为 DMA1 Stream5/Channel0；
- SPI3、两路 DMA 和 EXTI9_5 的抢占优先级均为 5；
- PA4 继续作为 DAC_OUT1；
- USART2 用于调试，USART1 为串口屏预留。

### 2.2 Debug/Release 构建

工作目录：

```text
```

工具链：

```text
Arm GNU Toolchain 14.2.1
CMake + Ninja
```

| 构建 | 结果 | RAM | FLASH |
|---|---|---:|---:|
| Debug | 40/40，exit 0 | 7096 B / 128 KB | 31116 B / 1 MB |
| Release | 40/40，exit 0 | 7096 B / 128 KB | 18164 B / 1 MB |

因此构建证据为 `L1 Builds`；结合下述实物联调，指定组合最高为 L3。

### 2.3 烧录

2026-07-28 使用 OpenOCD 0.12.0、ST-LINK V2、SWD 对 Release ELF 执行写入、校验
和 `reset run`。目标电压 3.273 V，日志出现 `Programming Finished` 和
`Verified OK`，命令 exit 0。USART2 随后持续输出主循环心跳和 FPGA/ADC 数据。

## 3. T0–T9 状态

| Gate | 要求 | 当前 |
|---|---|---|
| T0 仓库边界 | 明确 canonical 路径，工作区状态可解释 | 通过：独立 Git，比赛时复制后开发 |
| T1 CubeMX 可再生 | `.ioc`、生成层和 CMake 一致 | 通过当前生成快照核对；下一次重新生成仍需审计差异 |
| T2 空应用构建 | Debug/Release 均用 Arm 工具链构建 | 通过 |
| T3 烧录启动 | 目标板可烧录、复位、进入主循环 | 通过：写入/校验/reset run 和 USART2 心跳 |
| T4 冒烟测试 | 时钟、GPIO、串口、SysTick 和错误出口 | 通过当前 FPGA 通信链：心跳、DRDY 和错误不 ACK 可观察 |
| T5 DMA/中断 | 至少一条真实 DMA/IRQ 回调链 | 通过：SPI3 RX/TX DMA、EXTI DRDY、CRC/ACK 实物联调 |
| T6 分层 | 不含题目算法、状态机和标定值 | 通过当前骨架核对；板级预留外设允许存在 |
| T7 Library 接入 | Library 显式选择，默认不启用 experimental | 部分：CMSIS-DSP 可链接，Framework Library 尚未接入 |
| T8 资源记录 | Flash、RAM、栈和构建版本 | 通过 |
| T9 操作文档 | 复制、生成、构建、烧录、调试和回退可复现 | 通过本组合；新题仍需按实际 `.ioc` 和接线复验 |

## 4. 使用口径

现在可以说：

- “这是比赛新题默认的 STM32F407 板级起点”；
- “Debug/Release 都能在当前 Arm 工具链编译”；
- “FPGA、调试串口和串口屏的接口分工已经写入 `.ioc`”；
- “指定 FPGA/AD9280 和接线下，SPI3 DMA、DRDY、CRC/ACK 与采样窗口已完成 L3”。

现在不能说：

- “串口屏已经接入”；
- “T0–T9 或 L3/L4 已全部通过”。

下一道门是按明日题目复制工程并重新核对 `.ioc`、引脚和测量条件；串口屏接入仍
需独立验证，任何位流、时钟、引脚或 Adapter 变化都要重做对应 L3。
