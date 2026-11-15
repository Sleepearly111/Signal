# FPGA—STM32 高速采样桥 L3 验证

- 日期：2026-07-28
- 对象：EP4CE10F17C8 + AD9280 + STM32F407ZGT6
- 工程：`projects/已完成项目/FPGA_STM32高速采样桥/`
- 等级：`L3 Hardware Tested`

已验证 SPI3 DMA、DRDY、CRC、序号、ACK、256 点窗口保存和 USART2 调试输出；
并在 AD9280 25 MSPS 下观察到 100 kHz/2 Vpp 与 2 MHz 正弦输入的有效变化。
固定测试包连续约一分钟未再出现帧错误。

该等级只覆盖归档中的板卡、接线、固件和测试条件。未覆盖连续 25 MSPS
传输、全频带幅度标定、低频抽取、正式题目指标、串口屏或 L4 验收。

关键故障记录：CN2-35 与 FPGA 板 UART2_RX 复用，UART2 跳帽未拔时会造成
间歇帧头/CRC 错误；E10/E11 与 AD9708 复用，SPI 已迁移到 F2/F3。
