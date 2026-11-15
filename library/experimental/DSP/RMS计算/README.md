# RMS计算

> 状态：experimental  
> 当前验证等级：L2 Software Tested（仅主机软件测试）

## 用途与来源

本模块来源于2025G的RMS测量需求，用于对调用者提供的等间隔`float`采样数据计算均值、总RMS和去直流AC RMS。

它是无全局变量、无隐藏缓存、无硬件依赖的纯C数学模块。输入值采用什么物理单位，输出就沿用相同单位。

## API

- `rms_calc_mean()`：计算样本平均值。
- `rms_calc_total()`：计算包含直流分量的总RMS。
- `rms_calc_ac()`：先计算均值，再计算去直流AC RMS。

接口拒绝空指针、零长度、NaN和Infinity。单元素数组的总RMS有效，AC RMS为0。任何错误返回均不修改输出值。

## 限制

- 不包含AD637、ADS8688、ADC、DMA、HAL或稳定状态机；
- 不负责采样率、Nyquist、削顶、量程和测量有效性判断；
- 不包含标定参数或Vpp换算；
- 公共接口使用`float`以兼顾STM32兼容性和DSP效率；高动态范围需求可另行评估`double`版本；
- 本模块的软件验证不继承2025G硬件测量链的L3等级。

## 测试

主机测试覆盖直流、正负对称样本、整周期正弦近似样本、单元素、空指针、零长度、NaN、Infinity，以及错误时输出保持不变。

2026-07-21使用TDM-GCC 9.2.0、CMake 4.3.4和Ninja 1.13.2完成独立测试构建。源码按C11及`-Wall -Wextra -Werror`编译；测试程序输出`rms_calc tests passed`，CTest结果为1/1通过。配置、编译、测试程序和CTest退出码均为0。

构建目录为`%TEMP%\rms-calc-build-20260721-d1`，Signal目录内未生成构建产物。当前L2只证明本模块的主机软件行为，不代表STM32性能或硬件测量链已验证。
