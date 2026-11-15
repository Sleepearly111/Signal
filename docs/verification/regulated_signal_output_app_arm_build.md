# regulated_signal_output_app Arm 构建记录

日期：2026-07-24  
结论：`experimental / L1 Builds`

## 范围

根 `library/CMakeLists.txt` 启用：

```text
SIGNAL_FRAMEWORK_VALIDATE_REGULATED_OUTPUT_APP=ON
SIGNAL_FRAMEWORK_BUILD_TESTS=ON
```

该选项自动启用 `nonblocking_signal_workflow`。构建覆盖：

- workflow 静态库；
- workflow 测试 ELF；
- regulated output App 静态库；
- regulated output App fake-port 测试 ELF。

## 工具与结果

- Arm GCC：STM32Cube bundle 14.3.1；
- CMake：STM32Cube bundle 4.3.1；
- Ninja：STM32Cube bundle 1.13.2；
- 目标：Generic Cortex-M4，`-mcpu=cortex-m4 -mthumb`；
- App 源码：`-Wall -Wextra -Wpedantic -Werror`；
- 构建结果：8/8 成功。

链接测试 ELF 时出现 `nosys.specs` 的 `_write`、`_read`、`_close` 等未实现
警告，这是无板级系统调用桩时的预期现象，链接返回成功。

## 测试与等级边界

测试源覆盖：

- 线性 fake plant 收敛并保持最终输出；
- 目标不可达、控制限幅和最大迭代；
- 无效与饱和测量；
- `uint32_t` 时间回绕总超时；
- Abort；
- 非法配置与 Adapter I/O 失败；
- 故障路径的 `safe_stop`。

当前机器没有可执行 ARM ELF 的仿真器，也没有可用的本机 C 编译器，所以测试
断言没有实际运行。该记录只支持 L1 Builds，不支持 L2 Software Tested、
STM32 集成、硬件验证或比赛验证。CubeMX、接线、烧录和板级测试仍由用户执行。
