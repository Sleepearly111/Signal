# Signal Library Source Registry

## 2026-07-28 FPGA 协议增量

| ID | 类别 | 模块 | Framework 状态 | 来源证据 | 下一道门 |
|---|---|---|---|---|---|
| `DRV-FPGAPKT-001` | 协议 | `FPGA_PACKET` | experimental / L2 | FPGA—STM32 来源整机 L3；提取模块仅独立主机测试 | 新题 Adapter、Arm 构建、真实 SPI/DRDY 和目标板 L3 |

该模块不包含 SPI、DMA、GPIO、ACK 调度或固定引脚，来源整机 L3 不传递给提取模块。

> 角色：历史来源、指纹和候选 ID 登记；不是当前 Library 完整库存
> Phase：2A基础快照 + 2026-07-24局部增量
> 基础快照日期：2026-07-19
> 当前库存与等级：以 [`../asset_index.md`](../asset_index.md) 和每个模块 README
> 为准；本表未覆盖后来新增的全部模块、脚手架和参考应用。

## 1. 登记规则

本表登记的是 **Signal Framework 模块资产**，不是来源工程文件的成熟度。必须同时区分：

- **Framework 状态**：模块在 Signal library 生命周期中的位置；
- **来源证据**：来源工程曾达到的验证等级或用户确认范围；
- **提取后等级**：新接口/新实现实际重新完成的验证等级。

来源工程的 L3/L4 行为不会自动传递给提取后的模块。截至2026-07-24，`DSP-STATS-001`和`DSP-INSTFREQ-001`已分别进入`experimental`并完成各自的主机软件测试；其余条目仍是静态盘点候选，不得据此标为`hardware-tested`或`verified`。

生命周期：

```text
observed → candidate → experimental → hardware-tested → verified → deprecated
```

优先级含义：

- P0：当前比赛交付直接需要时立即处理；
- P1：低风险、可主机验证的首批提取候选；
- P2：通用价值高，但需要 adapter/fake bus；
- P3：分支复杂或强硬件耦合，必须有额外回归设计；
- Hold：只登记，当前不得开始提取。

比赛优先原则可以覆盖下面的 Framework 排序，但覆盖原因必须写入 `docs/ai_decisions.md`。

## 2. 来源仓库快照

| 来源 | 分支 | HEAD | 整体状态 | 与候选相关的状态 |
|---|---|---|---|---|
| `2023D/` | `2023d` | `f15b13e8ef2b` | dirty（4 项） | 本表引用的驱动/DSP 文件相对 HEAD 干净 |
| `2024C/` | `2024e` | `426536bcd799` | dirty（2 项） | AD9833/ADS8688 文件相对 HEAD 干净 |
| `stm32f4-template/` | `master` | `0ed1de410e6e` | dirty（10 项） | AD9833/ADS8688 已修改；adc/dds 服务未跟踪 |

表中的 SHA-256 均针对 2026-07-19 的**工作区文件内容**。对 dirty/untracked 来源，它不是 HEAD 内容证明。

## 3. 候选总表

| ID | 类别 | 候选 | Framework 状态 | 来源证据 | 优先级 | 下一道门 |
|---|---|---|---|---|---|---|
| `DSP-STATS-001` | DSP | statistics / percentile | experimental / L2 | 2023D L0/L1 原型 | P1 | STM32 集成与板级性能验证 |
| `DSP-INSTFREQ-001` | DSP | capture interval / instant frequency | experimental / L2 | 2023D当前样机输入捕获链有板上行为；新模块只完成主机测试 | P1 | STM32集成、Arm构建与板级性能验证 |
| `DSP-WINDOW-001` | DSP | window / FFT 基础函数 | candidate | 多工程存在；验证未单独分离 | P2 | 拆出纯算法，确认 CMSIS-DSP 版本 |
| `DSP-TRANSFER-001` | DSP | transfer function + LUT | candidate | 2025G 已完成范围内有实测价值，模块证据未拆分 | P2 | 固化输入单位、LUT 边界和误差 |
| `DSP-ADVA-001` | DSP/Application | advanced learning/replay | observed | 2025G 发挥阶段实验，当前文件 dirty | Hold | 等用户稳定并提交实验边界 |
| `DRV-AD9833-001` | Driver | AD9833 DDS | candidate | test0 L4 行为基准；2025G 有后续硬件经验 | P2 | 协议内核 + GPIO bus fake；见 `ad9833.md` |
| `DRV-ADS8688-001` | Driver | ADS8688 ADC | candidate | test0/2025G 有多分支硬件经验 | P3 | 明确状态机与总线分支；见 `ads8688.md` |
| `SVC-ADCMEAS-001` | Service | ADC measure | candidate | 2025G 路径有实测价值；template 为 dirty 快照 | P3 | 明确 direct Vpp/RMS/AD637 语义 |
| `SVC-DDSOUT-001` | Service | DDS + VGA output | candidate | 2025G 部分链路有硬件经验 | P3 | 分离 DDS、DAC、增益模型和标定 |
| `SVC-CAPTURE-001` | Service | ADC/TIM capture | candidate | 2023D 未硬测 | Hold | 移除固定句柄与全局 HAL callback 所有权 |
| `SVC-HMI-001` | Service | HMI transport | candidate | 多项目有使用经验，2023D 版本未硬测 | Hold | 拆 transport/protocol/view-model |
| `SVC-AD630-001` | Service | AD630 feedback / phase loop | candidate | test0 L4 整机行为基准 | Hold | 先固化 golden regression，再拆执行器与测量 |

