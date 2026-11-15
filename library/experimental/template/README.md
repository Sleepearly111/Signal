# Experimental Module Template

此目录是未来模块的结构模板，不是可直接用于题目的公共模块，也不属于默认 `signal_framework` 构建目标。

## 使用前必须完成

1. 在 `docs/library/module_registry.md` 中确认 Registry ID；
2. 复制到新模块目录并改名，禁止保留通用 `signal_module_*` 公共符号；
3. 用模块实际配置替换 `signal_module_config_t`；
4. 用最小 bus/adapter 接口替换通用 transfer；
5. 明确输入、输出、状态、超时和失败模式；
6. 添加 `tests/` 下的主机测试或 fake bus；
7. 新增模块 README，记录来源哈希和验证等级；
8. 通过 CMake 独立构建后再接入任何题目工程。

## 模板体现的规则

- 显式 context，不使用隐藏全局变量；
- 外部 I/O 通过 ops 和 user context 注入；
- 错误使用状态码返回；
- `init` 校验参数，`deinit` 清理上下文；
- 不包含 HAL、`main.h`、固定引脚或题目配置。

模板只提供最小形状。驱动、DSP、服务的最终 API 必须依据各自候选文档设计，不能机械复制本模板。
