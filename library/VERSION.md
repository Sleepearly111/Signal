# Signal Framework Library Version

| 字段 | 当前值 |
|---|---|
| Framework version | `1.0` |
| Library schema version | `0.4.0-experimental` |
| Phase | `2C / reusable contest workflow cores` |
| API stability | `experimental` |
| Module count | `18`（10 个 L2、4 个 L1、3 个 L0、1 个 candidate） |
| Validation level | 混合；各模块以自身 README 和验证记录为准 |
| Established | `2026-07-19` |
| Last inventory check | `2026-07-24` |

`0.4.0-experimental` 表示根 CMake 除十个 L2 软件模块聚合入口外，还为扫频服务、滤波器判型、校准记录和 Vin/Vout 传递响应四个 L1 新题核心提供了显式验证开关。它不表示 API 已稳定，也不表示测试已经运行，更不表示 STM32 工程接入、目标板性能或任何硬件链路已经验证。设计决定见 [`../docs/decisions/0001-library-l2-aggregation.md`](../docs/decisions/0001-library-l2-aggregation.md) 和 [`../docs/decisions/0002-contest-workflow-cores.md`](../docs/decisions/0002-contest-workflow-cores.md)。

版本规则：

- 只修改文档或模板注释：不必提升 library schema version；
- 改变目录约定、CMake 入口或模板契约：提升 pre-1.0 minor/patch，并记录 AI decision/ADR；
- 首个真实模块进入 experimental：记录模块自身版本和验证等级，不自动把 library 提升为稳定版；
- 只有公共 API、回归矩阵和晋升规则稳定后，才讨论 library `1.0.0`。
