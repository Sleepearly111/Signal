# 捕获间隔与瞬时频率核心设计

> 候选编号：`DSP-INSTFREQ-001`  
> 当前阶段：Library候选评审与接口设计  
> 当前状态：`candidate / L0 Draft`  
> 设计日期：2026-07-24  
> 本文不创建Library模块，不复制来源代码，不构成`experimental`准入或硬件验证结论。

## 1. 来源分析

### 1.1 来源项目

- 来源项目：`projects/AI生成项目/2023D/`
- 来源提交：`7b15a787b626726fd7fbfba4b5f12c057ad56c1b`
- 候选来源文件：
  - `User/Inc/inst_freq.h`
  - `User/Src/inst_freq.c`

本次评审实际观察到的来源指纹：

| 文件 | SHA-256 |
|---|---|
| `User/Inc/inst_freq.h` | `181FEE017F2C5D73CD94FA0EC5A8F3B1F1A511D3DD07AC11EF9285F8F8B769DF` |
| `User/Src/inst_freq.c` | `A47782FCC6F73FBC5311FB50A3B662E4B29A6932A5981EA946EBE5ED21C18F02` |

`docs/library/module_registry.md`已经登记`DSP-INSTFREQ-001`，但其中保存的是更早来源快照的文件指纹。若后续进入实现阶段，应先单独更新注册表来源记录；本阶段不修改注册表。

来源工程的板上验证只证明2023D整机中的对应路径有效，不会自动传递给未来重新设计的纯C模块。

### 1.2 当前能力

来源实现具备以下行为：

1. 接收16位定时器输入捕获计数值；
2. 使用模减处理相邻捕获值之间的一次16位计数器回绕；
3. 把相邻计数差分成零、过短、正常和过长间隔；
4. 按固定数量合并多个连续间隔；
5. 只有当合并窗口内每个间隔均有效时才计算频率；
6. 输出一组经过降采样的平均瞬时频率；
7. 统计原始间隔数量、异常数量以及最小/最大非零间隔。

来源工程中，输入捕获预分频使一个捕获间隔可以代表多个载波周期。因此频率计算实际上依赖：

```text
timer_clock_hz
× cycles_per_capture_interval
× merge_length
÷ merged_ticks
```

`cycles_per_capture_interval`不能被隐藏为某个STM32寄存器配置，否则新模块仍然与TIM8耦合。

### 1.3 当前耦合

来源实现目前包含以下工程或题目耦合：

- 通过`app_config.h`取得定时器时钟、捕获预分频、合并数量和有效阈值；
- 固定假设TIM8为16位计数器；
- 固定使用2023D的载频、比较器质量和阈值解释；
- 输出容量和类型使用项目约定；
- 注释和过滤语义包含CW、FM、2ASK、2FSK、2PSK业务解释；
- “长间隔”受到16位计数器多次回绕不可观测问题影响；
- 质量统计尚未形成独立、稳定的公共契约；
- 来源调用链与DMA缓冲、采集窗口和项目状态机相连。

这些耦合不能进入纯计算内核。

### 1.4 输入语义澄清

需求中写有“输入capture interval数组”，同时又要求内核处理计数器回绕。这两种输入不能由同一个模糊接口同时表达：

- **原始捕获计数值数组**保存计数器快照，只有这一级能够根据`counter_bits`计算模差和一次回绕；
- **间隔数组**已经是差值，无法再判断原始计数器是否回绕，也无法恢复跨越一个以上计数器周期的真实间隔。

因此新设计必须明确拆成两层：

```text
原始捕获计数值
  → 计数值转间隔
  → 间隔分类与合并
  → 频率序列和质量统计
```

调用者若已经拥有可靠间隔，可直接调用第二层；调用者若只有DMA捕获计数值，则先调用第一层。

## 2. 提取目标

### 2.1 保留能力

未来模块只保留下列纯数值能力：

- 相邻捕获计数值转为tick间隔；
- 参数化计数器位宽和一次回绕模差；
- 零、过短、正常、过长间隔分类；
- 多个连续有效间隔合并；
- 根据定时器时钟和每次捕获代表的信号周期数计算频率；
- 输出有效频率点数量、无效窗口数量和质量统计；
- 检查输出容量、整数溢出和非法配置。

