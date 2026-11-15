# 陌生稳幅信号源题端到端架构演练

> 演练 ID：`DRYRUN-REGULATED-OUTPUT-001`  
> 日期：2026-07-24  
> 等级：`L0 Architecture Dry Run`  
> 目的：验证 Framework 能否从陌生要求正确调用 Knowledge、Library、用户硬件
> 门和代码交付流程。没有执行 CubeMX、构建、烧录或硬件测试。

## 1. 合成题目

设计一个信号输出装置：

- 输出正弦波，频率范围由题目现场给出；
- 用户设置目标 Vpp；
- 接入未知但允许范围内的负载后自动调节幅度；
- 在限定时间内稳定到题目误差；
- 饱和、测量失效或超时时安全退出；
- 基础要求允许固定频率/固定幅度，发挥要求是一键自动稳幅。

题目没有给出 MCU、DDS/DAC、VGA、幅值测量路径、负载、频率上限和精度，因此
当前不能直接生成板级代码。

## 2. 验收矩阵

| ID | 要求 | 关键未知 | 验证 |
|---|---|---|---|
| B1 | 固定频率正弦输出 | 频率、负载、幅度 | 示波器测频率/Vpp |
| B2 | 固定安全幅度 | 输出链动态范围 | 多负载重复测量 |
| F1 | 目标 Vpp 自动稳幅 | 测量链、执行器和标定 | 多目标、多频点误差 |
| F2 | 规定时间完成 | 建立时间和迭代 | 上电到稳定计时 |
| E1 | 饱和/断线/超时安全 | Safe Stop 定义 | 故障注入 |

## 3. 能力码

```text
G-DDS 或 G-DAC
M-AMP-ADC 或 M-AMP-RMS
L-LEVEL
L-CAL
A-FLOW
```

按 [`../capability_matrix.md`](../capability_matrix.md) 路由，当前命中多行约束的
并集。任何测量方案超过 Nyquist、量程或检波带宽都应否决。

## 4. Knowledge 调用

| ID | 用于决定 | 可沿用 | 不可沿用 |
|---|---|---|---|
| `K-DEV-001` | DDS/VGA/RMS 器件候选 | 器件类别和已知坑 | 当前库存和具体批次 |
| `K-MEAS-003/003A` | VGA 模型和闭环安全 | 标定方法、限幅、最小读数、超时 | 2025G 拟合常数和控制值 |
| `K-MEAS-001` | ADC 测幅可行性 | 有效采样率和 Nyquist 检查 | 历史 SPI/通道吞吐 |
| `K-MEAS-002/004` | 高频 RMS 测幅候选 | 建立时间、换算条件 | 历史标定表 |
| `K-STM32-002/003` | DAC/DMA 输出候选 | 触发链检查项 | PSC/ARR、DMA 映射和负载结论 |
| `K-ANALOG-001` | 输出滤波与补偿 | 分频段标定方法 | 历史 RC 补偿表 |
| `K-PROC-001` | 标定证据 | 记录字段和条件 | 任何旧板数值 |

没有把 Knowledge 当成代码，也没有把来源 L3/L4 传给新题。

## 5. Library 调用

### 主方案候选：DDS/VGA + 幅值测量

- AD9833 协议：`experimental / L2 Software Tested`；
- AD603 model/LUT：`experimental / L2 Software Tested`；
- `regulated_signal_output_app`：`experimental / L1 Builds`；
- `calibration_record`：`experimental / L1 Builds`；
- `contest_app` scaffold：`experimental / L1 Builds`。

需要新题 Board Adapter 实现：

```text
configure_signal(frequency_hz, waveform)
set_control(control_value)
start_measurement()
poll_measurement(done, measured_vpp, valid, saturated)
safe_stop()
```

### 回退方案

1. BASIC：固定频率、固定安全控制量，不启动闭环；
2. 测量链不满足时，ADC 交流测幅改为 RMS/检波 DC；
3. DDS 不满足波形/频率时，改 DAC/DMA 或限制频段；
4. 自动闭环失败时保持人工标定的离散频点表；
5. 发挥失败不破坏 B1/B2。

## 6. 五类预算当前状态

| 预算 | 当前可写 | 必须由用户/题目确认 |
|---|---|---|
| 采样 | 公式、Nyquist 和每周期点数门 | 真实 SPI/ADC/RMS 路径 |
| 幅度 | 逐级量程和饱和表结构 | 供电、负载、VGA/运放范围 |
| 带宽/建立 | DDS、VGA、滤波、检波和稳定时间组成 | 器件型号和实测 |
| 资源/实时性 | 非阻塞状态机和有限迭代 | MCU、定时器/DMA/CPU |
| 误差 | 时钟、增益、频响、测量和标定残差项 | 题目精度和实测数据 |

当前 G2 不能通过，状态为 `BLOCKED at HARDWARE_USER_GATE`，而不是猜测硬件。

## 7. 给用户的 CubeMX 清单形状

收到 MCU、板卡和器件后，AI 应给出：

- DDS 使用 GPIO/SPI 或 DAC/DMA 的外设需求；
- 幅值执行器使用 DAC/PWM/SPI 的需求；
- 幅值 ADC/RMS DC 采集的 ADC、DMA 和采样触发；
- 系统时间基准；
- UI/UART（若题目需要）；
- IRQ 优先级和总线冲突；
- Safe Stop 对应的输出状态。

AI 随后暂停。用户返回 `.ioc`、生成文件、实际 HAL 句柄、构建日志和最小冒烟
结果后，才进入 Adapter 实现。

## 8. 预期代码文件

```text
User/Inc/app_config.h
User/Inc/board_signal_adapter.h
User/Inc/regulated_output_service.h
User/Inc/contest_app.h
User/Src/board_signal_adapter.c
User/Src/regulated_output_service.c
User/Src/contest_app.c
User/Src/app_main.c
docs/solution_architecture.md
docs/board_adapter_contract.md
docs/code_delivery_checklist.md
docs/verification/
```

`Core/` 只在 CubeMX USER CODE 保护段调用 `app_main` 或转发最小事件。

## 9. 状态机

```text
IDLE
→ CONFIGURE
→ SETTLE
→ MEASURE
→ VALIDATE
→ CORRECT
→ HOLD/DONE

任意状态
→ TIMEOUT / INVALID / SATURATED / IO_ERROR / ABORT
→ SAFE_STOP
```

校正比、执行器范围、最小有效 Vpp、最大迭代、总超时和收敛误差均来自新题
配置，不照搬历史 `[0.7, 1.3]` 或 `2%`。

## 10. 演练结论

该演练证明 Framework 能把陌生稳幅输出题路由到正确的 Knowledge、Library、
Adapter 契约、用户硬件门、代码文件和回退路径。

没有证明：

- 任何具体 DDS/DAC/VGA/ADC/RMS 组合可行；
- 当前模块已在新题 MCU 构建；
- fake 测试或 ARM ELF 已运行；
- 真实幅度、精度、稳定时间和 Safe Stop 已实测；
- 题目达到 L3/L4。

因此结论严格保持 `L0 Architecture Dry Run`。
