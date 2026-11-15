# Signal Framework 自动审计

> 工具：`tests/framework_audit.ps1`  
> 类型：只读一致性检查  
> 不包含：C 编译、CTest、Arm 构建、烧录或硬件测试

## 目的

随着 Knowledge、Library 和脚手架增长，人工维护多个索引容易再次出现：

- 新文件未登记；
- 本地链接失效；
- L2/L0/candidate 数量与磁盘不一致；
- 根 CMake 漏掉模块；
- 脚手架意外依赖 HAL、`main.h` 或动态内存；
- 文档继续声称“module count 0/1”；
- 空模板被误称为可用工程。

该脚本把这些条件变成可重复的失败门。

## 运行

在 Signal 根目录：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/framework_audit.ps1
```

也可显式指定仓库：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tests/framework_audit.ps1 `
```

脚本只读文件。全部通过返回 `0`；任一检查失败返回 `1`。

## 2026-07-24 当前运行记录

命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/framework_audit.ps1
```

结果：

```text
Checks: 124
Failures: 0
Signal Framework audit passed.
Exit code: 0
```

首次运行暴露了 Windows PowerShell 5 对无 BOM UTF-8 脚本中中文路径的代码页问题。脚本随后改为纯 ASCII，通过文件内容和稳定英文标识发现中文路径；当前确认脚本自身 `NON_ASCII=0`。

该记录只证明 2026-07-24 当前工作区的 124 项静态一致性检查通过。当前覆盖比赛现场运行手册、代码交付检查表、当前/历史事实源标签、目标完备性审计、滤波器识别与稳幅输出两个端到端演练、题型能力路由、四个新题 L1 核心、两个 L1 状态机脚手架、扫频到判型组合测试、滤波器识别与幅值闭环参考 App、Cortex-M4 工具链、资产登记、烧录指南及“用户操作 CubeMX/硬件，AI 等待证据后继续”的协作门。

## 当前覆盖

### 入口

- AI Context/Rules/Index；
- 比赛现场执行手册、目标完备性审计和代码交付检查表；
- 新题 SOP；
- 方案架构手册；
- 合成陌生题端到端 L0 架构演练；
- 资产索引；
- Knowledge Index；
- Library README/Version。

### Markdown

检查 Framework、Knowledge 和 Library 范围的本地 Markdown 链接；跳过 `projects/` 和 `.git/`。

### Knowledge

- 所有正文是否出现在 `knowledge/INDEX.md`；
- 权威定义行中的稳定 ID 是否重复。

### Library

- 11 个 L2 README 和等级标签；
- 4 个 L1、3 个 L0 README 和等级标签；
- 1 个 candidate；
- 18 个资产是否出现在 `docs/asset_index.md`；
- 10 个根 CMake 选择项、聚合目标和脚手架选项。

### Scaffolds

- 必需头文件、实现、测试、CMake 和 README；
- 禁止 HAL、`main.h`、动态内存和 `printf`；
- Workflow 的超时、迭代、无效测量、饱和与 Safe Stop；
- Contest App 的 BASIC、ADVANCED、FALLBACK、REPORTING 和 Safe Stop。
- regulated output App 的测量质量门、校正/执行器限幅、超时、Abort 和故障 Safe Stop。

### 过时状态

检查已知的旧状态文本，例如模块数 0/1 和根模板可用声明。

## 证据边界

该审计通过只表示：

- 文件、索引和链接当前一致；
- 必需代码形状存在；
- 没有命中指定的禁止依赖文本。

它不解析完整 C/CMake 语义，也不执行软件或硬件。脚手架仍需真实 C 编译和测试；Library 仍需 Arm/STM32/硬件验证。

## 增加资产时

新增 Knowledge、Library 模块、脚手架或权威入口时，应同步扩展该脚本。审计器本身的改动也必须人工检查，不能因为脚本返回 0 就假设覆盖范围完整。
