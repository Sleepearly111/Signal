# TF2 — 二阶传递函数数学内核

> 状态：experimental  
> API版本：`0.1.0`  
> 当前验证等级：L2 Software Tested（仅主机软件测试）

## 来源

本模块来源于2025G项目中的二阶模型计算部分。来源工程保持不变；提取版重新设计了独立接口，只保留低通、高通、带通和带阻二阶模型的数学响应计算。

来源文件：`projects/已完成项目/2025G/2025G/Src/adva_learn.c`  
提取前SHA-256：`5C765D9F99622CB2DF1B63B3B397F45415D6B099E4FC2AD3FA6B88EE280978E0`

提取版不包含2025G项目的滤波器自动分类、模型拟合搜索、扫频状态机、实测LUT、预置模型或硬件接口，不能继承来源工程的L3/L4等级。

## 当前验证等级

**L2 Software Tested**。

2026-07-21 使用TDM-GCC 9.2.0完成独立主机配置、严格告警编译和测试运行。该等级只覆盖当前纯数学API的软件测试，不包含STM32编译、板级性能或硬件回归，不能继承2025G来源工程的L3/L4等级。

## 模块定位

这是一个无全局变量、无隐藏状态的纯C数学模型库。调用者提供滤波器类型、增益K、中心频率 `f0_hz`、品质因数Q和计算频率，模块返回幅值响应与弧度相位。

本模块不是：

- 测量系统；
- 硬件驱动；
- 自动识别或模型拟合模块；
- ADC、DAC、DDS、AD637或ADS8688服务；
- 项目标定数据容器。

## API

公开头文件：`include/tf2.h`。

- `tf2_init()`：检查类型、增益、中心频率和Q，并初始化调用者提供的 `tf2_model_t`。
- `tf2_calc_response()`：计算指定非负频率下的 `magnitude` 和 `phase_rad`。

函数通过 `tf2_status_t` 返回状态。`f0_hz`和Q必须大于0，增益必须非负，所有浮点参数必须有限。失败时不改写调用者的模型或响应结构。

采用的归一化形式为：

```text
x = f / f0
D(jx) = (1 - x^2) + j(x/Q)
```

四种分子分别为：

```text
低通：1
高通：-x^2
带通：j(x/Q)
带阻：1-x^2
```

最终响应整体乘以增益K。相位范围归一化到 `(-pi, pi]`；当幅值严格为0时，相位在数学上没有定义，本API约定返回0。

## 测试

`tests/test_tf2.c`覆盖：

- 二阶低通在远低于、等于和远高于中心频率时的趋势与中心点响应；
- 二阶高通的低频衰减和高频通过；
- 二阶带通在中心频率附近取得最大响应；
- 二阶带阻在中心频率产生陷波；
- `Q <= 0`、`f0 <= 0`、负增益、非法类型、负计算频率和空指针；
- 错误返回时不改写响应输出。

### 2026-07-21主机验证记录

工具链：

- GCC：TDM-GCC 9.2.0，`gcc.exe (tdm64-1) 9.2.0`；
- CMake：4.3.4；
- 构建执行器：Ninja 1.13.2；
- C标准：C11；
- 告警选项：`-Wall -Wextra -Werror`。

成功配置命令：

```powershell
      -B "$env:TEMP\tf2-build-20260721-b15-ninja" `
      -G Ninja `
```

配置退出码：`0`。CMake识别C编译器为GNU 9.2.0。

编译命令：

```powershell
cmake --build "$env:TEMP\tf2-build-20260721-b15-ninja" --verbose
```

编译退出码：`0`。`tf2.c`和`test_tf2.c`均以 `-std=c11 -Wall -Wextra -Werror` 编译，并生成 `libtf2.a`和`test_tf2.exe`。

测试命令与结果：

```powershell
& "$env:TEMP\tf2-build-20260721-b15-ninja\test_tf2.exe"
# 输出：tf2 tests passed
# 退出码：0

ctest --test-dir "$env:TEMP\tf2-build-20260721-b15-ninja" --output-on-failure
# 结果：1/1 passed，0 failed
# 退出码：0
```

首次使用TDM自带 `mingw32-make.exe` 配置成功，但编译退出码为`2`，原因是该构建执行器不能正确处理模块中文绝对路径，报告 `No rule to make target`。切换到Ninja后，在相同TDM-GCC、相同源码和相同告警选项下配置、编译与测试均成功；该失败记录属于构建工具路径兼容边界。

通用独立构建方式：

```powershell
cmake -S . -B <build-dir> -G Ninja -DCMAKE_C_COMPILER=<gcc> -DCMAKE_MAKE_PROGRAM=<ninja>
cmake --build <build-dir> --verbose
ctest --test-dir <build-dir> --output-on-failure
```

## 可复用范围

- 二阶滤波器理论分析；
- 信号模型的幅频与相频计算；
- 扫频分析中的理论响应生成；
- 模型拟合或波形重构模块的独立数学依赖。

## 限制条件

- 不包含真实硬件误差、器件容差、温漂、噪声、饱和或附加延迟；
- 不包含任何标定数据或实测LUT；
- 不执行滤波器类型识别，也不从测量数据拟合K、f0或Q；
- 计算结果是理想二阶模型，不代表硬件滤波器实测结果；
- 当前没有STM32板级性能或硬件回归结论。
