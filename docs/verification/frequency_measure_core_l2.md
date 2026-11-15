# DSP-INSTFREQ-001 主机验证记录

> 日期：2026-07-24  
> 模块：capture interval / instant frequency core  
> Framework状态：experimental  
> 验证等级：L2 Software Tested  
> 结果：通过，不包含硬件验证

## 1. 范围与来源

来源项目：

- 路径：`projects/AI生成项目/2023D/`
- 分支：`2023d`
- HEAD：`7b15a787b626726fd7fbfba4b5f12c057ad56c1b`
- 来源文件：`User/Inc/inst_freq.h`、`User/Src/inst_freq.c`
- 两个来源文件相对当前HEAD无修改

来源工作区SHA-256：

| 文件 | SHA-256 |
|---|---|
| `User/Inc/inst_freq.h` | `181FEE017F2C5D73CD94FA0EC5A8F3B1F1A511D3DD07AC11EF9285F8F8B769DF` |
| `User/Src/inst_freq.c` | `A47782FCC6F73FBC5311FB50A3B662E4B29A6932A5981EA946EBE5ED21C18F02` |

新模块按
`docs/library_candidate/frequency_measure_core_design.md`
重新实现，没有复制来源源码。

已移除：

- STM32 HAL、TIM8和DMA；
- `app_config.h`；
- 固定计数器位宽、时钟、合并长度和阈值；
- AM/FM/ASK/FSK/PSK业务语义；
- 全局状态、隐藏工作区和动态内存。

## 2. 实现资产

目标目录：

```text
library/experimental/DSP/frequency_measure_core/
├─ CMakeLists.txt
├─ README.md
├─ include/frequency_measure_core.h
├─ src/frequency_measure_core.c
└─ tests/test_frequency_measure_core.c
```

验证时文件SHA-256：

| 文件 | SHA-256 |
|---|---|
| `CMakeLists.txt` | `688A61F484F2C02AE7351839486CB33FC375AB53E51CF78DB6DE184E7D50FFDB` |
| `include/frequency_measure_core.h` | `9434EA4403C0BC78C92EAEC627051262BAAB2C84B636559124C7D620DA21C1BC` |
| `src/frequency_measure_core.c` | `A203AB3B77EDBF38DE158103224D283ADAADFC24707806161D58936A8FF0BC30` |
| `tests/test_frequency_measure_core.c` | `0BF273FC7FD20028B38CADBCE139555295591CE7B43F2E624EBE910706EC75FF` |

README不参与被测二进制，其内容在测试通过后补充。

## 3. 工具链

| 工具 | 版本 |
|---|---|
| CMake | 4.3.4 |
| Ninja | 1.12.1 |
| Windows主机GCC | TDM64 MinGW GCC 4.9.2 |
| C标准 | C11，禁用编译器扩展 |
| 告警 | `-Wall -Wextra -Wpedantic -Werror` |

## 4. 执行命令

构建目录位于系统临时目录，不在Signal仓库内：

```text
      -G Ninja

cmake --build
      --verbose

ctest --test-dir
      --output-on-failure
```

## 5. 构建结果

配置和生成成功。

模块实现和测试均使用：

```text
-std=c11 -Wall -Wextra -Wpedantic -Werror
```

编译、静态库生成和测试程序链接退出码均为0，没有告警。

## 6. 测试矩阵

| 类别 | 覆盖内容 | 结果 |
|---|---|---|
| 正常周期 | 168 MHz、每捕获8周期、672 tick，输出2 MHz | 通过 |
| 计数器回绕 | 8、16、24、32位一次模回绕 | 通过 |
| 零间隔 | 统计zero，窗口无效且无除零 | 通过 |
| 短间隔 | 小于有效下界，统计short | 通过 |
| 长间隔 | 大于有效上界，统计long | 通过 |
| 时钟参数 | 168 MHz与84 MHz线性缩放 | 通过 |
| 合并长度 | 1、2、5，抖动间隔按总tick求平均 | 通过 |
| 尾部数据 | 不足完整窗口的数据单独计数 | 通过 |
| 质量状态 | EMPTY、INSUFFICIENT、INVALID、DEGRADED、GOOD | 通过 |
| 容量 | 工作区和频率输出不足时拒绝部分输出 | 通过 |
| 输入范围 | 捕获值超出计数器位宽时拒绝 | 通过 |
| 配置错误 | 0时钟、0位宽等非法配置 | 通过 |
| 算术边界 | `uint64_t`合并求和溢出 | 通过 |

CTest输出：

```text
Start 1: DSP-INSTFREQ-001
1/1 Test #1: DSP-INSTFREQ-001 ... Passed

100% tests passed, 0 tests failed out of 1
```

测试程序同时输出：

```text
DSP-INSTFREQ-001: all host tests passed
```

## 7. 落盘后复测

模块写入
`library/experimental/DSP/frequency_measure_core/`
后，使用独立的新构建目录
重新执行配置、严格构建和CTest。

落盘后的编译命令仍包含：

```text
-std=c11 -Wall -Wextra -Wpedantic -Werror
```

结果再次为1/1通过、0失败，证明最终目录中的源码与最初被测临时版本行为一致，Signal仓库内未生成构建产物。

## 8. 结论

`DSP-INSTFREQ-001`达到：

```text
experimental / L2 Software Tested
```

L2证据只覆盖当前纯C接口的主机行为：

- 计数值模差；
- 间隔分类；
- 非重叠窗口合并；
- 频率计算；
- 容量、输入和算术错误路径；
- 质量统计。

## 9. 未验证内容

- 未集成STM32、TIM输入捕获或DMA；
- 未完成Arm交叉编译；
- 未测量目标板运行时间、代码尺寸和RAM；
- 未验证多次计数器回绕恢复，因为仅有计数快照时该信息不可观测；
- 未证明任何2023D调制分类阈值或整机识别率；
- 未达到L3、L4或`verified`。

如需升级，应先由另一个阶段把该模块接入明确的真实硬件工程，记录时钟、计数器位宽、输入捕获预分频、信号频率、异常事件和板上对照结果。
