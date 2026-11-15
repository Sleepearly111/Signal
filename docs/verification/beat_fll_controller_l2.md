# DSP-BEATFLL-001 主机软件验证记录

> 日期：2026-07-27  
> 对象：`library/experimental/DSP/beat_fll_controller/`  
> 结论：experimental / L2 Software Tested

## 1. 验证边界

本记录只证明纯 C 拍频 FLL 控制状态机在指定 Windows 主机工具链下完成严格构建并通过测试。它不证明 AD630/ADS8688 测量、Arm/STM32 集成、DMA/ISR/软件 DDS、实时性或硬件行为；2025G 的 L3 证据也不传递给本模块。

## 2. 工具链与命令

- CMake 4.3.4
- MinGW Makefiles
- TDM64 GCC 9.2.0
- C11，`-Wall -Wextra -Wpedantic -Werror`

```text
cmake -S library/experimental/DSP/beat_fll_controller
      -B build/beat_fll_controller-mingw
      -G "MinGW Makefiles"
cmake --build build/beat_fll_controller-mingw --verbose
ctest --test-dir build/beat_fll_controller-mingw --output-on-failure
```

三条命令退出码均为 0。CTest：`DSP-BEATFLL-001` 1/1 通过。

## 3. 测试覆盖

- 配置拒绝和启动/停止；
- stale/invalid 观测不推进状态；
- 粗细步进和带裕量的 P&O 反向；
- 连续 fresh 观测锁定；
- HOLD 冻结与连续 fresh 观测解锁。

## 4. 被测文件指纹

| 文件 | SHA-256 |
|---|---|
| `include/beat_fll_controller.h` | `B960C91C496209E02B5ABA9787FA9182841A22C8526943E8104F375F519D8FC2` |
| `src/beat_fll_controller.c` | `97C2F681F5CB6C67D95AAD6A1ED2724FAFC0C0F5AE069B26550091C01B635589` |
| `tests/test_beat_fll_controller.c` | `81BA6620E0F0181F433CD022838D2A89C8434A3E6CFBA3CAD664538686FFD5C3` |

文件变化后必须重新验证。下一道门是 Arm 构建、STM32 接入、资源/实时性和板级扰动回归。
