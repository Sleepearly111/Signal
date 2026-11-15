# PLL_Lab_2026 Handoff Document

> **Status:** `L1 Build Verified / Flashable`  
> **Updated:** 2026-07-24 (v2.0 — correlation lock refactor)  
> **Purpose:** Survive Codex conversation loss — all context needed to continue work.  

---

## 1. Current Progress

### Completed (v2.0 — Correlation Lock Refactor)
- [x] **New module:** `User/{Inc,Src}/LOCK/signal_lock.{h,c}` — correlation-lock state machine + PI controller
- [x] **New config:** `User/Inc/LOCK/signal_lock_config.h` — all tunable parameters in one file
- [x] **Refactored main.c:** 3 new modes (SELF_TEST / CORRELATION_MEASURE / CORRELATION_LOCK)
- [x] **New UART commands:** seed, wave, output_phase, cal detector_zero, polarity, dds_ref_phase
- [x] DDS_REF (AD9833-A) = AD630 carrier reference; DDS_OUT (AD9833-B) = A' reconstructed output
- [x] Primary error signal: `phase_error_mv = mean_mv - target_mv` (signed DC, NOT ac_rms)
- [x] PI controller with configurable Kp, Ki, integral/frequency clamps
- [x] Lock state machine: IDLE → ACQUIRE → TRACK → LOCKED (with FAULT detection)
- [x] `cal detector_zero` zero-calibration command
- [x] Unified DDS update: both channels from single set of corrections
- [x] Default boot mode = CORRELATION_MEASURE (safe — never auto-locks)
- [x] Amplitude boundary documented: single AD630 cannot disambiguate amplitude × cos(Δφ)
- [x] **BUILD SUCCESSFUL** — 0 errors, 0 warnings, `-Wall -Wextra`
- [x] ELF: 55,036 B text + 488 B data + 2,672 B BSS = 58,196 B total

### Not Yet Verified (needs hardware)
- [ ] Flash and run on hardware
- [ ] AD9833 MCO2 clock verified with oscilloscope (currently assumed 4 MHz)
- [ ] DDS output frequency accuracy
- [ ] ADS8688 sampling in new loop context
- [ ] PLL closed-loop stability
- [ ] UART command shell responsiveness

---

## 2. Build Information

### Actual Build Commands

```bash

# Configure
cmake --preset Debug \
  -G "Ninja" \

# Build
cmake --build build/Debug
```

### Configure Result
```
-- The C compiler identification is GNU 14.3.1
-- The CXX compiler identification is GNU 14.3.1
-- Build type: Debug
-- Configuring done (10.2s)
```

### Build Result
```
[35/35] Linking C executable PLL_Lab_2026    ← SUCCESS
0 errors, 0 warnings on project source files
```

### Toolchain Used

### CMake Infrastructure (manually created)
| File | Purpose |
|------|---------|
| `cmake/gcc-arm-none-eabi.cmake` | ARM Cortex-M4 toolchain (replaces cube-cmake generated) |
| `cmake/stm32cubemx/CMakeLists.txt` | HAL static library from firmware repo |
| `CMakeLists.txt` | Root project config |

### Output Files
| File | Path | Size |
|------|------|------|
| ELF | `build/Debug/PLL_Lab_2026` | 1,643,196 bytes (with debug) |
| HEX | `build/Debug/PLL_Lab_2026.hex` | 154,289 bytes |
| BIN (raw) | `build/Debug/PLL_Lab_2026.bin` | 54,832 bytes |
| MAP | `build/Debug/PLL_Lab_2026.map` | 660,796 bytes |

### Memory Footprint
```
   text    data     bss     dec     hex
  54340     484    2604   57428    e054
```
Flash: ~54 KB (of 1 MB) | RAM: ~3 KB (of 192 KB)

---

## 3. FCLK Unified Configuration

**Single source of truth:** `User/Inc/PLL/pll_lab_config.h`

```c
#define PLL_LAB_AD9833_MCLK_HZ  4000000UL
#define PLL_LAB_AD9833_HZ_TO_WORD  (268435456.0 / (double)PLL_LAB_AD9833_MCLK_HZ)
```

Referenced by:
- AD9833.c:16 — `#define FCLK PLL_LAB_AD9833_MCLK_HZ`
- main.c:215 — `const double freq_hz_to_word = PLL_LAB_AD9833_HZ_TO_WORD;`

