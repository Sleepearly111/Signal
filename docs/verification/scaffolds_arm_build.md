# 通用状态机脚手架 Arm 构建记录

- 日期：2026-07-24
- 工具链：STM32Cube bundle Arm GCC 14.3.1、CMake 4.3.1、Ninja 1.13.2
- 目标：Generic Cortex-M4，C11，严格告警

启用：

```text
SIGNAL_FRAMEWORK_VALIDATE_SIGNAL_WORKFLOW_SCAFFOLD=ON
SIGNAL_FRAMEWORK_VALIDATE_CONTEST_APP_SCAFFOLD=ON
SIGNAL_FRAMEWORK_BUILD_TESTS=ON
```

结果：

```text
Configure/Generate: exit 0
Build: 8/8
```

生成两个静态库和两个 ARM 测试 ELF。nosys 对 `puts/assert` 所需系统调用给出
预期链接警告，但链接退出码为 0。

## 证据边界

该记录支持两个脚手架标记为 `L1 Builds`。测试 ELF 没有在主机、模拟器或
目标板运行，不能升为 L2；也不证明复制后题目专用状态、物理单位、Adapter
和回退策略正确。
