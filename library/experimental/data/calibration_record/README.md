# Calibration Record

- 状态：`experimental / L1 Builds`
- 用途：保存与读取带版本、板卡标识和 CRC32 的校准曲线。
- 格式：`SCAL` 魔数、schema、kind、board ID、点数、显式小端
  IEEE-754 单精度点对、CRC32。

## 边界

- 最大 32 点，无动态内存。
- x 轴必须严格递增；NaN、Inf、未知类型和未知 schema 被拒绝。
- CRC 只检查传输/存储损坏，不证明标定数据在当前板卡、负载和温度下有效。
- `board_id` 由项目定义，模块不猜测 MCU UID 或板卡身份。
- 不直接读写 Flash/EEPROM；持久化由 Adapter 负责。

测试源文件覆盖 round-trip、CRC 损坏、魔数、版本、曲线单调性与容量不足。
2026-07-24 已用 Arm GCC 14.3.1 和根 CMake/Ninja 构建静态库，构建退出码为 0；
测试断言尚未运行，因此仍不是 L2。