**Formula:** `freq_word = f_out_hz × 2^28 / MCLK`

**⚠️  MCLK = 4 MHz assumes HSE = 8 MHz crystal and MCO2 divider = 2.  
Verify PC9 with oscilloscope. If incorrect, update ONLY `pll_lab_config.h` and rebuild.**

---

## 4. ADS8688 Range Configuration

**Single source of truth:** `User/Inc/PLL/pll_lab_config.h`

```c
#define PLL_LAB_ADS8688_RANGE_MV  10240.0f
```

**Range register written in ads8688_set():** `ADS8688_RANGE_PM_10V24 (0x00)` → ±10.24 V

**Conversion in ADS8688_CodeToMilliVolt():**
```c
voltage_mV = (code - 32768) / 32768.0f * PLL_LAB_ADS8688_RANGE_MV
```

| Code | Voltage |
|------|---------|
| 0 | -10240 mV |
| 32768 | 0 mV |
| 65535 | ~+10240 mV |

**⚠️  pid_phase.c thresholds** (10 mV RMS, 8 mV drift, etc.) are in TRUE MILLIVOLTS now.
Previously, ADS8688_CodeToMilliVolt used 5120.0f (half the actual range), so all
voltage readings and PLL thresholds were off by factor 2×.

---

## 5. Residual H-Contest Dependencies

**CLEAN.** No actual code references to:
- `calculate.h` / `calculate.c`
- `windowfunction`  
- `arm_cfft` / `arm_cmplx_mag`
- `Demultiplex`
- `lcd_adjust`
- `screen_fi` / `flag_phase` / `flag_no_adjust` / `flag3`
- `USART1_RX_BUF` / `USART1_RX_STA` (serial screen protocol)
- `USART1_IRQ_User_Handler`

The only remaining traces are intentional comments in `pid_phase.c` (lines 7, 31)
explaining what was removed.

---

## 6. Key Files and Their Roles

| File | Role | v2.0 Status |
|------|------|-------------|
| `Core/Src/main.c` | Init, 3-mode dispatch, UART shell, periodic status | **Rewritten (v2.0)** |
| `User/Src/LOCK/signal_lock.c` | Correlation lock state machine + PI controller | **NEW (v2.0)** |
| `User/Inc/LOCK/signal_lock.h` | Public interface: types, API | **NEW (v2.0)** |
| `User/Inc/LOCK/signal_lock_config.h` | All tunable parameters (PI, lock, timing) | **NEW (v2.0)** |
| `User/Src/PLL/pid_phase.c` | AD630_MeasureWindowServiced() — wrapped by signal_lock | Unchanged (v1.0 cleanup) |
| `User/Inc/PLL/pid_phase.h` | AD630LockMetric, measurement API | Unchanged |
| `User/Src/AD9833.c` | Dual DDS driver — uses unified FCLK | FCLK only |
| `User/Src/ADS8688.c` | External ADC — uses unified RANGE_MV | Range formula fixed |
| `User/Inc/PLL/pll_lab_config.h` | Unified MCLK + range constants | Unchanged |
| `Core/Src/stm32f4xx_it.c` | ISR vectors — cleaned | Unchanged from v1.0 |
| `CMakeLists.txt` | Build — added signal_lock.c + LOCK include path | Updated (v2.0) |
| `cmake/gcc-arm-none-eabi.cmake` | ARM GCC toolchain file | Unchanged |
| `cmake/stm32cubemx/CMakeLists.txt` | HAL library from firmware repo | Unchanged |
| `Core/Src/gpio.c`, `usart.c`, `tim.c`, `adc.c`, `dma.c` | Peripheral drivers | Unchanged |

### pid_phase.c — What's Kept vs Replaced

| Function | Fate | Reason |
|----------|------|--------|
| `AD630_MeasureWindowServiced()` | **Kept** (wrapped by signal_lock.c) | Low-level ADS8688 sampling window — still correct |
| `PLLPhase_UpdateWindow()` | **Not called** (old SEARCH/LOCKED state machine) | Replaced by signal_lock PI controller with proper signed phase_error |
| `PLLPhase_IsLocked()` | **Not called** | Replaced by signal_lock state machine |
| `PLLPhase_ResetRuntime()` | **Not called** | Replaced by SignalLock_Reset() |
| `get_phaseV()`, `get_deltV()` | **Kept** (unused but harmless) | Legacy utility functions |

