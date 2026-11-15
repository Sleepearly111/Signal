# ADR-0002：新题流程核心保持独立 experimental 模块

- 状态：Accepted
- 日期：2026-07-24

## 背景

端到端陌生题演练暴露出通用扫频、滤波器判型和校准数据格式缺口。它们跨越
Service、DSP 和数据持久化边界，不能塞入单点 workflow，也不能继承历史项目
状态机与板卡参数。

## 决定

建立四个无 HAL、无动态内存的独立 experimental 模块：

- `sweep_service`：只编排频点、测量回调、重试和超时；
- `filter_classifier`：只对已校准幅频数据做保守判型；
- `calibration_record`：只定义版本化二进制格式和完整性检查。
- `transfer_response`：只处理 Vin/Vout 质量、增益/dB 和电平交点。

它们以单独的 `SIGNAL_FRAMEWORK_VALIDATE_*` 开关接入根 CMake，不进入已有
`BUILD_ALL_L2`。2026-07-24 根 Arm 构建通过后为 L1；只有测试实际运行通过
才考虑 L2。

## 后果

- App 可以组合四个核心而无需让 Library 依赖 CubeMX。
- 板级设频、采集和 Flash 读写仍由用户确认后的 Adapter 实现。
- 当前根 Arm 构建支持 L1；CTest/主机测试运行和硬件证据必须分别补齐。