## 4. 内容指纹

### 4.1 首批纯 DSP

| ID | 来源文件 | SHA-256 |
|---|---|---|
| `DSP-STATS-001` | `2023D/User/Inc/stats.h` | `474D186345D4FFEA13B9420341264F10C7C058E85B0638DFC59D8FB05A421168` |
| `DSP-STATS-001` | `2023D/User/Src/stats.c` | `6B8F2C03499C1F194F8A1563005A61212A0458AA46CCE132AFFD25B498E1E0E8` |
| `DSP-INSTFREQ-001` | `2023D/User/Inc/inst_freq.h` | `181FEE017F2C5D73CD94FA0EC5A8F3B1F1A511D3DD07AC11EF9285F8F8B769DF` |
| `DSP-INSTFREQ-001` | `2023D/User/Src/inst_freq.c` | `A47782FCC6F73FBC5311FB50A3B662E4B29A6932A5981EA946EBE5ED21C18F02` |
| `DSP-WINDOW-001` | `2023D/User/Inc/CALCULATE/windowfunction.h` | `B08006574A58C06062CA8EE9E4D82A6BC1B97573CE4EC521A6F5B60BA5F4CB2E` |
| `DSP-WINDOW-001` | `2023D/User/Src/CALCULATE/windowfunction.c` | `9013DBD449C322847995733E43DA785880D500D4F0B877435A5C2E6162568E68` |

### 4.2 服务候选

| ID | 来源文件 | SHA-256 |
|---|---|---|
| `SVC-CAPTURE-001` | `2023D/User/Inc/capture.h` | `97A350820C174AC68A0F0EEFB67B342F8F40A0FEE83ED3B79F5664DD38390AA8` |
| `SVC-CAPTURE-001` | `2023D/User/Src/capture.c` | `27D40C968B3020C3666B11D1EF387F47F984D8E34AB6AF8E01B2A9792820264A` |
| `SVC-HMI-001` | `2023D/User/Inc/hmi.h` | `CF2117EF33493FE83091322920803668431C450040577F53A79A10803A27030D` |
| `SVC-HMI-001` | `2023D/User/Src/hmi.c` | `2029795A550CB7FC9CAA2FFF272DE5F7546F5D60651DA4E2AE64736173446F7D` |

AD9833 与 ADS8688 的全部版本指纹分别记录在 `ad9833.md`、`ads8688.md`。

## 5. Phase 1A 出口检查

- [x] 生命周期和“双轴状态”定义完成；
- [x] 来源仓库、分支、HEAD 和 dirty 边界已记录；
- [x] 首批驱动、DSP、服务候选均分配稳定 ID；
- [x] 首批候选工作区内容 SHA-256 已记录；
- [x] AD9833、ADS8688 已建立独立候选档案；
- [x] 未创建 `library/`，未把任何条目错误晋升为 experimental；
- [x] 未修改任何子仓库。

Phase 1B 才允许建立简化的 `library/` 物理骨架，且需要独立确认。

## 6. 2026-07-24增量记录

`DSP-INSTFREQ-001`已按
`docs/library_candidate/frequency_measure_core_design.md`
重新设计并独立实现，目标目录为：

```text
library/experimental/DSP/frequency_measure_core/
```

提取版不复制2023D源码，不依赖HAL、TIM8、DMA、`app_config.h`或调制类型逻辑。主机环境使用CMake 4.3.4、Ninja 1.12.1和TDM64 MinGW GCC 4.9.2，以C11及`-Wall -Wextra -Wpedantic -Werror`完成严格构建；CTest结果为1/1通过。

当前结论为`experimental / L2 Software Tested`。详细证据见
`docs/verification/frequency_measure_core_l2.md`。该结论不包含STM32集成、Arm构建、目标板性能或硬件验证。

## 7. 2026-07-27 增量记录

| ID | 类别 | 模块 | Framework 状态 | 来源证据 | 下一道门 |
|---|---|---|---|---|---|
| `DSP-BEATFLL-001` | DSP/控制 | `beat_fll_controller` | experimental / L2 | 2025G 来源整机 L3；新模块仅完成独立主机测试 | Arm 构建、STM32 接入、执行器与板级扰动回归 |

模块位于 `library/experimental/DSP/beat_fll_controller/`，只保留 fresh 观测、P&O 粗细搜索、HOLD 和解锁滞回；不复制 AD630、ADS8688、HAL、DMA、软件 DDS 和项目阈值。证据见 `docs/verification/beat_fll_controller_l2.md`。
