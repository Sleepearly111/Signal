# Contest App Scaffold

> 类型：整题编排脚手架，不是稳定 Library 模块  
> 状态：experimental scaffold  
> 当前验证等级：L1 Builds

## 职责

该脚手架负责顶层比赛流程：

```text
Start BASIC/ADVANCED
→ 非阻塞运行模式
→ ADVANCED 失败时按配置回退 BASIC
→ 安全停机
→ 非阻塞报告
→ DONE / FAULT / ABORTED
```

它不实现任何具体测量、DDS、ADC、闭环或 UI。具体基础/发挥模式应由项目 Service 实现；单个测量或闭环可从相邻的 `nonblocking_signal_workflow` 脚手架建立。

## 关键语义

- BASIC 与 ADVANCED 是项目模式，不是验证等级；
- 只有 ADVANCED 执行失败且配置允许时才自动回退 BASIC；
- 回退前必须 `safe_stop`；
- 模式成功/失败都进入报告流程；
- 报告本身必须非阻塞；
- 整题有跨 `uint32_t` 回绕安全的总超时；
- 任意 I/O 错误、报告错误、Abort 或运行中 Deinit 都进入安全状态；
- mode result code 由项目定义，脚手架不解释业务错误。

## 项目接入

项目至少实现：

- `start_mode`；
- `service_mode`；
- `is_mode_done`；
- `get_mode_result`；
- `start_report`；
- `poll_report`；
- `safe_stop`。

建议映射：

```text
contest_app
  ├─ BASIC service
  ├─ ADVANCED service
  │    └─ one or more signal_workflow instances
  ├─ report/HMI service
  └─ board safe-stop coordinator
```

复制后必须把 `contest_app_*` 改成题目名称，并明确基础、发挥和回退的题目含义。

## 测试意图

主机 fake-port 测试覆盖：

- BASIC 成功；
- ADVANCED 成功；
- ADVANCED 失败后 BASIC 回退成功；
- 禁止回退时进入故障并报告；
- 总超时回绕、Busy 和 Abort；
- 缺失回调和报告启动失败。

2026-07-24 使用 STM32Cube bundle Arm GCC/CMake/Ninja 完成根构建 8/8，
静态库和测试 ELF 均生成，因此当前为 L1。测试 ELF 尚未执行，不能升为 L2；
证据见
[`../../../../docs/verification/scaffolds_arm_build.md`](../../../../docs/verification/scaffolds_arm_build.md)。
