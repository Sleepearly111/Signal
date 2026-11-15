# LUT-INTERP — 一维查表插值数学内核

> 状态：experimental  
> API版本：`0.1.0`  
> 当前验证等级：L2 Software Tested（仅主机软件测试）

## 来源

本模块来源于2025G项目中的频率标定查表需求。来源文件为 `projects/已完成项目/2025G/2025G/Src/transfer_calc.c`，提取前SHA-256为 `EA20A5BCEF7B404A9511366D73DB3C37A4AA27A09923F941FFDA75049CDCB88F`。

提取版只保留“根据严格递增的一维节点进行线性插值”的通用数学能力。2025G的29点低通实测数据、频率范围、端点钳位策略和题目参数均未进入本模块；来源工程保持不变，模块不继承来源项目的L3/L4等级。

## 当前验证等级

**L2 Software Tested**。

2026-07-21已使用TDM-GCC 9.2.0完成独立主机配置、严格告警编译和测试运行。该等级只覆盖当前数学API的软件行为，不包含STM32编译、板级性能或硬件回归，也不继承2025G来源工程的L3/L4等级。

## 模块定位

这是一个无全局变量、无隐藏状态的通用纯C数学插值模块。调用者提供 `x_table[]`、`y_table[]`及节点数量；模块验证表格后，在有效区间内计算线性插值。

本模块不是：

- 标定数据库；
- 测量模块；
- 硬件驱动；
- 标定表生成器或有效性判定系统。

## API

公开头文件：`include/lut_interp.h`。

- `lut_interp_init()`：检查对象和表指针、至少两个节点、全部节点为有限数，以及 `x_table`严格递增；成功后保存调用者表格的只读引用。
- `lut_interp_linear()`：在表格范围内使用二分查找定位相邻节点并计算线性插值，通过输出指针返回结果。

所有函数返回 `lut_status_t`。初始化和计算失败时不改写调用者对象或输出值。表格内存由调用者拥有，并且在LUT对象使用期间必须保持有效且不得改变排序关系。

## 边界策略

- `x == x_table[0]`：返回第一个端点值；
- `x == x_table[size - 1]`：返回最后一个端点值；
- `x < x_table[0]`或`x > x_table[size - 1]`：返回 `LUT_ERROR_RANGE`；
- 不执行端点钳位；
- 不执行线性外推。

## 测试

`tests/test_lut_interp.c`覆盖：

- 两点表 `{0,10}->{0,100}` 在 `x=5`时得到50；
- 多节点表的两个区间内插值；
- 最小和最大端点；
- 空指针、长度不足、重复节点、降序节点和非有限表值；
- 查询低于或高于范围，以及非有限查询值；
- 错误返回时不改写对象或输出；
- 使用合理epsilon比较浮点结果。

### 2026-07-21主机验证记录

工具链与配置：

- GCC：TDM-GCC 9.2.0，`gcc.exe (tdm64-1) 9.2.0`；
- CMake：4.3.4；
- Ninja：1.13.2；
- C标准：C11；
- 告警选项：`-Wall -Wextra -Werror`；
- 构建目录：`%TEMP%\lut-interp-build-20260721-b2`，未在Signal目录生成构建产物。

配置命令：

```powershell
      -B "$env:TEMP\lut-interp-build-20260721-b2" `
      -G Ninja `
```

配置退出码：`0`，CMake识别C编译器为GNU 9.2.0。

编译命令：

```powershell
cmake --build "$env:TEMP\lut-interp-build-20260721-b2" --verbose
```

编译退出码：`0`。`lut_interp.c`和`test_lut_interp.c`均以 `-std=c11 -Wall -Wextra -Werror` 编译，生成 `liblut_interp.a`和`test_lut_interp.exe`。

测试命令与结果：

```powershell
& "$env:TEMP\lut-interp-build-20260721-b2\test_lut_interp.exe"
# 输出：lut_interp tests passed
# 退出码：0

ctest --test-dir "$env:TEMP\lut-interp-build-20260721-b2" --output-on-failure
# 结果：1/1 passed，0 failed
# 退出码：0
```

通用独立构建方式：

```powershell
cmake -S . -B <build-dir> -G Ninja -DCMAKE_C_COMPILER=<gcc> -DCMAKE_MAKE_PROGRAM=<ninja>
cmake --build <build-dir> --verbose
ctest --test-dir <build-dir> --output-on-failure
```

## 可复用范围

- 频率补偿；
- 增益校准；
- 传感器标定；
- 任意严格递增一维自变量表的区间内线性插值。

## 限制条件

- 不包含任何2025G或其他项目数据；
- 不负责生成、保存或更新标定表；
- 不负责判断标定过程或硬件测量是否有效；
- 不支持重复或非严格递增的x节点；
- 不执行范围外钳位或外推；
- 不包含器件误差、温漂、滞回或多维插值；
- 当前没有STM32板级性能或硬件回归结论。
