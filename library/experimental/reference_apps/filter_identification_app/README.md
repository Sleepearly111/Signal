# Filter Identification Reference App

- 状态：`reference app / L1 Builds`
- 组合：`sweep_service → transfer_response → filter_classifier`
- 接口：`Init/Start/Service/IsDone/GetResult/Abort/Deinit`

## 用途

这是从陌生滤波器题方案直接落到代码的完整、无 HAL 参考应用。它：

- 复制并拥有最多 32 个频点；
- 通过 `sweep_ops_t` 接受板级设频和双通道测量 Adapter；
- 保存扫频、测量质量、判型和电平交点结果；
- 将平坦或证据不足的响应安全输出为 UNKNOWN；
- 区分扫频、传递响应和分类器失败；
- 支持 Busy、Abort、Safe Stop 和总超时。

## 用户完成 CubeMX 后的唯一板级接缝

项目只需根据实际 `.ioc` 实现：

```text
set_frequency
start_measurement
poll_measurement
safe_stop
```

这些回调分别绑定实际 DDS/DAC、Vin/Vout ADC/RMS 路径和安全输出。AI 必须在
用户返回 CubeMX 事实后才能编写它们，不得在本参考应用中写死 HAL 句柄或引脚。

## 证据边界

测试源码覆盖低通成功、平坦 UNKNOWN、无效测量故障和 Abort。2026-07-24
使用 STM32Cube bundle Arm GCC/CMake/Ninja 完成根构建 22/22，App 静态库和
测试 ELF 均生成；断言未运行，因此不是 L2。它不计入纯 Library 模块数量。
