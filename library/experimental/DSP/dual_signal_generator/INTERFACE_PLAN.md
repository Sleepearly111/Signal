# dual_signal_generator 接口规划

## 目标边界

建立纯 DSP 样本生成接口。模块一次生成一路稳态信号，不负责 HAL、DAC、TIM、DMA、缓冲分配或硬件启动。需要两路信号时，调用者分别提交两份配置。

## 规划输入

- `Signal_Config.freq_hz`：载波频率，必须低于 Nyquist；
- `Signal_Config.amplitude`：衰减前峰值幅度；
- `Signal_Config.phase_deg`：附加相位；
- `Signal_Config.delay_seconds`：同时作用于载波与 AM 包络的稳态时延；
- `Signal_Config.attenuation_db`：非负电压衰减；
- `Signal_Config.modulation`：无调制或 AM，以及调制频率和深度；
- `Signal_Config.sample_rate_hz`、`buffer_size`：采样率与生成数量；
- 调用者提供的单路 `float` 输出缓冲及容量。

## 规划输出

- `DualSignalGenerator_Generate()` 将 `buffer_size` 个样本写入调用者缓冲；
- 返回空指针、容量不足、范围错误或调制配置错误等明确状态；
- 错误发生在生成前，参数校验失败时不写输出缓冲。

## 解耦要求

- 不包含 2024C 的固定频率范围、1 Msps、1000 点或 2 kHz AM 常量；
- 不包含 SD/SM、无线、多径页面等题目命名；
- 不包含 DAC 中心码、STM32 HAL 句柄或全局工程状态；
- 时延同时作用于载波和 AM 包络，不简化为单一载波相位偏移；
- 调用者拥有输出缓冲，模块不使用动态内存。

## 本轮不做

- 不建立 CMake 或完整自动测试框架；
- 不实现硬件播放与 DAC 码值换算；
- 不承诺分数延时滤波、零填充历史、多径簇、噪声或频偏能力；
- 当前状态保持 experimental / L0 Software Draft。
