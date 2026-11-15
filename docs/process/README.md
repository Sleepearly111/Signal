# Development Process

比赛中使用 STM32F407ZGT6 时，默认复制根级 `stm32f4-template/`，再按
[`STM32F407比赛基线流程.md`](STM32F407比赛基线流程.md) 增删外设；构建和烧录
继续执行 CMake/Ninja + OpenOCD 流程。不得直接修改 canonical 基线。

## 新题流程

1. 先按 [`比赛现场执行手册.md`](比赛现场执行手册.md) 确定现场节奏、基础保底和回退时刻；
2. 可先使用 [`../templates/chatgpt_phase1_prompt.md`](../templates/chatgpt_phase1_prompt.md) 让 ChatGPT 输出器件级方案草案，只判断用什么器件、总体思路、连接图和 FPGA 是否值得使用；
3. ChatGPT 第一阶段只需要题目、器件清单、关键原理图/接口表和现场仪器条件；不默认上传完整 Knowledge、Library、`.ioc`、FPGA 源码或历史工程；
4. 将 ChatGPT 草案交回 Codex，`01-新题方案与工程基线` 再按题目定向读取器件清单、Knowledge/Library 入口和现行规则，审查并冻结方案；
5. 01 内部只给出或校正 A 现有器件、B 性能增强、C 保底/异构三种方案；
6. 停在 `SOLUTION_USER_GATE`，等待用户选择或组合；
7. 用户选定后才建立完整验收矩阵、预算、状态文档和方案架构；
8. 确定 board profile 与 CubeMX 变更；
9. 用户按 [`STM32烧录指南.md`](STM32烧录指南.md) 完成构建、烧录和证据回传；
10. 按当前证据显式选择 experimental 资产，不把候选当成 verified；
11. 分阶段实现：构建 → 关键冒烟 → 单器件 → 信号链 → 整机；
12. 参数标定写入项目配置并记录条件；
13. 使用 [`../templates/code_delivery_checklist.md`](../templates/code_delivery_checklist.md) 冻结交付；
14. 指标达标后更新项目状态；
15. 最后评估 library 晋升，不在赛题开发中途反向污染模板。

用户选定方案后、正式写代码前，必须二次定向阅读相关 Knowledge 正文和 Library
README/接口/验证记录。比赛工程默认拆成新题方案与工程基线、基础功能、发挥功能、
联调验收四类对话，
通过项目内 `docs/project_status.md` 交接。每满 60 分钟询问一次是否保存 commit；
只提醒，未经用户确认不提交。独立提醒入口为
[`../../tests/tools/contest_commit_reminder.ps1`](../reproduction.md)。

## 模块提取流程

1. 在 `docs/library/module_registry.md` 分配稳定 ID；
2. 记录来源仓库、commit、dirty 边界和工作区 SHA-256；
3. 选择已验证来源，不从模板中盲选最新文件；
4. 建立单模块候选档案和 golden regression；
5. 提取纯算法或协议内核到 `library/experimental`；
6. 使用 context、adapter 消除全局变量和固定引脚；
7. 添加主机/fake bus 测试；
8. 回接来源工程并构建；
9. 重新进行硬件回归；
10. 用户确认后晋升 verified。

来源证据、Framework 状态和提取后验证等级必须分开记录。没有实际实现时，candidate 不得写成 experimental。

## Bug 修复流程

1. 记录可复现现象和当前证据；
2. 区分代码、配置、接线、采样能力和器件状态；
3. 做最小修复；
4. 重跑受影响层及上层回归；
5. 记录根因，不只记录改动。

## 文档任务流程

当用户要求“只增加文档”时：只创建/修改 Markdown 等文档，不运行会重生成工程文件的工具，不构建，不格式化源码，不移动目录。
