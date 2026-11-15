# 2026G Board Adapter 契约草案

> 状态：`DRAFT / HARDWARE_USER_GATE`。以下板级字段来自复制基线，CubeMX 重新生成
> 和实际接线确认后才能冻结。

## 1. Adapter 清单

| Adapter | 外设 | 当前基线句柄/资源 | 上层 Service | 状态 |
|---|---|---|---|---|
| `FpgaMockAdapter` | 无 | 纯软件数据源 | Measurement Service | 02 阶段实现 |
| `FpgaSpiAdapter` | SPI3 | `hspi3`，PB0 CS，PD6 DRDY | FPGA Link Service | 复制基线；未适配 AD9248 |
| `DisplayUartAdapter` | USART1 | `huart1` | Display Service | 屏幕协议未冻结 |
| `DiagnosticsAdapter` | USART2 | `huart2` | App/diagnostics | 基线预留 |
| `CalibrationStorageAdapter` | Flash/外存 | TODO | Calibration Service | 未冻结 |

## 2. 依赖方向

```text
App → Service → Board Adapter → CubeMX/HAL
                  ↓
             Protocol core
```

Mock 与 Real 数据源必须返回相同的通道统计、样本窗口、采样率、滤波器版本和错误。
业务层不得因为数据源是 Mock 而跳过质量门。

## 3. 回调所有权

| 回调 | 当前复制基线唯一入口 | ISR 内操作 | 主循环操作 |
|---|---|---|---|
| `HAL_GPIO_EXTI_Callback` | `Core/Src/main.c` | 转发 DRDY 事件 | 启动/推进帧读取 |
| `HAL_SPI_TxRxCpltCallback` | `Core/Src/main.c` | 设置 DMA 完成事件 | CRC、分片、ACK |
| `HAL_SPI_ErrorCallback` | `Core/Src/main.c` | 设置错误事件 | Abort、诊断、重试 |

CubeMX 重新生成后，回调只能保留一个入口或统一分发点。ISR 禁止打印、CRC、FFT、
浮点分析或阻塞等待。

## 4. ISR 到主循环事件

| 事件 | 生产者 | 消费者 | 数据所有权 | 溢出策略 |
|---|---|---|---|---|
| DRDY | EXTI | FPGA Link Service | 单个事件计数/时间戳 | 计数并报告丢事件 |
| DMA complete | SPI DMA IRQ | FPGA Link Service | DMA buffer 完成后归 CPU | Busy 时拒绝新事务 |
| DMA error | SPI/DMA IRQ | FPGA Link Service | 最后错误+计数 | Safe Stop |

DMA buffer 的确切数量、覆盖策略和并发保护在本题 `.ioc` 与协议冻结后补充。
`volatile` 不作为原子性或互斥证明。

## 5. 目标 API

```text
Init(context, config, handles)
Start(context, request)
Service(context, now_ms)
IsDone(context)
GetResult(context, result)
Abort(context)
OnIrq(context, event)
GetDiagnostics(context, diagnostics)
```

所有长操作非阻塞；所有状态有单状态超时和 2000 ms 总超时；错误不得静默吞掉。

## 6. 错误与 Safe Stop

至少区分：

- 非法参数/能力不匹配；
- Busy；
- DRDY、SPI 或分片超时；
- HAL/DMA 错误；
- CRC、版本、长度、序号、采集 ID、乱序和重复错误；
- FPGA FIFO/采样溢出；
- 两量程均削顶或无有效动态范围；
- Abort。

Safe Stop 顺序：

1. 拒绝新请求；
2. 终止或等待当前 DMA；
3. 拉高 CS，释放 SPI；
4. 清除当前采集 ID 和未完成分片；
5. 保留最后错误、计数和最后成功时间；
6. 返回可再次 `Start` 的安全状态。

Safe Stop 必须可重复调用。

## 7. 冻结前待确认

- [ ] 本题 `.ioc` 与生成文件；
- [ ] SPI 模式、速率、DMA Stream/Channel 和 NVIC；
- [ ] CS/DRDY 实装引脚、边沿、上下拉和空闲电平；
- [ ] USART1 屏幕型号、协议和吞吐；
- [ ] FPGA 消息类型、元数据、样本格式、滤波增益和分片；
- [ ] DMA buffer 所有权、超时与溢出注入测试；
- [ ] 可重复 Safe Stop 的板级实测。
