# DRV-FPGAPKT-001 L2 验证

- 日期：2026-07-28
- 对象：`library/experimental/drivers/fpga_packet`
- 工具链：CMake，MinGW Makefiles，TDM64 GCC 4.9.2
- 结果：独立构建成功，CTest 1/1 通过
- 等级：`experimental / L2 Software Tested`

实际成功命令：

```powershell
cmake --build build/fpga-packet-mingw-en
ctest --test-dir build/fpga-packet-mingw-en --output-on-failure
```

测试覆盖固定包 CRC `0x9971`、256 点斜坡 CRC `0xF87A`、字段解析，以及坏同步字、
坏版本、超长、头部长度不一致和坏 CRC。

本轮全量 L2 聚合构建还暴露了旧 MinGW Make 无法处理现有中文模块路径的问题：
构建在 `RMS计算` 路径处停止，因此不能把本轮写成“11 模块聚合测试通过”。
该失败与本模块独立测试结论分开记录。

未覆盖：Arm 交叉构建、STM32 Adapter、SPI/DMA/DRDY 时序和硬件链路。来源整机
L3 不传递给本协议模块。