### 2.2 去除内容

未来模块不得包含：

- STM32 HAL头文件、句柄或回调；
- TIM8、通道、输入捕获预分频寄存器和时钟树配置；
- DMA启动、停止、剩余计数和缓冲所有权；
- ADC或固定采集窗口控制；
- `app_config.h`及任何项目全局变量；
- CW、AM、FM、2ASK、2FSK、2PSK等业务类型；
- 题目固定载频、码率、阈值或输出格式；
- 串口日志、HMI和动态内存分配；
- 隐藏静态工作区。

### 2.3 模块职责边界

建议职责划分：

| 层次 | 职责 | 是否属于本候选 |
|---|---|---|
| STM32采集服务 | 配置TIM、启动DMA、冻结窗口、读取有效长度 | 否，属于未来`SVC-CAPTURE-001`范围 |
| 捕获计数转换 | 处理计数位宽和一次模回绕，生成tick间隔 | 是 |
| 间隔测量核心 | 分类、合并、计算频率、输出质量统计 | 是 |
| 通用统计 | 百分位、均值、中间比例等 | 否，复用`DSP-STATS-001` |
| 调制分类 | 根据频率和异常间隔判断FM/FSK/ASK/PSK | 否，保持项目层 |

## 3. 新接口设计

### 3.1 设计原则

- 纯C11，不引用HAL、CMSIS或平台头文件；
- 调用者拥有全部输入、输出和工作区；
- 不进行动态分配；
- 配置显式传入，不读取全局变量；
- 每个失败状态可区分，失败时结果具有确定初始化状态；
- 频率单位固定为Hz，计时单位固定为timer tick；
- 每个合并窗口必须逐个验证原始间隔，不能只验证总和；
- 不在接口中出现调制类型语义。

### 3.2 建议公开API

以下仅为接口规划，不是正式头文件。

```c
fm_status_t fm_intervals_from_captures_u32(
    const fm_config_t *config,
    const uint32_t *captures,
    size_t capture_count,
    uint64_t *intervals,
    size_t interval_capacity,
    size_t *interval_count);
```

作用：

- 把原始捕获计数值转换为tick间隔；
- 根据`counter_bits`执行模差；
- 最多处理一次计数器回绕；
- 输出数量应为`capture_count - 1`；
- 输出空间不足时返回明确状态，不静默截断。

```c
fm_status_t fm_measure_intervals_u64(
    const fm_config_t *config,
    const uint64_t *intervals,
    size_t interval_count,
    double *frequency_hz,
    size_t frequency_capacity,
    fm_result_t *result);
```

作用：

- 分类每个输入间隔；
- 每`merge_length`个间隔形成一个非重叠窗口；
- 窗口内任意间隔为零、过短或过长时，该频率点无效；
- 对完整且有效的窗口计算平均频率；
- 输出频率序列和质量统计；
- 尾部不足一个完整合并窗口的间隔计入`trailing_interval_count`，不补零。

可选便利接口：

```c
fm_status_t fm_measure_captures_u32(
    const fm_config_t *config,
    const uint32_t *captures,
    size_t capture_count,
    uint64_t *interval_workspace,
    size_t interval_capacity,
    double *frequency_hz,
    size_t frequency_capacity,
    fm_result_t *result);
```

该接口只能组合前两层，不增加业务逻辑。是否保留应在实现阶段根据API最小化原则决定。

### 3.3 频率计算

对于一个包含`merge_length`个有效间隔的窗口：

```text
signal_cycles = cycles_per_capture_interval × merge_length
frequency_hz  = timer_clock_hz × signal_cycles / sum_interval_ticks
```

计算要求：

- `sum_interval_ticks`使用`uint64_t`并在加法前检查溢出；
- 频率计算使用`double`，避免模块隐式限定单精度；
- 嵌入式调用者若需要`float`，由调用层显式转换；
- `cycles_per_capture_interval`必须大于0；
- `timer_clock_hz`必须大于0且可表示为`uint64_t`；
- 不允许零tick进入除法。

