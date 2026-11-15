# regulated_signal_output_app

状态：`experimental / L1 Builds`

这是“信号源输出 + 幅度测量 + 有限次反馈校正”的参考应用。它复用
`nonblocking_signal_workflow`，以 Hz、Vpp 和执行器控制量作为明确物理量，
不包含 HAL、CubeMX、固定引脚、具体 DDS/DAC/VGA 型号或板卡标定值。

## 能做什么

- 配置信号频率与波形；
- 设置初始控制量；
- 非阻塞等待稳定和幅度测量；
- 按 `target_vpp / measured_vpp` 计算校正比；
- 对校正比、控制范围、迭代次数和总时间分别限幅；
- 拒绝低于最小有效读数、无效或饱和的测量；
- 在故障、超时和 Abort 时调用 `safe_stop`。

收敛后保留最终控制值，不调用 `safe_stop`。因此 Adapter 必须保证“保持输出”
和“故障安全停机”是两个清晰可验证的板级行为。

## Adapter 契约

板级代码只实现：

1. `configure_signal(frequency_hz, waveform)`；
2. `set_control(control_value)`；
3. `start_measurement()`；
4. `poll_measurement(done, measured_vpp, valid, saturated)`；
5. `safe_stop()`。

CubeMX、时钟、引脚、DMA、IRQ、DDS/DAC/VGA 驱动和测量链均由用户配置并确认。
本参考应用不能据此宣称硬件可用。

## API

`Init -> Start -> Service -> IsDone -> GetResult`，另有 `Abort` 和 `Deinit`。

## 测试矩阵

- 线性 fake plant 收敛并保持最终输出；
- 目标不可达时触发最大迭代并安全停机；
- 无效与饱和测量；
- `uint32_t` 时间回绕下的总超时；
- Abort；
- 非法配置和 Adapter I/O 失败。

2026-07-24 使用 STM32Cube bundle Arm GCC 14.3.1、CMake 4.3.1 和 Ninja
1.13.2，在 Cortex-M4 严格告警配置下完成根构建 8/8，包含 App 静态库和测试
ELF。当前环境不能执行该 ARM ELF，因此只支持 L1 Builds；只有测试程序在主机
或目标运行环境中真实运行通过后才能评为 L2。
