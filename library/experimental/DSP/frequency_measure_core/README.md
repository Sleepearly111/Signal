# DSP-INSTFREQ-001 — Capture Interval Frequency Core

> 状态：experimental  
> API版本：`0.1.0`  
> 当前验证等级：L2 Software Tested（仅主机软件测试）

## 来源与重新设计

本模块由2023D的输入捕获频率测量需求启发，来源项目保持不变：

- 项目：`projects/AI生成项目/2023D/`
- 来源提交：`7b15a787b626726fd7fbfba4b5f12c057ad56c1b`
- 来源文件：`User/Inc/inst_freq.h`、`User/Src/inst_freq.c`

来源工作区SHA-256：

| 文件 | SHA-256 |
|---|---|
| `User/Inc/inst_freq.h` | `181FEE017F2C5D73CD94FA0EC5A8F3B1F1A511D3DD07AC11EF9285F8F8B769DF` |
| `User/Src/inst_freq.c` | `A47782FCC6F73FBC5311FB50A3B662E4B29A6932A5981EA946EBE5ED21C18F02` |

本模块没有复制来源实现。它按
[`frequency_measure_core_design.md`](../../../../docs/library_candidate/frequency_measure_core_design.md)
重新设计为纯C接口，移除了STM32 HAL、TIM8、DMA、`app_config.h`、固定载频阈值和调制类型业务逻辑。

来源工程的硬件表现不自动成为本模块的硬件验证证据。

## 功能

模块提供两个独立步骤：

1. `fm_intervals_from_captures_u32()`：
   - 将1～32位计数器捕获值转换为64位tick间隔；
   - 使用模差处理相邻捕获值之间的一次计数器回绕；
   - 拒绝超出配置计数器位宽的输入值。
2. `fm_measure_intervals_u64()`：
   - 把间隔分类为zero、short、normal和long；
   - 按调用者配置的`merge_length`合并非重叠窗口；
   - 只为全部间隔有效的窗口计算平均频率；
   - 返回有效/无效窗口数、min/max间隔和质量状态。

频率公式为：

```text
frequency_hz =
    timer_clock_hz
    × cycles_per_capture_interval
    × merge_length
    ÷ merged_interval_ticks
```

## API

公开头文件：`include/frequency_measure_core.h`。

配置结构`fm_config_t`显式包含：

- `timer_clock_hz`
- `counter_bits`
- `cycles_per_capture_interval`
- `merge_length`
- `min_valid_interval_ticks`
- `max_valid_interval_ticks`

调用者提供输入、间隔工作区和频率输出缓冲。模块不使用动态内存、隐藏静态缓冲或全局配置。

`fm_result_t`输出：

- zero/short/normal/long间隔数量；
- 最小非零和最大间隔；
- 完整、有效、无效窗口数量；
- 尾部未组成完整窗口的间隔数量；
- `EMPTY`、`INSUFFICIENT`、`INVALID`、`DEGRADED`或`GOOD`质量状态。

状态码描述API执行结果，质量枚举描述输入数据质量，两者不能混用。

## 关键语义

- 有效区间是闭区间：
  `min_valid_interval_ticks <= interval <= max_valid_interval_ticks`。
- 一个合并窗口内任意间隔无效，则整个频率点无效。
- 输出频率数组不为无效窗口保留空洞。
- 尾部不足一个合并窗口的数据不补零，单独计数。
- 输出容量不足时返回错误，不产生部分结果。
- 频率输出使用`double`；嵌入式调用者可在应用层显式转换为`float`。

## 回绕限制

模差只能得到相邻计数快照之间模`2^counter_bits`的间隔：

- 可以处理一次计数器回绕；
- 无法判断两个捕获事件之间跨越了多少个完整计数周期；
- 相同捕获值既可能代表零间隔，也可能代表恰好跨越完整模数，模块按零间隔报告；
- 需要处理多次回绕时，采集层必须提供扩展时间戳、溢出计数或可靠的64位间隔。

本模块不负责DMA缓冲截断、事件缺失原因或采集窗口同步。

## 构建与测试

```text
cmake -S . -B build -G Ninja \
  -DCMAKE_MAKE_PROGRAM=<ninja> \
  -DCMAKE_C_COMPILER=<gcc>
cmake --build build --verbose
ctest --test-dir build --output-on-failure
```

2026-07-24使用以下环境完成独立主机测试：

- CMake 4.3.4
- Ninja 1.12.1
- TDM64 MinGW GCC 4.9.2
- C11
- `-Wall -Wextra -Wpedantic -Werror`

CTest结果：`DSP-INSTFREQ-001` 1/1通过。

测试覆盖正常2 MHz参考向量、8/16/24/32位计数器回绕、零/短/长间隔、不同timer clock、不同merge length、尾部数据、空与不足输入、容量不足、超范围捕获值和64位求和溢出。

详细证据见
[`frequency_measure_core_l2.md`](../../../../docs/verification/frequency_measure_core_l2.md)。

## 与Framework其他资产的边界

- `DSP-STATS-001`可对频率输出做百分位统计；本模块不复制其功能。
- FFT工具处理均匀采样幅值序列；本模块处理事件时间间隔。
- `frequency_compensation`根据频率修正幅度；本模块负责测得频率。
- `SVC-CAPTURE-001`候选负责HAL、TIM和DMA采集生命周期；本模块不拥有硬件。

## 限制

- 当前没有STM32集成、目标板运行时间、RAM布局或实时性验证；
- 当前没有Arm交叉编译证据；
- 不判断输入捕获预分频寄存器配置是否正确；
- 不判断频率混叠、比较器毛刺原因或调制类型；
- 配置阈值由调用者负责建立，模块不提供2023D默认值；
- 输入、工作区和输出缓冲不得互相重叠；
- 当前`experimental / L2`不等于`verified`或硬件测试通过。