### 3.4 回绕能力边界

当原始计数器位宽为`N`时：

```text
modulus = 2^N
delta   = (current - previous) mod modulus
```

该方法只能得到模`2^N`的差值。若两个捕获事件之间跨越一个或多个完整计数周期，纯计数快照无法知道实际跨越次数。

因此：

- `counter_bits`建议支持1～32；
- 32位模数计算必须使用`uint64_t`，避免`1U << 32`未定义；
- 模差为0既可能是重复捕获，也可能恰好跨越完整周期，内核只能按“不可区分的零间隔”报告；
- 需要识别多次回绕时，采集层必须提供扩展时间戳、溢出计数或直接生成可靠的64位间隔。

## 4. 数据结构设计

### 4.1 状态码

```c
typedef enum {
    FM_STATUS_OK = 0,
    FM_STATUS_NULL_ARGUMENT,
    FM_STATUS_INVALID_CONFIG,
    FM_STATUS_EMPTY_INPUT,
    FM_STATUS_INSUFFICIENT_INPUT,
    FM_STATUS_OUTPUT_TOO_SMALL,
    FM_STATUS_ARITHMETIC_OVERFLOW
} fm_status_t;
```

状态码描述API是否成功执行，不等同于信号质量。

### 4.2 配置结构

```c
typedef struct {
    uint64_t timer_clock_hz;
    uint8_t  counter_bits;
    uint32_t cycles_per_capture_interval;
    size_t   merge_length;
    uint64_t min_valid_interval_ticks;
    uint64_t max_valid_interval_ticks;
} fm_config_t;
```

字段约束：

| 字段 | 含义 | 约束 |
|---|---|---|
| `timer_clock_hz` | 输入捕获计数器tick频率 | `> 0` |
| `counter_bits` | 原始捕获计数器位宽 | `1..32`；直接输入间隔时不参与计算，但仍需保持合法配置 |
| `cycles_per_capture_interval` | 相邻捕获样本之间代表的被测信号周期数 | `> 0` |
| `merge_length` | 每个频率点合并的连续间隔数 | `> 0` |
| `min_valid_interval_ticks` | 正常间隔下界 | `> 0`，建议采用闭区间 |
| `max_valid_interval_ticks` | 正常间隔上界 | `>= min_valid_interval_ticks` |

有效区间建议明确定义为：

```text
min_valid_interval_ticks <= interval <= max_valid_interval_ticks
```

零间隔单独统计；小于下界为short，大于上界为long。

### 4.3 质量枚举

```c
typedef enum {
    FM_QUALITY_EMPTY = 0,
    FM_QUALITY_INSUFFICIENT,
    FM_QUALITY_INVALID,
    FM_QUALITY_DEGRADED,
    FM_QUALITY_GOOD
} fm_quality_t;
```

建议采用不依赖题目阈值的确定性语义：

- `EMPTY`：没有输入间隔；
- `INSUFFICIENT`：没有一个完整合并窗口；
- `INVALID`：存在完整窗口，但有效频率点为0；
- `DEGRADED`：既有有效窗口，也有异常或无效窗口；
- `GOOD`：所有完整窗口均有效。

尾部不足一个窗口的数据单独统计，不强制把`GOOD`降为`DEGRADED`。调用者可依据尾部数量另行决定是否接受。

### 4.4 结果结构

```c
typedef struct {
    size_t input_interval_count;

    size_t zero_interval_count;
    size_t short_interval_count;
    size_t normal_interval_count;
    size_t long_interval_count;

    uint64_t min_nonzero_interval_ticks;
    uint64_t max_interval_ticks;

    size_t complete_window_count;
    size_t valid_frequency_count;
    size_t invalid_frequency_count;
    size_t trailing_interval_count;

    fm_quality_t quality;
} fm_result_t;
```

约束：

