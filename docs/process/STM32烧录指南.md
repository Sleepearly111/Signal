# STM32 构建与烧录操作指南

> 当前 STM32F407ZGT6 比赛工程默认使用 OpenOCD，不默认使用
> `STM32_Programmer_CLI`。当前板级基线、CMake/Ninja 和 OpenOCD
> 流程见 [`STM32F407比赛基线流程.md`](STM32F407比赛基线流程.md)；
> 当前本机已验证命令也可参考
> [`STM32F407_OpenOCD烧录指南.md`](STM32F407_OpenOCD烧录指南.md)。

> 当前用途：用户执行构建、ST-LINK/SWD 烧录与校验时的操作基线。  
> 协作边界：烧录由用户操作；AI 只给出命令、检查项并根据用户返回日志继续分析。

## 1. 使用前必须替换的项目参数

原指南中的下列值是 2025G 历史工程事实，不得直接沿用到新题：

| 参数 | 历史值 | 新题要求 |
|---|---|---|
| 工程目录 | `projects/进行中项目/2025G/2025G` | 替换为当前工程绝对路径 |
| 构建目录 | `build/Debug` | 以当前 CMake preset/实际目录为准 |
| 固件名 | `app.elf` | 以本次链接目标为准 |
| GNU 工具链 | `14.3.1+st.2` | 记录并使用当前实际版本 |
| Programmer | `2.22.0+st.1` | 记录并使用当前实际版本 |

## 2. 烧录前检查

- 用户已在 CubeMX 完成配置并生成工程；
- 当前 `.ioc`、MCU 型号和链接脚本相互一致；
- ST-LINK 已通过 USB 连接；
- 目标板已上电，Programmer 能读到合理目标电压；
- SWDIO、SWCLK、GND、目标电源参考连接正确；
- 构建产物来自当前工作区，而不是旧工程或旧 build 目录；
- 已确认本次操作不会覆盖仍需保留的目标板程序。

## 3. Git Bash 操作模板

先填写本次任务的三个变量：

```bash
PROJECT_DIR="<当前工程绝对路径>"
ELF_PATH="<当前 ELF 相对工程路径>"
```

把实际存在的工具版本临时加入 PATH：

```bash
export PATH="$B/gnu-tools-for-stm32/<version>/bin:$B/cmake/<version>/bin:$B/ninja/<version>/bin:$PATH"
cd "$PROJECT_DIR"
```

只构建：

```bash
cmake --build <build-dir>
```

确认构建退出码为 0、ELF 路径正确后，由用户执行烧录和校验：

```bash
"$B/programmer/<version>/bin/STM32_Programmer_CLI.exe" \
  -c port=SWD \
  -w "$ELF_PATH" \
  -v
```

不得在构建失败时继续烧录旧 ELF。

## 4. 2025G 已知可用命令（仅历史复现）

以下命令只对应原指南记录的 2025G 路径与版本：

```bash
export PATH="$B/gnu-tools-for-stm32/14.3.1+st.2/bin:$B/cmake/4.3.1+st.1/bin:$B/ninja/1.13.2+st.1/bin:$PATH"
cmake --build build/Debug &&
  "$B/programmer/2.22.0+st.1/bin/STM32_Programmer_CLI.exe" \
  -c port=SWD \
  -w build/Debug/app.elf \
  -v
```

它可以作为工具调用范例，但不能证明路径、版本、板卡连接或 ELF 仍然有效。

## 5. 用户返回给 AI 的证据

烧录完成后请返回：

1. 当前工程路径、分支/HEAD 和目标 MCU；
2. 使用的完整构建命令与退出码；
3. ELF 的实际路径和生成时间；
4. Programmer 完整命令、目标电压、Device ID；
5. 擦除、写入、校验是否分别成功；
6. 复位后的 LED、串口或最小功能现象；
7. 若失败，保留完整原始错误文本，不只给截图结论。

AI 根据这些当前证据判断是构建、连接、保护、供电还是应用问题，再给下一步；
不会自行发起烧录。

## 6. 常见错误

| 错误信息 | 首要检查 | 用户操作 |
|---|---|---|
| `No debug probe detected` | ST-LINK USB 是否枚举 | 重新插接 USB，等待后重试 |
| `DEV_USB_COMM_ERR` | USB 通信或线缆 | 重新插接，更换端口/线缆后重试 |
| `failed to erase memory` | 写保护、供电、SWD 稳定性 | 先读取保护状态，避免盲目反复擦除 |
| `ninja: no work to do` | ELF 是否确为当前源码产物 | 该信息本身正常，继续核对 ELF 时间和路径 |
| 写入成功但无运行现象 | 复位、BOOT、时钟、链接地址和应用状态 | 返回 Programmer 日志及最小串口/LED 现象 |

## 7. 证据等级

- 构建 exit 0：最多证明 L1；
- Programmer 写入和 verify 成功：证明固件写入一致，不证明功能正确；
- 指定接线和条件下完成最小功能实测：才能形成 L3 板级证据；
- 按题目验收矩阵完整通过：才属于 L4 或等价整题证据。
