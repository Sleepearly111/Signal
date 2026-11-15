# AD603增益模型

> 状态：experimental  
> API版本：`0.1.0`  
> 当前验证等级：L2 Software Tested（仅主机软件测试）

## 来源

本模块来源于2025G的AD603增益控制、标定和闭环安全经验，以及 `docs/design/AD603增益模型设计.md` 的冻结边界。来源Knowledge在2025G当前硬件链内为L3 Hardware Tested；本模块是重新设计的纯数学候选，不继承来源等级。

模块不包含2025G的控制电压、拟合斜率、截距、原始标定点或安全增益常数。标定数组由具体板卡或项目提供，并在模型使用期间保持有效。

## 当前验证等级

**L2 Software Tested**。

2026-07-21已使用TDM-GCC 9.2.0完成独立配置、严格告警编译和主机测试。本等级只覆盖数学映射、单调性、范围、单位转换及错误行为，不包含STM32编译、真实标定数据或AD603硬件回归。

## 模块定位

本模块只负责AD603控制电压与电压增益之间的数学语义：

- 控制电压V → 增益dB；
- 在增益表严格单调时，增益dB → 控制电压V；
- dB ↔ 正线性电压增益V/V；
- 表格合法性、范围和单调方向报告。

本模块不负责GPIO、DAC、ADC、HAL、AD603硬件写入、RMS测量、闭环控制、标定流程或数据持久化。

## 数据所有权

`ad603_model_t`只保存调用者的：

- `voltage_table`引用；
- `gain_table`引用；
- 节点数量；
- 增益单调方向。

模块不复制大型标定数据，也不使用动态内存。调用者必须保证数组在模型生命周期内有效且内容不被修改。

控制电压表必须严格递增。增益表可以严格递增、严格递减或非单调：

- 三者均可执行控制电压→增益正向查询；
- 只有严格递增或严格递减时允许反向查询；
- 非单调表反向查询返回 `AD603_NON_MONOTONIC`。

## API

公开头文件：`include/ad603_model.h`。

- `ad603_model_init()`：验证指针、节点数量、有限数和控制电压严格递增，并记录增益单调方向。
- `ad603_model_gain_from_voltage()`：表内查询控制电压对应的dB增益。
- `ad603_model_voltage_from_gain()`：对严格单调增益表执行反向表内查询。
- `ad603_gain_db_to_linear()`：把dB换算为正线性V/V增益。
- `ad603_gain_linear_to_db()`：把正线性V/V增益换算为dB。

范围外查询返回 `AD603_OUT_OF_RANGE`，不钳位、不外推。失败时不改写输出。

## 与一维LUT关系

正向映射以及递增增益表的反向映射复用 `library/experimental/DSP/一维查表插值/`。递减增益表使用同样的表内线性插值语义和只读二分定位，不要求复制或反转调用者数组。

LUT只负责插值；本模块负责AD603的V、dB、V/V、映射方向和错误语义。两者均不保存项目标定数据。

## 测试

`tests/test_ad603_model.c`覆盖：

- 0.2 V、0.5 V、1.0 V端点正向查询；
- 多区间电压→增益插值；
- 严格递增增益表的增益→电压查询；
- 严格递减增益表的正向与反向查询；
- 非单调增益表允许正向查询、拒绝反向查询；
- 电压和增益范围外错误；
- 20 dB ↔ 10 V/V和-20 dB ↔ 0.1 V/V；
- 空指针、长度不足、重复/降序电压和非有限数据；
- 错误返回时模型或输出保持不变。

独立构建方式：

```powershell
cmake -S . -B <build-dir> -G Ninja -DCMAKE_C_COMPILER=<gcc> -DCMAKE_MAKE_PROGRAM=<ninja>
cmake --build <build-dir> --verbose
ctest --test-dir <build-dir> --output-on-failure
```

### 2026-07-21主机验证记录

工具链：

- GCC：TDM-GCC 9.2.0，`gcc.exe (tdm64-1) 9.2.0`；
- CMake：4.3.4；
- Ninja：1.13.2；
- C标准：C11；
- 告警选项：`-Wall -Wextra -Werror`；
- 构建目录：`%TEMP%\ad603-model-build-20260721-c2`。

配置命令：

```powershell
      -B "$env:TEMP\ad603-model-build-20260721-c2" `
      -G Ninja `
```

配置退出码：`0`，CMake识别C编译器为GNU 9.2.0。

编译命令：

```powershell
cmake --build "$env:TEMP\ad603-model-build-20260721-c2" --verbose
```

编译退出码：`0`。AD603模型、LUT依赖和测试均以 `-std=c11 -Wall -Wextra -Werror` 编译，生成 `libad603_model.a`和`test_ad603_model.exe`。

测试命令与结果：

```powershell
& "$env:TEMP\ad603-model-build-20260721-c2\test_ad603_model.exe"
# 输出：ad603_model tests passed
# 退出码：0

ctest --test-dir "$env:TEMP\ad603-model-build-20260721-c2" --output-on-failure
# 结果：1/1 passed，0 failed
# 退出码：0
```

构建产物仅位于系统临时目录，没有写入Signal根工程。

## 未来使用关系

```text
具体板卡AD603标定数据
    ↓
AD603增益模型
    ↓ 推荐控制电压与可信状态
只消费STABLE结果的RMS闭环控制器
    ↓
板级DAC/AD603适配器
```

本模块不直接消费RMS结果，也不执行硬件控制。

## 限制条件

- 不包含任何项目标定数据；
- 第一版只处理单条件一维曲线，不处理频率、温度或负载的多维补偿；
- 反向查询要求增益节点严格单调；
- 不检测真实硬件削顶、饱和、带宽或最小有效读数；
- 不判断标定数据是否匹配当前板卡和实验条件；
- 当前没有STM32编译、板级性能或硬件回归结论。
