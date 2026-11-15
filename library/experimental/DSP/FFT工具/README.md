# FFT工具

> 状态：experimental  
> 当前验证等级：L2 Software Tested（仅主机软件测试）

## 用途与来源

本模块来源于2025G `dsp_utils`中的通用频谱分析需求，提供去直流、Hann/五项Flat-Top窗、实数FFT、单边峰值幅度谱和峰值频率计算。

提取版是纯C数学模块，不依赖CMSIS-DSP。FFT长度由调用者传入，必须是至少为2的二次幂；频谱和FFT工作区均由调用者提供，无全局变量或隐藏缓存。

## API

- `fft_remove_dc()`：就地去直流并返回均值。
- `fft_apply_window()`：就地加窗并返回实际离散窗的相干增益。
- `fft_real_forward()`：执行基2实数正向FFT，输出`N/2+1`个复数频点。
- `fft_magnitude()`：按相干增益生成单边峰值幅度谱。
- `fft_find_peak_frequency()`：跳过DC查找最大频点，并使用三点抛物线插值估计频率。

## 限制

- 不包含ADC、DMA、HAL、题目状态机、40 kHz专项逻辑、谐波分类、相位提取或回放链路；
- 不判断采样率、Nyquist、混叠、削顶和模拟前端带宽是否有效；
- 只支持二次幂FFT长度，当前实现优先考虑可移植与可测试性，未宣称达到CMSIS-DSP性能；
- 幅值结果是按窗相干增益校正的单边峰值幅度，不是RMS；
- 本模块重新从L0验证，不继承2025G硬件等级。

## 测试

主机测试覆盖去直流、两种窗函数、整bin正弦FFT、幅值归一化、峰值频率以及空指针、非法长度、工作区不足、非有限数和非法采样率。

2026-07-21使用TDM-GCC 9.2.0、CMake 4.3.4和Ninja 1.13.2完成独立测试构建。源码按C11及`-Wall -Wextra -Werror`编译；测试程序输出`fft_tools tests passed`，CTest结果为1/1通过。配置、编译、测试程序和CTest退出码均为0。

构建目录为`%TEMP%\fft-tools-build-20260721-d2`，Signal目录内未生成构建产物。当前L2只证明纯C实现的主机软件行为，不代表STM32性能、CMSIS-DSP等价性或硬件频谱测量已经验证。