---

## 7. Hardware Pin Map

### AD9833 (dual DDS, software SPI)
```
Signal    A-chip  B-chip
FSYNC     PB12    PD12
SCLK      PB13    PD13
SDATA     PB15    PD15
MCLK      PC9     PC9    (shared MCO2 output: HSE/2 = 4 MHz)
```

### ADS8688 (external ADC, software SPI)
```
CS        PD2
SCLK      PD3
SDI       PD4
SDO       PD5
RST       PD0
DAISY     PD1
```

### UART
```
USART1 TX  PA9   (command shell, 115200 8N1)
USART1 RX  PA10
USART2 TX  PA2   (printf debug,  115200 8N1)
USART2 RX  PA3
```

### ADC1 (internal, for FFT — retained but unused in PLL Lab)
```
ADC1_IN9   PB1
```

### LEDs / Keys
```
LED0       PF9
LED1       PF10
KEY0       PE4  (EXTI4)
KEY1       PE3  (EXTI3)
WKUP       PA0  (EXTI0)
```

---

## 8. Clock Configuration

```
HSE = 8 MHz (on-board crystal)
  ├── PLL: PLLM=8, PLLN=336, PLLP=2 → SYSCLK = 168 MHz
  │   ├── HCLK  = 168 MHz
  │   ├── APB1  = 42 MHz  (PCLK1, for TIM3/TIM7/USART2)
  │   └── APB2  = 84 MHz  (PCLK2, for ADC1/USART1)
  └── MCO2: HSE/2 = 4 MHz → AD9833 MCLK (PC9)

TIM7: PSC=8399, ARR=1999 → 84e6 / (8400*2000) = 5 Hz → 200 ms period
```

---

## 9. Next Steps (Priority Order)

### Phase 1 — Verify (immediate, needs hardware)
1. Flash using OpenOCD + ST-Link
2. **Follow open-loop test plan:** [`docs/PHASE2_OPEN_LOOP_TEST.md`](PHASE2_OPEN_LOOP_TEST.md)
3. **Follow correlation test plan:** [`docs/CORRELATION_LOCK_TEST.md`](CORRELATION_LOCK_TEST.md) — Tests 1–6 (measurement only, no closed-loop)
4. Verify DDS output: 10 kHz sine on both channels
5. Measure MCO2 on PC9 — confirm 4 MHz (± tolerance)
6. Test all new UART commands: `seed`, `wave`, `cal detector_zero`, `polarity`, `dds_ref_phase`
7. Complete CORRELATION_LOCK_TEST.md Tests 1–6 before any closed-loop attempt

### Phase 2 — Hardware Integration
1. Connect AD630 analog board to ADS8688 inputs
2. Verify ADS8688 reads correct phase detector voltages
3. Test PLL lock with known 1 Hz frequency offset
4. Characterize lock time, pull-in range

### Phase 3 — Polish
1. Add frequency sweep command
2. Add CSV data logging over UART
3. Interrupt-driven UART receive (currently polling)
4. Extract to Library asset

---

## 10. Remaining Warnings

**0 warnings** on project source files with `-Wall -Wextra`.

HAL library warnings suppressed with `-w` (vendor code, not our responsibility).

---

## 11. UART Command Reference

| Command | Args | Effect |
|---------|------|--------|
| `mode open` | — | Switch to open-loop (no PLL) |
| `mode measure` | — | Switch to measure-only (sample AD630, no adjust) |
| `mode closed` | — | Switch to closed-loop (needs `lock start`) |
| `freq ref <hz>` | float | Set reference DDS frequency (ch A) |
| `freq ctrl <hz>` | float | Set controlled DDS frequency (ch B) |
| `phase ref <deg>` | float | Set reference DDS phase |
| `phase ctrl <deg>` | float | Set controlled DDS phase |
| `lock start` | — | Enable PLL closed-loop |
| `lock stop` | — | Disable PLL closed-loop |
| `status` | — | Print current config |
| `reset` | — | Re-init DDS + PLL runtime |

Implementation: polling `HAL_UART_Receive()` in main loop, non-blocking.
Buffer: 64 bytes. Supports `\r\n`, `\n`, backspace.

---

## 12. Flash Command (OpenOCD + ST-Link)

```bash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program build/Debug/PLL_Lab_2026 verify reset exit"
```

*End of handoff.*
