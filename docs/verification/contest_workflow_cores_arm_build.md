# 新题流程核心 Arm 交叉构建记录

- 日期：2026-07-24
- 对象：`sweep_service`、`filter_classifier`、`calibration_record`
- 工具链：先用系统 Arm GNU Toolchain `14.2.1` 逐单元编译和测试 ELF 链接；
  再用 STM32Cube bundle Arm GCC `14.3.1`、CMake `4.3.1`、Ninja `1.13.2`
  验证根构建入口。
- 目标参数：`-mcpu=cortex-m4 -mthumb`
- 源码约束：C11、`-Wall -Wextra -Wpedantic -Werror`

## 结果

六个翻译单元均编译成功，退出码为 0：

- 三个模块实现；
- 三个对应测试源文件。

随后使用 `--specs=nosys.specs` 分别链接三个测试 ELF：

```text
SWEEP_LINK_EXIT=0
FILTER_LINK_EXIT=0
CALIBRATION_LINK_EXIT=0
```

生成：

| ELF | 大小 |
|---|---:|
| `test_sweep_service.elf` | 58460 bytes |
| `test_filter_classifier.elf` | 56164 bytes |
| `test_calibration_record.elf` | 55368 bytes |

链接器对 `_write`、`_read`、`_close` 等 nosys 桩给出预期警告。这些警告来自测试
中的 `assert/puts` 运行时，并不代表已经存在目标板串口或系统调用实现。

首次启用根 CMake 测试目标时，因未声明 `CMAKE_SYSTEM_NAME=Generic`，CMake
错误加入 Windows 的 `kernel32` 与 `--major-image-version`，三个测试链接失败。
该问题促成新增 `library/cmake/arm-none-eabi-cortex-m4.cmake`；它不写死工具
安装路径，只声明 Generic Cortex-M4、静态 try-compile 和 nosys 链接边界。
首版工具链文件曾在 ABI try-compile 子工程中过早要求 `CMAKE_C_COMPILER`，
导致配置失败；移除该非标准检查，由 CMake 自己校验命令行传入的编译器。

修正后使用该 toolchain 重新配置，根构建完成 12/12：

- 三个模块静态库；
- 三个测试翻译单元；
- 三个 ARM 测试 ELF。

配置、生成和构建退出码均为 0，没有再出现 Windows 库或 PE 链接参数。
测试 ELF 仍未运行，不能把“链接成功”写成断言通过。

## 证据边界

该记录证明三个核心和测试源文件可被 Cortex-M4 Arm GCC 严格编译、链接。
它不证明：

- 测试断言已经运行；
- CMake/CTest 已通过；
- 已接入 STM32 CubeMX 工程；
- 时钟、引脚、DMA、IRQ、Flash 或外设可用；
- 已烧录或完成任何硬件实测。

根 CMake 配置和 6/6 静态库构建退出码均为 0，因此三个模块当前为
`experimental / L1 Builds`。获得 Windows/Linux 可执行主机测试或目标板测试
运行结果后，才能重新判断 L2；硬件步骤由用户
按 [`../process/STM32烧录指南.md`](../process/STM32烧录指南.md) 操作。

## Vin/Vout 传递响应核心扩展

同日新增 `transfer_response` 后，使用同一 Generic Cortex-M4 toolchain 重新
配置根构建，并同时启用四个核心和测试目标：

```text
Configure/Generate: exit 0
Build: 16/16
```

构建内容为四个静态库、四个测试翻译单元和四个 ARM 测试 ELF。
`transfer_response` 同样定为 `experimental / L1 Builds`；测试 ELF 未运行。

## 组合管线构建

新增 `filter_identification_pipeline_tests`，用 fake 低通测量组合：

```text
sweep_service -> transfer_response -> filter_classifier
```

根 CMake 自动启用依赖，配置/生成退出码为 0，整体完成 18/18 构建，其中包括
组合测试 ARM ELF。该结果证明三个 API 可以组合编译和链接；断言尚未运行，
不证明 LOW_PASS 判型在执行时已经通过。

## 完整参考应用构建

在四个核心和 pipeline 之上新增 `filter_identification_app`。它提供
`Init/Start/Service/IsDone/GetResult/Abort/Deinit`，并把平坦响应作为成功结束
的 UNKNOWN，而不是强制分类。

同一工具链根构建结果：

```text
Configure/Generate: exit 0
Build: 22/22
```

App 静态库和 fake 测试 ELF 均生成。测试源码覆盖低通成功、平坦 UNKNOWN、
无效测量故障和 Abort，但 ELF 未执行，所以参考应用当前为 L1。
