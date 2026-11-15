# 2026-07-28 联调记录

## 接线

| 信号 | STM32 | FPGA |
|---|---|---|
| SCLK | PC10 / SPI3 | L8 / J8-3 |
| CS_N | PB0 | P6 / J8-4 |
| MOSI | PC12 / SPI3 | F2 / CN2-5 |
| MISO | PC11 / SPI3 | F3 / CN2-6 |
| DRDY | PD6 | K8 / CN2-35 |
| GND | GND | GND |

必须拔掉 FPGA 板 UART2_RX 跳帽，因为它与 CN2-35/DRDY 共用。不要连接
J8-1 的 3.3 V。FPGA 的 E10/E11 留给 AD9708，不再作为 SPI 引脚。

## 协议

- SPI Mode 0，MSB first，STM32 约 656.25 kbit/s；
- 读命令：`A1`；ACK：`A2 seq_lo seq_hi`；
- 帧：`A5 5A 01 kind status seq_le16 len_le16 payload crc_le16`；
- CRC-16/CCITT：初值 `FFFF`，多项式 `1021`；
- DRDY 保持到正确 ACK；未 ACK 前旧包不覆盖。

## 证据与结果

FPGA 顶层 `adc_capture_top`：Quartus 0 errors，311/10320 LE，177
寄存器，2136 RAM bits；TimeQuest setup/hold TNS 均为 0。STM32
Debug/Release 完成 Arm 构建并实际烧录；Release 记录为 RAM 7096 B、
FLASH 18164 B。

- 固定测试包连续约 1 分钟无 CRC/帧头错误；
- 100 kHz、2 Vpp、0 V offset、High-Z：典型
  `min=0x66 max=0x98 pp=50 avg≈127.6`；
- 2 MHz 正弦：典型 `min=0x64 max=0x9A~0x9C pp=54~56 avg≈127.8`，
  CRC 正确、未见削顶。

2 MHz 测试若用于正式幅度标定，应再次确认发生器幅度和端接。

## 限制

- 25 MSPS 的 Nyquist 为 12.5 MHz，但当前只实测 100 kHz 和 2 MHz；
- 256 点窗口只有 10.24 us，不能直接可靠测 1 kHz 的 Vpp；
- SPI 是冻结窗口加 ACK，不是 25 MSPS 连续实时流；
- 更低频需 FPGA 抽取/降采样或更长窗口；
- FPGA testbench 已写但未运行，不能声称仿真通过；
- SOF 断电丢失，比赛前需确认配置芯片或重新下载。
