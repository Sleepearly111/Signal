# Signal Framework Library

> Framework：v1.0
> Library 状态：experimental
> 当前资产：11 个 L2 软件测试模块、4 个 L1 构建模块、3 个 L0 草案和 1 个未独立验证候选；准确清单见 [`../docs/asset_index.md`](../docs/asset_index.md)

本目录是 Signal Framework 公共模块的物理入口。模块来自 2023D、2024C 和 2025G 的可复用能力重新设计；来源工程保持独立，来源硬件证据不会自动传递给提取后的模块。当前没有模块进入稳定或通用 `verified` 状态。

每个 L2 模块保留自己的独立 `CMakeLists.txt`，同时可由根级 `library/CMakeLists.txt` 显式选择。聚合入口只解决主机端构建和依赖传递；新题仍必须逐模块阅读 README，并重新完成 Arm 构建和板级验证。

## 当前结构

```text
library/
├─ README.md
├─ VERSION.md
├─ CMakeLists.txt
├─ .gitignore
└─ experimental/
   ├─ include/
   ├─ src/
   ├─ tests/
   ├─ stats/
   │  └─ README.md
   ├─ template/
      ├─ module.h
      ├─ module.c
      ├─ README.md
      └─ tests/
   ├─ services/
   │  └─ sweep_service/
   ├─ data/
   │  └─ calibration_record/
   └─ scaffolds/
      ├─ nonblocking_signal_workflow/
      │  ├─ include/
      │  ├─ src/
      │  ├─ tests/
      │  ├─ CMakeLists.txt
      │  └─ README.md
      └─ contest_app/
         ├─ include/
         ├─ src/
         ├─ tests/
         ├─ CMakeLists.txt
         └─ README.md
```

其他 DSP、驱动、标定和 STM32F4 模块按自身目录保存。`scaffolds/` 提供可复制的代码形状，不属于稳定模块，也不进入模块数量。

## CMake 目标

- `SignalFramework::signal_framework`：空的 INTERFACE 目标，为后续模块提供稳定入口；
- `SignalFramework::experimental_bundle`：链接本次显式启用的 experimental 模块，不代表稳定 API；
- `signal_framework_template_check`：默认不存在；仅在显式开启 `SIGNAL_FRAMEWORK_VALIDATE_TEMPLATE=ON` 时编译模板。
- `signal_workflow_scaffold`：仅在显式开启 `SIGNAL_FRAMEWORK_VALIDATE_SIGNAL_WORKFLOW_SCAFFOLD=ON` 时构建和测试非阻塞流程脚手架；不进入模块计数或聚合目标。
- `contest_app_scaffold`：仅在显式开启 `SIGNAL_FRAMEWORK_VALIDATE_CONTEST_APP_SCAFFOLD=ON` 时构建和测试整题编排脚手架；不进入模块计数或聚合目标。
- `SignalFramework::signal_stats_experimental`：仅在显式开启 `SIGNAL_FRAMEWORK_BUILD_DSP_STATS=ON` 时创建；不会自动链接到稳定入口。

构建当前全部十一个 L2 软件模块：

```shell
cmake -S library -B <build-dir> -DSIGNAL_FRAMEWORK_BUILD_ALL_L2=ON
cmake --build <build-dir>
ctest --test-dir <build-dir> --output-on-failure
```

也可以只启用新题需要的模块：

| 选项 | 模块 |
|---|---|
| `SIGNAL_FRAMEWORK_BUILD_DSP_STATS` | Statistics / Percentile |
| `SIGNAL_FRAMEWORK_BUILD_DSP_INSTFREQ` | Capture Interval Frequency |
| `SIGNAL_FRAMEWORK_BUILD_DSP_RMS` | RMS 计算 |
| `SIGNAL_FRAMEWORK_BUILD_DSP_FFT` | FFT 工具 |
| `SIGNAL_FRAMEWORK_BUILD_DSP_LUT` | 一维 LUT 插值 |
| `SIGNAL_FRAMEWORK_BUILD_DSP_TF2` | 二阶传递函数 |
| `SIGNAL_FRAMEWORK_BUILD_AD603_MODEL` | AD603 增益模型 |
| `SIGNAL_FRAMEWORK_BUILD_AD9833` | AD9833 协议驱动 |
| `SIGNAL_FRAMEWORK_BUILD_ADS8688` | ADS8688 协议驱动 |
| `SIGNAL_FRAMEWORK_BUILD_FPGA_PACKET` | FPGA 窗口数据包协议核 |

新题通用核心使用独立验证开关，不进入 `BUILD_ALL_L2`：

| 选项 | 模块 |
|---|---|
| `SIGNAL_FRAMEWORK_VALIDATE_SWEEP_SERVICE` | 非阻塞扫频服务 |
| `SIGNAL_FRAMEWORK_VALIDATE_FILTER_CLASSIFIER` | 滤波器保守判型 |
| `SIGNAL_FRAMEWORK_VALIDATE_CALIBRATION_RECORD` | 版本化校准记录 |
| `SIGNAL_FRAMEWORK_VALIDATE_TRANSFER_RESPONSE` | Vin/Vout 传递响应处理 |
| `SIGNAL_FRAMEWORK_VALIDATE_FILTER_ID_PIPELINE` | 扫频→响应→判型集成测试 |
| `SIGNAL_FRAMEWORK_VALIDATE_FILTER_ID_APP` | 完整滤波器识别参考应用 |
| `SIGNAL_FRAMEWORK_VALIDATE_REGULATED_OUTPUT_APP` | 信号输出与幅值反馈闭环参考应用 |

