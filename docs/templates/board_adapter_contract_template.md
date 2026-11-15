# <项目名> Board Adapter 契约

> 作用：冻结 HAL、DMA、IRQ、引脚和全局回调的所有权。  
> 该文件属于具体项目，不属于纯 Library。

## 1. Adapter 清单

| Adapter | 外设 | HAL 句柄 | GPIO | DMA/IRQ | 上层 Service |
|---|---|---|---|---|---|

## 2. 依赖方向

```text
App → Service → Board Adapter → CubeMX/HAL
                  ↓
            Library protocol core
```

禁止：

- Library 直接包含项目 `main.h`；
- 驱动协议内核拥有固定 HAL 句柄或引脚；
- Adapter 执行滤波器分类、闭环策略或 UI；
- 多个文件同时拥有同一个 HAL 全局回调；
- ISR 中阻塞、打印、FFT、浮点闭环或等待设备。

## 3. 回调所有权

| HAL/IRQ 回调 | 唯一所有者 | 分发对象 | ISR 内操作 | 主循环操作 |
|---|---|---|---|---|

若必须分发：

```c
void HAL_xxx_Callback(Handle *handle)
{
    if (handle == expected_handle) {
        BoardAdapter_OnIrq(&g_adapter);
    }
}
```

回调入口只能有一个。`BoardAdapter_OnIrq()` 只保存最小事件、计数、时间戳或 DMA 位置。

## 4. ISR 到主循环事件

| 事件 | 生产者 | 消费者 | 数据所有权 | 溢出策略 | 并发保护 |
|---|---|---|---|---|---|

必须说明：

- 单生产者/单消费者还是多生产者；
- 索引和标志是否需要临界区；
- DMA Buffer 何时归 CPU；
- Buffer 覆盖、丢事件和计数溢出如何观察；
- 缓存一致性是否适用。

`volatile` 只表示每次访问内存，不自动提供原子性、互斥或内存屏障。

## 5. Adapter API

```text
Init(context, config, handles)
Start(context, request)
Service(context, now)
IsDone(context)
GetResult(context, result)
Abort(context)
OnIrq(context, event)
GetDiagnostics(context, diagnostics)
```

| API | 可从 ISR 调用 | 可阻塞 | 最长时间 | 错误 |
|---|---|---|---|---|

## 6. 错误映射

| HAL/硬件错误 | Adapter 错误 | Service 响应 | 安全动作 | 恢复条件 |
|---|---|---|---|---|

错误不得被静默吞掉。至少保存：

- 最后错误；
- 错误计数；
- 超时计数；
- 丢事件/溢出计数；
- 最近一次成功时间；
- 当前外设模式。

## 7. 模式与总线所有权

| 资源 | 所有者 | 允许模式 | 切换步骤 | 互斥 |
|---|---|---|---|---|

ADS8688 等有 AUTO/MANUAL 状态的器件必须显式停止旧模式、独占总线、清理流水线并重新建立样本归属。

## 8. Safe Stop

按顺序定义：

1. 停止新请求；
2. 禁止或等待 DMA/IRQ；
3. 停止定时器/转换；
4. 器件回到安全输出或低增益；
5. 释放总线所有权；
6. 保存错误和最后状态。

Safe Stop 必须可重复调用。

## 9. 验证矩阵

| ID | 测试 | 注入/条件 | 预期 | 结果 | 等级 |
|---|---|---|---|---|---|
| A1 | 初始化 | 合法/非法句柄 | | | |
| A2 | 正常事务 | | | | |
| A3 | Busy | 重复 Start | | | |
| A4 | Timeout | 设备无响应 | | | |
| A5 | HAL error | 注入错误 | | | |
| A6 | Abort | 各运行状态 | | | |
| A7 | IRQ/DMA | 完成、半满、错误 | | | |
| A8 | 溢出 | 消费者故意变慢 | | | |
| A9 | 模式切换 | AUTO↔MANUAL | | | |
| A10 | Safe Stop | 重复调用 | | | |
