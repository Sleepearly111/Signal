# Signal · 电赛信号类研发资料库

Signal 汇集电赛信号类项目的代码、接线、器件、调试和实测资料。主平台是 STM32F407，涵盖 DDS、ADC/DAC、滤波器辨识、模拟鉴相和 FPGA 高速采样。

## 从一个项目开始

| 项目 | 内容 | 现有证据 |
| --- | --- | --- |
| [2026 G · 周期信号测量分析装置](projects/2026G/) | AD9248 单路直连、FPGA 抽取、STM32 频谱和幅值测量 | 三问各 5 组台架数据在记录条件下通过数值和时间门限 |
| [2025 G · 电路模型探究装置](projects/2025G/) | DDS 激励、增益闭环、扫频判型、周期回放与同频控制 | 分功能、分阶段实测，参见项目复盘 |
| [2023 H · 信号分离装置](projects/2023H/) | test0 基线、AD630 鉴相、双 AD9833、FLL/PLL | 历史整题结果与后续锁相子实验分别记录 |
| [2023 D · 信号调制识别](projects/2023D/) | AD603、TLV3501、包络采集和调制识别 | 已有频率与 CW/AM 实测，其余调制类型见覆盖范围 |
| [2024 C · 无线传输信号模拟系统](projects/2024C/) | 双 DAC、延时/相移、衰减和电阻合路 | 存档中的验证记录存在版本差异，参见项目页 |
| [FPGA—STM32 高速采样桥](projects/fpga-stm32-bridge/) | AD9280、SPI 分包、CRC、ACK、DMA | 指定硬件组合的通信联调 |
| [LTC1068 低通试验](projects/ltc1068-5khz/) | STM32 定时器输出 500 kHz 时钟的滤波模块试验 | 目标与台架检查，实测增益和截止频率待核 |

## 跨项目资料

- [Library](library/) 收录实验级 DSP、器件驱动、协议、标定和工作流模块。
- [器件资料与读数边界](knowledge/devices.md) 汇总数据手册、典型用法和模块级注意事项。
- [Knowledge](knowledge/) 按采样、信号输出、模拟前端、同步、标定等任务查找工程经验。
- [Framework](docs/) 收录开题、接线、联调、验证和复盘流程。
- [历届赛题入口](docs/problems.md) 链接官方题目和公示。各题原文以发布方页面为准。

GitHub 原有的 [2025G 工程](2025G/) 仍可从其原目录查阅。各题资料页记录所引用的硬件条件、代码位置和验证范围。

## Library 主机验证

```sh
cmake -S library -B build/library -DSIGNAL_FRAMEWORK_BUILD_ALL_L2=ON
cmake --build build/library --config Debug
ctest --test-dir build/library -C Debug --output-on-failure
```

Debug 配置保留 C 断言。若启用需要厂商 DSP 库的分支，请按模块文档准备对应依赖。

[发布范围与依赖](docs/release-scope.md) · [2026G 技术文章](docs/articles/2026g.md)
