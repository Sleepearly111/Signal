# dac_dma_output 测试说明

> 当前只定义未来 fake-HAL 与板级测试方法，尚未执行自动化或硬件测试。
> 模块验证状态保持 L0 Software Draft。

## 前置条件

- 两个 DAC 通道已配置为同一外部定时器 TRGO 触发；
- 两个 DMA 已正确链接到 `DAC_HandleTypeDef`，方向为 memory-to-peripheral；
- DMA 使用循环模式，数据宽度与 `alignment`、波表存储格式一致；
- 定时器 PSC/ARR、TRGO 模式和 DAC TEN 位已由板级代码检查。

## 1. 双通道启动同步

- 为两路配置可区分且起点已知的波表。
- 调用 `Init()` 后确认状态为 STOPPED，再调用 `Start()`。
- fake-HAL 应验证调用顺序：启动通道 A DMA、启动通道 B DMA、最后启动共同定时器。
- 板级测试应使用示波器测量两通道首个更新沿的偏差；仅看到两路均有波形不足以证明同步。

## 2. DMA 循环输出

- 使用短周期波表启动输出，并观察多个缓冲周期。
- fake-HAL 检查两次 `HAL_DAC_Start_DMA()` 收到相同的 `buffer_length`。
- 板级测试检查波表首尾连续性、周期重复、DMA 错误标志和长时间运行稳定性。

## 3. Buffer 更新

- 运行状态下调用 `UpdateBuffer()` 替换两路波表。
- fake-HAL 验证顺序：停定时器、停两路 DMA、更新地址、启动两路 DMA、启动定时器。
- 板级测试记录更新间断、毛刺以及两通道是否从新波表索引 0 同步开始。
- 任一 HAL 调用失败时，状态应进入 ERROR，不能报告 RUNNING。

## 4. Stop/Start 流程

- RUNNING 状态调用 `Stop()`，预期先停止定时器，再停止两路 DMA，状态变为 STOPPED。
- STOPPED 状态重复 `Stop()` 应返回 OK。
- STOPPED 状态再次 `Start()` 应重新启动；RUNNING 状态重复 `Start()` 应返回状态错误。
- 增加第二路 DMA 启动失败、定时器启动失败和停止失败的 fake-HAL 路径，检查清理动作与 ERROR 状态。

## 尚未覆盖

- 真实 STM32F4 寄存器配置与 HAL 版本差异；
- TEN 位、TRGO 路由和 ARR=0 的板级前置条件检查；
- DMA cache、一致性、更新毛刺、同步误差和长期运行；
- 不同 STM32F4 子系列及单 DAC 器件。
