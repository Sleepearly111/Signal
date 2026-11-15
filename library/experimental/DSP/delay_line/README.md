# delay_line

- 来源：2024C
- 功能：使用调用者提供的环形缓冲，对连续离散样本执行可配置的整数采样点延迟。
- 当前状态：experimental
- Verification：L0 Software Draft
- 验证边界：来源项目已验证；模块本身未独立验证，不是 Hardware Tested。

本模块根据 2024C 中调制包络的整数采样延迟需求重新设计，没有复制项目的 SM 索引实现。来源项目的硬件验证等级不传递给本模块。

## 模块边界

- 纯 C 流式整数延时线，无 STM32、DMA、DAC 或题目逻辑；
- 无动态内存、`printf` 或全局状态；
- 调用者拥有 `DelayLine` 对象和 `float` 环形存储；
- 支持 `delay=0`、运行时修改延迟、跨调用保留历史和原地处理；
- 初始化与复位后的未知历史按零处理；
- 当前不支持分数延时、时间单位换算或多通道交织数据。

## 文件

- `include/delay_line.h`：上下文、状态码和公开接口；
- `src/delay_line.c`：环形缓冲与流式处理实现；
- `tests/test_delay_line.md`：基础测试说明，尚非自动化测试。

接口规划见 [INTERFACE_PLAN.md](INTERFACE_PLAN.md)。
