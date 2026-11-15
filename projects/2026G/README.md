# 2026 G · 周期信号测量分析装置

[官方题目](https://res.nuedc-training.com.cn/topic/2026/topic_139.html) · [接线与器件](wiring.md) · [台架测试报告](verification/bench-2026-08-01.md) · [结果 CSV](verification/results.csv) · [频谱分量 CSV](verification/spectrum.csv)

## 最终方案

```text
信号源 → 50 Ω 同轴线 → AD9248 模块 INA（板载 AD8132）
      → FPGA 25 MSPS 采集、FIR、2/25 采样率转换
      → 2 MSPS、4096 点窗口 → SPI3 → STM32F407 → USART1 串口屏
```

最终构建变体为 `AD9248-Single-Direct`，只读取 A 通道。FPGA 的 320 抽头 FIR 在降采样前抑制带外输入。STM32 检查窗口元数据、包序和 CRC，Hann 窗后的 4096 点 FFT 用于找基频与谐波。2 MSPS / 4096 = 488.28125 Hz。结合题目给定的 500 Hz 频率网格做联合正弦拟合，再进行频响、幅值标定及 RMS 和峰峰值计算。

## 代码阅读顺序

| 想看什么 | 入口 |
| --- | --- |
| 主循环、数据和 UI 更新 | [bringup_app.c](User/App/bringup_app.c) |
| FPGA 数据格式和帧语义 | [g2026_fpga_messages.c](User/Protocol/g2026_fpga_messages.c)、[fpga_protocol.c](User/Protocol/fpga_protocol.c) |
| 4096 点窗口组装 | [fpga_window_assembler.c](User/Service/fpga_window_assembler.c) |
| FFT、重构、正弦联合拟合 | [g2026_measurement_dsp.c](User/DSP/g2026_measurement_dsp.c) |
| 基频和谐波族、频率网格 | [g2026_spectral_logic.c](User/DSP/g2026_spectral_logic.c) |
| 单路直连标定 | [g2026_calibration.c](User/Config/g2026_calibration.c) |
| 串口屏显示 | [g2026_display_adapter.c](User/Display/g2026_display_adapter.c)、[tjc_display.c](User/Display/tjc_display.c) |

STM32 应用层来源为封箱提交 `d4a179b`。CubeMX 工程与板级外设的复原范围见[接线页](wiring.md)。保持输出采样率、窗口或协议任一参数变化时，应重新核对对端配置和测试。

## 台架结果

2026-08-01 的记录包含三问各 5 组，每组以 20 个完整窗口的中位数统计。最大峰峰值误差 1.470 mV，有效值误差 0.428 mV，频谱分量幅值误差约 0.51 mVpk。最慢首个有效结果为 1.547 s。第三问用了 200 mVpp、1.0–1.2 MHz 实际干扰。基频测试点在题目 500 Hz 网格上。

该记录支持这 15 组的数值与时间结论。原装置显示周期是否完整、频谱布局、超出本次频段的干扰、装置输入端接与长期稳定性需要独立核对。

[历版方案](docs/2026G_three_variant_4096_plan.md) · [CubeMX 配置](fpga_bridge.ioc) · [板级接口](docs/board_adapter_contract.md)


## 主机测试

配置、构建和运行 STM32 应用层的主机测试。

~~~sh
cmake -S projects/2026G/tests -B build/g2026-tests
cmake --build build/g2026-tests --config Debug
ctest --test-dir build/g2026-tests -C Debug --output-on-failure
~~~

默认运行协议、组包、标定和谱峰逻辑等不需要 SDK 的测试。过去的双路增益测试可通过 CMake 选项 `G2026_CMSIS_DSP_INCLUDE_DIR` 指向兼容 STM32CubeF4 的 `arm_math.h` 所在目录后启用。主机测试只验证相应软件行为。