- `valid_frequency_count + invalid_frequency_count == complete_window_count`；
- 频率数组前`valid_frequency_count`项有效；
- `invalid_frequency_count`表示被过滤的完整窗口，不在输出数组中留洞；
- 无非零间隔时`min_nonzero_interval_ticks`定义为0；
- 结果在任何返回路径上先清零，避免调用者读取旧数据；
- 输出容量不足应在处理前计算并返回`FM_STATUS_OUTPUT_TOO_SMALL`，不产生部分结果。

### 4.5 是否输出逐间隔分类

首版不建议输出每个间隔的分类数组。结果结构中的计数已经覆盖质量统计，减少工作区和API复杂度。

若第二个项目确实需要逐事件标签，可在后续小版本增加可选调用者缓冲；不能因为2023D的调制分类需求预先把ASK/PSK标签塞进核心。

## 5. 测试方案

### 5.1 主机测试环境

进入实现阶段后建议：

- 独立CMake目标；
- C11；
- GCC/Clang至少一种主机工具链；
- 开启`-Wall -Wextra -Werror`或等价严格告警；
- CTest执行；
- 不链接HAL、CMSIS或项目源码；
- 测试数据由测试文件生成，不复制2023D缓冲区。

### 5.2 必测用例

#### 正常周期

配置示例：

```text
timer_clock_hz = 168000000
cycles_per_capture_interval = 8
merge_length = 5
interval = 672 ticks
```

期望：

- 每个有效窗口输出约2 MHz；
- zero/short/long均为0；
- `quality == FM_QUALITY_GOOD`。

#### 计数器回绕

使用16位计数器，让前一个捕获值接近65535、后一个值回绕到较小值，模差仍为预期间隔。

还应测试：

- 8位、16位、24位和32位配置；
- 32位模数计算无未定义移位；
- 恰好同值产生零间隔；
- 文档明确多次回绕不可观测，不编造恢复结果。

#### 零间隔

相邻捕获值相同，或直接输入0间隔。

期望：

- `zero_interval_count`增加；
- 包含该间隔的合并窗口无效；
- 不发生除零；
- 仍可输出后续独立有效窗口。

#### 异常短间隔

输入小于`min_valid_interval_ticks`的非零值。

期望：

- `short_interval_count`增加；
- 对应窗口无效；
- 其他窗口不受污染。

#### 异常长间隔

输入大于`max_valid_interval_ticks`的值。

期望：

- `long_interval_count`增加；
- 对应窗口无效；
- 直接输入64位间隔时能覆盖大于计数器模数的值。

#### 不同timer clock

保持interval和其他配置不变，分别使用不同`timer_clock_hz`。

期望：

- 输出频率按时钟线性缩放；
- 非法0时钟返回`FM_STATUS_INVALID_CONFIG`。

#### 不同merge length

测试1、2、5及更大合并长度。

期望：

- 恒定周期下平均频率一致；
- 抖动间隔的输出等于总周期数除以总ticks，而不是各频率的算术平均；
- 尾部不足窗口时正确统计；
- `merge_length == 0`被拒绝。

### 5.3 边界与健壮性测试

除规定用例外，还应覆盖：

- 空指针、空输入和只有一个原始捕获值；
- 输出缓冲容量为0或小于最大可能输出数；
- `min_valid_interval_ticks > max_valid_interval_ticks`；
- `cycles_per_capture_interval == 0`；
- 极大间隔求和的`uint64_t`溢出；
- `capture_count - 1`和窗口数量计算的`size_t`边界；
- 全部无效、部分有效和全部有效三种质量；
- 一个异常间隔使整个合并窗口无效；
- 输出容量充足时不改写缓冲区尾部；
- 输入和输出别名是否禁止，并在接口契约中说明。

### 5.4 参考模型

测试应使用独立、易审查的64位或高精度参考公式生成期望值，不调用被测实现自身的辅助函数。

浮点比较使用相对误差和绝对误差组合，不能只比较打印后整数频率。

### 5.5 验证等级规划

| 阶段 | 证据 | 可记录等级 |
|---|---|---|
| 只有本文设计 | 接口和测试计划 | `candidate / L0 Draft` |
| 独立代码可严格编译 | 构建日志 | `experimental / L1 Builds` |
| 主机测试完整通过 | CTest、工具链、用例和退出码 | `experimental / L2 Software Tested` |
| 新模块接入真实STM32项目并记录条件 | 板级回归 | 可评估L3，仍需单独确认 |
| 完整竞赛条件验证并由用户确认 | 题目验收证据 | 可评估L4 |

