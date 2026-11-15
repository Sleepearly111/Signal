# STM32F407 OpenOCD 构建与烧录指南

本指南用于当前 Signal 工程的 STM32F407 + OpenOCD 烧录流程。

## 前提

- ST-LINK 已通过 USB 连接电脑；
- STM32F407 开发板已上电，目标电压约 3.3 V；
- OpenOCD：

```text
```

## 工程变量

```powershell
```

## 编译

```powershell
Set-Location $P
if (Test-Path -LiteralPath "build_ltc1068") { Remove-Item -LiteralPath "build_ltc1068" -Recurse -Force }
New-Item -ItemType Directory -Path "build_ltc1068" | Out-Null
Set-Location "build_ltc1068"
cmake -G Ninja `
  -DCMAKE_MAKE_PROGRAM=$N `
  -DCMAKE_TOOLCHAIN_FILE=../cmake/gcc-arm-none-eabi.cmake `
  -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .
```

构建产物：

```text
build_ltc1068/LTC1068_5kHz.elf
```

## OpenOCD 烧录与校验

```powershell
Set-Location $P
& $O `
  -f interface/stlink.cfg `
  -f target/stm32f4x.cfg `
  -c "adapter speed 500" `
  -c "init; halt; program $P/build_ltc1068/LTC1068_5kHz.elf verify; reset; exit"
```

必须看到：

```text
Programming Finished
Verified OK
```

## 烧录后检查

1. 示波器探头接 STM32 `PC6`，地夹接 STM32 `GND`；
2. 应看到约 500 kHz、50% 占空比、0～3.3 V 方波；
3. 再连接 LTC1068 `S_CLK`，接通 `+5 V/GND/-5 V`；
4. 外部信号发生器接 `S_IN`，示波器观察 `S_IN/S_OUT`。

## 常见问题

- `ninja: no work to do`：代码未改变，属于正常提示；
- `open failed`：拔插 ST-LINK USB，确认目标板供电后重试；
- `No debug probe detected`：检查 ST-LINK、SWDIO、SWCLK、GND；
- `Verified OK`：表示烧录内容已经通过校验。
