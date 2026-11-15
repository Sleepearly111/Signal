# Library Lifecycle

## 状态

### experimental

含义：候选公共模块。

要求：

- 有独立目录
- 有 README
- 有测试
- 有明确 API

不代表硬件验证。

### verified

进入条件必须全部满足：

1. 公开 API 稳定
2. 依赖隔离
3. 软件测试通过
4. 至少一个真实硬件工程验证
5. 记录验证条件
6. 用户确认

### deprecated

表示旧模块。

保留原因：

- 历史参考
- 兼容旧项目

## 验证等级关系

```text
L0 Draft
    ↓
L1 Builds
    ↓
L2 Software Tested
    ↓
L3 Hardware Tested
    ↓
L4 Contest Verified
```

验证等级和 Library 状态不是一回事。

例如，`experimental + L2` 是合法状态。
