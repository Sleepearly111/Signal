# dual_signal_generator

- 来源：2024C
- 功能：生成具有可配置载波、AM、幅度衰减、时延和相位的通用离散信号。
- 当前状态：experimental
- Verification：L0 Software Draft
- 验证边界：来源项目已验证；模块本身未独立验证，不是 Hardware Tested。

本模块根据 2024C `User/Src/dual_channel.c` 与 `User/Inc/dual_channel.h` 的信号模型重新设计，没有复制原项目实现。来源项目的硬件验证等级不传递给本模块。

## 模块边界

- 纯 C 数学模块，无 STM32 HAL、DAC、DMA、TIM 或串口屏依赖；
- 无全局工程变量、动态内存和 `printf`；
- 调用者提供 `float` 输出缓冲及容量；
- 单次调用生成一路信号，两路或多路信号由调用者使用不同配置分别生成；
- 当前不包含硬件播放、码值量化、零填充延时线或分数延时滤波。

## 文件

- `include/dual_signal_generator.h`：公开数据类型和生成接口；
- `src/dual_signal_generator.c`：通用样本生成实现；
- `tests/test_dual_signal_generator.md`：基础测试说明，尚非自动化测试。

接口规划见 [INTERFACE_PLAN.md](INTERFACE_PLAN.md)。
