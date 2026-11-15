# dac_dma_output

- 来源：2024C
- 功能：通过共同定时器触发两路 STM32F4 DAC 循环 DMA，并提供同步启停和双波表更新流程。
- 当前状态：experimental
- Verification：L0 Software Draft
- 验证边界：来源项目已验证；本模块未独立测试，不是 Hardware Tested。

## 来源与重建边界

参考来源为 2024C 的 `Core/Src/dac.c`、`dma.c`、`tim.c`，以及 `User/Src/dual_channel.c` 中的初始化、更新、停止和同步重启流程。本模块重新建立平台服务，没有复制 2024C 的信号模型、SD/SM、AM、无线参数、串口屏或 HMI 逻辑。

## 公开接口

- `DacDmaOutput_Init()`：校验并保存已经配置完成的 HAL 句柄、通道、DMA、触发信息和两路波表，不启动外设；
- `DacDmaOutput_Start()`：先启动两路 DAC DMA，最后启动共同触发定时器；
- `DacDmaOutput_Stop()`：先停止共同触发，再停止两路 DAC DMA；
- `DacDmaOutput_UpdateBuffer()`：停止输出、同时替换两路波表并按同步顺序重启；
- `DacDmaOutput_GetStatus()`：返回 UNINITIALIZED、STOPPED、RUNNING 或 ERROR。

所有状态保存在调用者拥有的 `DacDmaOutput` 对象中，无项目全局变量、动态内存或 `printf`。

## MCU 范围

- 面向使用 STM32F4 HAL、同一 DAC 外设内两个可用通道的 STM32F4 器件；
- 当前实现按 STM32F4 `DAC_HandleTypeDef` 的 `DMA_Handle1`/`DMA_Handle2` 链接模型校验 DMA；
- 不适用于只有单 DAC 通道的器件，也未验证 STM32F0/F1/F3/F7/H7/G4 等系列；
- 两路必须由同一外部触发源驱动，并使用相同波表长度。

## HAL 版本假设

- 来源工程使用 STM32F4 HAL Driver V1.8.5；
- 假设 `HAL_DAC_Start_DMA()`、`HAL_DAC_Stop_DMA()`、`HAL_TIM_Base_Start()` 和 `HAL_TIM_Base_Stop()` 的接口及句柄布局与 V1.8.5 兼容；
- `trigger_source` 用于记录板级配置意图，本模块不读取 DAC 通道配置来自动证明 TRGO 路由一致；
- 移植到其他 HAL 版本前必须核对 DAC 触发宏、句柄 DMA 字段、返回语义和停止行为。

## 外设前置条件与限制

本模块不初始化 GPIO、DAC、DMA、NVIC、定时器或时钟树。调用前必须由 CubeMX 或板级代码完成：

- 两个 DAC 通道选择同一个定时器 TRGO；
- 两个 DMA 正确链接、使用 memory-to-peripheral 和循环模式；
- DMA 数据宽度、波表存储和 `DAC_ALIGN_*` 一致；
- 定时器 TRGO 为更新事件，PSC/ARR 能产生目标采样率；
- DAC 触发使能位已经实际生效。

`UpdateBuffer()` 使用停机重启，不是无缝 DMA 双缓冲切换，可能产生输出间断或毛刺。任一 HAL 操作失败后状态进入 ERROR；当前不提供自动回滚或错误恢复。

## 2024C 平台问题记录

### DAC_TRIGGER_T6_TRGO 与 TEN 位

2024C 使用的 HAL/芯片组合中，`DAC_TRIGGER_T6_TRGO` 的编码与 `DAC_TRIGGER_NONE` 判定发生冲突，HAL 配置后 TEN1/TEN2 未置位，表现为 DMA 数据不能通过触发更新到输出。项目曾直接设置 CR 的 TEN 位恢复输出。

本模块不复制该寄存器修补。原因是问题与 HAL 版本、芯片和触发选择相关，平台层应在初始化后核对触发配置与 TEN 位，并在确认适用条件后采用受控修复或升级 HAL。

### TIM ARR=0

2024C 板上观察到 TIM6 ARR=0 时计数器停在 0 且没有有效 TRGO；在 84 MHz 定时器时钟下改用 PSC=41、ARR=1 得到 1 MHz 更新率。

本模块不改写 PSC/ARR。定时器输入时钟、分频、自动重装和目标采样率必须由板级代码计算并验证；不能把 2024C 的数值直接用于其他时钟树或定时器。

## 文件

- `include/dac_dma_output.h`：配置、上下文、状态与公开接口；
- `src/dac_dma_output.c`：双通道 HAL 启停和波表更新流程；
- `tests/test_dac_dma_output.md`：未来 fake-HAL 与板级测试说明。
