# FPGA packet protocol

- ID：`DRV-FPGAPKT-001`
- 生命周期：`experimental`
- 当前等级：`L2 Software Tested`
- 作用：解析 FPGA 到主控的固定头、变长负载和 CRC-16/CCITT 数据包。

该模块只处理字节协议，不包含 SPI、GPIO、DMA、DRDY、ACK 时序、HAL
句柄或固定引脚。板级工程应在 Adapter 中完成总线收发，再把完整帧交给本模块。

## 帧格式

```text
A5 5A | version | kind | status | seq_le16 | len_le16 | payload | crc_le16
```

CRC 参数：初值 `0xFFFF`，多项式 `0x1021`，不反射，无最终异或；覆盖
9 字节头和 payload。当前默认最大 payload 为 256 字节。

## API

- `fpga_packet_crc16_ccitt()`：计算 CRC；
- `fpga_packet_parse_header()`：检查同步字、版本和长度；
- `fpga_packet_decode()`：再次核对头部长度，复制字段并校验 CRC。

## 本轮验证

```powershell
cmake -S library/experimental/drivers/fpga_packet -B build/fpga-packet
cmake --build build/fpga-packet
ctest --test-dir build/fpga-packet --output-on-failure
```

测试覆盖固定数值包 CRC `0x9971`、256 点斜坡包 CRC `0xF87A`、字段解析，
以及同步字、版本、长度和 CRC 错误拒绝。

这里的 L2 仅证明纯 C 协议核的主机测试。来源整机的 L3 不传递给该模块；
换到新 FPGA、主控或传输接口后仍需重新做 Adapter、时序和硬件回归。
