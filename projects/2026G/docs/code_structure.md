# 2026G 首版代码结构规范

## 1. 采用的结构证据

| 参考 | 采用 | 明确不采用 |
|---|---|---|
| `stm32f4-template` | 唯一板级源头、CMake/CubeMX 形状、统一回调入口 | 直接在 canonical 目录开发、继承来源 L3 |
| 2025G | 集中配置、主循环推进 Service、基础/发挥隔离 | 题目参数、标定表、旧状态机和旧 `.ioc` |
| test0 | HAL/MX 初始化顺序和最小启动闭环 | 巨型 `main.c`、题目全局变量 |
| FPGA 高速采样桥 | Protocol/Adapter/Service/App 分层、CRC/ACK、DRDY/DMA 事件 | AD9280 位宽、25 MSPS、256 点和旧引脚结论 |
| 2023D | `Init/Start/Service/IsDone`、采集/估计/HMI/自测职责拆分 | 分类阈值、采样参数和 dirty 板级配置 |
| 2024C | App context、物理单位和 UI/业务边界 | 未经验证的参数和硬件动作 |

## 2. 2026G 目录与依赖

```text
User/
├─ App/       g2026_app、g2026_ui_model
├─ Service/   g2026_measurement_service、fpga_link_service、display_service
├─ Adapter/   fpga_mock_adapter；硬件门后再实现 fpga_spi_adapter
├─ Protocol/  fpga_packet_protocol、g2026_fpga_messages
├─ DSP/       rms、spectrum、fundamental、wave_reconstruct
└─ Config/    g2026_config.h
```

依赖只能是：

```text
App → Service → Adapter → CubeMX/HAL
        └────→ DSP/Protocol
```

DSP 与 Protocol 不包含 `main.h`、HAL 句柄、固定引脚或项目全局变量。

## 3. C 接口规范

- 使用 C11、显式 context/config/request/result，不新增隐藏的可变全局状态。
- 长操作统一采用
  `Init → Start → Service → IsDone → GetResult → Abort`。
- 公共结构字段带物理单位后缀，例如 `_hz`、`_mv`、`_milli_hz`、`_sample_count`、
  `_timeout_ms`；协议中不直接传 `float` 或 C 结构体内存。
- 所有状态都有超时、错误码和可重复调用的 Safe Stop；禁止无限等待。
- ISR 只记录事件、计数、DMA 位置或时间戳；CRC、重组、FFT、日志和 UI 在主循环。
- BASIC 未通过前，ADVANCED 不启动、不占用外设、不拥有回调。
- 标定常量必须标注 `TODO: 实测`，并绑定板卡、量程、端接、采样率、滤波版本和日期。
- 不使用动态内存；4096 点窗口、FFT 复数区和显示缓冲的所有权必须在接口中明确。

## 4. 首版实现顺序

1. 冻结 2026G FPGA 消息字段与样本分片契约。
2. 接入纯 `DRV-FPGAPKT-001`，增加项目消息解析和错误注入测试。
3. 实现 `MockFpgaSource` 与 4096 点黄金向量。
4. 接入 RMS/FFT 纯算法，新增基频、谐波关系和重构 Vpp 测试。
5. 实现非阻塞 Measurement Service 和 2 秒总超时。
6. 实现 BASIC App、USART2 文本报告和 UI Model。
7. 用户通过硬件门后，实现 `RealFpgaSource`、DMA/IRQ 分发和板级 Safe Stop。

## 5. 首版验收门

- 主机测试覆盖正常、边界、削顶、CRC、乱序、丢片、重复、超时、Abort 和回绕。
- CMake 显式列出全部源文件，不能借用旧 build 产物。
- Debug/Release 构建、主机测试、板级测量和正式验收分别报告。
- 任何来源项目的 L2/L3/L4 不自动成为本项目证据。
