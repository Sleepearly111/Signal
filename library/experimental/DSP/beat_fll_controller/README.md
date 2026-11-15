# DSP-BEATFLL-001 — Beat-frequency FLL Controller

> 状态：experimental  
> API 版本：`0.1.0`  
> 当前验证等级：L2 Software Tested（仅主机软件测试）

## 用途与边界

本模块把 2025G 发挥(2) 中经过硬件调试的“拍频搜索—锁定保持”控制思想重构为纯 C 状态机。输入是调用者已经判定为新鲜、有效的拍频绝对值；输出是一次带符号的频率调整量。

模块不包含 AD630、ADS8688、ADC 采样、过零检测、STM32 HAL、DMA、定时器、DAC、软件 DDS 或串口打印。来源工程的 L3 实测结论不会自动传递给本模块。

## 控制语义

- `SEARCH`：根据拍频下降/上升执行扰动观察（P&O）；大拍频使用粗步进，小拍频使用细步进。
- `HOLD`：连续满足锁定阈值后冻结调整量。
- 解锁滞回：仅连续的新鲜有效观测超过解锁阈值时返回 `SEARCH`。
- stale/invalid 输入不推进确认计数、不改变方向，也不产生调整。

所有阈值、步进、反向裕量和确认次数都由调用者显式配置，不内置 2025G 的板级参数。

## API

公开头文件：`include/beat_fll_controller.h`。

- `beat_fll_init()`：验证并保存配置，进入 `IDLE`。
- `beat_fll_start()`：清空历史并进入 `SEARCH`。
- `beat_fll_update()`：消费一次观测并返回状态、调整量和事件标志。
- `beat_fll_stop()`：进入 `IDLE` 并清空搜索历史。

调整量的单位由调用者定义，但粗细步进必须使用同一单位。控制器不负责限幅、累计 trim、频率换算或执行器量化。

## 构建与测试

```text
cmake -S library/experimental/DSP/beat_fll_controller -B <build>
cmake --build <build>
ctest --test-dir <build> --output-on-failure
```

测试覆盖配置拒绝、启动/停止、stale/invalid 观测、粗细步进、P&O 反向裕量、连续锁定确认以及 HOLD 解锁滞回。L2 结论不包含 Arm 交叉编译、STM32 集成、实时性或硬件验证。

## 来源与复验

- 来源项目：`projects/已完成项目/2025G/2025G/`
- 来源总结：`docs/2025G_发挥2_FLL_DDS_调试总结.md`
- Knowledge：`K-DSP-005`
- 提取方式：接口重构，不复制项目 HAL、ISR、阈值或状态机外围代码。

接入新题时必须重新验证拍频测量的新鲜度语义、控制方向、执行器分辨率、限幅、超时、波形质量和全频段硬件表现。
