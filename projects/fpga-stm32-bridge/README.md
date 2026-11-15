# FPGA—STM32 高速采样桥

## 组成与链路

```text
模拟信号 → AD9280 → EP4CE10F17C8 → SPI3 DMA → STM32F407ZGT6
```

AD9280 基线是 8 位原始采集，时钟由 FPGA 输出，记录为 25 MHz。该基线协议每包 256 个原始点，校验 CRC 后应答。MCU 使用 SPI3、CS_N、DRDY 和共地，调试用 USART2，USART1 预留显示。信号方向是 STM32 发出 SCK/MOSI/CS_N，FPGA 发出 MISO/DRDY。具体 FPGA 管脚按所用工程和板卡，不可把 2026G AD9248 架构的包格式混用。

[STM32 应用层](source/) · [原始测量经验与接线核查](../../knowledge/硬件经验/FPGA_AD9280_STM32高速采样桥接经验.md)

2026-07-28 归档支持原 AD9280、FPGA 板和 F407 组合的 L3 通信联调。带宽、幅值精度、全速无间隙传输和换型 ADC 需要分别验证。

[联调记录](verification.md)