2023D来源工程已有板上行为不能替代新模块的L1/L2/L3验证。

## 6. 与现有Framework资产关系

### 6.1 `DSP-STATS-001`

关系：互补，不冲突。

- 本候选负责计数差、间隔有效性、合并和频率换算；
- stats负责百分位数、中间比例等通用统计；
- 本候选首版只维护计数、min/max，不应为此依赖完整stats模块；
- 若上层需要频率中位数或分位数，应对输出频率调用stats，而不是把排序引入本核心。

### 6.2 FFT工具

关系：输入模型和用途不同，不冲突。

- FFT处理均匀采样的幅值序列，输出频谱和峰值频率；
- 本候选处理定时器事件时间或间隔，直接进行周期测量；
- 本候选不做窗函数、频谱、谐波或候选频点扫描；
- 不应为了“统一频率模块”把两者合并。

### 6.3 `frequency_compensation`

关系：名称中都含frequency，但职责不同。

- `frequency_compensation`根据频率校准曲线修正幅度；
- 本候选从事件间隔测得频率；
- 前者可以消费后者的结果，但不存在接口或实现重叠。

### 6.4 `SVC-CAPTURE-001`

关系：上下游分层，不能合并。

- `SVC-CAPTURE-001`候选负责ADC/TIM/DMA等采集生命周期；
- `DSP-INSTFREQ-001`只接受调用者提供的数据；
- HAL回调所有权、DMA剩余计数、缓冲截断和固定窗口属于采集服务；
- 间隔分类、回绕模差和频率计算属于本纯计算核心。

### 6.5 measurement相关资产

当前Framework未发现另一个已经实现并测试的“输入捕获间隔转瞬时频率”纯C模块。模块注册表已经预留`DSP-INSTFREQ-001`，因此应沿用该编号，不新建重复候选。

本模块可定位为DSP/measurement core，但不承担仪器驱动、ADC测量服务或板级校准。

## 7. 目录规划

本阶段不创建下列目录，仅规划未来形态：

```text
library/experimental/DSP/frequency_measure_core/
├─ CMakeLists.txt
├─ README.md
├─ include/
│  └─ frequency_measure_core.h
├─ src/
│  └─ frequency_measure_core.c
└─ tests/
   └─ test_frequency_measure_core.c
```

建议同步规划验证记录：

```text
docs/verification/frequency_measure_core_l2.md
```

目录名和头文件名在实现阶段还需与Framework命名习惯复核。不得把来源`inst_freq.c/.h`直接复制到该目录。

## 8. 评审结论与后续计划

### 8.1 是否建议进入`library/experimental`

**有条件建议进入，但当前不能进入。**

理由：

- 计数器模差、间隔分类、周期合并和频率换算具有跨项目复用价值；
- 纯数值边界明确，HAL和题目逻辑可以完全移除；
- 可建立确定性主机参考向量；
- 与现有FFT、stats和采集服务职责不冲突；
- Framework已经登记`DSP-INSTFREQ-001`候选。

进入条件：

1. 用户单独授权下一阶段；
2. 按本文接口重新实现，不复制来源源码；
3. 完成独立目录、README、显式API和CMake；
4. 完成全部主机测试并保存证据；
5. 更新模块注册表的来源指纹、状态和验证等级；
6. 明确一次回绕与多次回绕不可观测边界。

### 8.2 后续实现计划

下一阶段建议一次只实现该模块：

1. 冻结配置、状态码、质量枚举和两层API；
2. 新建独立目录和公共头文件；
3. 先写host测试与参考向量；
4. 重新实现计数转间隔和间隔测量核心；
5. 严格编译并运行CTest；
6. 记录L1/L2证据；
7. 更新模块注册表；
8. 完成后停止，不自动接入2023D或其他硬件项目。

本设计不修改来源工程，也不把来源工程的硬件验证等级继承给未来模块。