使用 Arm GCC 构建这些核心时必须声明交叉目标，避免 CMake 把编译器当作 Windows
本机工具链并错误加入 `kernel32`：

```shell
cmake -S library -B <build-dir> -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=library/cmake/arm-none-eabi-cortex-m4.cmake \
  -DCMAKE_C_COMPILER=<arm-none-eabi-gcc> \
  -DCMAKE_MAKE_PROGRAM=<ninja> \
  -DSIGNAL_FRAMEWORK_VALIDATE_SWEEP_SERVICE=ON \
  -DSIGNAL_FRAMEWORK_VALIDATE_FILTER_CLASSIFIER=ON \
  -DSIGNAL_FRAMEWORK_VALIDATE_CALIBRATION_RECORD=ON \
  -DSIGNAL_FRAMEWORK_VALIDATE_TRANSFER_RESPONSE=ON \
  -DSIGNAL_FRAMEWORK_VALIDATE_FILTER_ID_PIPELINE=ON \
  -DSIGNAL_FRAMEWORK_VALIDATE_FILTER_ID_APP=ON \
  -DSIGNAL_FRAMEWORK_VALIDATE_REGULATED_OUTPUT_APP=ON
cmake --build <build-dir>
```

该配置可编译/链接 ARM 测试 ELF，但不能在 Windows 上执行 CTest。

完整参考应用见
[`experimental/reference_apps/filter_identification_app/README.md`](experimental/reference_apps/filter_identification_app/README.md)。
它把 sweep、Vin/Vout 响应和判型组合为单一非阻塞接口，项目只需在用户确认
CubeMX 后实现四个 Adapter 回调。它是 L1 参考应用，不计入纯 Library 模块数。

幅值闭环参考应用见
[`experimental/reference_apps/regulated_signal_output_app/README.md`](experimental/reference_apps/regulated_signal_output_app/README.md)。
它组合信号配置、控制量设置和 Vpp 测量，收敛后保持最终输出，故障、超时或
Abort 时执行板级 Safe Stop。它也是 L1 参考应用，不计入纯 Library 模块数。

新题可链接 `SignalFramework::experimental_bundle`，也可只链接启用后的具体目标。不得仅因为聚合构建通过，就把这些目标称为 stable、STM32-tested 或 hardware-tested。

验证代码脚手架：

```shell
cmake -S library -B <build-dir> -DSIGNAL_FRAMEWORK_VALIDATE_SIGNAL_WORKFLOW_SCAFFOLD=ON
cmake --build <build-dir>
ctest --test-dir <build-dir> --output-on-failure
```

脚手架见 [`experimental/scaffolds/nonblocking_signal_workflow/README.md`](experimental/scaffolds/nonblocking_signal_workflow/README.md)。它当前为 L1 Builds；测试 ELF 已交叉构建，但断言未运行。

整题 App 脚手架使用：

```shell
cmake -S library -B <build-dir> -DSIGNAL_FRAMEWORK_VALIDATE_CONTEST_APP_SCAFFOLD=ON
cmake --build <build-dir>
ctest --test-dir <build-dir> --output-on-failure
```

见 [`experimental/scaffolds/contest_app/README.md`](experimental/scaffolds/contest_app/README.md)。

配置空骨架：

```shell
cmake -S library -B <build-dir>
cmake --build <build-dir>
```

可选模板编译检查：

```shell
cmake -S library -B <build-dir> -DSIGNAL_FRAMEWORK_VALIDATE_TEMPLATE=ON
cmake --build <build-dir>
```

构建并运行 `DSP-STATS-001` 主机测试：

```shell
cmake -S library -B <build-dir> -DSIGNAL_FRAMEWORK_BUILD_DSP_STATS=ON
cmake --build <build-dir>
ctest --test-dir <build-dir> --output-on-failure
```

这些命令只验证 Framework 骨架或模板语法，不证明任何 STM32 工程、器件驱动或硬件链路通过。

Phase 1B 的实际构建记录见 [`../docs/verification/phase1b_skeleton.md`](../docs/verification/phase1b_skeleton.md)。
`DSP-STATS-001` 的提取验证记录见 [`../docs/verification/phase2a_dsp_stats.md`](../docs/verification/phase2a_dsp_stats.md)。

## 模块进入流程

1. 在 `docs/library/module_registry.md` 中取得稳定 ID；
2. 使用 `docs/templates/module_candidate_template.md` 完成候选档案；
3. 从 `experimental/template/` 新建模块结构，但必须替换通用命名和 TODO；
4. 记录来源 commit、dirty 状态和工作区 SHA-256；
5. 增加主机测试或 fake bus；
6. 通过独立构建后才可标记 L1/L2；
7. 完成真实板级回归并由用户确认后，才考虑稳定 API。

## 强制边界

- 不从题目工程整目录复制代码；
- experimental 不等于 hardware-tested 或 verified；
- 公共模块不得依赖 `main.h`、固定 HAL 句柄、固定引脚或题目全局变量；
- library 不拥有全局 HAL callback；
- 项目标定值和比赛状态机留在项目层；
- 比赛任务与 Framework 整理冲突时，比赛任务优先。

候选状态与来源指纹见 [`../docs/library/module_registry.md`](../docs/library/module_registry.md)。
